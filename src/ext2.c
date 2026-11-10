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

static int ext2_read_inode(ext2_fs_t *fs, uint32_t inodeNumber, uint8_t *inode)
{
	uint8_t gd[32];

	uint32_t group;
	uint32_t index;
	uint32_t gdtBlock;
	uint32_t inodeTable;
	uint64_t offset;

	if(inodeNumber==0||inodeNumber>fs->inodesCount)
		return -1;

	group=(inodeNumber-1)/fs->inodesPerGroup;
	index=(inodeNumber-1)%fs->inodesPerGroup;

	gdtBlock=fs->firstDataBlock+1;

	if(ext2_read_bytes(fs, gdtBlock*fs->blockSize+group*32, sizeof(gd), gd)!=0)
		return -1;

	inodeTable=get_le32(gd+8);

	if(inodeTable==0||inodeTable>=fs->blocksCount)
		return -1;

	offset=(uint64_t)inodeTable*fs->blockSize+(uint64_t)index*fs->inodeSize;

	if(offset>0xFFFFFFFFull)
		return -1;

	return ext2_read_bytes(fs, (uint32_t)offset, fs->inodeSize, inode);
}

static int ext2_get_data_block(ext2_fs_t *fs, const uint8_t *inode, uint32_t logical, uint32_t *physical)
{
	uint32_t pointers=fs->blockSize/4;

	/* Direct blocks */
	if(logical<12)
	{
		*physical=get_le32(inode+40+logical*4);
		return 0;
	}

	logical-=12;

	/* Single, double and triple indirect blocks */
	uint32_t depth;
	for(depth=1;depth<=3;depth++)
	{
		uint64_t capacity=1;

		for(uint32_t i=0;i<depth;i++)
			capacity*=pointers;

		if(logical<capacity)
			break;

		logical-=(uint32_t)capacity;
	}

	if(depth>3)
		return -1;

	uint32_t block=get_le32(inode+40+(11+depth)*4);

	if(block==0)
	{
		*physical=0;
		return 0;
	}

	while(depth)
	{
		uint32_t j;

		uint8_t indirect[EXT2_MAX_BLOCK_SIZE]={ 0 };
		if(ext2_read_block(fs, block, indirect)!=0)
			return -1;

		uint32_t divisor=1;

		for(j=1;j<depth;j++)
			divisor*=pointers;

		uint32_t index=logical/divisor;
		logical%=divisor;

		if(index>=pointers)
			return -1;

		block=get_le32(indirect+index*4);

		if(block==0)
		{
			*physical=0;
			return 0;
		}

		depth--;
	}

	if(block>=fs->blocksCount)
		return -1;

	*physical=block;

	return 0;
}

static int ext2_find_entry(ext2_fs_t *fs, const uint8_t *dirInode, const char *name, uint32_t nameLength, uint32_t *resultInode)
{
	uint32_t directorySize=get_le32(dirInode + 4);
	uint32_t remaining=directorySize;
	uint32_t logical=0;

	if(get_le32(dirInode+32)&0x00001000)
		return -1; /* Indexed directories not supported */

	uint8_t blockBuffer[EXT2_MAX_BLOCK_SIZE];

	while(remaining)
	{
		uint32_t physical=0;
		if(ext2_get_data_block(fs, dirInode, logical, &physical)!=0)
			return -1;

		uint32_t blockLength=remaining;

		if(blockLength>fs->blockSize)
			blockLength=fs->blockSize;

		if(physical==0)
		{
			remaining-=blockLength;
			logical++;
			continue;
		}

		if(ext2_read_block(fs, physical, blockBuffer)!=0)
			return -1;

		uint32_t offset=0;
		while(offset<blockLength)
		{
			uint8_t *entry=blockBuffer+offset;

			uint32_t entryInode=get_le32(entry);
			uint16_t recordLength=get_le16(entry+4);
			uint16_t entryNameLength;

			if(fs->featureIncompat&EXT2_FEATURE_INCOMPAT_FILETYPE)
				entryNameLength=entry[6];
			else
				entryNameLength=get_le16(entry+6);

			if(recordLength<8||(recordLength&3)||recordLength>blockLength-offset||entryNameLength>recordLength-8)
				return -1;

			if(entryInode!=0&&entryNameLength==nameLength&&entryNameLength!=0&&memcmp(entry+8, name, nameLength)==0)
			{
				*resultInode=entryInode;
				return 0;
			}

			offset+=recordLength;
		}

		remaining-=blockLength;
		logical++;
	}

	return -1;
}

static int ext2_resolve_path(ext2_fs_t *fs, const char *path, uint8_t *resultInode)
{
	if(!path||!*path)
		return -1;

	uint8_t currentInode[EXT2_MAX_BLOCK_SIZE]={ 0 };
	uint8_t nextInode[EXT2_MAX_BLOCK_SIZE]={ 0 };
	uint32_t inodeNumber=EXT2_ROOT_INODE;

	if(ext2_read_inode(fs, inodeNumber, currentInode) != 0)
		return -1;

	char component[256]={ 0 };

	while(*path)
	{
		while(*path=='/')
			path++;

		if(!*path)
			break;

		uint32_t length=0;

		while(path[length]&&path[length]!='/')
		{
			if(length>=255)
				return -1;

			component[length]=path[length];
			length++;
		}

		component[length]='\0';
		path+=length;

		if((get_le16(currentInode)&0xF000)!=EXT2_S_IFDIR)
			return -1;

		uint32_t nextNumber=0;
		if(ext2_find_entry(fs, currentInode, component, length, &nextNumber) != 0)
			return -1;

		if(ext2_read_inode(
			fs,
			nextNumber,
			nextInode
		) != 0)
			return -1;

		memcpy(currentInode, nextInode, fs->inodeSize);
	}

	memcpy(resultInode, currentInode, fs->inodeSize);

	return 0;
}

int ext2_read_file(ext2_fs_t *fs, const char *path, void *buffer, uint32_t bufferSize, uint32_t *bytesRead)
{
	uint8_t *dst=(uint8_t *)buffer;

	if(bytesRead)
		*bytesRead=0;

	if(!fs||!fs->mounted||!path)
		return -1;

	uint8_t inode[EXT2_MAX_BLOCK_SIZE];
	if(ext2_resolve_path(fs, path, inode)!=0)
	{
		uart_puts("File not found: ");
		uart_puts(path);
		uart_putc('\n');
		return -1;
	}

	if((get_le16(inode)&0xF000)!=0x8000)
	{
		uart_puts("Not a regular file.\n");
		return -1;
	}

	uint32_t fileSize=get_le32(inode+4);

	if(!buffer)
	{
		if(bytesRead)
			*bytesRead=fileSize;

		return 0;
	}

	/* Reject files larger than our current 32-bit size support. */
	if(fileSize==0xFFFFFFFFu)
		return -1;

	if(fileSize>bufferSize)
	{
		uart_puts("Buffer too small.\n");
		return -1;
	}

	if(fileSize&&!buffer)
		return -1;

	uint32_t remaining=fileSize;
	uint32_t logical=0;

	while(remaining)
	{
		uint32_t physical=0;
		if(ext2_get_data_block(fs, inode, logical, &physical)!=0)
			return -1;

		uint32_t amount=remaining;

		if(amount>fs->blockSize)
			amount=fs->blockSize;

		if(physical==0)
			memset(dst, 0, amount);
		else
		{
			uint8_t blockBuffer[EXT2_MAX_BLOCK_SIZE]={ 0 };
			if(ext2_read_block(fs, physical, blockBuffer)!=0)
				return -1;

			memcpy(dst, blockBuffer, amount);
		}

		dst+=amount;
		remaining-=amount;
		logical++;
	}

	if(bytesRead)
		*bytesRead=fileSize;

	return 0;
}
