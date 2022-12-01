//--------------------------------------------------------//
// Application LogSolaire                                 //
//                                                        //
// 30/10/2016 : 4.0 - Version pour smartphones            //
// 19/12/2017 : 5.0 - Recuperation puissance Envoy        //
// 08/02/2018 : 5.1 - Interface Domoticz                  //
// 28/10/2018 : 5.2 - Comparaison calcul Tarif jour/nuit  //
// 30/11/2019 : 6.0 - Thread UDP pour wattmetre           //
// 07/11/2022 : 7.0 - Kit Air Chaud Ameliore              //
//--------------------------------------------------------//

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <poll.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include "logSol.h"

#include <netdb.h> /* struct hostent, gethostbyname */
#include <asm/socket.h>			/* arch-dependent defines	*/
#include <linux/sockios.h>		/* the SIOCxxx I/O controls	*/

char response[16000];

int readWattmetre(int device);
int GetEnvoy(int *pCumul, int *pCurrent);
void genHtmlDomoticzCommand(char *tmp);
void log_printf(const char * format, ... );
void genHtmlESPRelay(char *tmp);
void genHtmlDomoticz(int index, int value);
void genHtmlDomoticzFloat(int index, float value);

unsigned long G_resetCounter = 0;
unsigned long G_lastResetCounter = 0;
int G_resetGlobalWattmetreSolaire;
int G_Puissance;
int G_Puissance_PZEM_EDF;
long G_PuissanceTotalJour;
unsigned long G_PuissanceTotalAnnee;
long G_PuissanceTotalJourAvant;
unsigned long G_PuissanceTotalAnneeAvant;
int  G_DureeProtectionGel;
int G_compteurEDF;
int G_globalProdEDF;
int G_pAppEDF;
int G_prodEDF;
float G_T1;
float G_T2;
int G_fan = 0;
int G_Relais_1 = 0;
FILE *G_fdLog;
int G_logFileIndex = 0;
int G_lastCompteurEDF = 0;
int g_sockUDPRelay;
long G_AutoConsommation = 0;
long G_AutoConsommationAvant = 0;
long G_TotalPuissanceSolaire = 0;
long G_TotalPuissanceSolaireAvant = 0;
long G_TotalPuissanceConsommee = 0;
long G_TotalPuissanceConsommeeAvant = 0;

#define TARIF_NORMAL   0.1410   // Tarif normal en centime euros par kWh
#define TARIF_JOUR     0.1672
#define TARIF_NUIT     0.1158



// Declaration des variables globales
int leave = 0;
struct sInfo tabHour[LOG_DURATION];
struct sInfo tabDay[LOG_DURATION];
struct sInfoCumulMonth tabCumulMonth[LOG_MONTH_DURATION];
int nCurrentTabHourIndex;
int nCurrentTabDayIndex;
struct sInfoHistoMonth tabHistoMonth[LOG_MONTH_DURATION]; // Historique sur 60 mois
char tmp[8000];
char listData[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
int  mday_month[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
char *mois[12] = { "Jan", "Fev", "Mar", "Avr", "Mai", "Jui", "Jul", "Aou", "Sep", "Oct", "Nov", "Dec" };

#define CONV(a) ((a >= 62) ? listData[61] : (a < 0) ? listData[0] : listData[a])

int tabPuissance[LOG_DURATION];

// Declaration des fonctions
int openSerialPort(void);
void saveData(void);
void loadData(void);
int getData(int fd, struct sInfo *s);
int setESPRelay(int state, int value);
void genHtmlDomoticzSwitch(int index, int value);


struct point
{   
    int index;
    int power;
};
 
#define MAX_MESURES  6

// This measures comes from direct mesure of consumption of Radiator
struct point mesures[MAX_MESURES] = { {1,79}, {40, 272}, {100, 387}, {150, 430}, {200, 470}, {255, 500} };
 
int getIndexFromPower(int power)
{
    if (power >= 643)
           return 256;
 
       if (power < 79)
           return -1;
             
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
      
       return 255;
}
 
int getPowerFromIndex(int index)
{
    if (index < 1)
        return 0;

    if (index == 256)
           return 643;
 
    for (int i = 1 ; i < MAX_MESURES ; i++)
           {
              if (index <= mesures[i].index)
                  {
                     float a = (float)((mesures[i].power - mesures[i-1].power)) / (float)((mesures[i].index - mesures[i-1].index));
                     float b = mesures[i-1].power;
                    
                     // y = a.x+b
                     float power = a * (index - mesures[i-1].index) + b;
                     if (power < 100.0)
                         return (int)power;
                     return (int)(power - power/10.0);
                     }
              }
      
       return -1;
}

int updatePower(int lastIndex, int power)
{
    int newPower = power + getPowerFromIndex(lastIndex);
    int newIndex = getIndexFromPower(newPower);

    printf("updatePower: lastIndex=%d power=%d RealPower=%d newIndex=%d\n", lastIndex, power, newPower, newIndex);

    if (newIndex == lastIndex)
        return newIndex;

    if (newIndex < 1)
        {
        setESPRelay(0, 1);
        if (G_Relais_1)
            genHtmlDomoticzSwitch(DOMOTICZ_INDEX_RELAIS_COMMANDE, 0);
        G_Relais_1 = 0;
        return newIndex;
        }
    setESPRelay(1, newIndex);
    if (G_Relais_1 == 0)
        genHtmlDomoticzSwitch(DOMOTICZ_INDEX_RELAIS_COMMANDE, 1);
    G_Relais_1 = 1;
    return newIndex; 
}

void signal_function(int param)
{
	leave = 1;
}

int getPercent(long a, long b)
{
	if (b ==0)
		return 0;

	float ra = (float)a;
	float rb = (float)b;
	return (int)(ra*100.0/rb);
}

void *UDP_monitoring_thread(void *param)
{
    int sockfd; 
    char buffer[200]; 
    struct sockaddr_in servaddr, cliaddr; 
    char *s1,*s2,*s3;
      
    // Creating socket file descriptor 
    if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
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
    while (noError)
        {
        int len, n; 
        n = recvfrom(sockfd, (char *)buffer, 200,  
                MSG_WAITALL, ( struct sockaddr *) &cliaddr, 
                &len); 
        buffer[n] = '\0'; 

        //printf("[UDP Thread] Receive from %s\n", buffer);

        if (strncmp(buffer, "PZEM_SOLAIRE", 12) == 0)
            {
            s1 = strchr(buffer, ' ');
            if (s1)
                {
                int p = atoi(++s1);
                if (p != -1)
                    G_Puissance = p;
                else
                    printf("[UDP Thread] Read error : previous value reused\n"); 
                s2 = strchr(s1, ' ');
                if (s2 && (s3=strchr(++s2, ' ')))
                    {
                    int sequence = atoi(++s3);
                    if (sequence < G_lastResetCounter && G_lastResetCounter != 0)
                        G_resetGlobalWattmetreSolaire++;
                    G_lastResetCounter = sequence;
                    }//if (s2)
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
                    //printf("[UDP Thread] Receive Puissance PZEM EDF=%d\n", G_Puissance_PZEM_EDF);
                    }
                else
                    printf("[UDP Thread] Read error : previous value reused\n"); 
                s2 = strchr(s1, ' ');
                if (s2 && (s3=strchr(++s2, ' ')))
                    {
                    int sequence = atoi(++s3);
                    //printf("[UDP Thread] Sequence=%d\n", sequence);
                    }//if (s2)
                }//if (s1)
            }//if (PZEM_EDF)
        else if (strncmp(buffer, "TELEINFO", 8) == 0)
            {
            s1 = strchr(buffer, ' ');
            if (s1)
                {
                G_compteurEDF = atoi(++s1);
                if (G_compteurEDF == -1)
                    printf("[UDP Thread] Read error : previous value reused\n"); 
                s1 = strchr(s1, ' ');
                if (s1)
                    {
                    int n = atoi(++s1);
                    }//if (s1)
                s1 = strchr(s1, ' ');
                if (s1)
                    {
                    G_pAppEDF = atoi(++s1);
                    if (G_pAppEDF == 0)
                        G_prodEDF = G_Puissance_PZEM_EDF;
                    else
                       {
                       G_pAppEDF = G_Puissance_PZEM_EDF;
                       G_prodEDF = 0;
                       }
                    }//if (s1)
                s1 = strchr(s1, ' ');
                if (s1)
                    {
                    int n = atoi(++s1);
                    }//if (s2)
                }//if (s1)
            }//if (TELEINFO)
        else if (strncmp(buffer, "TEMP", 4) == 0)
        {
            float fTemp1, fTemp2;
            fTemp1 = atof(&buffer[5]);
            int n = 5;
            while (buffer[n] != 32)
                n++;
            fTemp2 = atof(&buffer[n]);
            if (fTemp1 > 0.0 && fTemp2 > 0.0 && fTemp1 < 140.0 && fTemp2 < 140.0)
            {
                G_T1 = fTemp1;
                G_T2 = fTemp2;
                //printf("[UDP Thread] Receive Temperatures [%f, %f]", G_T1, G_T2);
                genHtmlDomoticzFloat(DOMOTICZ_INDEX_T1, G_T1);
                genHtmlDomoticzFloat(DOMOTICZ_INDEX_T2, G_T2);
            }
            else
            {
                printf("[UDP Thread] Receive Temperatures with ERRORS - Keep old values [%f, %f]", G_T1, G_T2);
            }
        }
        else
            printf("[UDP Thread] Data not recognized : %s", buffer);
        }//while (noError)
   return 0;
}

//
// Function genereHTML
//
int genHTML(struct sInfo *s, struct tm *pTime)
{
	FILE *fHTML;
	int n = 0;
	int i;
	int index;
	int nbMonths;
	int maxHour;
	int firstMonth;
	int lastMonth;
	int current, cumul;
	int max;
	int temp;
	int r1, r2;
	
	fHTML = fopen(HTML_FILE, "w");
	if (fHTML < 0)
		{
		printf("-- Error : cannot open %s\n", HTML_FILE);
		return 0;
		}
		
	fputs("<html><head><META HTTP-EQUIV=\"refresh\" CONTENT=\"60\"><TITLE>Installation Solaire de Lan Ar Gleiz</TITLE></head><body>", fHTML);
	fputs("<script> (function(i,s,o,g,r,a,m){i['GoogleAnalyticsObject']=r;i[r]=i[r]||function(){\r\n", fHTML);
	fputs("  (i[r].q=i[r].q||[]).push(arguments)},i[r].l=1*new Date();a=s.createElement(o),\r\n", fHTML);
	fputs("  m=s.getElementsByTagName(o)[0];a.async=1;a.src=g;m.parentNode.insertBefore(a,m)\r\n", fHTML);
	fputs("  })(window,document,'script','//www.google-analytics.com/analytics.js','ga');\r\n", fHTML);
	fputs("  ga('create', 'UA-44232486-1', 'lvam.dyndns.org');\r\n", fHTML);
	fputs("  ga('send', 'pageview');\r\n", fHTML);
	fputs("</script>\r\n", fHTML);
	fputs("<h1>Installation solaire de Lan Ar Gleiz</h1>", fHTML);

	fputs("<center><a href=\"histo.html\">Historique mensuel</a>", fHTML);
	fputs("<TABLE BORDER WIDTH=\"90%\"><TR><TD ALIGN=CENTER>", fHTML);
	sprintf(tmp, "<img src=\"http://chart.apis.google.com/chart?cht=gom&chd=t:%d&chs=300x200&chl=T=%d&chds=0,80&chco=00FF00,FF0000&chdl=Temperature%%20Panneaux%%20Solaires&chdlp=b\"></TD><TD ALIGN=CENTER>",
		s->TpanneauxSolaires, s->TpanneauxSolaires); 
	fputs(tmp, fHTML);
	sprintf(tmp, "<img src=\"http://chart.apis.google.com/chart?cht=gom&chd=t:%d&chs=300x200&chl=T=%d&chds=0,80&chco=00FF00,FF0000&chdl=Temperature%%20Cuve&chdlp=b\"></TD><TD ALIGN=CENTER>",
		s->Tcuve, s->Tcuve); 
	fputs(tmp, fHTML);
	sprintf(tmp, "<img src=\"http://chart.apis.google.com/chart?cht=gom&chd=t:%d&chs=300x200&chl=T=%d&chds=0,80&chco=00FF00,FF0000&chdl=Temperature%%20E.C.S&chdlp=b\"></TD></TR>",
		s->Tchaudiere, s->Tchaudiere); 
	fputs(tmp, fHTML);
	fputs("<TR><TH COLSPAN=3 ALIGN=CENTER>Mode de fonctionnement : ", fHTML);
	if (s->mode == 'C')
		fputs("CHAUDIERE + Prechauffage solaire", fHTML);
	else
		fputs("SOLAIRE + Appoint EDF", fHTML);
	fputs("</TR>", fHTML);
		
	fputs("<TR><TH COLSPAN=3 ALIGN=CENTER>Historique detaille sur 2 heures<br>\r\n", fHTML);

// Generation du graphique HOUR
	max = 1;
	for (i = 0 ; i < LOG_DURATION ; i++)
       if (tabPuissance[i] > max)  max = tabPuissance[i];

	n = 0;
	n = sprintf(tmp, "<img src=\"http://chart.apis.google.com/chart?cht=lc&chd=s:");

	for (i = 0 ; i < LOG_DURATION ; i++)
		{
		index = (nCurrentTabHourIndex + i);
		if (index >= LOG_DURATION)
			index = index - LOG_DURATION;
        temp = tabPuissance[index]*62/max;
        if (temp >= 62) temp = 61;
		n += sprintf(&tmp[n], "%c", listData[temp]);
		}

	n += sprintf(&tmp[n], "&chs=900x200&chco=FF0000&chdl=Puissance(%dW)&chg=0,13\"><br><br>\r\n",max);
	fputs(tmp, fHTML);

	n = 0;
	n = sprintf(tmp, "<img src=\"http://chart.apis.google.com/chart?cht=lc&chd=s:");
	for (i = 0 ; i < LOG_DURATION ; i++)
		{
		index = (nCurrentTabHourIndex + i);
		if (index >= LOG_DURATION)
			index = index - LOG_DURATION;
		if (i < (LOG_DURATION-1))
			{
			n+= sprintf(&tmp[n], "%c", listData[tabHour[index].TpanneauxSolaires*SCALE_FACTOR]);
			}
		else
			n+= sprintf(&tmp[n], "%c,", listData[tabHour[index].TpanneauxSolaires*SCALE_FACTOR]);
		}
	for (i = 0 ; i < LOG_DURATION ; i++)
		{
		index = (nCurrentTabHourIndex + i);
		if (index >= LOG_DURATION)
			index = index - LOG_DURATION;
		if (i < (LOG_DURATION-1))
			{
			n+= sprintf(&tmp[n], "%c", listData[tabHour[index].Tcuve*SCALE_FACTOR]);
			}
		else
			{
			n+= sprintf(&tmp[n], "%c,", listData[tabHour[index].Tcuve*SCALE_FACTOR]);
			}
		}
	for (i = 0 ; i < LOG_DURATION ; i++)
		{
		index = (nCurrentTabHourIndex + i);
		if (index >= LOG_DURATION)
			index = index - LOG_DURATION;
		if (i < (LOG_DURATION-1))
			{
			n+= sprintf(&tmp[n], "%c", listData[tabHour[index].Tchaudiere*SCALE_FACTOR]);
			}
		else
			{
			n+= sprintf(&tmp[n], "%c,", listData[tabHour[index].Tchaudiere*SCALE_FACTOR]);
			}
		}
	for (i = 0 ; i < LOG_DURATION ; i++)
		{
		index = (nCurrentTabHourIndex + i);
		if (index >= LOG_DURATION)
			index = index - LOG_DURATION;
		if (i < (LOG_DURATION-1))
			{
			n+= sprintf(&tmp[n], "%c", listData[tabHour[index].pompe*SCALE_FACTOR]);
			}
		else
			{
			n+= sprintf(&tmp[n], "%c", listData[tabHour[index].pompe*SCALE_FACTOR]);
			}
		}

	n+= sprintf(&tmp[n], "&chs=900x200&chco=FF0000,0000FF,F0F000,00FF00&chdl=T.P.Solaires|T.cuve|T.chaudiere|Pompe&chg=0,13\"></TH></TR>");
	fputs(tmp, fHTML);		

// Generation du graphique DAY
	fputs("<TR><TH COLSPAN=3 ALIGN=CENTER>Historique sur 6 jours (tous les 1/4h, sauf 0h-7h)<br>", fHTML);

	max = 1;
	for (i = 0 ; i < LOG_DURATION ; i++)
	{
		if (i == nCurrentTabDayIndex)
			temp = tabDay[i].puissance / 3600;
		else
			temp = tabDay[i].puissance;
		if (temp > max) max = temp;
	}

	n = 0;
	n = sprintf(tmp, "<img src=\"http://chart.apis.google.com/chart?cht=lc&chd=s:");

	for (i = 0 ; i < LOG_DURATION ; i++)
		{
		index = (nCurrentTabDayIndex + i);
		if (index >= LOG_DURATION)
			index = index - LOG_DURATION;
        temp = tabDay[index].puissance*62/max;
        if (temp >= 62) temp = 61;
        if (temp < 0) temp = 0;
		n += sprintf(&tmp[n], "%c", listData[temp]);
		}

	n += sprintf(&tmp[n], "&chs=900x200&chco=FF0000&chdl=Puissance(%dWh)&chg=0,13\"><br><br>\r\n", max);
	fputs(tmp, fHTML);

	n = 0;
	n+= sprintf(&tmp[n], "<img src=\"http://chart.apis.google.com/chart?cht=lc&chd=s:");
	for (i = 1 ; i < LOG_DURATION ; i++)
		{
	        index = (nCurrentTabDayIndex + i);
		if (index >= LOG_DURATION)
			index = index - LOG_DURATION;
		n+= sprintf(&tmp[n], "%c", CONV(tabDay[index].TpanneauxSolaires*SCALE_FACTOR));
		}
	for (i = 1 ; i < LOG_DURATION ; i++)
		{
		index = (nCurrentTabDayIndex + i);
		if (index >= LOG_DURATION)
			index = index - LOG_DURATION;
		n+= sprintf(&tmp[n], "%c", CONV(tabDay[index].Tcuve*SCALE_FACTOR));
		}
	for (i = 1 ; i < LOG_DURATION ; i++)
		{
		index = (nCurrentTabDayIndex + i);
		if (index >= LOG_DURATION)
			index = index - LOG_DURATION;
		n+= sprintf(&tmp[n], "%c", CONV(tabDay[index].Tchaudiere*SCALE_FACTOR));
		}
	for (i = 1 ; i < LOG_DURATION ; i++)
		{
		index = (nCurrentTabDayIndex + i);
		if (index >= LOG_DURATION)
			index = index - LOG_DURATION;
		n+= sprintf(&tmp[n], "%c", CONV(tabDay[index].pompe*SCALE_FACTOR));
		}

	n+= sprintf(&tmp[n], "&chs=900x200&chco=FF0000,0000FF,F0F000,00FF00&chdl=T.P.Solaires|T.cuve|T.chaudiere|Pompe&chg=0,13\"></TH></TR>");
	fputs(tmp, fHTML);	

// Generation du graphique MONTH
	fputs("<TR><TH COLSPAN=3 ALIGN=CENTER>Historique mensuel<br>", fHTML);

	// Calcul du nombre de mois a afficher et du maximum d'heures
	nbMonths = 0;
	maxHour = 0;
	for (i = 0 ; i < LOG_MONTH_DURATION ; i++)
		{
		if (strlen(tabCumulMonth[i].month))
			{
			nbMonths++;
			if ((tabCumulMonth[i].nbSeconds / 3600) > maxHour)
				maxHour = (tabCumulMonth[i].nbSeconds / 3600);
			}
		}

	firstMonth = 0;
	lastMonth = firstMonth + NB_MONTHS_DISPLAY - 1;
	if (lastMonth >= nbMonths)
		lastMonth = nbMonths - 1;
				
	while (firstMonth < nbMonths)
		{	
		n = 0;
		n+= sprintf(&tmp[n], "<img src=\"http://chart.apis.google.com/chart?chs=900x%d&cht=bhs&chco=4D89F9&chbh=20&chd=s:", ((lastMonth - firstMonth + 1)*25));	
			
		for (i = firstMonth ; i <= lastMonth ; i++)
			{
			if (strlen(tabCumulMonth[i].month))
				{
				//printf("Calcul du mois %s - Max=%d - Val=%d - Index=%d\n", tabCumulMonth[i].month, maxHour, (tabCumulMonth[i].nbSeconds / 3600), (tabCumulMonth[i].nbSeconds / 3600 * 61)/maxHour); 
				n += sprintf(&tmp[n], "%c", listData[(tabCumulMonth[i].nbSeconds / 3600 * 61)/maxHour]);
				}
			}
		n += sprintf(&tmp[n], "&chxl=0:");
		for (i = lastMonth ; i >= firstMonth ; i--)
			{
			if (strlen(tabCumulMonth[i].month))
				{
				n += sprintf(&tmp[n], "|%s-(%3dh)", tabCumulMonth[i].month, (tabCumulMonth[i].nbSeconds / 3600));
				}
			}
		n += sprintf(&tmp[n], "&chxt=y&chm=N,000000,0,-1,11\"><br>");
		fputs(tmp, fHTML);
		
		firstMonth = lastMonth + 1;
		lastMonth = lastMonth + NB_MONTHS_DISPLAY;
		if (lastMonth >= nbMonths)
			lastMonth = nbMonths - 1;
		}
			
	n = sprintf(tmp, "</TH></TR></TABLE><br><CENTER>Page generee le %d/%d/%d %02d:%02d:%02d LogSol Version %s - ",
		pTime->tm_mday, (pTime->tm_mon+1), (pTime->tm_year+1900), pTime->tm_hour, pTime->tm_min, pTime->tm_sec, VERSION);
		 
	fputs(tmp, fHTML);
    	n = sprintf(tmp, "<br>Puissance Solaire=%d W (Reset=%u) Pconso=%d W Pfournie=%d W", G_Puissance, G_resetGlobalWattmetreSolaire, G_pAppEDF, G_prodEDF);
	fputs(tmp, fHTML);
	n = sprintf(tmp, " Journalier=%d Wh Hier=%d Wh Annuel=%d kWh (An n-1=%d kWh)  soit <a>%d km</a><br>", G_PuissanceTotalJour/3600, G_PuissanceTotalJourAvant/3600, G_PuissanceTotalAnnee/3600000, G_PuissanceTotalAnneeAvant/3600000,
			G_PuissanceTotalAnnee/36000/14);
	fputs(tmp, fHTML);
	r1 = getPercent(G_AutoConsommation, G_TotalPuissanceSolaire);
	r2 = getPercent(G_AutoConsommation, G_TotalPuissanceConsommee);
        sprintf(tmp, "<br>En cours : AutoConsommation=%ld Wh TotalSolaire=%ld Wh R=%d%% TotalConsommee=%ld Wh R=%d%%", G_AutoConsommation/3600, G_TotalPuissanceSolaire/3600, r1, G_TotalPuissanceConsommee/3600, r2);
	fputs(tmp, fHTML);
	r1 = getPercent(G_AutoConsommationAvant, G_TotalPuissanceSolaireAvant);
	r2 = getPercent(G_AutoConsommationAvant, G_TotalPuissanceConsommeeAvant);
        sprintf(tmp, "<br>An n-1   : AutoConsommation=%ld Wh TotalSolaire=%ld Wh R=%d%% TotalConsommee=%ld Wh R=%d%%", G_AutoConsommationAvant/3600, G_TotalPuissanceSolaireAvant/3600, r1, G_TotalPuissanceConsommeeAvant/3600, r2);
	fputs(tmp, fHTML);
        sprintf(tmp, "<br>Duree protection Gel : %d s ", G_DureeProtectionGel);
	fputs(tmp, fHTML);
	n = sprintf(tmp, "</CENTER></body></html>");
	fputs(tmp, fHTML);

	fclose(fHTML);

	return 0;
}

void addKeyword(FILE *fd, char *word, int value)
{
    char tmp[128];
  
    sprintf(tmp, "%s %d <br>\r\n", word, value);
    fputs(tmp, fd);
}

void addKeywordFloat(FILE *fd, char *word, float value)
{
    char tmp[128];
  
    sprintf(tmp, "%s %f <br>\r\n", word, value);
    fputs(tmp, fd);
}

//
// Function genereHTML
//
int genDataSmartphone(struct sInfo *s, struct tm *pTime, int relais)
{
	FILE *fHTML;
	int n = 0;
	int i;
	int index;
	int nbMonths;
	int maxHour;
	int firstMonth;
	int lastMonth;
	int r1, r2;
	
	fHTML = fopen(HTML_SMARTPHONE_FILE, "w");
	if (fHTML < 0)
		{
		printf("-- Error : cannot open %s\n", HTML_SMARTPHONE_FILE);
		return 0;
		}
		
	fputs("<html><head><META HTTP-EQUIV=\"refresh\" CONTENT=\"5\"><TITLE>Installation Solaire de Lan Ar Gleiz (Version SmartData)</TITLE></head><body>", fHTML);
	fputs("<script> (function(i,s,o,g,r,a,m){i['GoogleAnalyticsObject']=r;i[r]=i[r]||function(){\r\n", fHTML);
	fputs("  (i[r].q=i[r].q||[]).push(arguments)},i[r].l=1*new Date();a=s.createElement(o),\r\n", fHTML);
	fputs("  m=s.getElementsByTagName(o)[0];a.async=1;a.src=g;m.parentNode.insertBefore(a,m)\r\n", fHTML);
	fputs("  })(window,document,'script','//www.google-analytics.com/analytics.js','ga');\r\n", fHTML);
	fputs("  ga('create', 'UA-44232486-1', 'lvam.dyndns.org');\r\n", fHTML);
	fputs("  ga('send', 'pageview');\r\n", fHTML);
	fputs("</script>\r\n", fHTML);
        addKeyword(fHTML, "@TEMPERATURE_PANNEAUX",   s->TpanneauxSolaires);
        addKeyword(fHTML, "@TEMPERATURE_CUVE",       s->Tcuve);
        addKeyword(fHTML, "@TEMPERATURE_EAU_CHAUDE", s->Tchaudiere);
        addKeyword(fHTML, "@ETAT_CIRCULATEUR",       s->pompe);
        addKeyword(fHTML, "@DUREE_PROTECTION_GEL",   G_DureeProtectionGel);
        addKeyword(fHTML, "@PUISSANCE_SOLAIRE",      G_Puissance);
        addKeyword(fHTML, "@PUISSANCE_CONSOMMEE",    G_pAppEDF);
        addKeyword(fHTML, "@PUISSANCE_INJECTEE",     G_prodEDF);
        addKeyword(fHTML, "@ETAT_PRISE_CONNECTEE",   relais);
        addKeyword(fHTML, "@KM_ZOE",                 (G_PuissanceTotalAnnee/36000/14));
        addKeyword(fHTML, "@INDEX_LINKY",            G_compteurEDF);
	r1 = getPercent(G_AutoConsommation, G_TotalPuissanceSolaire);
	r2 = getPercent(G_AutoConsommation, G_TotalPuissanceConsommee);
	addKeyword(fHTML, "@TAUX_PRODUCTION_SOLAIRE",r1);
	addKeyword(fHTML, "@TAUX_AUTOCONSOMMATION",  r2);
        addKeywordFloat(fHTML, "@T1",                     G_T1);
        addKeywordFloat(fHTML, "@T2",                     G_T2);
        addKeyword(fHTML, "@FAN",                    G_fan);

	sprintf(tmp, "<br>Page generee le %d/%d/%d %02d:%02d:%02d LogSol Version %s",
		pTime->tm_mday, (pTime->tm_mon+1), (pTime->tm_year+1900), pTime->tm_hour, pTime->tm_min, pTime->tm_sec, VERSION);
	fputs(tmp, fHTML);
	fputs("</body></html>\r\n", fHTML);

	fclose(fHTML);

	return 0;
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
void genHtmlDomoticzCommand(char *tmp)
{
    /* first what are we going to send and where are we going to send it? */
    int portno =        7979;
    char *host =        IP_ADDRESS_DOMOTICZ;
    int i;

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


char *readUntilTag(char *string, char *pTag)
{
	int n = strlen(pTag);
	int length = strlen(string);
	int index = 0;
	char *p = string;

	while (strncmp(p, pTag, n) != 0)
	{
		p++; index++;
		if ((index + n) >= length)
			return NULL;
	}
	p += n;
	return p;
}

int getPowerValue(char *response, int *pCumul, int *pCurrent)
{
    char *p = response;

    if ((p = readUntilTag(p, "<td>Cumul Production</td>")) == NULL) return -1;
    if ((p = readUntilTag(p, "<td>")) == NULL) return -1;
    //printf("Conversion du cumul de production : %s = %d\n", p, atoi(p));
    *pCumul = atoi(p);
    if ((p = readUntilTag(p, "<td>Production instantanée</td>")) == NULL) return -1;
    if ((p = readUntilTag(p, "<td>")) == NULL) return -1;
    //printf("Conversion de la production instantanée : %s = %d\n", p, atoi(p));
    *pCurrent = atoi(p);
    return 0;
}

int GetEnvoy(int *pCumul, int *pCurrent)
{
    /* first what are we going to send and where are we going to send it? */
    int portno =        80;
    char *host =        IP_ADDRESS_ENVOY;
    char *message = "GET /home?locale=fr&classic=1 HTTP/1.0\r\n\r\n";
    int i;

    struct hostent *server;
    struct sockaddr_in serv_addr;
    int sockfd, bytes, sent, received, total;

    /* create the socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;

    /* lookup the ip address */
    server = gethostbyname(host);
    if (server == NULL)
    {
    	close(sockfd);
    	return -1;
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
        return -1;
    }

    /* send the request */
    total = strlen(message);
    sent = 0;
    do {
        bytes = write(sockfd,message+sent,total-sent);
        if (bytes < 0)
            return -1;
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
    	return -1;
    }

    /* receive the response */
    memset(response,0,sizeof(response));
    total = sizeof(response)-1;
    received = 0;
    do {
        bytes = read(sockfd,response+received,total-received);
        if (bytes < 0)
            return -1;
        if (bytes == 0)
            break;
        received+=bytes;
    } while (received < total);
    response[received] = 0;
    //printf("logSol: connect OK - %d bytes received\n", received);

    /* close the socket */
    close(sockfd);

    /* process response */
    //printf("Response:\n%s\n",response);
    return getPowerValue(response, pCumul, pCurrent);
}


void getCompteurValue(char *response)
{
    char *p = response;
    int index;
    int i,n;
    unsigned long compteur[256];
    unsigned long ref;
    int refPuissance;
    unsigned long pApp[256];

    //printf("Read ESP : %s\r\n", p);

    if ((p = readUntilTag(p, "<body>")) == NULL)
    	    {
    	    G_compteurEDF = -1;
    	    return;
    	    }
    n = 0;
    while (p)
    		{
    		index = atoi(p); // Read Index
    		while (*p != ' ') p++;
    		p++; // Go til Base Index
    		compteur[n] = atoi(p);
    		while (*p != ' ') p++;
    		p++;
    		pApp[n] = (atoi(p)*93)/100;
    		n++;

    		p = readUntilTag(p, "<br>\n");
    		//printf("Read until Tag returns : %s\r\n", p);
    		if (p != 0 && strncmp(p, "</body>", 7) == 0)
    			p = 0;
    		}

	refPuissance = 0;
	for (i = 0 ; i < n ; i++)
		refPuissance += pApp[i];

	if (compteur[0] == compteur[n-1] && n > 3)  // Pas de comptage : c'est de la production
    	    {
    		G_compteurEDF = compteur[n-1];
    		G_pAppEDF = 0;
    		G_prodEDF = (refPuissance / n);
    	    }
	else
		{
		G_compteurEDF = compteur[n-1];
		if (n > 40 && n < 50)  // Environ 1 minute
			G_pAppEDF = (compteur[n-1] - compteur[0]) * 60;
		else
			G_pAppEDF = (refPuissance / n);
		G_prodEDF = 0;
		}

	log_printf("\nESP : n=%d %d->%d puissance=%d\r\n", n, compteur[0], compteur[n-1], (refPuissance / n));
	if (G_pAppEDF == 0 && G_prodEDF != 0)
		{
		char tmp[100];
		int j = 0;
		for (i = 0 ; i < n ; i++)
			{
			j += sprintf(&tmp[j], "%d,", pApp[i]);
			if (j > 90)
				{
				log_printf("%s\n", tmp);
				j = 0;
				}
			}
		log_printf("%s : Moyenne=%d", tmp, G_prodEDF);
		}
	return;
}

void GetESP(void)
{
    /* first what are we going to send and where are we going to send it? */
    int portno =        80;
    char *host =        IP_ADDRESS_ESP_EDF;
    char *message = "GET /readBase HTTP/1.0\r\n\r\n";
    int i;

    struct hostent *server;
    struct sockaddr_in serv_addr;
    int sockfd, bytes, sent, received, total;

    G_compteurEDF = -1;

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
    total = strlen(message);
    sent = 0;
    do {
        bytes = write(sockfd,message+sent,total-sent);
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

    if (poll(&fds, 1, 4000) <= 0)
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
    //printf("logSol: connect OK - %d bytes received\n", received);

    /* close the socket */
    close(sockfd);

    /* process response */
    //printf("Response:\n%s\n",response);
    getCompteurValue(response);
    return;
}

void initESPRelay()
{
    // création d'une socket UDP
    g_sockUDPRelay = socket(AF_INET, SOCK_DGRAM, 0); 
    if (g_sockUDPRelay == -1) 
        {
        perror("erreur création socket"); 
        }
}

int setESPRelay(int state, int value)
{
    char tmp[6];
    int lg;
    struct sockaddr_in addr_serveur;
    int nb_octets;

    tmp[0] = 'R';
    tmp[1] = 'E';
    tmp[2] = 'L';
    tmp[3] = 'A';
    tmp[4] = state + G_fan;
    tmp[5] = (value & 0xFF);
           
    //printf("setESPRelay : Envoie de state=%d value=%d\n", state+G_fan, value);

    struct hostent *hostinfo = NULL;
    hostinfo = gethostbyname(IP_ADDRESS_ESP_RELAY); /* on récupère les informations de l'hôte auquel on veut se connecter */
    if (hostinfo == NULL) /* l'hôte n'existe pas */
        {
        fprintf (stderr, "Unknown host %s.\n", IP_ADDRESS_ESP_RELAY);
        return 0;
        }

    memset(&addr_serveur, 0, sizeof(struct sockaddr_in)); 
    addr_serveur.sin_family = AF_INET; 
    addr_serveur.sin_port = htons(UDP_PORT_RELAY); 
    memcpy(&addr_serveur.sin_addr.s_addr, hostinfo->h_addr, hostinfo->h_length);
    lg = sizeof(struct sockaddr_in);
    nb_octets = sendto(g_sockUDPRelay, tmp, 6, 0, (struct sockaddr*)&addr_serveur, lg);
    if (nb_octets == -1) 
        {
        perror("erreur envoi message"); 
        return 0;
        }
    if (state == 0)
        genHtmlDomoticz(DOMOTICZ_INDEX_PRISE_COMMANDEE, -1);
    else
        genHtmlDomoticz(DOMOTICZ_INDEX_PRISE_COMMANDEE, value);
    return value;
}

void genHtmlESPRelay(char *tmp)
{
    /* first what are we going to send and where are we going to send it? */
    int portno =        80;
    char *host =        IP_ADDRESS_ESP_RELAY;
    int i;

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

void genHtmlWeather()
{
    /* first what are we going to send and where are we going to send it? */
    int portno =        80;
    char *host =        "api.openweathermap.org";
    char *requete =     "data/2.5/weather?id=2972009&APPID=698c098bac85a36f185188c8b924e0ba";
    int i;

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
    		printf("gethostbyname failed : host=%s", host);
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
    total = strlen(requete);
    sent = 0;
    do {
        bytes = write(sockfd,requete+sent,total-sent);
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
    //printf("logSol: Response from weather - %d bytes received : %s\n", received, response);

    /* close the socket */
    close(sockfd);

    return;
}

// Function Main
int main(int argc, char **argv)
{
	int fd;
	char c;
	int ret;
	int count;
	int countDay;
	int compteur;
        int countRelais = 0;
	struct sInfo s;
	struct timeval clock;
	struct tm *pTime;
	int i;
	char currentMonth[12];
	int dczPanneauxSolaires = 0;
	int dczCuve = 0;
	int dczECS = 0;
	int dczPompe = 0;
	int dczPompePrevious = -1;
	int dczCount = 0;
	int dczGelPrevious = -1;
	int dczKmZoePrevious = -1;
        pthread_t threadUDP;
        int lastPower = -1;
        int nTimeToStop = -1;
	
        initESPRelay();
        setESPRelay(0, 1);

	int start=1;
        G_resetGlobalWattmetreSolaire = 0;
        G_Puissance = 0;
        G_Puissance_PZEM_EDF = 0;
	G_PuissanceTotalJourAvant = 120*3600;
	G_PuissanceTotalAnnee = 67000*3600;
	G_PuissanceTotalJour = 0;
	G_PuissanceTotalAnneeAvant = 0;
	G_DureeProtectionGel = 0;
	G_globalProdEDF = 0;
	G_compteurEDF = -1;


	if (argc > 1 && strcmp(argv[1], "LOG") == 0)
		G_logFileIndex = 1;
	
	log_printf("logSol - Version " VERSION "\n");
	
	signal(SIGINT, signal_function);
	
	fd = openSerialPort();
	if (fd == -1)
		{
		return -1;
		}

	nCurrentTabHourIndex = 0;
	nCurrentTabDayIndex = LOG_DURATION-1;
	memset(tabHour, 0, sizeof(tabHour));
	memset(tabDay, 0, sizeof(tabDay));
	memset(tabCumulMonth, 0, sizeof(tabCumulMonth));
	memset(tabHistoMonth, 0, sizeof(tabHistoMonth));
        printf("Loading Data...\n");


	loadData();

        printf("Start thread UDP\n");

        ret = pthread_create(&threadUDP, NULL, UDP_monitoring_thread, NULL);
        if (ret)
            printf("Cannot create thread UDP_monitoring_thread\n");

	count = 60;
	countDay = 0;
	ret = 0;
	while (ret == 0 && leave == 0) 
		{
		ret = getData(fd, &s);
		if (count == 60)
			{
			tabHour[nCurrentTabHourIndex] = s;
                        if (G_compteurEDF > 0)
			   genHtmlDomoticzPower(DOMOTICZ_INDEX_CONSO_EDF, G_pAppEDF, G_compteurEDF);
			if (G_prodEDF != 0)
				{
				G_globalProdEDF += (G_prodEDF / 60);
				genHtmlDomoticzPower(DOMOTICZ_INDEX_PROD_EDF, G_prodEDF, G_globalProdEDF);
				}
			else
				{
				genHtmlDomoticzPower(DOMOTICZ_INDEX_PROD_EDF, 0, G_globalProdEDF);
				}
			tabPuissance[nCurrentTabHourIndex] = G_Puissance;

			nCurrentTabHourIndex++;
			if (nCurrentTabHourIndex == LOG_DURATION)
				nCurrentTabHourIndex = 0;
			count = 0;
			}

		// Mise a jour de la date reelle
		if (-1 == gettimeofday(&clock, NULL))
			{
			printf("Error : gettimeofday returns -1 - errno=%d\n", errno);
			close(fd);
			return 0;
			}
		pTime = localtime((time_t *)&clock.tv_sec);

#if 0
                // Gestion de la prise commandee
		if (G_prodEDF > THRESHOLD_POWER_REMOTE_PLUG && G_Relais_1 == 0)
			{
			G_Relais_1 = 1;
			lastPower = setESPRelay(1, 1);
			//printf("Declenchement du Relais 1 avec valeur 1 (ProdEDF=%d)\n", lastPower, G_prodEDF);
			genHtmlDomoticzSwitch(DOMOTICZ_INDEX_RELAIS_COMMANDE, 1);
                        countRelais = 0;
			}
                 else if (G_Relais_1 == 1)
			{
                        if (countRelais < 2)
                            countRelais++;
                        else if (G_pAppEDF >= 300)
                            {
			    //printf("Arret du Relais 1 (prodEDF=%d pAddEDF=%d)\n", G_prodEDF, G_pAppEDF);
                            lastPower = setESPRelay(0, 1);
			    G_Relais_1 = 0;
			    genHtmlDomoticzSwitch(DOMOTICZ_INDEX_RELAIS_COMMANDE, 0);
                            }
                        else
                            {
                            previousPower = lastPower;
                            //const int seuil = (G_Puissance >= 400 && G_T2 < 24.0) ? -100 : 50;
                            const int seuil = 0;
                            const int signedProd = (G_prodEDF - G_pAppEDF);
                            //printf("   ProdSolaire=%d ProductionSolaire=%d Consomme=%d signedProd=%d seuil=%d lastPower=%d",
                            //    G_Puissance, G_prodEDF, G_pAppEDF, signedProd, seuil, lastPower);
                            if (signedProd > (seuil+100) && lastPower > 80)
                                lastPower += 60;
                            else if (signedProd > (seuil+80) && lastPower > 80)
                                lastPower += 40;
                            else if (signedProd > (seuil+60) && lastPower > 80)
                                lastPower += 20;
                            else if (signedProd > (seuil+40))
                                lastPower += 10;
                            else if (signedProd >  (seuil+20))
                                lastPower += 5;
                            else if (signedProd > (seuil+10))
				lastPower++;
                            else if (signedProd < (seuil-60))
                                lastPower -= 80;
                            else if (signedProd < (seuil-40))
                                lastPower -= 60;
                            else if (signedProd < (seuil-30))
                                lastPower -= 40;
                            else if (signedProd < (seuil-20))
                                lastPower -= 10;
                            else if (signedProd < (seuil-10))
                                lastPower--;
                            if (lastPower > 255)
                                {
                                if (signedProd > 50 || previousPower == 256)
                                    lastPower = 256;
                                else
                                    lastPower = 255;
                                }
                            if (lastPower < 1)
                               {
                               if (previousPower > 1)
			           lastPower = 1;
                               else
                                   {
			           G_Relais_1 = 0;
                                   nTimeToStop = pTime->tm_min+15;
                                   if (nTimeToStop > 59)
                                       nTimeToStop = nTimeToStop - 60;
			           genHtmlDomoticzSwitch(DOMOTICZ_INDEX_RELAIS_COMMANDE, 0);
			           //printf("Arret du Relais 1 (prodEDF=%d pAddEDF=%d)\n", G_prodEDF, G_pAppEDF);
                                   }
                               }
                            else
                               {
			       lastPower = setESPRelay(1, lastPower);
			       //printf("Reprogrammation du Relais 1 : previous=%d new=%d (prodEDF=%d)\n", previousPower, lastPower, G_prodEDF);
                               }
                            countRelais = 0;
			    genHtmlDomoticz(DOMOTICZ_INDEX_PRISE_COMMANDEE, lastPower);
                            }
			}
#endif
                if (countRelais < 1)
                    countRelais++;
                else
                    {
                    countRelais = 0;
                    lastPower = updatePower(lastPower, (G_prodEDF - G_pAppEDF));
                    }

		G_TotalPuissanceSolaire += G_Puissance*PERIODE_RELEVE;
		if (G_prodEDF > 0)
			{
			// Cas ou l'on fournit...
			G_AutoConsommation += ((G_Puissance - G_prodEDF)*PERIODE_RELEVE);
			G_TotalPuissanceConsommee += (G_Puissance - G_prodEDF)*PERIODE_RELEVE;
                	}
		else
			{
			// Cas ou l'on consomme...
			G_AutoConsommation += (G_Puissance*PERIODE_RELEVE);
			G_TotalPuissanceConsommee += (G_pAppEDF + G_Puissance)*PERIODE_RELEVE;
			}

		//printf(" Puiss. Solaire Inst=%d CompteurEDF=%d PuissanceAppEDF=%d PuissanceProdEDF=%d AutoConso=%d prise=%d (%d W)\n", G_Puissance, G_compteurEDF, G_pAppEDF, G_prodEDF, G_AutoConsommation/3600, (G_Relais_1 == 1) ? lastPower : -1, (G_Relais_1 == 1) ? (lastPower*DIVIDER_POWER_REMOTE_PLUG/100)+THRESHOLD_POWER_REMOTE_PLUG:-1);
		count += PERIODE_RELEVE;

		if (s.TpanneauxSolaires <= 5 && s.pompe != 0)
			G_DureeProtectionGel += PERIODE_RELEVE;

		dczPanneauxSolaires += s.TpanneauxSolaires;
		dczCuve += s.Tcuve;
		dczECS += s.Tchaudiere;
		dczPompe += s.pompe;
		dczCount += 1;

		if (dczCount == (60/PERIODE_RELEVE))
		{
		    genHtmlDomoticz(DOMOTICZ_INDEX_PANNEAUX_SOLAIRES, (dczPanneauxSolaires/12));
			genHtmlDomoticz(DOMOTICZ_INDEX_CUVE, (dczCuve/12));
			genHtmlDomoticz(DOMOTICZ_INDEX_ECS, (dczECS/12));
			if (dczPompe/12 != dczPompePrevious)
			{
				genHtmlDomoticzSwitch(DOMOTICZ_INDEX_POMPE, (dczPompe/12));
				dczPompePrevious = (dczPompe/12);
			}
			if (dczGelPrevious != G_DureeProtectionGel)
			{
				genHtmlDomoticz(DOMOTICZ_INDEX_DUREE_PROT_GEL, G_DureeProtectionGel);
				dczGelPrevious = G_DureeProtectionGel;
			}
			genHtmlDomoticzPower(DOMOTICZ_INDEX_PRODUCTION_INST, G_Puissance, (G_PuissanceTotalAnnee/3600));
			if (dczKmZoePrevious != G_PuissanceTotalAnnee)
			{
				genHtmlDomoticz(DOMOTICZ_INDEX_KM_ZOE, (G_PuissanceTotalAnnee/36000/14));
				dczKmZoePrevious = G_PuissanceTotalAnnee;
			}
			dczCount = 0;
			dczPanneauxSolaires = 0;
			dczCuve = 0;
			dczECS = 0;
			dczPompe = 0;
		}

                // Arreter les ventileurs apres 23h
                if (pTime->tm_hour >= 23 && G_fan && G_Relais_1 == 0)
                {
                    G_fan = 0;
                    setESPRelay(0, 1);
                }
                if (pTime->tm_hour > 9 && pTime->tm_hour < 23 && G_fan == 2 && (G_T1 <= 22.0 && G_T2 < 22.0))
                {
                    G_fan = 0;
                    if (G_Relais_1 == 0)
                        setESPRelay(0, 1);
                    else
                        setESPRelay(1, lastPower);
                }
                if (pTime->tm_hour > 9 && pTime->tm_hour < 23 && G_fan == 0 && (G_T1 >= 26.0 || G_T2 >= 25.0))
                {
                    G_fan = 2;
                    if (G_Relais_1 == 0)
                        setESPRelay(0, 1);
                    else
                        setESPRelay(1, lastPower);
                    nTimeToStop = pTime->tm_min+15;
                    if (nTimeToStop > 59)
                        nTimeToStop = nTimeToStop - 60;
                }

                if (G_Relais_1 == 0 && pTime->tm_sec < 5)
                {
                    setESPRelay(0, 1);
                }
                if (G_Relais_1 == 0 && G_fan == 2 && nTimeToStop == pTime->tm_min && (G_T1 <= 24.0 && G_T2 < 24.0))
                {
                    nTimeToStop = -1;
                    G_fan = 0;
                    setESPRelay(0, 1);
                }

		// Moyenner la valeur sur la duree
		if (pTime->tm_hour >= 7 && pTime->tm_hour < 23 )
		{
			tabDay[nCurrentTabDayIndex].tm = s.tm;
			tabDay[nCurrentTabDayIndex].Tcuve += s.Tcuve;
			tabDay[nCurrentTabDayIndex].TpanneauxSolaires += s.TpanneauxSolaires;
			tabDay[nCurrentTabDayIndex].Tchaudiere += s.Tchaudiere;
			tabDay[nCurrentTabDayIndex].pompe += s.pompe;
			tabDay[nCurrentTabDayIndex].puissance += G_Puissance*PERIODE_RELEVE;

			if (countDay == (PERIOD_STAT))
				{
				tabDay[nCurrentTabDayIndex].Tcuve = tabDay[nCurrentTabDayIndex].Tcuve / (PERIOD_STAT/5);
				tabDay[nCurrentTabDayIndex].TpanneauxSolaires = tabDay[nCurrentTabDayIndex].TpanneauxSolaires / (PERIOD_STAT/5);
				tabDay[nCurrentTabDayIndex].Tchaudiere = tabDay[nCurrentTabDayIndex].Tchaudiere / (PERIOD_STAT/5);
				tabDay[nCurrentTabDayIndex].pompe = tabDay[nCurrentTabDayIndex].pompe / (PERIOD_STAT/5);
				tabDay[nCurrentTabDayIndex].puissance = tabDay[nCurrentTabDayIndex].puissance / 900; /* (3600/4 car 1/4h) */

				// Enregistrement des donnees
				saveData();

				nCurrentTabDayIndex++;
				if (nCurrentTabDayIndex == LOG_DURATION)
					nCurrentTabDayIndex = 0;
				memset(&tabDay[nCurrentTabDayIndex], 0, sizeof(struct sInfo));
				countDay = 0;
				}
			countDay += PERIODE_RELEVE;
		}

		// Detection des changements de mois
        sprintf(currentMonth, "%s_%4d", mois[pTime->tm_mon], (pTime->tm_year + 1900));
		if (strcmp(currentMonth, tabHistoMonth[0].month) != 0)
			{
			// Changement de mois
			for (i = (LOG_MONTH_DURATION-1) ; i >= 1 ; i--)
				tabHistoMonth[i] = tabHistoMonth[i-1];
			strcpy(tabHistoMonth[0].month, currentMonth);
			for (i = 0 ; i < (10*31) ; i++)
				tabHistoMonth[0].data[i] = 0;
			log_printf("Changement de mois pour le logMensuel : %s\n", tabHistoMonth[0].month);
			}

		// Toutes les heures, sauvegarde de la temperature ECS (chaudiere)
		if (pTime->tm_min == 0 && (pTime->tm_hour > 10 && pTime->tm_hour < 21) && pTime->tm_sec < 10)
			{
			int index = (pTime->tm_hour-11) + ((pTime->tm_mday - 1) * 10);
			
			tabHistoMonth[0].data[index] = s.Tchaudiere;
			log_printf("Il est %dh, sauvegarde de la temperature %d a l'index %d\n", pTime->tm_hour, s.Tchaudiere, index);
			}
			

		// 2/03/2010 : Cumuler sur le mois les secondes de fonctionnement de la pompe
		if (pTime->tm_mon != tabCumulMonth[0].tm_mon)
			{
			// Inserer un nouveau mois en 0
			for (i = (LOG_MONTH_DURATION -1) ; i > 0 ; i--)
				memcpy(&tabCumulMonth[i], &tabCumulMonth[i-1], sizeof(struct sInfoCumulMonth));
			tabCumulMonth[0].tm_mon = pTime->tm_mon;
			sprintf(tabCumulMonth[0].month, "%s_%4d", mois[pTime->tm_mon], (pTime->tm_year + 1900));
			tabCumulMonth[0].nbSeconds = 0;
			}
		
		if (s.pompe == 30)
			tabCumulMonth[0].nbSeconds += 5;

        if (pTime->tm_min == 0 && pTime->tm_hour == 0 && pTime->tm_sec <= 5 && G_PuissanceTotalJour != 0)
			{
        			char tmp[30];
				G_PuissanceTotalJourAvant = G_PuissanceTotalJour;
				G_PuissanceTotalJour = 0;
				//genHtmlDomoticzPower(DOMOTICZ_INDEX_PRODUCTION_JOURNEE, G_PuissanceTotalJourAvant/3600, G_PuissanceTotalJourAvant/3600);
				if (G_logFileIndex > 0)
					{
					G_logFileIndex++;
					if (G_logFileIndex == 10) G_logFileIndex = 1;
					G_fdLog = fopen(tmp, "w");
					fclose(G_fdLog); // To clean the file
					}

			}
		if (pTime->tm_yday == 0 && pTime->tm_min == 0 && pTime->tm_hour == 0 && pTime->tm_sec <= 5 && G_PuissanceTotalAnnee != 0)
			{
				G_PuissanceTotalAnneeAvant = G_PuissanceTotalAnnee;
				G_PuissanceTotalAnnee = 0;
				G_AutoConsommationAvant = G_AutoConsommation;
				G_AutoConsommation = 0;
				G_TotalPuissanceSolaireAvant = G_TotalPuissanceSolaire;
				G_TotalPuissanceSolaire = 0;
				G_TotalPuissanceConsommeeAvant = G_TotalPuissanceConsommee;
				G_TotalPuissanceConsommee = 0;
			}
        G_PuissanceTotalJour += G_Puissance*PERIODE_RELEVE;
        G_PuissanceTotalAnnee += G_Puissance*PERIODE_RELEVE;

		genHTML(&s, pTime);
		genDataSmartphone(&s, pTime, (G_Relais_1 ? lastPower : -1));
		
		if (pTime->tm_min == 0 && (pTime->tm_hour > 10 && pTime->tm_hour < 21) && pTime->tm_sec < 10)
			genHTMLHistorique(&s, pTime);
		if (start == 1)
			{
			start = 0;
			genHTMLHistorique(&s, pTime);
			}
		}
		
	printf("Fermeture du port - Sortie du programme\n");
	close(fd);
        close(g_sockUDPRelay);
	return 0;
}

//
// Function saveData
//
void saveData(void)
{
	int f;
	int i;
	int index;
	int ret;

	//sprintf(tmp, "rm -f %s", SAVE_FILE);
	//ret = system(tmp);
	//printf("Effacement du fichier avant sauvegarde (ret=%d)\n", ret);
	
	log_printf("Ouverture du fichier %s pour sauvegarde\n", SAVE_FILE);
	
	f = open(SAVE_FILE, O_RDWR | O_TRUNC | O_CREAT);
	if (f == -1)
		{
		log_printf("Error : cannot create file %s\n", SAVE_FILE);
		return;
		}
	for (i = 0 ; i < LOG_DURATION ; i++)
		{
		index = (nCurrentTabDayIndex + i + 1);
		if (index >= LOG_DURATION)
			index = index - LOG_DURATION;
		write(f, &tabDay[index], sizeof(struct sInfo));
		}
	for (i = 0 ; i < LOG_MONTH_DURATION ; i++)
		{
		write(f, &tabCumulMonth[i], sizeof(struct sInfoCumulMonth));
		}
	write(f, tabHistoMonth, sizeof(tabHistoMonth));
	write(f, &G_PuissanceTotalJour, sizeof(long));
	write(f, &G_PuissanceTotalJourAvant, sizeof(long));
	write(f, &G_PuissanceTotalAnnee, sizeof(long));
	write(f, &G_PuissanceTotalAnneeAvant, sizeof(long));
	write(f, &G_DureeProtectionGel, sizeof(int));
	write(f, &G_globalProdEDF, sizeof(int));
	close(f);
}

//
// Function loadData
//
void loadData(void)
{
	int f;
	int i,j;
	int n;
	int h,m,s,d;
	int prior;
	struct timeval clock;
	struct tm *pTime;
	time_t lastHour;
	struct sInfo *st;
	char currentMonth[12];
		
	log_printf("Ouverture du fichier %s\n", SAVE_FILE);
	
	f = open(SAVE_FILE, O_RDONLY);
	if (f == -1)
		{
		printf("Error : cannot open file %s (no historic data)\n", SAVE_FILE);
		return;
		}
	for (i = 0 ; i < LOG_DURATION ; i++)
		{
		n = read(f, &tabDay[i], sizeof(struct sInfo));
#if 0
		n = read(f, &tabDay[i].tm, 8);
		n = read(f, &tabDay[i].mode, 1);
		n = read(f, &h, 3);
		n = read(f, &tabDay[i].Tcuve, 4);
		n = read(f, &tabDay[i].TpanneauxSolaires, 4);
		n = read(f, &tabDay[i].Tchaudiere, 4);
		n = read(f, &tabDay[i].pompe, 4);
		n = read(f, &tabDay[i].dtChaudiere, 4);
		n = read(f, &tabDay[i].dtPrechauffage, 4);
		n = read(f, &h, 4);
#endif
#if 0
		if (n != sizeof(struct sInfo))
			{
			printf("Error : invalid read size (%d) - Read cancelled\n", n);
			close(f);
			return;
			}
#endif

		st = &tabDay[i];
		if (st->TpanneauxSolaires == 255)
			st->TpanneauxSolaires = 0;
		pTime = localtime((time_t *)&st->tm);
		printf("[%d] %d %d:%d:%d %c %d %d %d %d dTC=%d dTP=%d puissance=%d\n", i,
			pTime->tm_mday, pTime->tm_hour, pTime->tm_min, pTime->tm_sec, st->mode, st->Tcuve, st->TpanneauxSolaires, st->Tchaudiere, st->pompe, st->dtChaudiere, st->dtPrechauffage, st->puissance);
		}

	for (i = 0 ; i < LOG_MONTH_DURATION ; i++)
		{
		read(f, &tabCumulMonth[i], sizeof(struct sInfoCumulMonth));
		}

	read(f, tabHistoMonth, sizeof(tabHistoMonth));

	read(f, &G_PuissanceTotalJour, sizeof(long));
	read(f, &G_PuissanceTotalJourAvant, sizeof(long));
	read(f, &G_PuissanceTotalAnnee, sizeof(long));
	read(f, &G_PuissanceTotalAnneeAvant, sizeof(long));
	read(f, &G_DureeProtectionGel, sizeof(int));
	read(f, &G_globalProdEDF, sizeof(int));

        close(f);

	// Resynchronisation par rapport a la derniere date
	nCurrentTabDayIndex = (LOG_DURATION - 1);
	lastHour = tabDay[nCurrentTabDayIndex].tm;

	if (lastHour == 0)
		{
		log_printf("Resynchronisation a l'index %d (date a 0)\n", nCurrentTabDayIndex);
		return;
		}
		
	if (-1 == gettimeofday(&clock, NULL))
		{
		printf("Error : gettimeofday returns -1 - errno=%d\n", errno);
		return;
		}
	log_printf("Dernier enregistrement @%ds - Date courante : %ds\n", (int)lastHour, (int)clock.tv_sec);
	
	do {
		lastHour += (15*60); // Ajouter 1/4h

		nCurrentTabDayIndex++;
		if (nCurrentTabDayIndex == LOG_DURATION)
			nCurrentTabDayIndex = 0;
		memset(&tabDay[nCurrentTabDayIndex], 0, sizeof(struct sInfo));
		
		prior = 0;
		if (lastHour < clock.tv_sec)
			prior = 1;
		if (prior == 1)
			printf("   Enregistrement suivant (index %d) @%d est anterieur\n", nCurrentTabDayIndex, (int)lastHour);
		else
			printf("   Enregistrement suivant (index %d) @%d est OK\n", nCurrentTabDayIndex, (int)lastHour);

		pTime = localtime((time_t *)&lastHour);
		if (prior == 1 && pTime->tm_hour < 7)
			{
			nCurrentTabDayIndex--;
			if (nCurrentTabDayIndex == -1)
				nCurrentTabDayIndex = (LOG_DURATION - 1);
			} 
	} while (prior == 1);	

#if 0
	pTime = localtime((time_t *)&clock.tv_sec);
	tabCumulMonth[0].tm_mon = pTime->tm_mon;
	sprintf(tabCumulMonth[0].month, "%s-%4d", mois[pTime->tm_mon], (pTime->tm_year + 1900));
	tabCumulMonth[0].nbSeconds = 19800;
#endif

	log_printf("Resynchronisation a l'index %d\n", nCurrentTabDayIndex);

	pTime = localtime((time_t *)&clock.tv_sec);
	sprintf(currentMonth, "%s_%4d", mois[pTime->tm_mon], (pTime->tm_year + 1900));
	log_printf("Lecture de l'historique - Mois en cours : %s (a comparer a %s)\n", tabHistoMonth[0].month, currentMonth);
	if (strlen(tabHistoMonth[0].month) == 0 || strcmp(currentMonth, tabHistoMonth[0].month) != 0)
		{
		// Changement de mois
		for (i = (LOG_MONTH_DURATION-1) ; i >= 1 ; i--)
			tabHistoMonth[i] = tabHistoMonth[i-1];
		strcpy(tabHistoMonth[0].month, currentMonth);
		for (i = 0 ; i < (10*31) ; i++)
			tabHistoMonth[0].data[i] = 0;
		log_printf("Changement de mois pour le logMensuel : %s\n", tabHistoMonth[0].month);
		}
	return;
}

//
// Function openSerialPort
//
int openSerialPort(void)
{
	int fd;
	int ret;
	struct termios theTermios;

	fd = open(SERIAL_PORT, O_RDWR);
	if (fd == -1)
		{
		printf("Erreur a l'ouverture de "SERIAL_PORT"\n");
		return -1;
		}

#if 0
	memset(&theTermios, 0, sizeof(struct termios));
	cfmakeraw(&theTermios);
	cfsetspeed(&theTermios, 9600);
	theTermios.c_cflag = CREAD | CLOCAL;	 // turn on READ and ignore modem control lines
	theTermios.c_cflag |= CS8;
	theTermios.c_cc[VMIN] = 0;
	theTermios.c_cc[VTIME] = 60;	 // 6 sec timeout (output every 5 sec)
	ret = ioctl(fd, TIOCSETA, &theTermios);
	if (ret != 0)
		printf("ioctl TIOCSETA returns %d\n", ret);
#endif

	return fd;
}

//
// Fonction de lecture de la temperature
//
int getData(int fd, struct sInfo *s)
{
	char buffer[100];
	char *p;
	int n;
	int ret;
	char c;
	struct timeval clock;
	int jour, heure, minute, seconde;
	
nextLine:	
//log_printf("Attente d'une ligne... ");
	// Lecture d'une ligne jusqu'a 0D-0A
	n = 0;
	c = 0;
	do {
		ret = read(fd, &c, 1);
		//printf("%c(%d) ", c, (int)c);
		if (ret < 1)
			{
			log_printf("read on serial port failed\r\n");
			continue;
			//return -1;  // 13/09/2012 : au lieu de retourner et d'arreter le programme, continuer a attendre...(risque de blocage)
			}
		buffer[n++] = c;
		if (n == 100)
			n = 0;
		} while (c != 10 && !leave);
	buffer[n] = 0;
	
	if (leave)
		return -1;
		
	//printf("Analyse de la ligne :#%s#", buffer);
		
	// Analyse de la ligne
	p = strtok(buffer, " :=");
	if (p == NULL)
		goto nextLine;
	jour = atoi(p);
	
	p = strtok(NULL, " :=");
	if (p == NULL)
		goto nextLine;
	heure = atoi(p);
	
	p = strtok(NULL, " :=");
	if (p == NULL)
		goto nextLine;
	minute = atoi(p);
	
	p = strtok(NULL, " :=");
	if (p == NULL)
		goto nextLine;
	seconde = atoi(p);
	
	p = strtok(NULL, " :=");
	if (p == NULL)
		goto nextLine;
	s->mode = *p;
	if (s->mode != 'C' && s->mode != 'P')
		{
		printf("Mode invalide : %c\n", s->mode);
		goto nextLine;
		}
		
	p = strtok(NULL, " :=");
	if (p == NULL)
		goto nextLine;
	s->Tcuve = atoi(p);
	
	p = strtok(NULL, " :=");
	if (p == NULL)
		goto nextLine;
	s->TpanneauxSolaires = atoi(p);
	
	p = strtok(NULL, " :=");
	if (p == NULL)
		goto nextLine;
	s->Tchaudiere = atoi(p);
	
	p = strtok(NULL, " :=");
	if (p == NULL)
		goto nextLine;
	s->pompe = atoi(p);
//	if (s->pompe == 65)
//		s->pompe = 30;

#if 0
	p = strtok(NULL, " :=");
	if (p == NULL)
		goto nextLine;
	if (strcmp(p, "dTC") != 0)
		{
		printf("dTC : %s\n", p);
		goto nextLine;
		}

	p = strtok(NULL, " :=");
	if (p == NULL)
		goto nextLine;
	s->dtChaudiere = atoi(p);

	p = strtok(NULL, " :=");
	if (p == NULL)
		goto nextLine;
	if (strcmp(p, "dTP") != 0)
		{
		printf("dTP : %s\n", p);
		goto nextLine;
		}

	p = strtok(NULL, " :=");
	if (p == NULL)
		goto nextLine;
	s->dtPrechauffage = atoi(p);

	p = strtok(NULL, " :=");
	if (p == NULL)
		goto nextLine;

	if (strncmp(p, "<br>", 4) != 0)
		{
		printf("<br> : %s\n", p);
		goto nextLine;
		}
#endif

	if (-1 == gettimeofday(&clock, NULL))
		{
		printf("Error : gettimeofday returns -1 - errno=%d\n", errno);
		return 0;
		}
	s->tm = clock.tv_sec;
	printf(" (%ds) %d %02d:%02d:%02d %c %d %d %d %d ", (int)s->tm,
		jour, heure, minute, seconde, s->mode, s->Tcuve, s->TpanneauxSolaires, s->Tchaudiere, s->pompe);


	//if (s->TpanneauxSolaires < 0 || s->TpanneauxSolaires > 99)
	//	s->TpanneauxSolaires = 0;
	
	if (s->Tchaudiere < 0 || s->Tchaudiere > 99)
		s->Tchaudiere = 0;
	
	return 0;	
}

void log_printf(const char * format, ... )
{
	char str[256];
	char tmp[30];
	char date[30];
	struct timeval clock;
	struct tm *pTime;
	va_list args;

	va_start (args, format);
    vsprintf (str, format, args);
    va_end (args);

	if (-1 != gettimeofday(&clock, NULL))
		{
		pTime = localtime((time_t *)&clock.tv_sec);
		sprintf(date, "%d/%d/%d %d:%d:%d ", pTime->tm_mday, (pTime->tm_mon+1), (pTime->tm_year+1900), pTime->tm_hour, pTime->tm_min, pTime->tm_sec);
		}
	printf(date);
    printf(str);

    if (G_logFileIndex > 0)
		{
		sprintf(tmp, "/var/www/logSol%d.txt", G_logFileIndex);
		G_fdLog = fopen(tmp, "a");
		if (G_fdLog)
				{
				fprintf(G_fdLog, date);
			fprintf(G_fdLog, str);
			fclose(G_fdLog);
			}
		G_fdLog = 0;
		}
}
