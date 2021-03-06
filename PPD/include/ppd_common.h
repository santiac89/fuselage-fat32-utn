/*
 * ppd_common.h
 *
 *  Created on: 07/10/2011
 *      Author: utn_so
 */

#ifndef PPD_COMMON_H_
#define PPD_COMMON_H_

#include <semaphore.h>

#include "log.h"
#include "nipc.h"
#include "tad_queue.h"


//extern uint32_t Head;
//extern uint32_t Sector;


typedef struct CHS_t {
	uint32_t cylinder;
	uint32_t head;
	uint32_t sector;
} __attribute__((__packed__)) CHS_t;

typedef struct request_t {
	NIPC_type type;
	uint32_t ID;
	struct CHS_t* CHS;
	char len[2];
	uint32_t sender;
	char* payload;
} __attribute__((__packed__)) request_t;

typedef enum {
	QUEUE1_ACTIVE=0x00, QUEUE2_ACTIVE=0x01, SSTF=0x02, FSCAN=0x03,
	UP=0x04, DOWN=0x5
} flag_t;

typedef struct multiQueue_t {
	queue_t* queue1;
	queue_t* queue2;
	flag_t qflag;
	flag_t direction;
	sem_t queueElemSem;
} __attribute__((__packed__)) multiQueue_t;

typedef uint32_t(*conditionFunction_t)(CHS_t,CHS_t);

// cambia de sectorNum a CHS para luego ser metido en la lista grande
CHS_t* COMMON_turnToCHS(uint32_t);

uint32_t COMMON_identity(CHS_t,CHS_t);

uint32_t COMMON_lessThan(CHS_t,CHS_t);

uint32_t COMMON_greaterThan(CHS_t,CHS_t);

char* COMMON_createLogChar(uint32_t sectorNum,request_t* request,uint32_t delay,uint32_t(*getNext)(queue_t*,queueNode_t**,uint32_t));

void COMMON_activeQueueStatus(queue_t* queue,queueNode_t* prevCandidate,CHS_t* CHSrequest);

void COMMON_passiveQueueStatus();

char* COMMON_writeInLog(queue_t* queue,queueNode_t* prevCandidate,request_t* request,uint32_t sectorNum,uint32_t(*getNext)(queue_t*,queueNode_t**,uint32_t),uint32_t delay);

void COMMON_readPPDConfig(char* configPath,uint32_t* port, uint32_t* diskID,uint32_t* startingMode, char** IP,
		char** sockUnixPath,char** diskFilePath,char** consolePath,char** logPath,flag_t* initialDirection,e_message_level* logFlag);

char* COMMON_getTypeByFlag(NIPC_type requestType);

#endif /* PPD_COMMON_H_ */
