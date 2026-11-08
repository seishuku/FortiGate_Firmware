#include "ext2.h"
#include "ide.h"
#include "io.h"
#include <stdint.h>
#include <string.h>

#define EXT2_SUPER_MAGIC 0xEF53
#define EXT2_SUPER_OFFSET 1024

#define EXT2_FEATURE_INCOMPAT_FILETYPE 0x0002
#define EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER 0x0001
#define EXT2_FEATURE_RO_COMPAT_LARGE_FILE 0x0002

#define EXT2_S_IFDIR 0x4000
#define EXT2_ROOT_INODE 2

#define EXT2_NDIR_BLOCKS 12
#define EXT2_MAX_BLOCK_SIZE 4096

static uint16_t get_le16(const uint8_t *p)
{
	return (uint16_t)p[0]|((uint16_t)p[1]<<8);
}

static uint32_t get_le32(const uint8_t *p)
{
	return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

/*
 * Read one filesystem block.
 *
 * EXT2 block numbers are relative to the start of the
 * filesystem, not the physical disk.
 */
static int ext2_read_block(ext2_fs_t *fs, uint32_t block, void *buffer)
{
	if(block>=fs->blocksCount)
		return -1;

	uint32_t sectorsPerBlock=fs->blockSize/512;

	if(sectorsPerBlock==0)
		return -1;

	if(block>(0xffffffffu/sectorsPerBlock))
		return -1;

	uint32_t lba=fs->partitionStart+block*sectorsPerBlock;

	if(lba<fs->partitionStart)
		return -1;

	if((uint64_t)block*sectorsPerBlock+sectorsPerBlock>fs->partitionSectors)
		return -1;

	return ide_read(fs->dev, lba, sectorsPerBlock, buffer)==IDE_OK?0:-1;
}

/*
 * Read arbitrary bytes from the filesystem.
 *
 * Handles reads that cross filesystem block boundaries.
 */
static int ext2_read_bytes(ext2_fs_t *fs, uint32_t offset, uint32_t length, void *buffer)
{
	uint8_t blockBuffer[EXT2_MAX_BLOCK_SIZE]={ 0 };
	uint8_t *dst=(uint8_t *)buffer;

	while(length)
	{
		uint32_t blockNumber=offset/fs->blockSize;
		uint32_t blockOffset=offset%fs->blockSize;

		uint32_t amount=fs->blockSize-blockOffset;

		if(amount>length)
			amount=length;

		if(ext2_read_block(fs, blockNumber, blockBuffer) != 0)
			return -1;

		memcpy(dst, blockBuffer+blockOffset, amount);

		dst+=amount;
		offset+=amount;
		length-=amount;
	}

	return 0;
}

/*
 * Mount an EXT2 filesystem.
 */
int ext2_mount(ext2_fs_t *fs, ide_dev_t *dev, uint32_t partitionStart, uint32_t partitionSectors)
{
	memset(fs, 0, sizeof(*fs));

	fs->dev=dev;
	fs->partitionStart=partitionStart;
	fs->partitionSectors=partitionSectors;

	uart_puts("Reading EXT superblock...\n");

	uint8_t superblock[1024]={ 0 };
	if(ide_read(dev, partitionStart+2, 2, superblock)!=IDE_OK)
	{
		uart_puts("Superblock read failed.\n");
		return -1;
	}

	if(get_le16(superblock+56)!=EXT2_SUPER_MAGIC)
	{
		uart_puts("EXT magic not found.\n");
		return -1;
	}

	fs->inodesCount=get_le32(superblock+0);
	fs->blocksCount=get_le32(superblock+4);
	fs->firstDataBlock=get_le32(superblock+20);

	uint32_t logBlockSize=get_le32(superblock+24);

	if(logBlockSize>2)
	{
		uart_puts("Unsupported EXT block size.\n");
		return -1;
	}

	fs->blockSize=1024u<<logBlockSize;

	fs->blocksPerGroup=get_le32(superblock+32);
	fs->inodesPerGroup=get_le32(superblock+40);

	fs->featureCompat=get_le32(superblock+92);
	fs->featureIncompat=get_le32(superblock+96);
	fs->featureRoCompat=get_le32(superblock+100);

	/*
	 * Revision 0 always uses 128-byte inodes.
	 * Dynamic revision stores the inode size at offset 88.
	 */
	if(get_le32(superblock+76)==0)
		fs->inodeSize=128;
	else
		fs->inodeSize=get_le16(superblock+88);

	if(fs->inodeSize<128 ||
	   fs->inodeSize>fs->blockSize)
	{
		uart_puts("Invalid inode size.\n");
		return -1;
	}

	if(fs->blocksPerGroup==0||fs->inodesPerGroup==0||fs->blocksCount==0)
	{
		uart_puts("Invalid EXT geometry.\n");
		return -1;
	}

	if(fs->blockSize>EXT2_MAX_BLOCK_SIZE)
	{
		uart_puts("Block size too large.\n");
		return -1;
	}

	/*
	 * This implementation supports FILETYPE, but not other
	 * incompatible features.
	 */
	uint32_t incompatSupported=EXT2_FEATURE_INCOMPAT_FILETYPE;

	if(fs->featureIncompat&~incompatSupported)
	{
		uart_puts("Unsupported EXT incompat features: ");
		uart_hex32(fs->featureIncompat);
		uart_putc('\n');
		return -1;
	}

	/*
	 * These read-only-compatible features are safe for our
	 * initial read-only implementation.
	 */
	uint32_t roCompatSupported=EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER|EXT2_FEATURE_RO_COMPAT_LARGE_FILE;

	if(fs->featureRoCompat&~roCompatSupported)
	{
		uart_puts("Unsupported EXT read-only features: ");
		uart_hex32(fs->featureRoCompat);
		uart_putc('\n');
		return -1;
	}

	/*
	 * Verify the filesystem fits inside the partition.
	 */
	if((uint64_t)fs->blocksCount*(fs->blockSize/512)>partitionSectors)
	{
		uart_puts("Filesystem exceeds partition.\n");
		return -1;
	}

	/*
	 * The group descriptor table starts at:
	 *
	 *   first_data_block + 1
	 *
	 * For 1 KiB filesystems, this is block 2.
	 */
	uint32_t groupDescBlock=fs->firstDataBlock+1;

	uint8_t groupDescriptor[32]={ 0 };
	if(ext2_read_bytes(fs, groupDescBlock*fs->blockSize, sizeof(groupDescriptor), groupDescriptor)!=0)
	{
		uart_puts("Group descriptor read failed.\n");
		return -1;
	}

	/*
	 * Group descriptor offset 8:
	 * starting block of the inode table.
	 */
	fs->inodeTableBlock=get_le32(groupDescriptor+8);

	if(fs->inodeTableBlock>=fs->blocksCount)
	{
		uart_puts("Invalid inode table location.\n");
		return -1;
	}

	fs->mounted=true;

	uart_puts("EXT filesystem mounted.\n");

	return 0;
}

/*
 * List the root directory.
 *
 * EXT2 inode 2 is always the root directory inode.
 */
int ext2_list_root(ext2_fs_t *fs)
{
	if(!fs||!fs->mounted)
		return -1;

	uart_puts("\nReading root inode...\n");

	uint32_t inodeIndex=EXT2_ROOT_INODE-1;
	uint32_t inodeTableOffset=inodeIndex*fs->inodeSize;
	uint32_t inodeOffset=fs->inodeTableBlock*fs->blockSize+inodeTableOffset;

	uint8_t inode[EXT2_MAX_BLOCK_SIZE]={ 0 };
	if(ext2_read_bytes(fs, inodeOffset, fs->inodeSize, inode)!=0)
	{
		uart_puts("Root inode read failed.\n");
		return -1;
	}

	if((get_le16(inode)&0xF000)!=EXT2_S_IFDIR)
	{
		uart_puts("Root inode is not a directory.\n");
		return -1;
	}

	uint32_t directorySize=get_le32(inode+4);

	uart_puts("Root directory size: ");
	uart_hex32(directorySize);
	uart_putc('\n');

	/*
	 * Indexed directories require additional handling.
	 * Reject them rather than interpreting the index as
	 * ordinary directory entries.
	 */
	if(get_le32(inode+32)&0x00001000)
	{
		uart_puts("Indexed directories unsupported.\n");
		return -1;
	}

	if(directorySize>EXT2_NDIR_BLOCKS*fs->blockSize)
	{
		uart_puts("Root directory requires indirect blocks.\n");
		return -1;
	}

	uint32_t remaining=directorySize;
	uint8_t dirBlock[EXT2_MAX_BLOCK_SIZE]={ 0 };

	uart_puts("\nRoot directory entries:\n");

	for(uint32_t i=0;i<EXT2_NDIR_BLOCKS&&remaining;i++)
	{
		uint32_t blockLength=0;
		uint32_t offset=0;

		uint32_t blockNumber=get_le32(inode+40+i*4);

		if(blockNumber==0)
		{
			remaining-=fs->blockSize;
			continue;
		}

		if(ext2_read_block(fs, blockNumber, dirBlock) != 0)
		{
			uart_puts("Directory block read failed.\n");
			return -1;
		}

		blockLength=remaining;

		if(blockLength>fs->blockSize)
			blockLength=fs->blockSize;

		offset=0;

		while(offset<blockLength)
		{
			if(blockLength-offset<8)
			{
				uart_puts("Malformed directory entry.\n");
				return -1;
			}

			const uint8_t *entry=dirBlock+offset;

			uint32_t entryInode=get_le32(entry);
			uint16_t recordLength=get_le16(entry + 4);
			uint16_t nameLength=0;
			uint8_t fileType=0;

			if(fs->featureIncompat&EXT2_FEATURE_INCOMPAT_FILETYPE)
			{
				nameLength=entry[6];
				fileType=entry[7];
			}
			else
			{
				nameLength=get_le16(entry+6);
				fileType=0;
			}

			if(recordLength<8||(recordLength&3)!=0||recordLength>blockLength-offset||nameLength>recordLength-8)
			{
				uart_puts("Invalid directory record.\n");
				return -1;
			}

			if(entryInode!=0&&nameLength!=0)
			{
				uart_puts("  inode=");
				uart_hex32(entryInode);

				uart_puts(" type=");
				uart_hex32(fileType);

				uart_puts(" name=");

				for(uint32_t j=0;j<nameLength;j++)
				{
					char c=(char)entry[8+j];

					if(c>=32&&c<=126)
						uart_putc(c);
					else
						uart_putc('?');
				}

				uart_putc('\n');
			}

			offset+=recordLength;
		}

		remaining-=blockLength;
	}

	uart_puts("Directory listing complete.\n");

	return 0;
}
