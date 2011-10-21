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


extern uint32_t raid_console;
extern pthread_mutex_t mutex_console;

uint32_t print_Console (char *message){

	if(raid_console == 0){
		pthread_mutex_lock(&mutex_console);
		printf("%s \n",message);
		pthread_mutex_unlock(&mutex_console);
	}

	return 0;
}