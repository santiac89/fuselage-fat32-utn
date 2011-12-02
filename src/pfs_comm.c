/*
 * pfs_comm.c
 *
 *  Created on: 08/09/2011
 *      Author: utn_so
 */

//PROBLEMAS A SOLUCIONAR: LINKEO CON COMMONS Y PASAJE DE LENGTH DE NIPC

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include "pfs_comm.h"
#include "comm.h"

#include "tad_sector.h"
#include <stdbool.h>
#include <semaphore.h>
#include "tad_bootsector.h"
#include "tad_sockets.h"
#include <sys/socket.h>
#include <errno.h>
#include <pthread.h>

#define FIONSPACE       _IOR('f', 118, int)

extern bootSector_t boot_sector;
extern socketPool_t sockets_toPPD;
extern uint32_t request_id;

char* PPDINTERFACE_readSectors(uint32_t* sectors_array, size_t sectors_array_len)
{
	/* BUSQUEDA DE UN SOCKET LIBRE */
	sem_wait(&sockets_toPPD.free_sockets);
	socketInet_t *ppd_socket = PPDINTERFACE_getFreeSocket();
	pthread_mutex_lock(&ppd_socket->sock_mutex);
	/* FIN BUSQUEDA DE SOCKET LIBRE */

	size_t responses_received = 0,requests_sent = 0;
	size_t response_message_len = 523;
	char *buffer = malloc(response_message_len*sectors_array_len);

	while (responses_received < sectors_array_len)
	{
		char *msg_buf;
		int32_t readable_bytes = 0;

		if (ioctl(ppd_socket->descriptor,FIONREAD,&readable_bytes) == 0)
		{
			if (readable_bytes >= 523)
			{
				msg_buf = malloc(response_message_len);
				int32_t received = SOCKET_recvAll(ppd_socket->descriptor,msg_buf,response_message_len,MSG_DONTWAIT);

				if (received > 0)
				{
					memcpy(buffer+(responses_received*response_message_len),msg_buf,response_message_len);
					responses_received++;
				}
				else if (received == SOCK_DISCONNECTED || received == SOCK_ERROR)
				{
					free(msg_buf);
					free(buffer);
					printf("ERROR: Se perdio la conexion con el otro extremo.\n");
					exit(-1);
				}
				free(msg_buf);
			}
		}

		if (requests_sent < sectors_array_len)
		{
			msg_buf = malloc(11);

			*msg_buf = READ_SECTORS;
			*((uint16_t*) (msg_buf+1)) = 8;
			*((uint32_t*) (msg_buf+3)) = request_id++;
			*((uint32_t*) (msg_buf+7)) = sectors_array[requests_sent];

			int32_t sent = SOCKET_sendAll(ppd_socket->descriptor,msg_buf,11,0);
			if (sent == SOCK_DISCONNECTED || sent == SOCK_ERROR)
			{
				printf("ERROR: Se perdio la conexion con el otro extremo.\n");
				exit(-1);
			}
			free(msg_buf);
			requests_sent++;
		}
	}

	ppd_socket->status = SOCK_FREE;
	pthread_mutex_unlock(&ppd_socket->sock_mutex);
	sem_post(&sockets_toPPD.free_sockets);

	char *final_buffer = splitAndSort(buffer,sectors_array,sectors_array_len);

	free(buffer);
	return final_buffer;
}

char* PPDINTERFACE_writeSectors(queue_t sectors_toWrite,size_t sectors_toWrite_len)
{
	/* BUSQUEDA DE UN SOCKET LIBRE */
	sem_wait(&sockets_toPPD.free_sockets);
	socketInet_t *ppd_socket = PPDINTERFACE_getFreeSocket();
	pthread_mutex_lock(&ppd_socket->sock_mutex);
	/* FIN BUSQUEDA DE SOCKET LIBRE */

	size_t sector_index = 0,responses_received = 0,requests_sent = 0;
	size_t response_message_len = 523;
	queueNode_t *cur_sector_node = sectors_toWrite.begin;

	while (responses_received < sectors_toWrite_len)
	{
		char *msg_buf;
		int32_t readable_bytes = 0;

		if (ioctl(ppd_socket->descriptor,FIONREAD,&readable_bytes) == 0)
		{
			if (readable_bytes >= 523)
			{
				msg_buf = malloc(response_message_len);
				int32_t received = SOCKET_recvAll(ppd_socket->descriptor,msg_buf,response_message_len,MSG_DONTWAIT);

				if (received > 0)
				{
					responses_received++;
				}
				else if (received == SOCK_DISCONNECTED || received == SOCK_ERROR)
				{
					free(msg_buf);
					printf("ERROR: Se perdio la conexion con el otro extremo.\n");
					exit(-1);
				}
				free(msg_buf);
			}
		}

		if (requests_sent < sectors_toWrite_len)
		{
			sector_t *sector_toWrite = (sector_t*) cur_sector_node->data;

			msg_buf = malloc(523);

			*msg_buf = WRITE_SECTORS;
			*((uint16_t*) (msg_buf+1)) = 520;
			*((uint32_t*) (msg_buf+3)) = request_id++;
			*((uint32_t*) (msg_buf+7)) = sector_toWrite->number;
			memcpy(msg_buf+11,sector_toWrite->data,boot_sector.bytes_perSector);

			int32_t sent = SOCKET_sendAll(ppd_socket->descriptor,msg_buf,response_message_len,0);
			if (sent == SOCK_DISCONNECTED || sent == SOCK_ERROR)
			{
				printf("ERROR: Se perdio la conexion con el otro extremo.\n");
				exit(-1);
			}
			free(msg_buf);

			cur_sector_node = cur_sector_node->next;

			requests_sent++;
		}
	}

	ppd_socket->status = SOCK_FREE;
	pthread_mutex_unlock(&ppd_socket->sock_mutex);
	sem_post(&sockets_toPPD.free_sockets);


	return NULL;
}

socketPool_t create_connections_pool(uint32_t max_conn,char* address,uint32_t port)
{
	socketPool_t sock_pool;
	sock_pool.size = max_conn;
	sock_pool.sockets = malloc(sizeof(socketInet_t)*max_conn);
	sem_init(&sock_pool.free_sockets,0,max_conn);
	uint32_t index = 0;

	for (;index < max_conn;index++)
	{
		sock_pool.sockets[index] = SOCKET_inet_create(SOCK_STREAM,address,port,MODE_CONNECT);

		if (sock_pool.sockets[index].status == SOCK_OK)
		{
			char* handshake = malloc(3);
			memset(handshake,0,3);
			send(sock_pool.sockets[index].descriptor,handshake,3,0);
			handshake = realloc(handshake,3);
			recv(sock_pool.sockets[index].descriptor,handshake,3,0);
			if (*(handshake+1) != 0x00)
			{
				printf("ERROR POR HANDSHAKE");
				exit(0);
			}
			free(handshake);
			pthread_mutex_init(&sock_pool.sockets[index].sock_mutex,NULL);
			sock_pool.sockets[index].status = SOCK_FREE;
		}
		else
		{
			sock_pool.size = 0;
			return sock_pool;
		}
	}

	return sock_pool;

}

int32_t sendMsgToPPD(socketInet_t socket,char *msg)
{
	uint32_t len = *((uint16_t*) (msg+1)) + 3 ;

	uint32_t transmitted = send(socket.descriptor,msg,len,0);
	//free(msg_inBytes);
	if (transmitted != len)
	{
		return errno;
	}
	else
	{
		return transmitted;
	}
}

char* splitAndSort(char *sectors,uint32_t *indexes_array,size_t array_len)
{
	size_t msg_len = boot_sector.bytes_perSector + 11;
	uint32_t index = 0,index2 = 0;
	char* buf = malloc(boot_sector.bytes_perSector * array_len);
	for (;index < array_len;index++)
	{
		index2=0;
		for (;index2 < array_len;index2++)
		{
			if (indexes_array[index] == *((uint32_t*) (sectors+(index2*msg_len)+7)))
			{
				memcpy(buf+(index*boot_sector.bytes_perSector),sectors+(index2*msg_len)+11,boot_sector.bytes_perSector);
			}
		}
	}
	return buf;
}

char* createRequest(NIPC_type msg_type,uint32_t payload_len,char* payload)
{
	char* msg = malloc(payload_len+(sizeof(char)*3)+sizeof(uint32_t)); //TIPO + LEN + PAYLOAD
	memcpy(msg,&msg_type,1);

	uint16_t len = payload_len+sizeof(uint32_t); //PAYLOAD + IDPEDIDO
	memcpy(msg+1,&len,2);

	memcpy(msg+3,&request_id,sizeof(uint32_t));
	memcpy(msg+7,payload,payload_len);
	request_id++;
	return msg;
}

socketInet_t* PPDINTERFACE_getFreeSocket()
{
	uint32_t sockets_toPPD_index = 0;

	for (;sockets_toPPD_index < sockets_toPPD.size;sockets_toPPD_index++)
	{
		if (sockets_toPPD.sockets[sockets_toPPD_index].status == SOCK_FREE)
		{
			sockets_toPPD.sockets[sockets_toPPD_index].status = SOCK_NOTFREE;
			return (sockets_toPPD.sockets+sockets_toPPD_index);
		}
	}
	return NULL;
}

