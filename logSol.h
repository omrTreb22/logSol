//--------------------------------------------------------//
// Application LogSolaire                                 //
//                                                        //
//--------------------------------------------------------//
// 24/10/2009 : creation                                  
// 08/11/2009 : Ajout de la sauvegarde sur DD
//              Calcul de la moyenne des valeurs sur le 1/4h
// 10/11/2009 : Suppression des heures de nuit (0h-7h)
// 19/11/2009 : Pour la pompe, affichage de la duree de fonctionnement sur le 1/4h
// 05/01/2010 : Gestion des temperatures negatives
// 07/02/2010 : Arret du programme apres 18h pour extinction automatique du Mac (heure programmable : argv[1])
// 28/02/2010 : Affichage du mode de fonctionnement
//  2/03/2010 : Stockage du cumul des heures de fonctionnement par mois
// 17/04/2010 : Affichage des cumul mensuel via GoogleAPI
// 21/04/2010 : Ajout de la grille horizontale sur les graphiques d'historique
// 28/06/2010 : Affichage du nombre d'heures de fonctionnement cumulees par mois
// 05/02/2011 : Separation du bar-graph en periode de 12 mois
// 19/09/2011 : Adaptation pour le ballon solaire
// 29/01/2012 : Ajout d'une page Historique mensuel
// 05/03/2012 : Bug sur l'historique : decalage de 1 jour
// 13/09/2012 : Reconnexion en cas de perte de la liaison serie
// 17/03/2013 : Suppression de dtC et dtP
// 26/03/2013 : Refonte des graphiques de l'historique
//              Separation en plusieurs fichiers source : logsol.c, histo.c, logsol.h
// 14/07/2013 : 3.0 : Version pour ARM-Raspberry PI
// 03/08/2013 : Detection du changement de mois manquante
// 20/10/2013 : Suppression des liens vers bib22.dyndns.org
// 30.10/2016 : 4.0 : Version avec data pour smartphones

#define VERSION "6.0"



// Declaration des definitions
//#define SERIAL_PORT  "/dev/cu.usbserial"
//#define SERIAL_PORT  "/dev/cu.PL2303-000014FA"
#define SERIAL_PORT "/dev/ttyUSB0"
#define LOG_DURATION (12*30)
#define LOG_MONTH_DURATION 60
#define NB_MONTHS_DISPLAY  12
#define SCALE_FACTOR  62/80
#define SAVE_FILE    "/home/logSol/logSolHistoryFile.dat"
#define HISTO_TXT_FILE "/home/logSol/logSolHistoryFile.txt"
#define LOGSOL_TXT_FILE "/home/logSol/logSolData.txt"
#define LOGSOL_CUMUL_MONTH_TXT_FILE "/home/logSol/logSolCumulMonthData.txt"
#define HTML_FILE    "/var/www/index.html"
#define HISTO_FILE    "/var/www/histo.html"
#define HTML_SMARTPHONE_FILE "/var/www/smartData.html"
#define PERIOD_STAT  900
#define PERIODE_RELEVE  5
#define UDP_PORT_RELAY                5006
#define THRESHOLD_POWER_REMOTE_PLUG   200
#define DIVIDER_POWER_REMOTE_PLUG     172

// Index for Domoticz
#define DOMOTICZ_INDEX_PANNEAUX_SOLAIRES   1
#define DOMOTICZ_INDEX_CUVE                2
#define DOMOTICZ_INDEX_ECS                 3
#define DOMOTICZ_INDEX_POMPE               10
#define DOMOTICZ_INDEX_PRODUCTION_INST     5
#define DOMOTICZ_INDEX_PRODUCTION_JOURNEE  6
#define DOMOTICZ_INDEX_DUREE_PROT_GEL      7
#define DOMOTICZ_INDEX_KM_ZOE              9
#define DOMOTICZ_INDEX_CONSO_EDF			  11
#define DOMOTICZ_INDEX_PROD_EDF            12
#define DOMOTICZ_INDEX_RELAIS_COMMANDE     13

#define IP_ADDRESS_ESP_WATT_SOLAIRE        "192.168.1.93"
#define IP_ADDRESS_ESP_EDF                 "192.168.1.91"
#define IP_ADDRESS_ESP_RELAY               "192.168.1.66"
#define IP_ADDRESS_ENVOY                   "192.168.1.86"
#define IP_ADDRESS_DOMOTICZ                "192.168.1.77"
#define PORT_UDP                           5005

// Declaration des types
typedef void (*sig_t) (int);
struct sInfo 
	{
	time_t tm;
	char mode;
	int Tcuve;
	int TpanneauxSolaires;
	int Tchaudiere;
	int pompe;
	int dtChaudiere;
	int dtPrechauffage;
	int puissance;
	};

struct sInfoOld
	{
	time_t tm;
	char mode;
	int Tcuve;
	int TpanneauxSolaires;
	int Tchaudiere;
	int pompe;
	int dtChaudiere;
	int dtPrechauffage;
	};

struct sInfoCumulMonth
	{
	int tm_mon;     // Mois courant
	int nbSeconds;  // Nombre de secondes
	char month[12];  // Mois + Annee
	};

struct sInfoHistoMonth
	{
	int data[31*10]; // 10 mesures par jour (11h-20h00)
	char month[12];  // Nom du mois-annee
	};

// Declaration des variables globales
extern struct sInfoHistoMonth tabHistoMonth[LOG_MONTH_DURATION]; // Historique sur 60 mois
extern char tmp[8000];
extern char listData[];
extern int  mday_month[12];
extern char *mois[12];
			
// Definition des fonctions externes
int genHTMLHistorique(struct sInfo *s, struct tm *pTime);
