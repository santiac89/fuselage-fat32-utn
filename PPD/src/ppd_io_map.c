#define _FILE_OFFSET_BITS 64
#define _USE_LARGEFILE64

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include <errno.h>

#include "ppd_io.h"
#include "log.h"

#define MAX_WRITINGS_PERSYNC 999999

extern uint32_t bytes_perSector;
extern uint32_t ReadTime;
extern uint32_t WriteTime;
extern uint32_t Sector;
extern uint32_t Cylinder;
extern t_log* Log;
char* Map;
uint32_t  page_size = 4096;
uint32_t sectors_perPage = 8;
uint32_t writings;

uint32_t IO_openDisk(char* diskFilePath){
	int32_t file_descriptor;
	uint32_t diskSize = Cylinder * Sector * bytes_perSector;
	writings = 0;

	if((file_descriptor = open(diskFilePath,O_RDWR)) == -1){
		log_error(Log,"Principal",strerror(errno));
		printf("Código de Error:%d Fallo al asociar el disco al proceso. %s\n",errno,strerror(errno));
		exit(1);
	}
	Map = mmap(NULL,diskSize, PROT_READ | PROT_WRITE, MAP_SHARED, file_descriptor,0);
	if(Map==MAP_FAILED){
		log_error(Log,"Principal",strerror(errno));
		printf("Código de Error:%d Fallo en la funcion mmap. %s\n",errno,strerror(errno));
		exit(1);
	}
	posix_madvise(Map,diskSize,POSIX_MADV_SEQUENTIAL);
	return file_descriptor;
}

void IO_readDisk(uint32_t sector,char* buf){
//	uint32_t page = floor(sector / sectors_perPage);

	memcpy(buf,Map+(sector*bytes_perSector),bytes_perSector);
	if (ReadTime != 0)
		usleep(ReadTime*1000);
}

void IO_writeDisk(uint32_t sector,char* buf){
//	uint32_t page = floor(sector / sectors_perPage);
	uint32_t diskSize = Cylinder * Sector * bytes_perSector;

	memcpy(Map+(sector*bytes_perSector),buf,bytes_perSector);

	if(writings >= MAX_WRITINGS_PERSYNC){
		msync(Map,diskSize,MS_ASYNC);
		writings = 0;
	}
	writings++;
	if (WriteTime != 0)
		usleep(WriteTime*1000);
}

void IO_closeDisk(uint32_t file_descriptor){
	uint32_t diskSize = Cylinder * Sector * bytes_perSector;

	if (munmap(Map, diskSize) == -1) {
		log_error(Log,"Principal",strerror(errno));
		printf("Código de Error:%d Fallo en la funcion munmap. %s\n",errno,strerror(errno));
		exit(1);
	}
	if(close(file_descriptor)<0){
		log_error(Log,"Principal",strerror(errno));
		printf("Código de Error:%d Fallo al cerrar el disco. %s\n",errno,strerror(errno));
		exit(1);
	}
}
