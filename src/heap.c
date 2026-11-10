#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "io.h"
#include "heap.h"

#define HEAP_ALIGNMENT 16u
#define HEAP_MAGIC     0x48454150u  /* "HEAP" */

extern uint8_t __stack_top[];

typedef struct heap_block
{
    uint32_t magic;
    size_t size;
    bool free;
    struct heap_block *next;
} __attribute__((aligned(16))) heap_block_t;

#define HEADER_SIZE ((sizeof(heap_block_t)+HEAP_ALIGNMENT-1u)&~(HEAP_ALIGNMENT-1u))

static heap_block_t *heapHead;
static uintptr_t heapBegin;
static uintptr_t heapLimit;
static bool heapReady;

static uintptr_t align_up(uintptr_t value)
{
    return (value+HEAP_ALIGNMENT-1u)&~(uintptr_t)(HEAP_ALIGNMENT-1u);
}

int heap_init(void)
{
    uintptr_t start=align_up((uintptr_t)0x100000); // beginning of the physical heap memory, has to be at 0x100000 due to legacy hardware using addresses below
    uintptr_t end=(uintptr_t)pci_read8(PCI_DEV(0, 0, 0, 0x5b))<< 3 << 20; // top of memory as reported by the northbridge

    heapReady=false;
    heapHead=0;

    if(end<=start)
        return -1;

    if((end-start)<=HEADER_SIZE)
        return -1;

    heapBegin=start;
    heapLimit=end;

    heapHead=(heap_block_t *)start;
    heapHead->size=(size_t)(end-start-HEADER_SIZE);
    heapHead->next=0;
    heapHead->magic=HEAP_MAGIC;
    heapHead->free=true;

    heapReady=true;

	return 0;
}

void *heap_alloc(size_t size)
{
    if(!heapReady||size==0)
        return 0;

    if(size>SIZE_MAX-(HEAP_ALIGNMENT-1u))
        return 0;

    size=(size+HEAP_ALIGNMENT-1u)&~(size_t)(HEAP_ALIGNMENT-1u);

    for(heap_block_t *block=heapHead;block;block=block->next)
    {
        if(block->magic!=HEAP_MAGIC)
			return 0;

        if(!block->free||block->size<size)
            continue;

        /* Split if enough space remains for another block. */
        if(block->size>=size+HEADER_SIZE+HEAP_ALIGNMENT)
        {
            uintptr_t newAddr=(uintptr_t)block+HEADER_SIZE+size;

            heap_block_t *newBlock=(heap_block_t *)newAddr;

            newBlock->size=block->size-size-HEADER_SIZE;
            newBlock->next=block->next;
            newBlock->magic=HEAP_MAGIC;
            newBlock->free=true;

            block->next=newBlock;
            block->size=size;
        }

        block->free=false;

        return (void *)((uintptr_t)block+HEADER_SIZE);
    }

    return 0;
}

void heap_free(void *ptr)
{
    if(!heapReady||!ptr)
        return;

    uintptr_t payload=(uintptr_t)ptr;

    if(payload<heapBegin+HEADER_SIZE||payload>=heapLimit)
        return;

    heap_block_t *block=(heap_block_t *)(payload-HEADER_SIZE);

    if((uintptr_t)block<heapBegin||(uintptr_t)block+HEADER_SIZE>heapLimit)
        return;

    if(block->magic!=HEAP_MAGIC||block->free)
        return;

    if(block->size>heapLimit-((uintptr_t)block+HEADER_SIZE))
        return;

    block->free=true;

    /*
     * Coalesce adjacent free blocks.
     * Blocks are kept in ascending address order.
     */
    heap_block_t *current=heapHead;

    while(current&&current->next)
    {
        heap_block_t *next=current->next;

        uintptr_t expectedNext=(uintptr_t)current+HEADER_SIZE+current->size;

        if(current->free&&next->free&&(uintptr_t)next==expectedNext)
        {
            current->size+=HEADER_SIZE+next->size;
            current->next=next->next;
            continue;
        }

        current=current->next;
    }
}

size_t heap_available(void)
{
    size_t total=0;

    if(!heapReady)
        return 0;

    for(heap_block_t *block=heapHead;block;block=block->next)
    {
        if(block->magic!=HEAP_MAGIC)
            return 0;

        if(block->free)
            total+=block->size;
    }

    return total;
}

size_t heap_largest_free(void)
{
    size_t largest=0;

    if(!heapReady)
        return 0;

    for(heap_block_t *block=heapHead;block;block=block->next)
    {
        if(block->magic!=HEAP_MAGIC)
            return 0;

        if(block->free&&block->size>largest)
            largest=block->size;
    }

    return largest;
}
