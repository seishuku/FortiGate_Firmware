#ifndef _EXT2_H_
#define _EXT2_H_

#include <stdint.h>
#include <stdbool.h>
#include "ide.h"

typedef struct
{
    ide_dev_t *dev;

    uint32_t partitionStart;
    uint32_t partitionSectors;

    uint32_t blocksCount;
    uint32_t inodesCount;
    uint32_t firstDataBlock;

    uint32_t blocksPerGroup;
    uint32_t inodesPerGroup;

    uint32_t blockSize;
    uint16_t inodeSize;

    uint32_t featureCompat;
    uint32_t featureIncompat;
    uint32_t featureRoCompat;

    uint32_t inodeTableBlock;

	bool mounted;
} ext2_fs_t;

int ext2_mount(ext2_fs_t *fs, ide_dev_t *dev, uint32_t partitionStart, uint32_t partitionSectors);
int ext2_list_root(ext2_fs_t *fs);
int ext2_read_file(ext2_fs_t *fs, const char *path, void *buffer, uint32_t bufferSize, uint32_t *bytesRead);

#endif
