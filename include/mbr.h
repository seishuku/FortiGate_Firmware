#ifndef _MBR_H_
#define _MBR_H_

#include <stdint.h>
#include "ide.h"

typedef struct
{
    uint8_t type;
    uint8_t status;
    uint32_t startLBA;
    uint32_t endLBA;
    uint32_t sectors;
} mbr_partition_t;

int mbr_get_partition(ide_dev_t *dev, uint8_t partitionNumber, mbr_partition_t *partition);

#endif
