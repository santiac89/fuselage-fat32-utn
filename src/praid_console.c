/*
 * praid_console.c
 *
 *  Created on: 09/10/2011
 *      Author: utn_so
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "praid_console.h"
#include <stdbool.h>
#include "log.h"


extern uint32_t RAID_CONSOLE;
extern uint32_t LOG_ENABLE;

extern pthread_mutex_t mutex_CONSOLE;
extern t_log *raid_log_file;

uint32_t print_Console (char *message, uint32_t number, uint32_t priority, bool display_int)
{
	/*
	 *
	 * ESTADOS DE LA CONSOLA
	 * 0: DISABLE
	 * 1: MENSAJES REQUERIDOS POR CATEDRA
	 * 2: DEBUG
	 *
	 * */

	if(RAID_CONSOLE == 1){//CONSOLE ENABLE
		if(priority > 0){
			if(display_int==true)
				printf("%s [%u]\n",message,number);
			else
				printf("%s \n",message);
		}
	}else if (RAID_CONSOLE == 2){//DEBUG MODE
		if(display_int==true)
			printf("%s [%u]\n",message,number);
		else
			printf("%s \n",message);
	}
	return 0;
}
uint32_t PRAID_WRITE_LOG(char* msg)
{
	if(LOG_ENABLE == 1){
		log_debug(raid_log_file,"PRAID",msg);
		return 0;
	}

return 1;
}