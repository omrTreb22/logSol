//--------------------------------------------------------//
// Application LogSolaire - histo.c                       //
//                                                        //
//--------------------------------------------------------//
// 24/03/2013 : Separation du fichier principal                                  

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "logSol.h"

//
// Function genereHTMLHistorique
//
int genHTMLHistorique(struct sInfo *s, struct tm *pTime)
{
	FILE *fHTML;
	int m, o, n = 0;
	int i;
	
	fHTML = fopen(HISTO_FILE, "w");

	fputs("<html><head><TITLE>Historique Installation Solaire de Lan Ar Gleiz</TITLE><script type=\"text/javascript\" src=\"https://www.google.com/jsapi\"></script>\r\n", fHTML);
   fputs("<script type=\"text/javascript\"> \r\n", fHTML);
   fputs("google.load(\"visualization\", \"1\", {packages:[\"corechart\"]});", fHTML);
   fputs("google.setOnLoadCallback(drawChart);\r\n", fHTML);
   fputs("function drawChart() {\r\n", fHTML);
   for (m = 0 ; m < 12 ; m++)
   	{
   	tabHistoMonth[m].month[3]='_';
	   sprintf(tmp, "     var data_%s = google.visualization.arrayToDataTable([\r\n", tabHistoMonth[m].month);
	   fputs(tmp, fHTML);
	   n = sprintf(tmp, "['Day'");
   	for (o = 0 ; o < 5 && (strlen(tabHistoMonth[m+(o*12)].month) != 0) ; o++)
         n += sprintf(&tmp[n], ", '%s'", tabHistoMonth[m+(o*12)].month);
		n += sprintf(&tmp[n], " ], \r\n");
		fputs(tmp, fHTML);
		
		for (i = 0 ; i < (10*31) ; i++)
			{
			n = sprintf(tmp, "['%d'", (i/10)+1);
			
			for (o = 0 ; o < 5 && (strlen(tabHistoMonth[m+(o*12)].month) != 0) ; o++)
				n += sprintf(&tmp[n], ", %d", tabHistoMonth[m+(o*12)].data[i]);
						
			n += sprintf(&tmp[n], " ], \r\n");
			fputs(tmp, fHTML);
			}
		fputs("]);\r\n", fHTML);

      sprintf(tmp, "var options_%s = { title: 'Historique de %s' }; \r\n", tabHistoMonth[m].month, tabHistoMonth[m].month);
      fputs(tmp, fHTML);

		sprintf(tmp, "var chart_%s = new google.visualization.LineChart(document.getElementById('chart_%s_div'));\r\n", tabHistoMonth[m].month, tabHistoMonth[m].month);
		fputs(tmp, fHTML);
			
		sprintf(tmp, "chart_%s.draw(data_%s, options_%s);\r\n", tabHistoMonth[m].month, tabHistoMonth[m].month, tabHistoMonth[m].month);
		fputs(tmp, fHTML);
   	}
   	
	sprintf(tmp, "} </script> </head>\r\n");
	fputs(tmp, fHTML);
		
	//fputs("<body><center><a href=\"/Bib22-Le%20site/index.html\">Vers le site bib22.dyndns.org</a>\r\n", fHTML);
	fputs("<body><h1>Installation solaire de Lan Ar Gleiz</h1><center>\r\n", fHTML);
	for (m = 0 ; m < 12 ; m++)
		{
		sprintf(tmp, "<div id=\"chart_%s_div\" style=\"width: 1200px; height: 200px;\"></div>\r\n", tabHistoMonth[m].month);
		fputs(tmp, fHTML);
		}
		
	n = sprintf(tmp, "</TABLE><br>Cette courbe affiche le releve de la temperature en haut du ballon solaire, "); fputs(tmp, fHTML);
	n = sprintf(tmp, "prise toutes les heures, de 11h a 20h. En hiver, cela represente la temperature de l'eau "); fputs(tmp, fHTML);
	n = sprintf(tmp, "en entree du ballon d'eau chaude sanitaire de la chaudiere fioul. En ete, lorsque la chaudiere "); fputs(tmp, fHTML);
	n = sprintf(tmp, "est eteinte, c'est la temperature effective de l'ECS, eventuellement avec un appoint electrique.<br>"); fputs(tmp, fHTML);

	n = sprintf(tmp, "<br><CENTER>Page generee le %d/%d/%d %02d:%02d:%02d LogSol Version %s</CENTER></body></html>",
		pTime->tm_mday, (pTime->tm_mon+1), (pTime->tm_year+1900), pTime->tm_hour, pTime->tm_min, pTime->tm_sec, VERSION);
		 
	fputs(tmp, fHTML);
	fclose(fHTML);
	
}

