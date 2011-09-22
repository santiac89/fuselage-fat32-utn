/*
 * proceso_filesystem.c
 *
 *  Created on: 04/09/2011
 *      Author: utn_so
 */
#include <stdio.h>
#include <stdint.h>
#include "fuse_operations.h"

#include "pfs_fat32.h"
#include "pfs_comm.h"
#include "ppd_io.h"
#include "tad_fat.h"
#include "pfs_addressing.h"



int main(int argc, char *argv[])
{


	FAT_struct fat;
	BS_struct boot_sector;

	FAT32_readBootSector(&boot_sector);
	FAT32_readFAT(&fat,boot_sector.sectors_perFat32);


	cluster_node *first;
	FAT_getFreeClusters(first,&fat);

	char *buf;
	FAT32_getClusterData(2,&buf);

	//

	return 0;
 //return fuse_main(argc, argv, &fuselage_oper,NULL);
}


int fuselage_readdir(const char *path, void *buf, fuse_fill_dir_t filler,off_t offset, struct fuse_file_info *fi)
{
		(void) offset;
	    (void) fi;


	    filler(buf, ".", NULL, 0);
	    filler(buf, "..", NULL, 0);
	    filler(buf, "HOLA MUNDO", NULL, 0);

	    return 0;
}


