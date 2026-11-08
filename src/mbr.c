#include <stdint.h>
#include "ide.h"
#include "mbr.h"

#define MBR_PARTITION_OFFSET 446

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int mbr_get_partition(ide_dev_t *dev, uint8_t partitionNumber, mbr_partition_t *partition)
{
    if(!dev||!partition)
        return -1;

    if(partitionNumber<1||partitionNumber>4)
        return -1;

    uint8_t sector[512]={ 0 };
    if(ide_read(dev, 0, 1, sector)!=IDE_OK)
        return -2;

    if(sector[510]!=0x55||sector[511]!=0xAA)
        return -3;

    const uint8_t *entry=&sector[MBR_PARTITION_OFFSET+(partitionNumber-1)*16];

    partition->status=entry[0];
    partition->type=entry[4];

    uint32_t start=read_le32(entry+8);
    uint32_t count=read_le32(entry+12);

    if(partition->type==0||count==0)
        return -4;

    uint64_t end=(uint64_t)start+count-1;

    if(end>0xFFFFFFFFull)
        return -5;

    if(end>=dev->sectors)
        return -6;

    partition->startLBA=start;
    partition->endLBA=(uint32_t)end;
    partition->sectors=count;

    return 0;
}
