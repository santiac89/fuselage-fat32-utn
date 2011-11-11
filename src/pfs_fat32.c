/*
 * pfs_fat32.c
 *
 *  Created on: 14/09/2011
 *      Author: utn_so
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <assert.h>

#include "tad_sector.h"
#include "tad_cluster.h"

#include "pfs_comm.h"
#include "pfs_fat32.h"
#include "utils.h"
#include "tad_direntry.h"
#include "tad_file.h"
#include "tad_queue.h"
#include "tad_lfnentry.h"
#include "log.h"
#include "file_cache.h"
extern bootSector_t boot_sector;
extern fatTable_t fat;
extern t_log *log_file;
extern queue_t file_caches;

/*
uint32_t fat32_readFAT(fatTable_t *fat)
{

	log_debug(log_file,"PFS","Leyendo FAT Table");
	uint32_t bytes_perFATentry = 4;
	fat->size = (boot_sector.bytes_perSector*boot_sector.sectors_perFat32) / bytes_perFATentry;

	//Luego se reemplazara esto  por el envio del mensaje al PPD/PRAID
	uint32_t sectors[boot_sector.sectors_perFat32];
	int sector;
	for (sector = 32; sector <= 32+boot_sector.sectors_perFat32; sector++)
	{
		sectors[sector-32] = sector;
	}

	fat->table = (uint32_t*) PFS_requestSectorsOperation(READ_SECTORS,sectors,boot_sector.sectors_perFat32);
	assert(*((char*) fat->table) == boot_sector.media_descriptor);
	log_debug(log_file,"PFS","FAT Table OK");
	fat->EOC = *(fat->table + 1);
	return 0;
}
*/

uint32_t fat32_readBootSector(bootSector_t *bs)
{
	log_debug(log_file,"PFS","Leyendo Boot Sector");
	uint32_t sectors[1] = {0} ;
	char *bootsector_data = PPDINTERFACE_readSectors(sectors,1);
	assert(*bootsector_data != 0x00);
	log_debug(log_file,"PFS","Boot Sector OK");
	memcpy(bs,bootsector_data,512);
	free(bootsector_data);
	return 0;

}


char* fat32_readRawCluster(uint32_t cluster_no)
{
	//uint32_t index = 0;
	//char* buf = malloc(boot_sector.sectors_perCluster*boot_sector.bytes_perSector);

	/* BUSQUEDA EN LA CACHE
	//queueNode_t *cur_block_node = current_cache->blocks.begin;
	while (cur_block_node != NULL)
	{
		cache_block_t *cur_block = (cache_block_t*) cur_block_node->data;
		if (cur_block->cluster_no == cluster_no)
		{
			cur_block->uses++;
			memcpy(buf,cur_block->data,boot_sector.bytes_perSector);
			return buf;
		}
		cur_block_node->next;
	}
	/*********
	uint32_t *sectors = (uint32_t*) cluster_to_sectors(cluster_no);
	uint32_t sector_index;

	for (sector_index = 0; sector_index < 8;sector_index++)
	{
		char* tmp_buf = PPDINTERFACE_readSector(*(sectors+sector_index)); //TODO: ARMAR ARRAY DE SECTORES Y MANDARLOS
		memcpy(buf+(sector_index*boot_sector.bytes_perSector),tmp_buf,boot_sector.bytes_perSector);
		free(tmp_buf);
	}
	*/
	uint32_t *sectors = (uint32_t*) cluster_to_sectors(cluster_no);
	char *buf = PPDINTERFACE_readSectors(sectors,boot_sector.sectors_perCluster);
	free(sectors);
	return buf;
}


cluster_t fat32_readCluster(uint32_t cluster_number)
{

	cluster_t new_cluster;

	new_cluster.number= cluster_number;
	new_cluster.size = boot_sector.bytes_perSector*boot_sector.sectors_perCluster;
	new_cluster.data = fat32_readRawCluster(cluster_number);
	uint32_t index_sector;
	uint32_t *sectors = cluster_to_sectors(cluster_number);
	queue_t sector_list;
	QUEUE_initialize(&sector_list);

	for (index_sector = 0;index_sector < boot_sector.sectors_perCluster;index_sector++)
			{
				sector_t *new_sector = malloc(sizeof(sector_t));
				new_sector->data = new_cluster.data+(index_sector*boot_sector.bytes_perSector);
				new_sector->number = *(sectors+index_sector);
				new_sector->size = boot_sector.bytes_perSector;
				new_sector->modified = false;
				QUEUE_appendNode(&sector_list,new_sector);
			}

	free(sectors);
	new_cluster.sectors = sector_list;

	return new_cluster;
}


cluster_set_t fat32_readClusterChain(uint32_t first_cluster)
{
	queue_t clusterNumber_queue = FAT_getClusterChain(&fat,first_cluster);
	queue_t cluster_list;
	uint32_t bytes_perCluster = boot_sector.sectors_perCluster*boot_sector.bytes_perSector;

	QUEUE_initialize(&cluster_list);
	queueNode_t *cur_clusterNumber_node;

	char* clusterChain_data = malloc(QUEUE_length(&clusterNumber_queue)*bytes_perCluster);
	memset(clusterChain_data,0,QUEUE_length(&clusterNumber_queue)*bytes_perCluster);
	uint32_t cluster_index = 0;

	while ((cur_clusterNumber_node = QUEUE_takeNode(&clusterNumber_queue)) != NULL)
	{
		uint32_t cluster_number = *((uint32_t*) cur_clusterNumber_node->data);
		char* buf = fat32_readRawCluster(cluster_number);
		memcpy(clusterChain_data+(cluster_index*bytes_perCluster),buf,bytes_perCluster);
		free(buf);
		cluster_t *new_cluster = CLUSTER_newCluster(clusterChain_data+(cluster_index*bytes_perCluster),cluster_number);

		QUEUE_appendNode(&cluster_list,new_cluster);
		free(cur_clusterNumber_node->data);
		free(cur_clusterNumber_node);

		cluster_index++;
	}

	cluster_set_t new_clusterChain;
	new_clusterChain.size = cluster_index;
	new_clusterChain.data = clusterChain_data;
	new_clusterChain.clusters = cluster_list;

	return new_clusterChain;

}

queue_t fat32_readDirectory( char* path,cluster_set_t *cluster_chain)
{

	if (path[strlen(path)-1] == '/' && strlen(path) != 1)
	{
		path[strlen(path)-1] = '\0';
	}

	char *token;
	bool dir_exists = false;

	if ((*cluster_chain).clusters.begin == NULL)
	*cluster_chain = fat32_readClusterChain(2);
	queue_t file_list = DIRENTRY_interpretTableData(*cluster_chain);


	if (strcmp(path,"/") == 0) return file_list;

	token = strtok(path,"/");

	queueNode_t* curr_file_node;

	do
	{
		dir_exists = false;
		while ((curr_file_node = QUEUE_takeNode(&file_list)) != NULL)
		{
			fat32file_t *curr_file = (fat32file_t*) curr_file_node->data;
			if (strcmp(curr_file->long_file_name,token) == 0 && (curr_file->dir_entry->file_attribute.subdirectory) == true)
			{

				dir_exists=true;
				log_debug(log_file,"PFS","fat32_readDirectory() -> LIST_destroyList(0x%x,FAT32FILE_T)",file_list);
				uint32_t first_cluster = DIRENTRY_getClusterNumber(curr_file->dir_entry);

				CLUSTER_freeChain(cluster_chain);
				FILE_freeQueue(&file_list);

				*cluster_chain = fat32_readClusterChain(first_cluster);
				file_list = DIRENTRY_interpretTableData(*cluster_chain);

				break;
			}
			FILE_free(curr_file);
			free(curr_file_node);
		}

		if (dir_exists == false)
		{
			log_debug(log_file,"PFS","fat32_readDirectory() -> LIST_destroyList(0x%x),FAT32FILE_T)",file_list);
			return file_list;
		}

	} while((token = strtok( NULL, "/" )) != NULL && dir_exists == true);

	return file_list;
}

queue_t fat32_readDirectory2(char* path)
{
	assert(strcmp(path,"") != 0);
	if (path[strlen(path)-1] == '/' && strlen(path) != 1)
	{
		path[strlen(path)-1] = '\0';
	}

	char *token;
	bool dir_exists = false;


	queue_t cluster_list = FAT_getClusterChain(&fat,2);
	queueNode_t *next_cluster,*cluster = cluster_list.begin;
	cluster_t cluster_data;
	queue_t partial_list;
	QUEUE_initialize(&partial_list);
	queue_t file_list;
	QUEUE_initialize(&file_list);

	while (cluster != NULL)
	{
		cluster_data = fat32_readCluster(*((uint32_t*) cluster->data));
		partial_list = DIRTABLE_interpretFromCluster(cluster_data);
		CLUSTER_free(&cluster_data);

		if (file_list.begin == 0x0)
		{
			file_list.begin = partial_list.begin;
			file_list.end = partial_list.end;
		}
		else
		{
			file_list.end->next = partial_list.begin;
			file_list.end = partial_list.end;
		}
		next_cluster = cluster->next;
		free(cluster->data);
		free(cluster);
		cluster = next_cluster;
	}

	if (strcmp(path,"/") == 0) return file_list;

	token = strtok(path,"/");

	queueNode_t* curr_file_node;

	do
	{
		dir_exists = false;
		while ((curr_file_node = QUEUE_takeNode(&file_list)) != NULL)
		{
			fat32file_2_t *curr_file = (fat32file_2_t*) curr_file_node->data;
			if (strcmp(curr_file->long_file_name,token) == 0 && (curr_file->dir_entry.file_attribute.subdirectory) == true)
			{
				dir_exists=true;
				log_debug(log_file,"PFS","fat32_readDirectory() -> LIST_destroyList(0x%x,FAT32FILE_T)",file_list);
				uint32_t first_cluster = DIRENTRY_getClusterNumber(&curr_file->dir_entry);
				FILE_freeQueue(&file_list);

				cluster_list = FAT_getClusterChain(&fat,first_cluster);
				cluster = cluster_list.begin;

				while (cluster != NULL)
				{
					cluster_data = fat32_readCluster(*((uint32_t*) cluster->data));
					partial_list = DIRTABLE_interpretFromCluster(cluster_data);
					CLUSTER_free(&cluster_data);

						if (file_list.begin == 0x0)
						{
							file_list.begin = partial_list.begin;
							file_list.end = partial_list.end;
						}
						else
						{
							file_list.end->next = partial_list.begin;
							file_list.end = partial_list.end;
						}
						next_cluster = cluster->next;
						free(cluster->data);
						free(cluster);
						cluster = next_cluster;
				}
				break;
			}
			FILE_free(curr_file);
			free(curr_file_node);
		}

		if (dir_exists == false)
		{
			log_debug(log_file,"PFS","fat32_readDirectory() -> LIST_destroyList(0x%x),FAT32FILE_T)",file_list);
			return file_list;
		}

	} while((token = strtok( NULL, "/" )) != NULL && dir_exists == true);

	return file_list;
}

fat32file_2_t* fat32_getFileEntry(char* path)
{
	char *location;
	char *filename;																					//Copio el path (hasta donde empeiza el filename sin incluirlo) a location
																								//TODO: Primero buscar en cache
	FILE_splitNameFromPath(path,&filename,&location);
	//log_debug(log_file,"PFS","fat32_getDirEntry() -> fat32_readDirectory(%s)",location);
	queue_t file_list;
	QUEUE_initialize(&file_list);
	file_list = fat32_readDirectory2(location); 										//Obtengo una lista de los ficheros que hay en "location"

	queueNode_t *curr_file_node; 																//Creo un puntero que apuntara al file_node en proceso
	free(location); 																			//Libero la memoria de location

	fat32file_2_t *curr_file;																		//Creo un puntero que apuntara al file struct en proceso
	fat32file_2_t *fileentry_found = NULL;															/* Apunto direntry_found a NULL, si al final de la funcion
																		 	 	 	 	 sigue en NULL es que no existe el archivo/carpeta */

	while  ((curr_file_node = QUEUE_takeNode(&file_list)) != NULL)						//Mientras voy tomando los nodos de la cola
	{
		curr_file = (fat32file_2_t*) curr_file_node->data; 										//Casteo el puntero 'data' del nodo tomado a el puntero file en proceso

		if (strcmp(curr_file->long_file_name,filename) == 0) 									//Si el nombre conicide con el nombre del archivo buscado es que existe
		{
			fileentry_found = curr_file;
		}

		FILE_free(curr_file);
		free(curr_file_node);

		if (fileentry_found != NULL) break; 														//Si encontro algo, salgo del ciclo while
	}

	FILE_freeQueue(&file_list);

	return fileentry_found;																		//Retorno el puntero a la direntry del archivo buscado
}

uint32_t fat32_mk(char* fullpath,uint32_t dir_or_archive) //0 o 1
{
		char *filename;
		char *path;
		lfnEntry_t new_lfn;
		dirEntry_t new_direntry;
		FILE_splitNameFromPath(fullpath,&filename,&path);


		uint32_t folderTableCluster;
		if (strcmp(path,"/") != 0)
		{
			fat32file_2_t *entryOfFolder = fat32_getFileEntry(path);
			folderTableCluster = DIRENTRY_getClusterNumber(&entryOfFolder->dir_entry);

		}
		else
		{
			folderTableCluster = 2;
		}

		queue_t cluster_list = FAT_getClusterChain(&fat,folderTableCluster);
		queueNode_t *cluster_node;
		dirEntry_t* dirtable_index;
		dirEntry_t* dirtable_lastentry;
		bool write = false;
		while ((cluster_node = QUEUE_takeNode(&cluster_list)) != NULL)
		{
			cluster_t cluster = fat32_readCluster(*((uint32_t*) cluster_node->data));
			dirtable_index = (dirEntry_t*) cluster.data;
			dirtable_lastentry = (dirEntry_t*) (cluster.data + 4096 - sizeof(dirEntry_t));
			while (dirtable_index != dirtable_lastentry)
			{
				if (*((char*) dirtable_index) == 0xE5 || *((char*) dirtable_index) == 0x00)
				{
					write = true;
					uint32_t cluster_toAssign = FAT_getFreeCluster(&fat);
					FAT_setUsed(&fat,cluster_toAssign);
					new_lfn = LFNENTRY_create(filename);
					new_direntry = DIRENTRY_create(filename,cluster_toAssign,dir_or_archive);
					memcpy(dirtable_index++,&new_lfn,sizeof(lfnEntry_t));
					memcpy(dirtable_index,&new_direntry,sizeof(dirEntry_t));
					fat32_writeCluster(&cluster);
					FAT_write(&fat);
					break;
				}
				dirtable_index++;
			} //TODO LIBERAR LA LISTA
			if (write) break;
		}

		if (write != true)
		{
			uint32_t appended_cluster = FAT_appendCluster(fat,folderTableCluster);
			cluster_t cluster = fat32_readCluster(appended_cluster);
			uint32_t cluster_toAssign = FAT_getFreeCluster(&fat);
			FAT_setUsed(&fat,cluster_toAssign);
			new_lfn = LFNENTRY_create(filename);
			new_direntry = DIRENTRY_create(filename,cluster_toAssign,dir_or_archive);
			memcpy(cluster.data,&new_lfn,sizeof(lfnEntry_t));
			memcpy(cluster.data+sizeof(dirEntry_t),&new_direntry,sizeof(dirEntry_t));
			fat32_writeCluster(&cluster);
			FAT_write(&fat);
		}

		return 0;

}
/*fat32file_t fat32_getFileStruct(const char* path,clusterChain_t* cluster_chain)
{
	char *location;
	char *filename;
	FILE_splitNameFromPath(path,&filename,&location);

	queue_t file_list = fat32_readDirectory(location,cluster_chain);
	queueNode_t* cur_node;
	fat32file_t* cur_file;
	fat32file_t ret_file;
	while ((cur_node = QUEUE_takeNode(&file_list)) != NULL)
	{
		cur_file = (fat32file_t*) cur_node->data;
		if (strcmp(cur_file->long_file_name,filename) == 0)
		{

			ret_file = *cur_file;
			ret_file.long_file_name = malloc(strlen(cur_file->long_file_name));
			memset(ret_file.long_file_name,0,strlen(cur_file->long_file_name));
			strcpy(ret_file.long_file_name,cur_file->long_file_name);
		}
		FILE_free(cur_file);
		free(cur_node);
	}
	free(filename);
	free(location);
	return ret_file;
}*/

void fat32_writeCluster(cluster_t *cluster)
{
	//queueNode_t *cur_sector_node;
	PPDINTERFACE_writeSectors(cluster->sectors);
	/*while ((cur_sector_node = QUEUE_takeNode(&(cluster->sectors))) != NULL)
	{
		PPDINTERFACE_writeSector(*((sector_t*) cur_sector_node->data));
		QUEUE_freeNode(cur_sector_node);
	}*/
}

uint32_t fat32_truncate(char* fullpath,off_t new_size)
{
	char *filename;
		char *path;
		uint32_t bytes_perCluster = boot_sector.bytes_perSector * boot_sector.sectors_perCluster;
		//cluster_set_t cluster_chain;
		//memset(&cluster_chain,0,sizeof(cluster_set_t));

		fat32file_2_t *file_entry = fat32_getFileEntry(fullpath);
		uint32_t original_size = file_entry->dir_entry.file_size;

		if (original_size == new_size)
		{
			//CLUSTER_freeChain(&cluster_chain);
			return 0;
		}

		uint32_t first_cluster_no = DIRENTRY_getClusterNumber(&file_entry->dir_entry);
		queue_t file_clusters = FAT_getClusterChain(&fat,first_cluster_no);
		uint32_t file_clusters_no = QUEUE_length(&file_clusters);
		queueNode_t *file_cluster;

		while((file_cluster = QUEUE_takeNode(&file_clusters)) != NULL) QUEUE_freeNode(file_cluster);

		size_t needed_clusters = ((new_size - 1) / bytes_perCluster) + 1;
		//cluster_set_t file_clustersChain;
		char* last_byte;
		cluster_t last_cluster;
		if (needed_clusters < file_clusters_no)
		{
			uint32_t rest_to_remove = file_clusters_no - needed_clusters;
			uint32_t index = 0;
			for(index=0;index < rest_to_remove;index++)
			{
					FAT_removeCluster(fat,first_cluster_no);
			}

			//file_clustersChain = fat32_readClusterChain(first_cluster_no);}
			queue_t cluster_chain = FAT_getClusterChain(&fat,first_cluster_no);
			last_cluster = fat32_readCluster(*((uint32_t*) cluster_chain.end->data));
			size_t file_truncated_clusters = QUEUE_length(&cluster_chain);

			memset(last_cluster.data+new_size-((file_truncated_clusters-1)*4096),0,4096-new_size-((file_truncated_clusters-1)*4096));
			fat32_writeCluster(&last_cluster);
			CLUSTER_free(&last_cluster);
		}
		else if (needed_clusters > file_clusters_no)
		{
			uint32_t index;
			uint32_t appended_cl = 0;
			size_t clusters_to_append = needed_clusters - file_clusters_no;

			for(index=0;index < clusters_to_append;index++)
			{
				appended_cl = FAT_appendCluster(fat,first_cluster_no);
				cluster_t new_cluster = fat32_readCluster(appended_cl);
				memset(new_cluster.data,0,4096);
				fat32_writeCluster(&new_cluster);
				CLUSTER_free(&new_cluster);
			}
		}
		else
		{
			queue_t cluster_chain = FAT_getClusterChain(&fat,first_cluster_no);
			last_cluster = fat32_readCluster(*((uint32_t*) cluster_chain.end->data));
			size_t file_truncated_clusters = QUEUE_length(&cluster_chain);
			//last_byte = last_cluster.data+original_size-((file_truncated_clusters-1)*4096);

			if (original_size < new_size)
			{
				last_byte = last_cluster.data+original_size;
			}
			else if (original_size > new_size)
			{
				last_byte = last_cluster.data+new_size;
			}

			memset(last_byte,0,(uint32_t) (last_cluster.data+4096-last_byte));
			fat32_writeCluster(&last_cluster);
			CLUSTER_free(&last_cluster);
		}

			file_entry->dir_entry.file_size = new_size;

			cluster_t dirtable = fat32_readCluster(file_entry->cluster);
			memcpy(dirtable.data+file_entry->offset+sizeof(dirEntry_t),&file_entry->dir_entry,sizeof(dirEntry_t));
			fat32_writeCluster(&dirtable);
			CLUSTER_free(&dirtable);
			FAT_write(&fat);

		return 0;
}
