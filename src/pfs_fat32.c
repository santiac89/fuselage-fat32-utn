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
#include <stdarg.h>

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
#include "pfs_addressing.h"

extern bootSector_t boot_sector;
extern fatTable_t fat;
extern t_log *log_file;
extern queue_t file_caches;



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

	//printf("READ RAW CLUSTER: ");
	//uint32_t rest = getMicroseconds();
		new_cluster.data = fat32_readRawCluster(cluster_number);
	//printf("%d\n",(getMicroseconds() - rest));


	uint32_t index_sector;
	uint32_t *sectors = (uint32_t*) cluster_to_sectors(cluster_number);
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

/*queue_t fat32_readDirectory2(const char* path)
{

	//TODO HACERLO RECURSIVO???
	assert(strcmp(path,"") != 0);
	char *fullpath =  malloc(strlen(path)+1);
	memset(fullpath,0,strlen(path)+1);
	if (path[strlen(path)-1] == '/' && strlen(path) != 1)
	{
		memcpy(fullpath,path,(strlen(path)-1));
	}
	else
	{
		memcpy(fullpath,path,strlen(path));
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

	if (strcmp(fullpath,"/") == 0)
	{
		free(fullpath);
		return file_list;
	}
	token = strtok(fullpath,"/");

	queueNode_t *cur_file_node;
	bool directory_exists = false;

	while ((cur_file_node = QUEUE_takeNode(&file_list)) != NULL)
	{
		fat32file_2_t *cur_file = (fat32file_2_t*) cur_file_node->data;
		if (strcmp(cur_file->long_file_name,token) == 0 && cur_file->dir_entry.file_attribute.subdirectory == true)
		{
			directory_exists = true;

			FILE_freeQueue(&file_list);
			cluster_list = FAT_getClusterChain(&fat, DIRENTRY_getClusterNumber(&cur_file->dir_entry) );

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

		}
		free(cur_file->long_file_name);
		free(cur_file);
		free(cur_file_node);

		if (dir_exists == false)
		{
			free(fullpath);
			return NULL;
		}

	}
}*/

queue_t fat32_readDirectory(const char* path)
{

	//TODO HACERLO RECURSIVO???
	assert(strcmp(path,"") != 0);
	char *fullpath =  malloc(strlen(path)+1);
	memset(fullpath,0,strlen(path)+1);
	if (path[strlen(path)-1] == '/' && strlen(path) != 1)
	{
		memcpy(fullpath,path,(strlen(path)-1));
	}
	else
	{
		memcpy(fullpath,path,strlen(path));
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

	if (strcmp(fullpath,"/") == 0)
	{
		free(fullpath);
		return file_list;
	}

	token = strtok(fullpath,"/");

	queueNode_t* curr_file_node;

	do
	{
		dir_exists = false;
		while ((curr_file_node = QUEUE_takeNode(&file_list)) != NULL)
		{
			fat32file_2_t *curr_file = (fat32file_2_t*) (curr_file_node->data);
			if (strcmp(curr_file->long_file_name,token) == 0 && (curr_file->dir_entry.file_attribute.subdirectory) == true)
			{
				dir_exists=true;
				FILE_freeQueue(&file_list);

				//TODO CUANDO SE BORRA UN ARCHIVO BORRAR LOS DATOS DEL CLUSTER

				cluster_list = FAT_getClusterChain(&fat,  DIRENTRY_getClusterNumber(&curr_file->dir_entry) );
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
			free(curr_file);
			free(curr_file_node);
		}

		if (dir_exists == false)
		{
			free(fullpath);
			return file_list;
		}

	} while((token = strtok( NULL, "/" )) != NULL && dir_exists == true);
	free(fullpath);
	return file_list;
}

fat32file_2_t* fat32_getFileEntry(const char* path)
{
	//TODO ARREGLAR QUE LEA EL DIRECTORIO RAIZ
	char *location;
	char *filename;																					//Copio el path (hasta donde empeiza el filename sin incluirlo) a location
																								//TODO: Primero buscar en cache
	FILE_splitNameFromPath(path,&filename,&location);

	queue_t file_list;
	QUEUE_initialize(&file_list);
	file_list = fat32_readDirectory(location); 										//Obtengo una lista de los ficheros que hay en "location"

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
		else
		{
			FILE_free(curr_file);
			free(curr_file);
		}


		free(curr_file_node);

		if (fileentry_found != NULL) break; 														//Si encontro algo, salgo del ciclo while
	}

	FILE_freeQueue(&file_list);

	return fileentry_found;																		//Retorno el puntero a la direntry del archivo buscado
}

uint32_t fat32_mk(const char* fullpath,uint32_t dir_or_archive) //0 o 1
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
			dirtable_lastentry = (dirEntry_t*) ((cluster.data + 4096) - (sizeof(dirEntry_t)));
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
			CLUSTER_free(&cluster);
			if (write) break;
		}

		if (write != true)
		{
			uint32_t appended_cluster = FAT_appendCluster(&fat,folderTableCluster);
			cluster_t cluster = fat32_readCluster(appended_cluster);
			uint32_t cluster_toAssign = FAT_getFreeCluster(&fat);

			FAT_setUsed(&fat,cluster_toAssign);


			new_lfn = LFNENTRY_create(filename);
			new_direntry = DIRENTRY_create(filename,cluster_toAssign,dir_or_archive);
			memcpy(cluster.data,&new_lfn,sizeof(lfnEntry_t));
			memcpy(cluster.data+sizeof(dirEntry_t),&new_direntry,sizeof(dirEntry_t));
			fat32_writeCluster(&cluster);
			FAT_write(&fat);
			CLUSTER_free(&cluster);

			//FAT_write(&fat);
		}

		return 0;

}

void fat32_writeCluster(cluster_t *cluster)
{
	//queueNode_t *cur_sector_node;
	PPDINTERFACE_writeSectors(cluster->sectors,QUEUE_length(&cluster->sectors));
	/*while ((cur_sector_node = QUEUE_takeNode(&(cluster->sectors))) != NULL)
	{
		PPDINTERFACE_writeSector(*((sector_t*) cur_sector_node->data));
		QUEUE_freeNode(cur_sector_node);
	}*/
}

uint32_t fat32_truncate(const char* fullpath,off_t new_size)
{
		uint32_t time = 0;

		uint32_t bytes_perCluster = boot_sector.bytes_perSector * boot_sector.sectors_perCluster;

		fat32file_2_t *file_entry = fat32_getFileEntry(fullpath);
		uint32_t original_size = file_entry->dir_entry.file_size;

		if (original_size == new_size)
		{
			return 0;
		}

		uint32_t first_cluster_no = DIRENTRY_getClusterNumber(&file_entry->dir_entry);
		queue_t file_clusters = FAT_getClusterChain(&fat,first_cluster_no);
		uint32_t file_clusters_no = QUEUE_length(&file_clusters);
		queueNode_t *file_cluster;

		while((file_cluster = QUEUE_takeNode(&file_clusters)) != NULL) QUEUE_freeNode(file_cluster);

		size_t needed_clusters = ((new_size - 1) / bytes_perCluster) + 1;

		char* last_byte;
		cluster_t last_cluster;

		if (needed_clusters < file_clusters_no)
		{
			uint32_t rest_to_remove = file_clusters_no - needed_clusters;
			uint32_t index = 0;
			for(index=0;index < rest_to_remove;index++)
			{
				printf("   -- REMOVE CLUSTER: ");
				time = getMicroseconds();
					FAT_removeCluster(&fat,first_cluster_no);
				printf("%d\n",(getMicroseconds() - time));
			}

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
				//printf("   -- APPEND CLUSTER: ");
				//time = getMicroseconds();
							appended_cl = FAT_appendCluster(&fat,first_cluster_no);
				//printf("%d\n",(getMicroseconds() - time));


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
			//size_t file_truncated_clusters = QUEUE_length(&cluster_chain);
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

void fat32_remove(const char* path)
{
		fat32file_2_t* file_entry = fat32_getFileEntry(path);
		cluster_t table_of_entry = fat32_readCluster(file_entry->cluster);
		*(table_of_entry.data+file_entry->offset) = 0xE5;
		*(table_of_entry.data+file_entry->offset+sizeof(dirEntry_t)) = 0xE5;

		uint32_t first_data_cluster = DIRENTRY_getClusterNumber(&file_entry->dir_entry);
		queue_t clusters = FAT_getClusterChain(&fat,first_data_cluster);

		queueNode_t* cur_cluster;
		//uint32_t *casted_fat = (uint32_t*) fat.table;
		while ((cur_cluster = QUEUE_takeNode(&clusters))!=NULL)
		{
			FAT_setFree(&fat,*((uint32_t*) cur_cluster->data));
		}

		FAT_write(&fat);
		fat32_writeCluster(&table_of_entry);
		CLUSTER_free(&table_of_entry);
}
