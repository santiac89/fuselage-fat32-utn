/*
 * pfs.c
 *
 *  Created on: 06/10/2011
 *      Author: utn_so
 */


#include <fuse.h>
#include <pthread.h>
#include <stdio.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>

#include "fuse_operations.h"
#include "config_manager.h"
#include "file_cache.h"
#include "tad_sockets.h"
#include "pfs_fat32.h"
#include "pfs_comm.h"
#include "tad_direntry.h"
#include "log.h"

char *mount_point;

extern socketPool_t sockets_toPPD;
config_param *config_param_list;
extern uint32_t cache_size_inBytes;
extern bootSector_t boot_sector;

fat_table_t fat;
t_log *log_file;

queue_t opened_files;

void finish_him()
{
	fflush(stdout);
	execl("/bin/umount","",mount_point);
	exit(0);
}

void* cache_dump()
{
	FILE *cache_file = fopen("cache_dump.txt","a+");

	queueNode_t *current_opened_file_node = opened_files.begin;

	while (current_opened_file_node != NULL)
	{

		opened_file_t *current_opened_file = (opened_file_t*) current_opened_file_node->data;
		if (current_opened_file->cache.begin != NULL)
		{
			time_t timestamp = time(NULL);
			char formatted_time[200];
			struct tm *time_struct = localtime(&timestamp);
			strftime(formatted_time, sizeof(formatted_time),"%d-%m-%Y %H:%M:%S", time_struct);
			fprintf(cache_file,"-------------------------------------\n");
			fprintf(cache_file,"Timestamp: %s\n",formatted_time);
			fprintf(cache_file,"Tamano de Bloque de Cache: %d Kb\n",(current_opened_file->cache_size)/1024);
			fprintf(cache_file,"Cantidad de Bloques de Cache: %d\n\n",QUEUE_length(&(current_opened_file->cache)));

			queueNode_t *cache_block_node = current_opened_file->cache.begin;
			uint32_t block_count = 1;
			while (cache_block_node != NULL)
			{
				fprintf(cache_file,"Contenido de Bloque de Cache %d:\n",block_count);
				cache_block_t *cache_block = (cache_block_t*) cache_block_node->data;
				char *byte = cache_block->data;
				uint32_t byte_count = 0;
				while (byte != cache_block->data+4096)
				{
					if (*byte == '\0')
						fprintf(cache_file,"/0");
					else if (*byte == '\n')
						fprintf(cache_file,"/n");
					else
						fprintf(cache_file,"%.1s",byte);

					byte_count++;
					byte++;

					if (byte_count == 50)
					{
						fprintf(cache_file,"\n");
						byte_count=0;
					}
				}

				fprintf(cache_file,"\n\n");

				block_count++;
				cache_block_node = cache_block_node->next;
			}
		}

		current_opened_file_node = current_opened_file_node->next;
	}

	fclose(cache_file);
	return NULL;
}

void* console_main(char* mount_point)
{
	uint32_t character = 0;

	char command[1024];
	char* cmd;
	char* param;

	while (1)
	{
		printf("\nfuselage>");
		memset(command,0,1024);
		while ((character = getchar()) != '\n')
			strcat(command,(char*) &character);

		param = strchr(command,' ');

		if (param != NULL)
			*param++ = '\0';

		if (strcmp(command,"exit") == 0)
		{
			break;
		}
		else if (strcmp(command,"finfo") == 0 && param != NULL)
		{
			fat32file_t *fileentry = fat32_getFileEntry(param);
			if (fileentry != NULL)
			{
				printf("\n#Clusters:\n");
				queue_t cluster_list = FAT_get_linked_clusters(DIRENTRY_getClusterNumber(&fileentry->dir_entry));

				uint32_t cluster_number = DIRENTRY_getClusterNumber(&fileentry->dir_entry);
				printf("%d, ",cluster_number);
				uint32_t cluster_count = 0;
				for (;cluster_count < 19;cluster_count++)
				{
					cluster_number = FAT_get_next_linked(cluster_number);
					if (FAT_isEOC(cluster_number))
					{
						break;
					}
					else
					{
						printf("%d, ",cluster_number);
					}
				}
				printf("\n");
			}
			else
			{
				printf("\nNo existe el archvivo");
			}
		}
		else if (strcmp(command,"fsinfo") == 0)
		{
			queue_t cluster_list = FAT_get_free_clusters(&fat);
			queueNode_t* cur_cluster_node = NULL;
			uint32_t count_free = 0;

			while ((cur_cluster_node = QUEUE_takeNode(&cluster_list)) != NULL)
			{
				++count_free;
				free(cur_cluster_node->data);
				free(cur_cluster_node);
			}

			printf("\nClusters ocupados: %d\n",fat.size - count_free);
			printf("Clusters libres: %d\n",count_free);
			printf("Tamaño sector: %d bytes\n",boot_sector.bytes_perSector);
			printf("Tamaño cluster: %d bytes\n",(boot_sector.bytes_perSector*boot_sector.sectors_perCluster));
			printf("Tamaño FAT: %d Kb",(sizeof(uint32_t)*fat.size/1024));
			fflush(stdout);

		}
		else
		{
			printf("\nComando no reconocido");
		}

	}

	execl("umount","",mount_point,NULL);
	exit(0);
	return NULL;
}

int main(int argc, char *argv[])
{
	if (CONFIG_read("./config/pfs.config",&config_param_list) != 1)
	{
		printf("\nNo se encuentra o no es posible leer el archivo ./config/pfs.config");
		exit(0);
	}

	char* ip = CONFIG_getValue(config_param_list,"IP");
	uint32_t port = atoi(CONFIG_getValue(config_param_list,"PORT"));
	uint32_t max_connections = atoi(CONFIG_getValue(config_param_list,"MAX_CONNECTIONS"));
	cache_size_inBytes = atoi(CONFIG_getValue(config_param_list,"CACHE_SIZE_INBYTES"));

	sockets_toPPD = ppd_create_connection_pool(max_connections,ip,port);

	if (sockets_toPPD.size == 0)
	{
		printf("¡¡ ERROR DE CONEXION CON EL OTRO EXTREMO !!");
		exit(-1);
	}

	signal(SIGKILL,finish_him);
	signal(SIGINT,finish_him);
	signal(SIGTERM,finish_him);
	signal(SIGUSR1,cache_dump);
	signal(SIGPIPE,finish_him);

	log_file = log_create("PFS","pfs.log",INFO,M_CONSOLE_DISABLE);

	ppd_read_boot_sector();
	FAT_read_table();

	pthread_t console_tid;
	pthread_create(&console_tid,NULL,console_main,argv[argc-1]);

	mount_point = malloc(strlen(argv[argc-1]));
	strncpy(mount_point,argv[argc-1],strlen(argv[argc-1]));

	fuse_main(argc,argv, &fuselage_oper,NULL);

	return 0;
}

int fuselage_readdir(const char *path, void *buf, fuse_fill_dir_t filler,off_t offset, struct fuse_file_info *fi)
{
	queue_t file_list = fat32_readDirectory(path); //Obtengo una lista de los ficheros que hay en "path"
	queueNode_t *curr_file_node = file_list.begin;
	fat32file_t *curr_file;
	while ((curr_file_node = QUEUE_takeNode(&file_list)) != NULL)
	{
		curr_file = (fat32file_t*) curr_file_node->data;
		filler(buf, curr_file->long_file_name, NULL, 0);
		FILE_free(curr_file);
		free(curr_file_node);
	}
	return 0;
}

static int fuselage_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;
	//memset(stbuf, 0, sizeof(struct stat));

	stbuf->st_blksize = 4096;
	if (strcmp(path,"/") == 0) //Si se solicitan los atributos del directorio raiz
	{
	  stbuf->st_mode = S_IFDIR | 0755;
	  stbuf->st_nlink = 2;
	  return res;
	}

	fat32file_t *current_file =  fat32_getFileEntry(path); //Obtengo la dirEntry_t del fichero apuntado por path

	if (current_file == NULL) //SI DEVOLVIO NULL, EL ARCHIVO O DIRECTORIO NO EXISTE
	{
		res = -ENOENT;
	}
	else if (current_file->dir_entry.file_attribute.subdirectory == true) //Si es un directorio
	{
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		stbuf->st_size = 4096;
		stbuf->st_ctim.tv_sec = DIRENTRY_convertDateTime(current_file->dir_entry.create_date,current_file->dir_entry.create_time);
		stbuf->st_atim.tv_sec = DIRENTRY_convertDateTime(current_file->dir_entry.create_date,current_file->dir_entry.create_time);
		stbuf->st_mtim.tv_sec = DIRENTRY_convertDateTime(current_file->dir_entry.create_date,current_file->dir_entry.create_time);

	}
	else if (current_file->dir_entry.file_attribute.archive == true) //Si es un archivo
	{
		stbuf->st_mode = S_IFREG | 0666;
		stbuf->st_nlink = 1;
		stbuf->st_size = current_file->dir_entry.file_size;
		stbuf->st_ino = DIRENTRY_getClusterNumber(&current_file->dir_entry);
		stbuf->st_ctim.tv_sec = DIRENTRY_convertDateTime(current_file->dir_entry.create_date,current_file->dir_entry.create_time);
		stbuf->st_atim.tv_sec = DIRENTRY_convertDateTime(current_file->dir_entry.create_date,current_file->dir_entry.create_time);
		stbuf->st_mtim.tv_sec = DIRENTRY_convertDateTime(current_file->dir_entry.create_date,current_file->dir_entry.create_time);
	}


	return res;
}

static int fuselage_open(const char *path, struct fuse_file_info *fi)
{
	opened_file_t *opened_file = OFILE_get(path);
	opened_file->open_count++;

	uint32_t cluster_number = DIRENTRY_getClusterNumber(&opened_file->file_entry.dir_entry);

	log_info(log_file,"PFS","\nARCHIVO ABIERTO: %s\nCLUSTERS ASOCIADOS: ",path);
	FILE *fd = fopen("pfs.log","a");

	fprintf(fd,"%d ",cluster_number);

	while (FAT_isEOC(cluster_number = FAT_get_next_linked(cluster_number)) == false)
	{
		fprintf(fd,"%d ",cluster_number);
	}

	fclose(fd);
	return 0;
}

static int fuselage_read(const char *path, char *file_buf, size_t bytes_to_read, off_t offset, struct fuse_file_info *fi)
{
	opened_file_t *opened_file = OFILE_get(path);

	if (offset >= opened_file->file_entry.dir_entry.file_size )
		return 0;
	else if (offset + bytes_to_read > opened_file->file_entry.dir_entry.file_size )
		bytes_to_read -= (offset + bytes_to_read - opened_file->file_entry.dir_entry.file_size);

	uint32_t bytes_perCluster = boot_sector.bytes_perSector * boot_sector.sectors_perCluster;

	uint32_t first_cluster_to_read = DIRENTRY_getClusterNumber(&(opened_file->file_entry.dir_entry));
	uint32_t clusters_to_skip = offset / bytes_perCluster;
	uint32_t offset_in_first_cluster = offset % bytes_perCluster;
	uint32_t clusters_to_read = ceil(((float)offset+bytes_to_read)/bytes_perCluster-((float)offset/(float)bytes_perCluster));
	uint32_t cluster_count = 0;

	char* tmp_buf = malloc(clusters_to_read*bytes_perCluster);

	for (;cluster_count < clusters_to_skip;cluster_count++)
	{
		first_cluster_to_read = FAT_get_next_linked(first_cluster_to_read);
	}

	for (cluster_count = 0;cluster_count < clusters_to_read;cluster_count++)
	{
		cache_block_t* block;
		if ((block = CACHE_read_block(&opened_file->cache,first_cluster_to_read)) != NULL)
		{
			memcpy(tmp_buf+(cluster_count*bytes_perCluster),block->data,bytes_perCluster);
		}
		else
		{
			cluster_t cluster_data = fat32_readCluster(first_cluster_to_read);
			cluster_t *cluster_from_replaced_block;

			if ((cluster_from_replaced_block  = CACHE_write_block(&opened_file->cache,cluster_data)) != NULL)
			{
				fat32_writeCluster(cluster_from_replaced_block);
				CLUSTER_free(cluster_from_replaced_block);
			}
			memcpy(tmp_buf+(cluster_count*bytes_perCluster),cluster_data.data,bytes_perCluster);
			CLUSTER_free(&cluster_data);
		}
		first_cluster_to_read = FAT_get_next_linked(first_cluster_to_read);
	}

	memcpy(file_buf,tmp_buf+offset_in_first_cluster,bytes_to_read);
	free(tmp_buf);

	return bytes_to_read;
}

static int fuselage_flush(const char *path, struct fuse_file_info *fi)
{

	queueNode_t *cur_cache_node;
	queue_t aux_queue;
	QUEUE_initialize(&aux_queue);

	opened_file_t *file_to_flush = OFILE_get(path);

	queueNode_t* cur_cache_block;
	while((cur_cache_block = QUEUE_takeNode(&file_to_flush->cache)) != NULL)
	{
		cache_block_t *cur_block = (cache_block_t*) cur_cache_block->data;
		if (cur_block->modified)
		{
			cluster_t *write_cluster = CLUSTER_newCluster(cur_block->data,cur_block->cluster_no);
			fat32_writeCluster(write_cluster);
			CLUSTER_free(write_cluster);
			free(write_cluster);
			free(cur_block);
			free(cur_cache_block);
		}
		else
		{
			free(cur_block->data);
			free(cur_block);
			free(cur_cache_block);
		}
	}

	return 0;
}

static int fuselage_rename(const char *cur_name, const char *new_name)
{
	/* INICIO DEFINICION VARIABLE */
		char *new_filename, *new_path, *path,*filename;
		char *utf16_filename = malloc(26);
		memset(utf16_filename,0,26);
	/* FIN DEFINICION VARIABLES */

	/* INICIO LOGICA DE LA FUNCION */

	if (fat32_getFileEntry(new_name) == NULL) // SI NO EXISTE OTRO ARCHIVO CON EL MISMO NOMBRE
	{
		FILE_splitNameFromPath(new_name,&new_filename,&new_path);
		FILE_splitNameFromPath(cur_name,&filename,&path);

		if (strcmp(path,new_path) == 0)
		{
			fat32file_t *file_entry = fat32_getFileEntry(cur_name);
			cluster_t entry_cluster = fat32_readCluster(file_entry->cluster);

			if (file_entry->has_lfn)
			{
				LFNENTRY_setNameChars(&file_entry->lfn_entry,new_filename);
				DIRENTRY_setDosName(&file_entry->dir_entry,new_filename);
				memcpy(entry_cluster.data+file_entry->offset,&file_entry->lfn_entry,sizeof(lfnEntry_t));
				memcpy(entry_cluster.data+file_entry->offset+sizeof(lfnEntry_t),&file_entry->dir_entry,sizeof(dirEntry_t));
			}
			else
			{
				DIRENTRY_setDosName(&file_entry->dir_entry,new_filename);
				memcpy(entry_cluster.data+file_entry->offset,&file_entry->dir_entry,sizeof(dirEntry_t));
			}
			free(new_filename);
			free(new_path);
			free(path);
			free(filename);

			fat32_writeCluster(&entry_cluster);
			CLUSTER_free(&entry_cluster);
		}
		else
		{
			fat32file_t *file_entry = fat32_getFileEntry(cur_name);

			LFNENTRY_setNameChars(&file_entry->lfn_entry,new_filename);
			DIRENTRY_setDosName(&file_entry->dir_entry,new_filename);

			fat32file_t *newpath_entry = fat32_getFileEntry(new_path);

			cluster_t path_cluster = fat32_readCluster(file_entry->cluster);
			cluster_t newpath_cluster = fat32_readCluster(DIRENTRY_getClusterNumber(&newpath_entry->dir_entry));

			*(path_cluster.data+file_entry->offset) = 0xE5;

			if (file_entry->has_lfn)
				*(path_cluster.data+file_entry->offset+sizeof(dirEntry_t)) = 0xE5;

			fat32_writeCluster(&path_cluster);
			CLUSTER_free(&path_cluster);

			dirEntry_t *dirtable_index = (dirEntry_t*) newpath_cluster.data;
			dirEntry_t *dirtable_lastentry = (dirEntry_t*) (newpath_cluster.data + 4096 - sizeof(dirEntry_t));

			while (dirtable_index != dirtable_lastentry)
			{
				if (*((char*) dirtable_index) == 0xE5 || *((char*) dirtable_index) == 0x00)
				{
					if (file_entry->has_lfn)
					{
						memcpy(dirtable_index++,&file_entry->lfn_entry,sizeof(dirEntry_t));
					}
					memcpy(dirtable_index,&file_entry->dir_entry,sizeof(dirEntry_t));
					break;
				}
				dirtable_index++;
			}

			fat32_writeCluster(&newpath_cluster);
			CLUSTER_free(&newpath_cluster);
		}

	}

	free(utf16_filename);

	return 0;

}

static int fuselage_truncate(const char *fullpath, off_t new_size)
{
	opened_file_t *opened_file = OFILE_get(fullpath);
	opened_file->file_entry.dir_entry.file_size = fat32_truncate(fullpath,new_size);
	return 0;
}

static int fuselage_write(const char *path, const char *file_buf, size_t bytes_to_write, off_t offset, struct fuse_file_info *fi)
{
	opened_file_t *opened_file = OFILE_get(path);
	pthread_mutex_lock(&opened_file->write_mutex);
	uint32_t bytes_perCluster = boot_sector.bytes_perSector * boot_sector.sectors_perCluster;

	if (offset+bytes_to_write > opened_file->file_entry.dir_entry.file_size)
		opened_file->file_entry.dir_entry.file_size = fat32_truncate(path,offset+bytes_to_write);

	uint32_t bytes_left_to_write = bytes_to_write;

	uint32_t first_cluster_to_write = DIRENTRY_getClusterNumber(&(opened_file->file_entry.dir_entry));
	uint32_t clusters_to_skip = offset / bytes_perCluster;
	uint32_t clusters_to_write = ceil(((float)offset+bytes_to_write)/bytes_perCluster-((float)offset/(float)bytes_perCluster));// ((offset+bytes_to_write) + (bytes_perCluster - 1)) / bytes_perCluster;
	uint32_t cluster_count = 0;
	uint32_t offset_in_file = offset;
	uint32_t offset_in_buffer = 0;

	for (;cluster_count < clusters_to_skip;cluster_count++)
	{
		first_cluster_to_write = FAT_get_next_linked(first_cluster_to_write);
	}

	for (cluster_count = 0;cluster_count < clusters_to_write;cluster_count++)
	{
		uint32_t offset_in_cluster = offset_in_file % bytes_perCluster;
		uint32_t bytes_in_this_write;

		if (bytes_left_to_write > bytes_perCluster - offset_in_cluster)
		{
			bytes_in_this_write = bytes_perCluster - offset_in_cluster;
		}
		else
		{
			bytes_in_this_write = bytes_left_to_write;
		}

		cache_block_t *block = CACHE_read_block(&opened_file->cache,first_cluster_to_write);

		if (block != NULL)
		{
			memcpy(block->data+offset_in_cluster,file_buf+offset_in_buffer,bytes_in_this_write);
			block->modified = true;
		}
		else
		{
			cluster_t cluster = fat32_readCluster(first_cluster_to_write);
			memcpy(cluster.data+offset_in_cluster,file_buf+offset_in_buffer,bytes_in_this_write);
			cluster_t *cluster_replaced = CACHE_write_block(&opened_file->cache,cluster);

			if (cluster_replaced != NULL)
			{
				fat32_writeCluster(cluster_replaced);
				CLUSTER_free(cluster_replaced);
			}
			else if (cluster_replaced == NULL && opened_file->cache_size == 0)
			{
				fat32_writeCluster(&cluster);
			}

			CLUSTER_free(&cluster);
		}

		bytes_left_to_write -= bytes_in_this_write;
		offset_in_file+=bytes_in_this_write;
		offset_in_buffer+=bytes_in_this_write;
		first_cluster_to_write = FAT_get_next_linked(first_cluster_to_write);
	}

	pthread_mutex_unlock(&opened_file->write_mutex);
	return bytes_to_write;
}

static int fuselage_create(const char *fullpath, mode_t mode, struct fuse_file_info *fi)
{
	return fat32_mk(fullpath,ARCHIVE_ATTR);
}

static int fuselage_mkdir(const char *fullpath, mode_t mode)
{
	return fat32_mk(fullpath,DIR_ATTR);
}

static int fuselage_rmdir(const char *path)
{
	fat32_remove(path);
	return 0;
}

static int fuselage_unlink(const char *path)
{
	fat32_remove(path);
	return 0;
}

static int fuselage_release(const char *path, struct fuse_file_info *fi)
{
	opened_file_t *opened_file = OFILE_get(path);
	if (opened_file != NULL)
	{
		if (--(opened_file->open_count) == 0 && opened_file->cache_size != 0)
		{
			fuselage_flush(path,fi);
		}
	}
	return 0;
}





