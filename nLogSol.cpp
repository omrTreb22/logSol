////--------------------------------------------------------//
// Application LogSolaire                                 //
//                                                        //
// 30/10/2016 : 4.0 - Version pour smartphones            //
// 19/12/2017 : 5.0 - Recuperation puissance Envoy        //
// 08/02/2018 : 5.1 - Interface Domoticz                  //
// 28/10/2018 : 5.2 - Comparaison calcul Tarif jour/nuit  //
// 30/11/2019 : 6.0 - Thread UDP pour wattmetre           //
// 07/11/2022 : 7.0 - Kit Air Chaud Ameliore              //
// -------------------------------------------------------//
// 23/10/2023 : 8.0 - Reecriture - simplification, menage //
//--------------------------------------------------------//


#include <fstream>
#include <iostream>
#include <stdexcept>

using std::cout;
using std::endl;
using std::fstream;
using std::ofstream;
using std::string;

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <netdb.h> /* struct hostent, gethostbyname */
#include <sys/time.h>
#include <asm/socket.h>			/* arch-dependent defines	*/
#include <linux/sockios.h>		/* the SIOCxxx I/O controls	*/
#include <pthread.h>

#define VERSION                            "8.1"
#define IP_ADDRESS_DOMOTICZ                "192.168.1.77"
#define IP_ADDRESS_ESP_RELAY               "192.168.1.94"
#define IP_ADDRESS_ESP_PUIS                "192.168.1.78"
#define PORT_UDP                           5005
#define UDP_PORT_RELAY                     5006
#define UDP_PORT_PUIS                      5080
#define UDP_RX_BUFFER                      1000
#define HTML_FILE                          "/var/www/index.html"
#define PERIODE_RELEVE                     5
#define FALSE                              0
#define TRUE                               1

// Index for Domoticz
#define DOMOTICZ_INDEX_PANNEAUX_SOLAIRES   1
#define DOMOTICZ_INDEX_CUVE                2
#define DOMOTICZ_INDEX_ECS                 3
#define DOMOTICZ_INDEX_POMPE               10
#define DOMOTICZ_INDEX_PRODUCTION_INST     5
#define DOMOTICZ_INDEX_PRODUCTION_JOURNEE  6
#define DOMOTICZ_INDEX_DUREE_PROT_GEL      7
#define DOMOTICZ_INDEX_KM_ZOE              9
#define DOMOTICZ_INDEX_CONSO_EDF           11
#define DOMOTICZ_INDEX_PROD_EDF            12
#define DOMOTICZ_INDEX_RELAIS_COMMANDE     13
#define DOMOTICZ_INDEX_T1                  14
#define DOMOTICZ_INDEX_T2                  15
#define DOMOTICZ_INDEX_PRISE_COMMANDEE     16
#define DOMOTICZ_INDEX_AUTOCONSO           19

#define COMMAND_FAN_ON                     1
#define COMMAND_FAN_OFF                    2
#define COMMAND_KACA_ON                    3
#define COMMAND_KACA_OFF                   4
#define COMMAND_SETLOG                     5

// Global variables
char buffer[UDP_RX_BUFFER]; 
int nBufferLength;
int G_leave = 0;
int nLogLevel = 1;
int powerReceived = 0;
unsigned long G_compteurEDF = 0;
unsigned long G_compteurInjectionEDF = 0;
unsigned long G_pAppEDF = 0;
unsigned long G_prodEDF = 0;
unsigned long G_globalProdEDF = 0;
unsigned long G_Puissance = 0;
unsigned long G_Puissance_PZEM_EDF = 0;
unsigned long G_TotalPuissanceSolaire = 0;
unsigned long G_multiple1MTotalPuissanceSolaire = 0;
unsigned long G_TotalPuissanceForKm = 0;
unsigned long G_previousPump = 0;
unsigned long G_TotalPuissanceSolairePrevious = 0;
unsigned long G_multiple1Mkm = 0;
         long G_PuissanceAutoConso = 0;
unsigned long G_TotalPuissanceAutoConso = 0;
unsigned long G_multiple1MTotalPuissanceAutoConso = 0;
unsigned long G_chargeDelestage = 0;
unsigned long G_globalSeconds = 0;
unsigned long G_lastReceivedTemperature = 0;
bool isTemperatureAlarm = 0;
float G_lastAction = 0.0;

float G_T1 = 0.0;
float G_T2 = 0.0;
float G_thSolarPanel = 0;
float G_thCuve = 0;
float G_thECS = 0;
unsigned long G_pump = 0;
int G_fan = 0;
int G_forceFan = 0;
int G_sockUDPRelay;
int G_lastPower = 0;
int G_count = 0;
int G_KACA_State = 1;
int G_saveRequired = 0;
int G_Stop_KACA = 0;

struct oldPersistentData
{
    unsigned long totalPuissanceSolaire;
    unsigned long multiple1MTotalPuissanceSolaire;
    unsigned long totalPuissanceForKm;
    unsigned long multiple1Mkm;
    unsigned long totalPuissanceAutoConso;
    unsigned long multiple1MTotalPuissanceAutoConso;
    unsigned long compteurInjectionEDF;
};

struct persistentData
{
    unsigned long totalPuissanceSolaire;
    unsigned long multiple1MTotalPuissanceSolaire;
    unsigned long totalPuissanceForKm;
    unsigned long multiple1Mkm;
    unsigned long totalPuissanceAutoConso;
    unsigned long multiple1MTotalPuissanceAutoConso;
    unsigned long compteurInjectionEDF;
};

struct point
{   
    int index;
    int power;
};
 
void genHtmlDomoticzSwitch(int index, int value);

#define MAX_VALUE_FOR_MEASURES  1536
// This measures comes from direct mesure of consumption of Radiator

// Valeurs pour le radiateur connecté en mode 900W (bouton 1 ON, Bouton 2 OFF)
//#define MAX_MESURES  27
//struct point mesures[MAX_MESURES] = {{1,8}, {11,38}, {21,155}, {31,218}, {41,274}, {51,306}, {61,323}, {71,353}, {81,371}, {91,388}, {101,400}, 
//{111,417}, {121,426}, {131,439}, {141,452}, {151,457}, {161,466}, {171,472}, {181,478}, {191,484}, {201,487}, 
//{211,490}, {221,497}, {231,502}, {241,504}, {251,508}, {255,511}};   // {256,557}

// Valeurs pour le radiateur connecté en mode 1500W (bouton 1 ON, Bouton 2 ON)  (valeurs non rellement mesurees : estimees)
//#define MAX_MESURES  27
//struct point mesures[MAX_MESURES] = {{1,8},{11,94},{21,273},{31,417},{41,494},{51,581},{61,613},{71,670},{81,704},{91,737},{101,760},
//{111,792},{121,809},{131,834},{141,858},{151,868},{161,885},{171,896},{181,908},{191,919},{201,925},
//{211,931},{221,944},{231,953},{241,957},{251,965},{255,970}}; // {256,1058}
 
// Valeurs pour le radiateur connecté en mode 1500W (bouton 1 ON, Bouton 2 ON)
#define MAX_MESURES  23
struct point mesures[MAX_MESURES] = {{1,1}, {60,3},{70,34},{80,63},{85,84},{90,100},{95,117},{100,132},
{110,170},{120,207},{130,230},{140,274},{150,302},{160,348},{170,359},{180,404},{200,467},{210,475},{220,516},
{230,550},{240,588},{250,606},{255,620}};  // 256:1536

using namespace std;

void logError(char *msg)
{
    char tmp[256];
    struct tm *pTime;
    struct timeval clock;
    FILE *fp;

    if (gettimeofday(&clock, NULL) != -1)
    {
        pTime = localtime((time_t *)&clock.tv_sec);
        sprintf(tmp, "%4d/%02d/%02d %02d:%02d:%02d %s", (pTime->tm_year+1900), (pTime->tm_mon+1), pTime->tm_mday,
           pTime->tm_hour, pTime->tm_min, pTime->tm_sec, msg);
    }
    else
        sprintf(tmp, "%s", msg);

    printf("Error: %s\n", tmp);
    fp = fopen("/home/logSol/errors.log", "a");
    if (fp == NULL)
        return;
    fwrite(tmp, strlen(tmp), 1, fp);
    fclose(fp);
}

void saveData()
{
    int f;
    int n;
    struct persistentData data;

    data.totalPuissanceSolaire = G_TotalPuissanceSolaire;
    data.multiple1MTotalPuissanceSolaire = G_multiple1MTotalPuissanceSolaire;
    data.totalPuissanceForKm = G_TotalPuissanceForKm;
    data.multiple1Mkm = G_multiple1Mkm;
    data.totalPuissanceAutoConso = G_TotalPuissanceAutoConso;
    data.multiple1MTotalPuissanceAutoConso = G_multiple1MTotalPuissanceAutoConso;
    data.compteurInjectionEDF = G_compteurInjectionEDF/3600;

    f = open("persistentData-LogSol.dat", O_WRONLY);
    if (f == -1)
    {
        f = open("persistentData-LogSol.dat", O_WRONLY | O_CREAT, S_IRWXO|S_IRWXU|S_IRWXG);
        if (f == -1)
        {
            logError("Error : cannot save file persistentData-LogSol.dat");
            return;
        }
    }
    try
    {
        n = write(f, &data, sizeof(struct persistentData));
        if (n != sizeof(struct persistentData))
        {
            char tmp[128];
            sprintf(tmp, "Error: write persistentData-LogSol.dat file with %d bytes instead of %d", n, sizeof(struct persistentData));
            logError(tmp);
        }
    } catch( const exception & e ) 
    {
        char tmp[128];
        sprintf(tmp, "Exception when writting in persistentData-LogSol.dat with cause %s", e.what());
        logError(tmp);
    }
    close(f);
}

void loadData()
{
    int f;
    int n;
    struct oldPersistentData data;

    f = open("persistentData-LogSol.dat", O_RDONLY);
    if (f == -1)
    {
        logError("Error : cannot load file persistentData-LogSol.dat");
        return;
    }
    n = read(f, &data, sizeof(struct oldPersistentData));
    if (n != sizeof(struct oldPersistentData))
    {
        char tmp[128];
        sprintf(tmp, "Error: read persistentData-LogSol.dat file returns %d bytes instead of %d", n, sizeof(struct oldPersistentData));
        logError(tmp);
    }
    G_TotalPuissanceSolaire = data.totalPuissanceSolaire;
    G_multiple1MTotalPuissanceSolaire = data.multiple1MTotalPuissanceSolaire;
    G_TotalPuissanceForKm = data.totalPuissanceForKm;
    G_multiple1Mkm = data.multiple1Mkm;
    G_TotalPuissanceAutoConso = data.totalPuissanceAutoConso;
    G_multiple1MTotalPuissanceAutoConso = data.multiple1MTotalPuissanceAutoConso;
    G_compteurInjectionEDF = data.compteurInjectionEDF * 3600;
    printf("ReadData : TotalPuissanceSolaire           = %ld\n", G_TotalPuissanceSolaire);
    printf("           multiplePuissanceSol            = %ld\n", G_multiple1MTotalPuissanceSolaire);
    printf("           TotalPuissanceForKm             = %ld\n", G_TotalPuissanceForKm);
    printf("           multiple1Mkm                    = %ld\n", G_multiple1Mkm);
    printf("           TotalPuissanceAutoConso         = %ld\n", G_TotalPuissanceAutoConso/3600);
    printf("           multipleTotalPuissanceAutoConso = %ld\n", G_multiple1MTotalPuissanceAutoConso*1000);
    printf("           compteurInjectionEDF            = %ld\n", G_compteurInjectionEDF/3600);
    close(f);
}

int getIndexFromPower(int power)
{
    if (power >= MAX_VALUE_FOR_MEASURES)
           return 256;
 
       if (power < 8)
           return -1;
             
    try
    {
        for (int i = 1 ; i < MAX_MESURES ; i++)
        {
            int offset = 0;
                    
            if (power <= mesures[i].power)
            {
                float a = (float)(mesures[i].power - mesures[i-1].power) / (float)(mesures[i].index - mesures[i-1].index);
                float b = mesures[i-1].power;
                    
                // y = a.x+b -> x = (y - b) / a
                float index = (float)(power - b) / (float)a + (float)mesures[i-1].index;
                if ((index - floor(index)) >= 0.5)
                    offset = 1;
                return (int)(index+offset);
            }
        } 
    } catch( const exception & e )
    {
        char tmp[128];
        sprintf(tmp, "Exception in getIndexFromPower with cause %s", e.what());
        logError(tmp);
        return -1;
    }
    return 255;
}
 
int getPowerFromIndex(int index)
{
    if (G_chargeDelestage)
    {
        return (int)((float)G_chargeDelestage * G_lastAction / 100.0);
    }

    if (index < 1)
        return 0;

    if (index == 256)
        return MAX_VALUE_FOR_MEASURES;
 
    try
    {
        for (int i = 1 ; i < MAX_MESURES ; i++)
        {
            if (index <= mesures[i].index)
            {
                float a = (float)((mesures[i].power - mesures[i-1].power)) / (float)((mesures[i].index - mesures[i-1].index));
                float b = mesures[i-1].power;
                    
                // y = a.x+b
                float power = a * (index - mesures[i-1].index) + b;
                return (int)power;
            }
        }
    } catch( const exception & e )
    {
        char tmp[128];
        sprintf(tmp, "Exception in getPowerFromIndex with cause %s", e.what());
        logError(tmp);
        return -1;
    }
    return -1;
}

int getPercent(long a, long b)
{
    int res;

    if (b ==0)
        return 0;

    float ra = (float)a;
    float rb = (float)b;
    try 
    {
        res = (int)(ra*100.0/rb);
    } catch( const exception & e )
    {
        char tmp[128];
        sprintf(tmp, "Exception in getPercent with cause %s", e.what());
        logError(tmp);
        res = 0;
    }
    return res;
}

void genHtmlDomoticzCommand(char *tmp)
{
    /* first what are we going to send and where are we going to send it? */
    int portno =        7979;
    char *host =        IP_ADDRESS_DOMOTICZ;
    int i;
    char response[5000];

    struct hostent *server;
    struct sockaddr_in serv_addr;
    int sockfd, bytes, sent, received, total;

    /* create the socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return;

    /* lookup the ip address */
    server = gethostbyname(host);
    if (server == NULL)
    {
    	close(sockfd);
    	return;
    }

    /* fill in the structure */
    memset(&serv_addr,0,sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);
    memcpy(&serv_addr.sin_addr.s_addr,server->h_addr,server->h_length);

    /* connect the socket */
    if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0)
    {
    	close(sockfd);
        return;
    }

    /* send the request */
    total = strlen(tmp);
    sent = 0;
    do {
        bytes = write(sockfd,tmp+sent,total-sent);
        if (bytes < 0)
            return;
        if (bytes == 0)
            break;
        sent+=bytes;
    } while (sent < total);

    //printf("logSol: connect OK - %d bytes sent\n", sent);

    /* Wait for data with timeout */
    struct pollfd fds;
    fds.fd = sockfd;
    fds.events = POLLIN;
    fds.revents = 0;

    if (poll(&fds, 1, 2000) <= 0)
    {
    	printf("Timeout on poll socket or poll error\r\n");
    	close(sockfd);
    	return;
    }

    /* receive the response */
    memset(response,0,sizeof(response));
    total = sizeof(response)-1;
    received = 0;
    do {
        bytes = read(sockfd,response+received,total-received);
        if (bytes < 0)
            return;
        if (bytes == 0)
            break;
        received+=bytes;
    } while (received < total);
    response[received] = 0;
    //printf("logSol: sent to domoticz OK - %d bytes received : %s\n", received, response);

    /* close the socket */
    close(sockfd);

    return;
}

void genHtmlDomoticzPower(int index, int value1, int value2)
{
    char tmp[256];

    sprintf(tmp, "GET /json.htm?type=command&param=udevice&idx=%d&nvalue=0&svalue=%d;%d HTTP/1.0\r\n\r\n", index, value1, value2);
    genHtmlDomoticzCommand(tmp);
}

void genHtmlDomoticz(int index, int value)
{
    char tmp[256];

    sprintf(tmp, "GET /json.htm?type=command&param=udevice&idx=%d&nvalue=0&svalue=%d HTTP/1.0\r\n\r\n", index, value);
    genHtmlDomoticzCommand(tmp);
}

void genHtmlDomoticzFloat(int index, float value)
{
    char tmp[256];

    sprintf(tmp, "GET /json.htm?type=command&param=udevice&idx=%d&nvalue=0&svalue=%f HTTP/1.0\r\n\r\n", index, value);
    genHtmlDomoticzCommand(tmp);
}

void genHtmlDomoticzSwitch(int index, int value)
{
	   char tmp[256];

	    sprintf(tmp, "GET /json.htm?type=command&param=switchlight&idx=%d&switchcmd=%s HTTP/1.0\r\n\r\n", index,
	    		(value==0) ? "Off" : "On");
	    genHtmlDomoticzCommand(tmp);
}

//
// Function genereHTML
//
void genDataSmartphone(struct tm *pTime)
{
    fstream file_out;
    int r1, r2;
    char tmp[512];
    string filename(HTML_FILE);
	
    file_out.open(filename, std::ios_base::out);
    if (!file_out.is_open()) 
    {
        cout << "failed to open " << filename << '\n';
        return;
    } 
    file_out << "<html><head><META HTTP-EQUIV=\"refresh\" CONTENT=\"5\"><TITLE>Installation Solaire de Lan Ar Gleiz (Version SmartData)</TITLE></head><body>" << endl;
    file_out << "@TEMPERATURE_PANNEAUX " << G_thSolarPanel << "<br>" << endl;
    file_out << "@TEMPERATURE_CUVE " << G_thCuve << "<br>" << endl;
    file_out << "@TEMPERATURE_EAU_CHAUDE " << G_thECS << "<br>" << endl;
    file_out << "@ETAT_CIRCULATEUR " << G_pump << "<br>" << endl;
    file_out << "@PUISSANCE_SOLAIRE " << G_Puissance << "<br>" << endl;
    file_out << "@PUISSANCE_CONSOMMEE " << G_pAppEDF << "<br>" << endl;
    file_out << "@PUISSANCE_INJECTEE " << G_prodEDF << "<br>" << endl;
    file_out << "@PUISSANCE_PRISE_CONNECTEE " << getPowerFromIndex(G_lastPower) << "<br>" << endl;
    file_out << "@PUISSANCE_AUTOCONSOMMATION " << G_PuissanceAutoConso << "<br>" << endl;
    file_out << "@EQU_KM " << (G_TotalPuissanceForKm/36000/15) + (G_multiple1Mkm * 1000) << "<br>" << endl;
    file_out << "@INDEX_LINKY " << G_compteurEDF << "<br>" << endl;
    file_out << "@T1 " << G_T1 << "<br>" << endl;
    file_out << "@T2 " << G_T2 << "<br>" << endl;
    file_out << "@FAN " << G_fan << "<br>" << endl;
    file_out << "@KACA " << G_KACA_State << "<br>" << endl;
    file_out << "@DELESTAGE " << G_chargeDelestage << "<br>" << endl;
    file_out << "@TEMPERATURE_ALARM " << isTemperatureAlarm << "<br>" << endl;

    file_out << "<br>Page generee le " << pTime->tm_mday << "/" << (pTime->tm_mon+1) << "/" << (pTime->tm_year+1900) << " " \
             << pTime->tm_hour << ":" << pTime->tm_min << ":" << pTime->tm_sec << " LogSol Version " << VERSION << "</body></html>" << endl;
    return;
}

void sendPuissance(int pSoutiree, int pInjectee)
{
#define SIZE_PUISSANCE 17
    char tmp[SIZE_PUISSANCE];
    int lg;
    struct sockaddr_in addr_serveur;
    int nb_octets;
    unsigned long realCompteurInjectionEDF = G_compteurInjectionEDF / 3600;

    tmp[0] = 'P';
    tmp[1] = 'U';
    tmp[2] = 'I';
    tmp[3] = 'S';
    tmp[4] = (pSoutiree & 0xFF);
    tmp[5] = (pSoutiree >> 8);
    tmp[6] = (pInjectee & 0xFF);
    tmp[7] = (pInjectee >> 8);
    tmp[8] = (G_compteurEDF & 0x000000FF);
    tmp[9] = (G_compteurEDF & 0x0000FF00) >> 8;
    tmp[10]= (G_compteurEDF & 0x00FF0000) >> 16;
    tmp[11]= (G_compteurEDF &0xFF000000) >> 24;
    tmp[12]= (realCompteurInjectionEDF & 0x000000FF);
    tmp[13]= (realCompteurInjectionEDF & 0x0000FF00) >> 8;
    tmp[14]= (realCompteurInjectionEDF & 0x00FF0000) >> 16;
    tmp[15]= (realCompteurInjectionEDF &0xFF000000) >> 24;
    tmp[16]= G_fan;
           
    printf("                                                                                  sendPuissance : Envoie de pSoutiree=%d pInjectee=%d IndexLinky=%d IndexInjection=%ld stateFan=%d\n", pSoutiree, pInjectee, G_compteurEDF, G_compteurInjectionEDF, G_fan);

    struct hostent *hostinfo = NULL;
    hostinfo = gethostbyname(IP_ADDRESS_ESP_PUIS); /* on récupère les informations de l'hôte auquel on veut se connecter */
    if (hostinfo == NULL) /* l'hôte n'existe pas */
        {
        fprintf (stderr, "Unknown host %s.\n", IP_ADDRESS_ESP_PUIS);
        return;
        }

    memset(&addr_serveur, 0, sizeof(struct sockaddr_in)); 
    addr_serveur.sin_family = AF_INET; 
    addr_serveur.sin_port = htons(UDP_PORT_PUIS); 
    memcpy(&addr_serveur.sin_addr.s_addr, hostinfo->h_addr, hostinfo->h_length);
    lg = sizeof(struct sockaddr_in);
    nb_octets = sendto(G_sockUDPRelay, tmp, SIZE_PUISSANCE, 0, (struct sockaddr*)&addr_serveur, lg);
    if (nb_octets == -1) 
        {
        perror("erreur envoi message"); 
        return;
        }
    return;
}

void processTemperatures()
{
    struct tm *pTime;
    struct timeval clock;
 
    if (gettimeofday(&clock, NULL) != -1)
    {
        pTime = localtime((time_t *)&clock.tv_sec);

        if (G_fan == 2 && (G_T2 < (G_T1+1.5) || G_T2 < 22) && !G_forceFan && G_KACA_State)
        {
            G_fan = 0;
            printf("                                                                                  Arret du Kit Air Chaud Automatisé : Temperatures trop faibles\n");
        }
        float externTemperature = G_thSolarPanel < G_T1 ? G_thSolarPanel : G_T1;
        float deltaT = externTemperature < 19.0 ? (19.0-externTemperature) : 0;
        if (G_fan == 0 && (G_T2 >= (23.0 + deltaT)) && G_KACA_State)
        {
            G_fan = 2;
            printf("                                                                                  Démarrage du Kit Air Chaud Automatisé\n");
        }
    }
}

void  processPower()
{
    int ret = FALSE;

    G_TotalPuissanceSolaire += G_Puissance*PERIODE_RELEVE;
    if (G_TotalPuissanceSolaire > (3600 * 1000))
    {
        G_multiple1MTotalPuissanceSolaire += 1;
        G_TotalPuissanceSolaire -= (3600 * 1000);
    }
    G_TotalPuissanceForKm += G_Puissance*PERIODE_RELEVE;
    if (G_TotalPuissanceForKm > (1000 * 36000 * 15))
    {
        G_TotalPuissanceForKm -= (1000 * 36000 * 15);
        G_multiple1Mkm++;
    }

    if (G_prodEDF > 0)
    {
        // Cas ou l'on fournit...
        G_PuissanceAutoConso = G_Puissance - G_prodEDF - getPowerFromIndex(G_lastPower);
        G_compteurInjectionEDF += G_prodEDF*PERIODE_RELEVE;   // Unité en Ws (Watt x secondes)
    }
    else
    {
        // Cas ou l'on consomme...
        G_PuissanceAutoConso = G_Puissance - getPowerFromIndex(G_lastPower);
    }
    if (G_PuissanceAutoConso < 0)
        G_PuissanceAutoConso = 0;
    G_TotalPuissanceAutoConso += G_PuissanceAutoConso*PERIODE_RELEVE;
    if (G_TotalPuissanceAutoConso > (1000 * 3600))
    {
        G_TotalPuissanceAutoConso -= (1000 * 3600);
        G_multiple1MTotalPuissanceAutoConso++;
    }
    
    genHtmlDomoticzPower(DOMOTICZ_INDEX_PRODUCTION_INST, G_Puissance, (G_TotalPuissanceSolaire/3600 + G_multiple1MTotalPuissanceSolaire*1000));
    genHtmlDomoticzPower(DOMOTICZ_INDEX_AUTOCONSO, G_PuissanceAutoConso, (G_TotalPuissanceAutoConso/3600 + G_multiple1MTotalPuissanceAutoConso*1000));
    if (G_TotalPuissanceSolairePrevious != G_TotalPuissanceForKm)
    {
        ret = TRUE;
        genHtmlDomoticz(DOMOTICZ_INDEX_KM_ZOE, (G_TotalPuissanceForKm/36000/15)+(G_multiple1Mkm * 1000));
        G_TotalPuissanceSolairePrevious = G_TotalPuissanceForKm;
    }
    G_count = (G_count + 1) & 1;

    G_saveRequired = ret;

    if (G_chargeDelestage && G_KACA_State)
    {
        if (G_prodEDF)
            sendPuissance(0, G_prodEDF);
        else
            sendPuissance(G_pAppEDF, 0);
    }
    if (G_chargeDelestage && !G_KACA_State && G_Stop_KACA)
    {
        if (G_Stop_KACA == 1)
            sendPuissance(0, 0);
        else
            sendPuissance(1000, 0);
        G_Stop_KACA--;
    }
}

char *getNextWord(char *s)
{
    char *p = strchr(s, ' '); 
    if (p == 0)
        return p;
    while (*p == ' ')
    {
        if (*p == 0)
            return 0;
        p++;
    }
    return p;
}

void processCommand(int command, int value)
{
    printf("Received Command %d with value %d\n", command, value);
    switch(command)
    {
    case COMMAND_FAN_ON :
        G_forceFan = 1;
        G_fan = 2;
        break;
    case COMMAND_FAN_OFF:
        G_forceFan = 0;
        G_fan = 0;
        break;
    case COMMAND_KACA_ON:
        G_KACA_State = 1;
        break;
    case COMMAND_KACA_OFF:
        G_KACA_State = 0;
        G_lastPower = 0;
        G_fan = 0;
        sendPuissance(1000, 0);
        G_Stop_KACA = 12;
        break;
    case COMMAND_SETLOG:
        nLogLevel = value;
        break;
    default:
        printf("Invalid command");
        break;
    }
    return;
}

void *UDP_monitoring_thread(void *param)
{
    unsigned int G_lastResetCounter = 0;
    int sockfd; 
    char tmp[256];
    struct sockaddr_in servaddr, cliaddr; 
    char *s1,*s2,*s3;
    int n; 
    unsigned int len; 
      
    // Creating socket file descriptor 
    if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) 
    { 
        perror("socket creation failed"); 
        return 0; 
    } 
      
    memset(&servaddr, 0, sizeof(servaddr)); 
      
    // Filling server information 
    servaddr.sin_family    = AF_INET; // IPv4 
    servaddr.sin_addr.s_addr = INADDR_ANY; 
    servaddr.sin_port = htons(PORT_UDP); 
      
    // Bind the socket with the server address 
    if (bind(sockfd, (const struct sockaddr *)&servaddr,  
            sizeof(servaddr)) < 0 ) 
    { 
        perror("bind failed"); 
        return 0; 
    } 
      
    int noError = 1;
    while (noError && !G_leave)
    {
        nBufferLength = recvfrom(sockfd, (char *)buffer, UDP_RX_BUFFER,  
                MSG_WAITALL, ( struct sockaddr *) &cliaddr, 
                &len); 
        buffer[nBufferLength] = '\0'; 

        //printf("[UDP Thread] Receive (Len=%d)  %s\n", nBufferLength, buffer);

        if (strncmp(buffer, "PZEM_SOLAIRE", 12) == 0)
        {
            printf("[UDP Thread] Receive (Len=%d)  %s\n", nBufferLength, buffer);
            s1 = strchr(buffer, ' ');
            if (s1)
            {
                int p = atoi(++s1);
                if (p != -1)
                {
                    if (p == 1)
                        p = 0;
                    G_Puissance = p;
                    //printf("[UDP Thread] Receive Puissance Solaire=%d\n", G_Puissance);
                }
                else
                    printf("[UDP Thread] Read error on Puissance Solaire : previous value reused\n"); 
                s2 = strchr(s1, ' ');
                if (s2 && (s3=strchr(++s2, ' ')))
                {
                    unsigned int sequence = atoi(++s3);
                    if ((G_lastResetCounter + 1) != sequence)
                    {
                        sprintf(tmp, "[UDP thread] PZEM_Solaire : invalid sequence (%d vs %ld)\n", sequence, G_lastResetCounter + 1);
                        logError(tmp);
                    }
                    G_lastResetCounter = sequence;
                }//if (s2)
                else
                {
                    logError("[UDP Thread] Read error on Puissance Solaire : sequence not found\n");
                }
            }//if (s1)
        }//if (PZEM_SOLAIRE)
        else if (strncmp(buffer, "PZEM_EDF", 8) == 0)
        {
            s1 = strchr(buffer, ' ');
            if (s1)
            {
                int p = atoi(++s1);
                if (p != -1)
                {
                    G_Puissance_PZEM_EDF = p;
                    printf("[UDP Thread] Receive Puissance PZEM EDF=%d\n", G_Puissance_PZEM_EDF);
                }
                else
                    logError("[UDP Thread] PZEM_EDF : -1 received, previous value reused\n"); 
                s2 = strchr(s1, ' ');
                if (s2 && (s3=strchr(++s2, ' ')))
                {
                    int sequence = atoi(++s3);
                    //printf("[UDP Thread] Sequence=%d\n", sequence);
                }//if (s2)
                else
                    logError("[UDP Thread] Read error on PZEM_EDF : sequence not found\n");
            }//if (s1)
            else
                logError("[UDP Thread] Read error on PZEM_EDF : value not found\n");
        }//if (PZEM_EDF)
        else if (strncmp(buffer, "TELEINFO", 8) == 0)
        {
            s1 = strchr(buffer, ' ');
            if (s1)
            {
                unsigned long newValue = atoi(++s1);
                if (G_compteurEDF == -1 )
                {
                    logError("[UDP Thread] Read error : decoded value for Compteur_EDF is -1\n"); 
                }
                G_compteurEDF = newValue;
                s1 = strchr(s1, ' ');
                if (s1)
                {
                    int n = atoi(++s1);
                }//if (s1)
                else
                    logError("[UDP Thread] Error on TELEINFO : first number (compteur EDF) not decoded\n");
                s1 = strchr(s1, ' ');
                if (s1)
                {
                    G_pAppEDF = atoi(++s1);
                    powerReceived = 1;
                    if (G_pAppEDF == 0)
                    {
                        G_prodEDF = G_Puissance_PZEM_EDF;
                        printf("[UDP Thread] Receive Puissance PZEM Production=%d\n", G_prodEDF);
                    }
                    else
                    {
                        G_pAppEDF = G_Puissance_PZEM_EDF;
                        G_prodEDF = 0;
                        printf("[UDP Thread] Receive Puissance PZEM Conso EDF=%d\n", G_pAppEDF);
                    }
                    genHtmlDomoticzPower(DOMOTICZ_INDEX_CONSO_EDF, G_pAppEDF, G_compteurEDF);
                    if (G_prodEDF != 0)
                    {
                        G_globalProdEDF += (G_prodEDF / 60 / 12);
                        genHtmlDomoticzPower(DOMOTICZ_INDEX_PROD_EDF, G_prodEDF, G_globalProdEDF);
                    }
                    else
                    {
                        genHtmlDomoticzPower(DOMOTICZ_INDEX_PROD_EDF, 0, G_globalProdEDF);
                    }
                    processPower();
                }//if (s1)
                else
                    logError("[UDP Thread] Error on TELEINFO : second number not decoded\n");
            }//if (s1)
        }//if (TELEINFO)
        else if (strncmp(buffer, "TEMP", 4) == 0)
        {
            float fTemp1, fTemp2;
            fTemp1 = atof(&buffer[5]);
            int m = 5;
            while (buffer[m] != 32 && m < UDP_RX_BUFFER)
                m++;
            if (m == UDP_RX_BUFFER)
            {
                printf("[UDP Thread] Error in TEMP : space not found\n");
                continue;
            }
            fTemp2 = atof(&buffer[m]);
            if (fTemp1 > 0.0 && fTemp2 > 0.0 && fTemp1 < 140.0 && fTemp2 < 140.0)
            {
                G_T1 = fTemp1;
                G_T2 = fTemp2;
                printf("[UDP Thread] Receive Temperatures [%f, %f]\n", G_T1, G_T2);
                genHtmlDomoticzFloat(DOMOTICZ_INDEX_T1, G_T1);
                genHtmlDomoticzFloat(DOMOTICZ_INDEX_T2, G_T2);
                processTemperatures();
            }
            else
            {
                sprintf(tmp, "[UDP Thread] Receive Temperatures with ERRORS - Keep old values [%f, %f]\n", G_T1, G_T2);
                printf(tmp);
            }
        }
        else if (strncmp(buffer, "THER", 4) == 0)
        {
            float fTemp1, fTemp2, fTemp3;
            fTemp1 = *(float *)&buffer[4];
            fTemp2 = *(float *)&buffer[8];
            fTemp3 = *(float *)&buffer[12];
            unsigned char pump = buffer[16];
            if (fTemp1 > 0.0   && fTemp2 > 0.0   && fTemp3 > 0.0   &&
                fTemp1 < 140.0 && fTemp2 < 140.0 && fTemp3 < 140.0)
            {
                G_thSolarPanel = fTemp1;
                G_thCuve = fTemp2;
                G_thECS = fTemp3;
                G_pump = pump;
                printf("[UDP Thread] Receive Thermal : [%f, %f, %f] Pompe=%d\n", G_thSolarPanel, G_thCuve, G_thECS, G_pump);
                genHtmlDomoticzFloat(DOMOTICZ_INDEX_PANNEAUX_SOLAIRES, G_thSolarPanel);
                genHtmlDomoticzFloat(DOMOTICZ_INDEX_CUVE, G_thCuve);
                genHtmlDomoticzFloat(DOMOTICZ_INDEX_ECS, G_thECS);
                if (G_previousPump != G_pump)
                {                  
                    genHtmlDomoticzSwitch(DOMOTICZ_INDEX_POMPE, G_pump);
                    G_previousPump = G_pump;
                }
                G_lastReceivedTemperature = G_globalSeconds;
                isTemperatureAlarm = 0;
            }
            else
            {
                sprintf(tmp, "[UDP Thread] Receive Temperatures with ERRORS - Keep old values [%f, %f, %f]\n", G_thSolarPanel, G_thCuve, G_thECS);
                printf(tmp);
            }
        }
        else if (strncmp(buffer, "FACT", 4) == 0)
        {
            float fAction;
            fAction = *(float *)&buffer[4];
            if (fAction >= 0.0   && fAction <= 100.0)
            {
                G_lastAction = fAction;
            }
            if (G_chargeDelestage)
            {
                G_T1 = *(float *)&buffer[8];
                G_T2 = *(float *)&buffer[12];
                printf("[UDP Thread] Receive FACT : %f %f°C %f°C\n", fAction, G_T1, G_T2);
                genHtmlDomoticzFloat(DOMOTICZ_INDEX_T1, G_T1);
                genHtmlDomoticzFloat(DOMOTICZ_INDEX_T2, G_T2);
                processTemperatures();
                genHtmlDomoticz(DOMOTICZ_INDEX_PRISE_COMMANDEE, getPowerFromIndex(G_lastAction));
            }
        }
        else if (strncmp(buffer, "COMMAND", 7) == 0)
        {
            char *command = buffer+8;
            char *next = getNextWord(command);
            printf("Command received: [%s]\n", command);
            if (strncmp(command, "FAN_ON", 6) == 0)
            {
                processCommand(COMMAND_FAN_ON, 0);
            }
            else if (strncmp(command, "FAN_OFF", 7) == 0)
            {
                processCommand(COMMAND_FAN_OFF, 0);
            }
            else if (strncmp(command, "KACA_ON", 7) == 0)
            {
                processCommand(COMMAND_KACA_ON, 0);
            }
            else if (strncmp(command, "KACA_OFF", 8) == 0)
            {
                processCommand(COMMAND_KACA_OFF, 0);
            }
        }
        else
        {
            sprintf(tmp, "[UDP Thread] Data not recognized : %s\n", buffer);
            printf(tmp);
        }
   }//while (noError)
   return 0;
}

void signal_function(int param)
{
    // char tmp[1024];
    // int n = 0;

    // n+=sprintf(&tmp[n], "Signal Function catched with code %d \n", param);
    // n+=sprintf(&tmp[n], "LastRx : n=%d %s\n", nBufferLength, buffer);
    // logError(tmp);
    G_leave = 1;
}

// Function Main
int main(int argc, char **argv)
{
    int ret;
    pthread_t threadUDP;
    struct tm *pTime;
    struct timeval clock;

    if (argc == 2)
    {
        G_chargeDelestage = (unsigned long)atol(argv[1]);
    }

    printf("logSol - Version " VERSION "   - ChargeDelestage=%ld\n", G_chargeDelestage);
	
    ret = pthread_create(&threadUDP, NULL, UDP_monitoring_thread, NULL);
    if (ret)
    {
        printf("Cannot create thread UDP_monitoring_thread\n");
        exit(-1);
    }

    G_sockUDPRelay = socket(AF_INET, SOCK_DGRAM, 0); 
    if (G_sockUDPRelay == -1) 
    {
        perror("Error while creating UDP socket"); 
        exit(-1);
    }

    loadData();

    while (G_leave == 0) 
    {
        int count = 0;
        while (count < 5 && powerReceived == 0 )
        {
            sleep(1);
            G_globalSeconds++;
            if (G_globalSeconds == 0)
                G_lastReceivedTemperature = 0;
            count++;
        }
    
        if ((G_globalSeconds - G_lastReceivedTemperature) > 120)
        {
            isTemperatureAlarm = 1;
        }

        powerReceived = 0;
        printf("                                                                                  Puissance Solaire:%d ConsoEDF:%d ProdEDF:%d\n", G_Puissance, G_pAppEDF, G_prodEDF);
        if (G_saveRequired)
        {
            saveData();
            G_saveRequired = 0;
        }

        if (gettimeofday(&clock, NULL) != -1)
        {
            pTime = localtime((time_t *)&clock.tv_sec);
            genDataSmartphone(pTime);
        }
    }
    if (G_leave)
    {
        logError("Signal catched - leaving\n");
        exit(-1);
    }
		
    printf("Fermeture du port - Sortie du programme\n");
    return 0;
}
