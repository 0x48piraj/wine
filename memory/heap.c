/*
 * Win32 heap functions
 *
 * Copyright 1996 Alexandre Julliard
 * Copyright 1998 Ulrich Weigand
 */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "wine/winbase16.h"
#include "wine/winestring.h"
#include "wine/unicode.h"
#include "selectors.h"
#include "global.h"
#include "winbase.h"
#include "winerror.h"
#include "winnt.h"
#include "heap.h"
#include "toolhelp.h"
#include "debugtools.h"
#include "winnls.h"

DEFAULT_DEBUG_CHANNEL(heap);

/* Note: the heap data structures are based on what Pietrek describes in his
 * book 'Windows 95 System Programming Secrets'. The layout is not exactly
 * the same, but could be easily adapted if it turns out some programs
 * require it.
 */

typedef struct tagARENA_INUSE
{
    DWORD  size;                    /* Block size; must be the first field */
    WORD   magic;                   /* Magic number */
    WORD   threadId;                /* Allocating thread id */
    void  *callerEIP;               /* EIP of caller upon allocation */
} ARENA_INUSE;

typedef struct tagARENA_FREE
{
    DWORD                 size;     /* Block size; must be the first field */
    WORD                  magic;    /* Magic number */
    WORD                  threadId; /* Freeing thread id */
    struct tagARENA_FREE *next;     /* Next free arena */
    struct tagARENA_FREE *prev;     /* Prev free arena */
} ARENA_FREE;

#define ARENA_FLAG_FREE        0x00000001  /* flags OR'ed with arena size */
#define ARENA_FLAG_PREV_FREE   0x00000002
#define ARENA_SIZE_MASK        0xfffffffc
#define ARENA_INUSE_MAGIC      0x4842      /* Value for arena 'magic' field */
#define ARENA_FREE_MAGIC       0x4846      /* Value for arena 'magic' field */

#define ARENA_INUSE_FILLER     0x55
#define ARENA_FREE_FILLER      0xaa

#define QUIET                  1           /* Suppress messages  */
#define NOISY                  0           /* Report all errors  */

#define HEAP_NB_FREE_LISTS   4   /* Number of free lists */

/* Max size of the blocks on the free lists */
static const DWORD HEAP_freeListSizes[HEAP_NB_FREE_LISTS] =
{
    0x20, 0x80, 0x200, 0xffffffff
};

typedef struct
{
    DWORD       size;
    ARENA_FREE  arena;
} FREE_LIST_ENTRY;

struct tagHEAP;

typedef struct tagSUBHEAP
{
    DWORD               size;       /* Size of the whole sub-heap */
    DWORD               commitSize; /* Committed size of the sub-heap */
    DWORD               headerSize; /* Size of the heap header */
    struct tagSUBHEAP  *next;       /* Next sub-heap */
    struct tagHEAP     *heap;       /* Main heap structure */
    DWORD               magic;      /* Magic number */
    WORD                selector;   /* Selector for HEAP_WINE_SEGPTR heaps */
} SUBHEAP;

#define SUBHEAP_MAGIC    ((DWORD)('S' | ('U'<<8) | ('B'<<16) | ('H'<<24)))

typedef struct tagHEAP
{
    SUBHEAP          subheap;       /* First sub-heap */
    struct tagHEAP  *next;          /* Next heap for this process */
    FREE_LIST_ENTRY  freeList[HEAP_NB_FREE_LISTS];  /* Free lists */
    CRITICAL_SECTION critSection;   /* Critical section for serialization */
    DWORD            flags;         /* Heap flags */
    DWORD            magic;         /* Magic number */
    void            *private;       /* Private pointer for the user of the heap */
} HEAP;

#define HEAP_MAGIC       ((DWORD)('H' | ('E'<<8) | ('A'<<16) | ('P'<<24)))

#define HEAP_DEF_SIZE        0x110000   /* Default heap size = 1Mb + 64Kb */
#define HEAP_MIN_BLOCK_SIZE  (8+sizeof(ARENA_FREE))  /* Min. heap block size */
#define COMMIT_MASK          0xffff  /* bitmask for commit/decommit granularity */

HANDLE SystemHeap = 0;
HANDLE SegptrHeap = 0;

SYSTEM_HEAP_DESCR *SystemHeapDescr = 0;

static HEAP *processHeap;  /* main process heap */
static HEAP *firstHeap;    /* head of secondary heaps list */

/* address where we try to map the system heap */
#define SYSTEM_HEAP_BASE  ((void*)0x65430000)

static BOOL HEAP_IsRealArena( HANDLE heap, DWORD flags, LPCVOID block, BOOL quiet );

#ifdef __GNUC__
#define GET_EIP()    (__builtin_return_address(0))
#define SET_EIP(ptr) ((ARENA_INUSE*)(ptr) - 1)->callerEIP = GET_EIP()
#else
#define GET_EIP()    0
#define SET_EIP(ptr) /* nothing */
#endif  /* __GNUC__ */

/***********************************************************************
 *           HEAP_Dump
 */
void HEAP_Dump( HEAP *heap )
{
    int i;
    SUBHEAP *subheap;
    char *ptr;

    DPRINTF( "Heap: %08lx\n", (DWORD)heap );
    DPRINTF( "Next: %08lx  Sub-heaps: %08lx",
	  (DWORD)heap->next, (DWORD)&heap->subheap );
    subheap = &heap->subheap;
    while (subheap->next)
    {
        DPRINTF( " -> %08lx", (DWORD)subheap->next );
        subheap = subheap->next;
    }

    DPRINTF( "\nFree lists:\n Block   Stat   Size    Id\n" );
    for (i = 0; i < HEAP_NB_FREE_LISTS; i++)
        DPRINTF( "%08lx free %08lx %04x prev=%08lx next=%08lx\n",
	      (DWORD)&heap->freeList[i].arena, heap->freeList[i].arena.size,
	      heap->freeList[i].arena.threadId,
	      (DWORD)heap->freeList[i].arena.prev,
	      (DWORD)heap->freeList[i].arena.next );

    subheap = &heap->subheap;
    while (subheap)
    {
        DWORD freeSize = 0, usedSize = 0, arenaSize = subheap->headerSize;
        DPRINTF( "\n\nSub-heap %08lx: size=%08lx committed=%08lx\n",
	      (DWORD)subheap, subheap->size, subheap->commitSize );
	
        DPRINTF( "\n Block   Stat   Size    Id\n" );
        ptr = (char*)subheap + subheap->headerSize;
        while (ptr < (char *)subheap + subheap->size)
        {
            if (*(DWORD *)ptr & ARENA_FLAG_FREE)
            {
                ARENA_FREE *pArena = (ARENA_FREE *)ptr;
                DPRINTF( "%08lx free %08lx %04x prev=%08lx next=%08lx\n",
		      (DWORD)pArena, pArena->size & ARENA_SIZE_MASK,
		      pArena->threadId, (DWORD)pArena->prev,
		      (DWORD)pArena->next);
                ptr += sizeof(*pArena) + (pArena->size & ARENA_SIZE_MASK);
                arenaSize += sizeof(ARENA_FREE);
                freeSize += pArena->size & ARENA_SIZE_MASK;
            }
            else if (*(DWORD *)ptr & ARENA_FLAG_PREV_FREE)
            {
                ARENA_INUSE *pArena = (ARENA_INUSE *)ptr;
                DPRINTF( "%08lx Used %08lx %04x back=%08lx EIP=%p\n",
		      (DWORD)pArena, pArena->size & ARENA_SIZE_MASK,
		      pArena->threadId, *((DWORD *)pArena - 1),
		      pArena->callerEIP );
                ptr += sizeof(*pArena) + (pArena->size & ARENA_SIZE_MASK);
                arenaSize += sizeof(ARENA_INUSE);
                usedSize += pArena->size & ARENA_SIZE_MASK;
            }
            else
            {
                ARENA_INUSE *pArena = (ARENA_INUSE *)ptr;
                DPRINTF( "%08lx used %08lx %04x EIP=%p\n",
		      (DWORD)pArena, pArena->size & ARENA_SIZE_MASK,
		      pArena->threadId, pArena->callerEIP );
                ptr += sizeof(*pArena) + (pArena->size & ARENA_SIZE_MASK);
                arenaSize += sizeof(ARENA_INUSE);
                usedSize += pArena->size & ARENA_SIZE_MASK;
            }
        }
        DPRINTF( "\nTotal: Size=%08lx Committed=%08lx Free=%08lx Used=%08lx Arenas=%08lx (%ld%%)\n\n",
	      subheap->size, subheap->commitSize, freeSize, usedSize,
	      arenaSize, (arenaSize * 100) / subheap->size );
        subheap = subheap->next;
    }
}


/***********************************************************************
 *           HEAP_GetPtr
 * RETURNS
 *	Pointer to the heap
 *	NULL: Failure
 */
static HEAP *HEAP_GetPtr(
             HANDLE heap /* [in] Handle to the heap */
) {
    HEAP *heapPtr = (HEAP *)heap;
    if (!heapPtr || (heapPtr->magic != HEAP_MAGIC))
    {
        ERR("Invalid heap %08x!\n", heap );
        SetLastError( ERROR_INVALID_HANDLE );
        return NULL;
    }
    if (TRACE_ON(heap) && !HEAP_IsRealArena( heap, 0, NULL, NOISY ))
    {
        HEAP_Dump( heapPtr );
        assert( FALSE );
        SetLastError( ERROR_INVALID_HANDLE );
        return NULL;
    }
    return heapPtr;
}


/***********************************************************************
 *           HEAP_InsertFreeBlock
 *
 * Insert a free block into the free list.
 */
static void HEAP_InsertFreeBlock( HEAP *heap, ARENA_FREE *pArena )
{
    FREE_LIST_ENTRY *pEntry = heap->freeList;
    while (pEntry->size < pArena->size) pEntry++;
    pArena->size      |= ARENA_FLAG_FREE;
    pArena->next       = pEntry->arena.next;
    pArena->next->prev = pArena;
    pArena->prev       = &pEntry->arena;
    pEntry->arena.next = pArena;
}


/***********************************************************************
 *           HEAP_FindSubHeap
 * Find the sub-heap containing a given address.
 *
 * RETURNS
 *	Pointer: Success
 *	NULL: Failure
 */
static SUBHEAP *HEAP_FindSubHeap(
                HEAP *heap, /* [in] Heap pointer */
                LPCVOID ptr /* [in] Address */
) {
    SUBHEAP *sub = &heap->subheap;
    while (sub)
    {
        if (((char *)ptr >= (char *)sub) &&
            ((char *)ptr < (char *)sub + sub->size)) return sub;
        sub = sub->next;
    }
    return NULL;
}


/***********************************************************************
 *           HEAP_Commit
 *
 * Make sure the heap storage is committed up to (not including) ptr.
 */
static inline BOOL HEAP_Commit( SUBHEAP *subheap, void *ptr )
{
    DWORD size = (DWORD)((char *)ptr - (char *)subheap);
    size = (size + COMMIT_MASK) & ~COMMIT_MASK;
    if (size > subheap->size) size = subheap->size;
    if (size <= subheap->commitSize) return TRUE;
    if (!VirtualAlloc( (char *)subheap + subheap->commitSize,
                       size - subheap->commitSize, MEM_COMMIT,
                       PAGE_EXECUTE_READWRITE))
    {
        WARN("Could not commit %08lx bytes at %08lx for heap %08lx\n",
                 size - subheap->commitSize,
                 (DWORD)((char *)subheap + subheap->commitSize),
                 (DWORD)subheap->heap );
        return FALSE;
    }
    subheap->commitSize = size;
    return TRUE;
}


/***********************************************************************
 *           HEAP_Decommit
 *
 * If possible, decommit the heap storage from (including) 'ptr'.
 */
static inline BOOL HEAP_Decommit( SUBHEAP *subheap, void *ptr )
{
    DWORD size = (DWORD)((char *)ptr - (char *)subheap);
    /* round to next block and add one full block */
    size = ((size + COMMIT_MASK) & ~COMMIT_MASK) + COMMIT_MASK + 1;
    if (size >= subheap->commitSize) return TRUE;
    if (!VirtualFree( (char *)subheap + size,
                      subheap->commitSize - size, MEM_DECOMMIT ))
    {
        WARN("Could not decommit %08lx bytes at %08lx for heap %08lx\n",
                 subheap->commitSize - size,
                 (DWORD)((char *)subheap + size),
                 (DWORD)subheap->heap );
        return FALSE;
    }
    subheap->commitSize = size;
    return TRUE;
}


/***********************************************************************
 *           HEAP_CreateFreeBlock
 *
 * Create a free block at a specified address. 'size' is the size of the
 * whole block, including the new arena.
 */
static void HEAP_CreateFreeBlock( SUBHEAP *subheap, void *ptr, DWORD size )
{
    ARENA_FREE *pFree;

    /* Create a free arena */

    pFree = (ARENA_FREE *)ptr;
    pFree->threadId = GetCurrentTask();
    pFree->magic = ARENA_FREE_MAGIC;

    /* If debugging, erase the freed block content */

    if (TRACE_ON(heap))
    {
        char *pEnd = (char *)ptr + size;
        if (pEnd > (char *)subheap + subheap->commitSize)
            pEnd = (char *)subheap + subheap->commitSize;
        if (pEnd > (char *)(pFree + 1))
            memset( pFree + 1, ARENA_FREE_FILLER, pEnd - (char *)(pFree + 1) );
    }

    /* Check if next block is free also */

    if (((char *)ptr + size < (char *)subheap + subheap->size) &&
        (*(DWORD *)((char *)ptr + size) & ARENA_FLAG_FREE))
    {
        /* Remove the next arena from the free list */
        ARENA_FREE *pNext = (ARENA_FREE *)((char *)ptr + size);
        pNext->next->prev = pNext->prev;
        pNext->prev->next = pNext->next;
        size += (pNext->size & ARENA_SIZE_MASK) + sizeof(*pNext);
        if (TRACE_ON(heap))
            memset( pNext, ARENA_FREE_FILLER, sizeof(ARENA_FREE) );
    }

    /* Set the next block PREV_FREE flag and pointer */

    if ((char *)ptr + size < (char *)subheap + subheap->size)
    {
        DWORD *pNext = (DWORD *)((char *)ptr + size);
        *pNext |= ARENA_FLAG_PREV_FREE;
        *(ARENA_FREE **)(pNext - 1) = pFree;
    }

    /* Last, insert the new block into the free list */

    pFree->size = size - sizeof(*pFree);
    HEAP_InsertFreeBlock( subheap->heap, pFree );
}


/***********************************************************************
 *           HEAP_MakeInUseBlockFree
 *
 * Turn an in-use block into a free block. Can also decommit the end of
 * the heap, and possibly even free the sub-heap altogether.
 */
static void HEAP_MakeInUseBlockFree( SUBHEAP *subheap, ARENA_INUSE *pArena )
{
    ARENA_FREE *pFree;
    DWORD size = (pArena->size & ARENA_SIZE_MASK) + sizeof(*pArena);

    /* Check if we can merge with previous block */

    if (pArena->size & ARENA_FLAG_PREV_FREE)
    {
        pFree = *((ARENA_FREE **)pArena - 1);
        size += (pFree->size & ARENA_SIZE_MASK) + sizeof(ARENA_FREE);
        /* Remove it from the free list */
        pFree->next->prev = pFree->prev;
        pFree->prev->next = pFree->next;
    }
    else pFree = (ARENA_FREE *)pArena;

    /* Create a free block */

    HEAP_CreateFreeBlock( subheap, pFree, size );
    size = (pFree->size & ARENA_SIZE_MASK) + sizeof(ARENA_FREE);
    if ((char *)pFree + size < (char *)subheap + subheap->size)
        return;  /* Not the last block, so nothing more to do */

    /* Free the whole sub-heap if it's empty and not the original one */

    if (((char *)pFree == (char *)subheap + subheap->headerSize) &&
        (subheap != &subheap->heap->subheap))
    {
        SUBHEAP *pPrev = &subheap->heap->subheap;
        /* Remove the free block from the list */
        pFree->next->prev = pFree->prev;
        pFree->prev->next = pFree->next;
        /* Remove the subheap from the list */
        while (pPrev && (pPrev->next != subheap)) pPrev = pPrev->next;
        if (pPrev) pPrev->next = subheap->next;
        /* Free the memory */
        subheap->magic = 0;
        if (subheap->selector) FreeSelector16( subheap->selector );
        VirtualFree( subheap, 0, MEM_RELEASE );
        return;
    }
    
    /* Decommit the end of the heap */

    if (!(subheap->heap->flags & HEAP_SHARED)) HEAP_Decommit( subheap, pFree + 1 );
}


/***********************************************************************
 *           HEAP_ShrinkBlock
 *
 * Shrink an in-use block.
 */
static void HEAP_ShrinkBlock(SUBHEAP *subheap, ARENA_INUSE *pArena, DWORD size)
{
    if ((pArena->size & ARENA_SIZE_MASK) >= size + HEAP_MIN_BLOCK_SIZE)
    {
        HEAP_CreateFreeBlock( subheap, (char *)(pArena + 1) + size,
                              (pArena->size & ARENA_SIZE_MASK) - size );
	/* assign size plus previous arena flags */
        pArena->size = size | (pArena->size & ~ARENA_SIZE_MASK);
    }
    else
    {
        /* Turn off PREV_FREE flag in next block */
        char *pNext = (char *)(pArena + 1) + (pArena->size & ARENA_SIZE_MASK);
        if (pNext < (char *)subheap + subheap->size)
            *(DWORD *)pNext &= ~ARENA_FLAG_PREV_FREE;
    }
}

/***********************************************************************
 *           HEAP_InitSubHeap
 */
static BOOL HEAP_InitSubHeap( HEAP *heap, LPVOID address, DWORD flags,
                                DWORD commitSize, DWORD totalSize )
{
    SUBHEAP *subheap = (SUBHEAP *)address;
    WORD selector = 0;
    FREE_LIST_ENTRY *pEntry;
    int i;

    /* Commit memory */

    if (flags & HEAP_SHARED)
        commitSize = totalSize;  /* always commit everything in a shared heap */
    if (!VirtualAlloc(address, commitSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE))
    {
        WARN("Could not commit %08lx bytes for sub-heap %08lx\n",
                   commitSize, (DWORD)address );
        return FALSE;
    }

    /* Allocate a selector if needed */

    if (flags & HEAP_WINE_SEGPTR)
    {
        selector = SELECTOR_AllocBlock( address, totalSize,
                           (flags & (HEAP_WINE_CODESEG|HEAP_WINE_CODE16SEG))
                            ? SEGMENT_CODE : SEGMENT_DATA,
                           (flags & HEAP_WINE_CODESEG) != 0, FALSE );
        if (!selector)
        {
            ERR("Could not allocate selector\n" );
            return FALSE;
        }
    }

    /* Fill the sub-heap structure */

    subheap->heap       = heap;
    subheap->selector   = selector;
    subheap->size       = totalSize;
    subheap->commitSize = commitSize;
    subheap->magic      = SUBHEAP_MAGIC;

    if ( subheap != (SUBHEAP *)heap )
    {
        /* If this is a secondary subheap, insert it into list */

        subheap->headerSize = sizeof(SUBHEAP);
        subheap->next       = heap->subheap.next;
        heap->subheap.next  = subheap;
    }
    else
    {
        /* If this is a primary subheap, initialize main heap */

        subheap->headerSize = sizeof(HEAP);
        subheap->next       = NULL;
        heap->next          = NULL;
        heap->flags         = flags;
        heap->magic         = HEAP_MAGIC;

        /* Build the free lists */

        for (i = 0, pEntry = heap->freeList; i < HEAP_NB_FREE_LISTS; i++, pEntry++)
        {
            pEntry->size           = HEAP_freeListSizes[i];
            pEntry->arena.size     = 0 | ARENA_FLAG_FREE;
            pEntry->arena.next     = i < HEAP_NB_FREE_LISTS-1 ?
                         &heap->freeList[i+1].arena : &heap->freeList[0].arena;
            pEntry->arena.prev     = i ? &heap->freeList[i-1].arena : 
                                   &heap->freeList[HEAP_NB_FREE_LISTS-1].arena;
            pEntry->arena.threadId = 0;
            pEntry->arena.magic    = ARENA_FREE_MAGIC;
        }

        /* Initialize critical section */

        InitializeCriticalSection( &heap->critSection );
	if (!SystemHeap) MakeCriticalSectionGlobal( &heap->critSection );
    }
 
    /* Create the first free block */

    HEAP_CreateFreeBlock( subheap, (LPBYTE)subheap + subheap->headerSize, 
                          subheap->size - subheap->headerSize );

    return TRUE;
}

/***********************************************************************
 *           HEAP_CreateSubHeap
 *
 * Create a sub-heap of the given size.
 * If heap == NULL, creates a main heap.
 */
static SUBHEAP *HEAP_CreateSubHeap( HEAP *heap, DWORD flags, 
                                    DWORD commitSize, DWORD totalSize )
{
    LPVOID address;

    /* Round-up sizes on a 64K boundary */

    if (flags & HEAP_WINE_SEGPTR)
    {
        totalSize = commitSize = 0x10000;  /* Only 64K at a time for SEGPTRs */
    }
    else
    {
        totalSize  = (totalSize + 0xffff) & 0xffff0000;
        commitSize = (commitSize + 0xffff) & 0xffff0000;
        if (!commitSize) commitSize = 0x10000;
        if (totalSize < commitSize) totalSize = commitSize;
    }

    /* Allocate the memory block */

    if (!(address = VirtualAlloc( NULL, totalSize,
                                  MEM_RESERVE, PAGE_EXECUTE_READWRITE )))
    {
        WARN("Could not VirtualAlloc %08lx bytes\n",
                 totalSize );
        return NULL;
    }

    /* Initialize subheap */

    if (!HEAP_InitSubHeap( heap? heap : (HEAP *)address, 
                           address, flags, commitSize, totalSize ))
    {
        VirtualFree( address, 0, MEM_RELEASE );
        return NULL;
    }

    return (SUBHEAP *)address;
}


/***********************************************************************
 *           HEAP_FindFreeBlock
 *
 * Find a free block at least as large as the requested size, and make sure
 * the requested size is committed.
 */
static ARENA_FREE *HEAP_FindFreeBlock( HEAP *heap, DWORD size,
                                       SUBHEAP **ppSubHeap )
{
    SUBHEAP *subheap;
    ARENA_FREE *pArena;
    FREE_LIST_ENTRY *pEntry = heap->freeList;

    /* Find a suitable free list, and in it find a block large enough */

    while (pEntry->size < size) pEntry++;
    pArena = pEntry->arena.next;
    while (pArena != &heap->freeList[0].arena)
    {
        if (pArena->size > size)
        {
            subheap = HEAP_FindSubHeap( heap, pArena );
            if (!HEAP_Commit( subheap, (char *)pArena + sizeof(ARENA_INUSE)
                                               + size + HEAP_MIN_BLOCK_SIZE))
                return NULL;
            *ppSubHeap = subheap;
            return pArena;
        }

        pArena = pArena->next;
    }

    /* If no block was found, attempt to grow the heap */

    if (!(heap->flags & HEAP_GROWABLE))
    {
        WARN("Not enough space in heap %08lx for %08lx bytes\n",
                 (DWORD)heap, size );
        return NULL;
    }
    /* make sure that we have a big enough size *committed* to fit another
     * last free arena in !
     * So just one heap struct, one first free arena which will eventually
     * get inuse, and HEAP_MIN_BLOCK_SIZE for the second free arena that
     * might get assigned all remaining free space in HEAP_ShrinkBlock() */
    size += sizeof(SUBHEAP) + sizeof(ARENA_INUSE) + HEAP_MIN_BLOCK_SIZE;
    if (!(subheap = HEAP_CreateSubHeap( heap, heap->flags, size,
                                        max( HEAP_DEF_SIZE, size ) )))
        return NULL;

    TRACE("created new sub-heap %08lx of %08lx bytes for heap %08lx\n",
                (DWORD)subheap, size, (DWORD)heap );

    *ppSubHeap = subheap;
    return (ARENA_FREE *)(subheap + 1);
}


/***********************************************************************
 *           HEAP_IsValidArenaPtr
 *
 * Check that the pointer is inside the range possible for arenas.
 */
static BOOL HEAP_IsValidArenaPtr( HEAP *heap, void *ptr )
{
    int i;
    SUBHEAP *subheap = HEAP_FindSubHeap( heap, ptr );
    if (!subheap) return FALSE;
    if ((char *)ptr >= (char *)subheap + subheap->headerSize) return TRUE;
    if (subheap != &heap->subheap) return FALSE;
    for (i = 0; i < HEAP_NB_FREE_LISTS; i++)
        if (ptr == (void *)&heap->freeList[i].arena) return TRUE;
    return FALSE;
}


/***********************************************************************
 *           HEAP_ValidateFreeArena
 */
static BOOL HEAP_ValidateFreeArena( SUBHEAP *subheap, ARENA_FREE *pArena )
{
    char *heapEnd = (char *)subheap + subheap->size;

    /* Check magic number */
    if (pArena->magic != ARENA_FREE_MAGIC)
    {
        ERR("Heap %08lx: invalid free arena magic for %08lx\n",
                 (DWORD)subheap->heap, (DWORD)pArena );
        return FALSE;
    }
    /* Check size flags */
    if (!(pArena->size & ARENA_FLAG_FREE) ||
        (pArena->size & ARENA_FLAG_PREV_FREE))
    {
        ERR("Heap %08lx: bad flags %lx for free arena %08lx\n",
                 (DWORD)subheap->heap, pArena->size & ~ARENA_SIZE_MASK, (DWORD)pArena );
    }
    /* Check arena size */
    if ((char *)(pArena + 1) + (pArena->size & ARENA_SIZE_MASK) > heapEnd)
    {
        ERR("Heap %08lx: bad size %08lx for free arena %08lx\n",
                 (DWORD)subheap->heap, (DWORD)pArena->size & ARENA_SIZE_MASK, (DWORD)pArena );
        return FALSE;
    }
    /* Check that next pointer is valid */
    if (!HEAP_IsValidArenaPtr( subheap->heap, pArena->next ))
    {
        ERR("Heap %08lx: bad next ptr %08lx for arena %08lx\n",
                 (DWORD)subheap->heap, (DWORD)pArena->next, (DWORD)pArena );
        return FALSE;
    }
    /* Check that next arena is free */
    if (!(pArena->next->size & ARENA_FLAG_FREE) ||
        (pArena->next->magic != ARENA_FREE_MAGIC))
    { 
        ERR("Heap %08lx: next arena %08lx invalid for %08lx\n", 
                 (DWORD)subheap->heap, (DWORD)pArena->next, (DWORD)pArena );
        return FALSE;
    }
    /* Check that prev pointer is valid */
    if (!HEAP_IsValidArenaPtr( subheap->heap, pArena->prev ))
    {
        ERR("Heap %08lx: bad prev ptr %08lx for arena %08lx\n",
                 (DWORD)subheap->heap, (DWORD)pArena->prev, (DWORD)pArena );
        return FALSE;
    }
    /* Check that prev arena is free */
    if (!(pArena->prev->size & ARENA_FLAG_FREE) ||
        (pArena->prev->magic != ARENA_FREE_MAGIC))
    { 
	/* this often means that the prev arena got overwritten
	 * by a memory write before that prev arena */
        ERR("Heap %08lx: prev arena %08lx invalid for %08lx\n", 
                 (DWORD)subheap->heap, (DWORD)pArena->prev, (DWORD)pArena );
        return FALSE;
    }
    /* Check that next block has PREV_FREE flag */
    if ((char *)(pArena + 1) + (pArena->size & ARENA_SIZE_MASK) < heapEnd)
    {
        if (!(*(DWORD *)((char *)(pArena + 1) +
            (pArena->size & ARENA_SIZE_MASK)) & ARENA_FLAG_PREV_FREE))
        {
            ERR("Heap %08lx: free arena %08lx next block has no PREV_FREE flag\n",
                     (DWORD)subheap->heap, (DWORD)pArena );
            return FALSE;
        }
        /* Check next block back pointer */
        if (*((ARENA_FREE **)((char *)(pArena + 1) +
            (pArena->size & ARENA_SIZE_MASK)) - 1) != pArena)
        {
            ERR("Heap %08lx: arena %08lx has wrong back ptr %08lx\n",
                     (DWORD)subheap->heap, (DWORD)pArena,
                     *((DWORD *)((char *)(pArena+1)+ (pArena->size & ARENA_SIZE_MASK)) - 1));
            return FALSE;
        }
    }
    return TRUE;
}


/***********************************************************************
 *           HEAP_ValidateInUseArena
 */
static BOOL HEAP_ValidateInUseArena( SUBHEAP *subheap, ARENA_INUSE *pArena, BOOL quiet )
{
    char *heapEnd = (char *)subheap + subheap->size;

    /* Check magic number */
    if (pArena->magic != ARENA_INUSE_MAGIC)
    {
        if (quiet == NOISY) {
        ERR("Heap %08lx: invalid in-use arena magic for %08lx\n",
                 (DWORD)subheap->heap, (DWORD)pArena );
            if (TRACE_ON(heap))
               HEAP_Dump( subheap->heap );
        }  else if (WARN_ON(heap)) {
            WARN("Heap %08lx: invalid in-use arena magic for %08lx\n",
                 (DWORD)subheap->heap, (DWORD)pArena );
            if (TRACE_ON(heap))
               HEAP_Dump( subheap->heap );
        }
        return FALSE;
    }
    /* Check size flags */
    if (pArena->size & ARENA_FLAG_FREE) 
    {
        ERR("Heap %08lx: bad flags %lx for in-use arena %08lx\n",
                 (DWORD)subheap->heap, pArena->size & ~ARENA_SIZE_MASK, (DWORD)pArena );
    }
    /* Check arena size */
    if ((char *)(pArena + 1) + (pArena->size & ARENA_SIZE_MASK) > heapEnd)
    {
        ERR("Heap %08lx: bad size %08lx for in-use arena %08lx\n",
                 (DWORD)subheap->heap, (DWORD)pArena->size & ARENA_SIZE_MASK, (DWORD)pArena );
        return FALSE;
    }
    /* Check next arena PREV_FREE flag */
    if (((char *)(pArena + 1) + (pArena->size & ARENA_SIZE_MASK) < heapEnd) &&
        (*(DWORD *)((char *)(pArena + 1) + (pArena->size & ARENA_SIZE_MASK)) & ARENA_FLAG_PREV_FREE))
    {
        ERR("Heap %08lx: in-use arena %08lx next block has PREV_FREE flag\n",
                 (DWORD)subheap->heap, (DWORD)pArena );
        return FALSE;
    }
    /* Check prev free arena */
    if (pArena->size & ARENA_FLAG_PREV_FREE)
    {
        ARENA_FREE *pPrev = *((ARENA_FREE **)pArena - 1);
        /* Check prev pointer */
        if (!HEAP_IsValidArenaPtr( subheap->heap, pPrev ))
        {
            ERR("Heap %08lx: bad back ptr %08lx for arena %08lx\n",
                    (DWORD)subheap->heap, (DWORD)pPrev, (DWORD)pArena );
            return FALSE;
        }
        /* Check that prev arena is free */
        if (!(pPrev->size & ARENA_FLAG_FREE) ||
            (pPrev->magic != ARENA_FREE_MAGIC))
        { 
            ERR("Heap %08lx: prev arena %08lx invalid for in-use %08lx\n", 
                     (DWORD)subheap->heap, (DWORD)pPrev, (DWORD)pArena );
            return FALSE;
        }
        /* Check that prev arena is really the previous block */
        if ((char *)(pPrev + 1) + (pPrev->size & ARENA_SIZE_MASK) != (char *)pArena)
        {
            ERR("Heap %08lx: prev arena %08lx is not prev for in-use %08lx\n",
                     (DWORD)subheap->heap, (DWORD)pPrev, (DWORD)pArena );
            return FALSE;
        }
    }
    return TRUE;
}


/***********************************************************************
 *           HEAP_IsInsideHeap
 * Checks whether the pointer points to a block inside a given heap.
 *
 * NOTES
 *	Should this return BOOL32?
 *
 * RETURNS
 *	!0: Success
 *	0: Failure
 */
int HEAP_IsInsideHeap(
    HANDLE heap, /* [in] Heap */
    DWORD flags,   /* [in] Flags */
    LPCVOID ptr    /* [in] Pointer */
) {
    HEAP *heapPtr = HEAP_GetPtr( heap );
    SUBHEAP *subheap;
    int ret;

    /* Validate the parameters */

    if (!heapPtr) return 0;
    flags |= heapPtr->flags;
    if (!(flags & HEAP_NO_SERIALIZE)) EnterCriticalSection( &heapPtr->critSection );
    ret = (((subheap = HEAP_FindSubHeap( heapPtr, ptr )) != NULL) &&
           (((char *)ptr >= (char *)subheap + subheap->headerSize
                              + sizeof(ARENA_INUSE))));
    if (!(flags & HEAP_NO_SERIALIZE)) LeaveCriticalSection( &heapPtr->critSection );
    return ret;
}


/***********************************************************************
 *           HEAP_GetSegptr
 *
 * Transform a linear pointer into a SEGPTR. The pointer must have been
 * allocated from a HEAP_WINE_SEGPTR heap.
 */
SEGPTR HEAP_GetSegptr( HANDLE heap, DWORD flags, LPCVOID ptr )
{
    HEAP *heapPtr = HEAP_GetPtr( heap );
    SUBHEAP *subheap;
    SEGPTR ret;

    /* Validate the parameters */

    if (!heapPtr) return 0;
    flags |= heapPtr->flags;
    if (!(flags & HEAP_WINE_SEGPTR))
    {
        ERR("Heap %08x is not a SEGPTR heap\n",
                 heap );
        return 0;
    }
    if (!(flags & HEAP_NO_SERIALIZE)) EnterCriticalSection( &heapPtr->critSection );

    /* Get the subheap */

    if (!(subheap = HEAP_FindSubHeap( heapPtr, ptr )))
    {
        ERR("%p is not inside heap %08x\n",
                 ptr, heap );
        if (!(flags & HEAP_NO_SERIALIZE)) LeaveCriticalSection( &heapPtr->critSection );
        return 0;
    }

    /* Build the SEGPTR */

    ret = PTR_SEG_OFF_TO_SEGPTR(subheap->selector, (DWORD)ptr-(DWORD)subheap);
    if (!(flags & HEAP_NO_SERIALIZE)) LeaveCriticalSection( &heapPtr->critSection );
    return ret;
}

/***********************************************************************
 *           HEAP_IsRealArena  [Internal]
 * Validates a block is a valid arena.
 *
 * RETURNS
 *	TRUE: Success
 *	FALSE: Failure
 */
static BOOL HEAP_IsRealArena(
              HANDLE heap,   /* [in] Handle to the heap */
              DWORD flags,   /* [in] Bit flags that control access during operation */
              LPCVOID block, /* [in] Optional pointer to memory block to validate */
              BOOL quiet     /* [in] Flag - if true, HEAP_ValidateInUseArena
                              *             does not complain    */
) {
    SUBHEAP *subheap;
    HEAP *heapPtr = (HEAP *)(heap);
    BOOL ret = TRUE;

    if (!heapPtr || (heapPtr->magic != HEAP_MAGIC))
    {
        ERR("Invalid heap %08x!\n", heap );
        return FALSE;
    }

    flags &= HEAP_NO_SERIALIZE;
    flags |= heapPtr->flags;
    /* calling HeapLock may result in infinite recursion, so do the critsect directly */
    if (!(flags & HEAP_NO_SERIALIZE))
        EnterCriticalSection( &heapPtr->critSection );

    if (block)
    {
        /* Only check this single memory block */

        /* The following code is really HEAP_IsInsideHeap   *
         * with serialization already done.                 */
        if (!(subheap = HEAP_FindSubHeap( heapPtr, block )) ||
            ((char *)block < (char *)subheap + subheap->headerSize
                              + sizeof(ARENA_INUSE)))
        {
            if (quiet == NOISY) 
                ERR("Heap %08lx: block %08lx is not inside heap\n",
                     (DWORD)heap, (DWORD)block );
            else if (WARN_ON(heap)) 
                WARN("Heap %08lx: block %08lx is not inside heap\n",
                     (DWORD)heap, (DWORD)block );
            ret = FALSE;
        } else
            ret = HEAP_ValidateInUseArena( subheap, (ARENA_INUSE *)block - 1, quiet );

        if (!(flags & HEAP_NO_SERIALIZE))
            LeaveCriticalSection( &heapPtr->critSection );
        return ret;
    }

    subheap = &heapPtr->subheap;
    while (subheap && ret)
    {
        char *ptr = (char *)subheap + subheap->headerSize;
        while (ptr < (char *)subheap + subheap->size)
        {
            if (*(DWORD *)ptr & ARENA_FLAG_FREE)
            {
                if (!HEAP_ValidateFreeArena( subheap, (ARENA_FREE *)ptr )) {
                    ret = FALSE;
                    break;
                }
                ptr += sizeof(ARENA_FREE) + (*(DWORD *)ptr & ARENA_SIZE_MASK);
            }
            else
            {
                if (!HEAP_ValidateInUseArena( subheap, (ARENA_INUSE *)ptr, NOISY )) {
                    ret = FALSE;
                    break;
                }
                ptr += sizeof(ARENA_INUSE) + (*(DWORD *)ptr & ARENA_SIZE_MASK);
            }
        }
        subheap = subheap->next;
    }

    if (!(flags & HEAP_NO_SERIALIZE))
	LeaveCriticalSection( &heapPtr->critSection );
    return ret;
}


/***********************************************************************
 *           HeapCreate   (KERNEL32.336)
 * RETURNS
 *	Handle of heap: Success
 *	NULL: Failure
 */
HANDLE WINAPI HeapCreate(
                DWORD flags,       /* [in] Heap allocation flag */
                DWORD initialSize, /* [in] Initial heap size */
                DWORD maxSize      /* [in] Maximum heap size */
) {
    SUBHEAP *subheap;

    if ( flags & HEAP_SHARED ) {
        WARN( "Shared Heap requested, returning system heap.\n" );
        return SystemHeap;
    }

    /* Allocate the heap block */

    if (!maxSize)
    {
        maxSize = HEAP_DEF_SIZE;
        flags |= HEAP_GROWABLE;
    }
    if (!(subheap = HEAP_CreateSubHeap( NULL, flags, initialSize, maxSize )))
    {
        SetLastError( ERROR_OUTOFMEMORY );
        return 0;
    }

    /* link it into the per-process heap list */
    if (processHeap)
    {
        HEAP *heapPtr = subheap->heap;
        EnterCriticalSection( &processHeap->critSection );
        heapPtr->next = firstHeap;
        firstHeap = heapPtr;
        LeaveCriticalSection( &processHeap->critSection );
    }
    else  /* assume the first heap we create is the process main heap */
    {
        processHeap = subheap->heap;
    }

    return (HANDLE)subheap;
}

/***********************************************************************
 *           HeapDestroy   (KERNEL32.337)
 * RETURNS
 *	TRUE: Success
 *	FALSE: Failure
 */
BOOL WINAPI HeapDestroy( HANDLE heap /* [in] Handle of heap */ )
{
    HEAP *heapPtr = HEAP_GetPtr( heap );
    SUBHEAP *subheap;

    if ( heap == SystemHeap ) { 
        WARN( "attempt to destroy system heap, returning TRUE!\n" );
        return TRUE;
    }
     
    TRACE("%08x\n", heap );
    if (!heapPtr) return FALSE;

    if (heapPtr == processHeap)  /* cannot delete the main process heap */
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    else /* remove it from the per-process list */
    {
        HEAP **pptr;
        EnterCriticalSection( &processHeap->critSection );
        pptr = &firstHeap;
        while (*pptr && *pptr != heapPtr) pptr = &(*pptr)->next;
        if (*pptr) *pptr = (*pptr)->next;
        LeaveCriticalSection( &processHeap->critSection );
    }

    DeleteCriticalSection( &heapPtr->critSection );
    subheap = &heapPtr->subheap;
    while (subheap)
    {
        SUBHEAP *next = subheap->next;
        if (subheap->selector) FreeSelector16( subheap->selector );
        VirtualFree( subheap, 0, MEM_RELEASE );
        subheap = next;
    }
    return TRUE;
}


/***********************************************************************
 *           HeapAlloc   (KERNEL32.334)
 * RETURNS
 *	Pointer to allocated memory block
 *	NULL: Failure
 */
LPVOID WINAPI HeapAlloc(
              HANDLE heap, /* [in] Handle of private heap block */
              DWORD flags,   /* [in] Heap allocation control flags */
              DWORD size     /* [in] Number of bytes to allocate */
) {
    ARENA_FREE *pArena;
    ARENA_INUSE *pInUse;
    SUBHEAP *subheap;
    HEAP *heapPtr = HEAP_GetPtr( heap );

    /* Validate the parameters */

    if (!heapPtr) return NULL;
    flags &= HEAP_GENERATE_EXCEPTIONS | HEAP_NO_SERIALIZE | HEAP_ZERO_MEMORY;
    flags |= heapPtr->flags;
    size = (size + 3) & ~3;
    if (size < HEAP_MIN_BLOCK_SIZE) size = HEAP_MIN_BLOCK_SIZE;

    if (!(flags & HEAP_NO_SERIALIZE)) EnterCriticalSection( &heapPtr->critSection );
    /* Locate a suitable free block */

    if (!(pArena = HEAP_FindFreeBlock( heapPtr, size, &subheap )))
    {
        TRACE("(%08x,%08lx,%08lx): returning NULL\n",
                  heap, flags, size  );
        if (!(flags & HEAP_NO_SERIALIZE)) LeaveCriticalSection( &heapPtr->critSection );
        SetLastError( ERROR_COMMITMENT_LIMIT );
        return NULL;
    }

    /* Remove the arena from the free list */

    pArena->next->prev = pArena->prev;
    pArena->prev->next = pArena->next;

    /* Build the in-use arena */

    pInUse = (ARENA_INUSE *)pArena;

    /* in-use arena is smaller than free arena,
     * so we have to add the difference to the size */
    pInUse->size      = (pInUse->size & ~ARENA_FLAG_FREE)
                        + sizeof(ARENA_FREE) - sizeof(ARENA_INUSE);
    pInUse->callerEIP = GET_EIP();
    pInUse->threadId  = GetCurrentTask();
    pInUse->magic     = ARENA_INUSE_MAGIC;

    /* Shrink the block */

    HEAP_ShrinkBlock( subheap, pInUse, size );

    if (flags & HEAP_ZERO_MEMORY)
        memset( pInUse + 1, 0, pInUse->size & ARENA_SIZE_MASK );
    else if (TRACE_ON(heap))
        memset( pInUse + 1, ARENA_INUSE_FILLER, pInUse->size & ARENA_SIZE_MASK );

    if (!(flags & HEAP_NO_SERIALIZE)) LeaveCriticalSection( &heapPtr->critSection );

    TRACE("(%08x,%08lx,%08lx): returning %08lx\n",
                  heap, flags, size, (DWORD)(pInUse + 1) );
    return (LPVOID)(pInUse + 1);
}


/***********************************************************************
 *           HeapFree   (KERNEL32.338)
 * RETURNS
 *	TRUE: Success
 *	FALSE: Failure
 */
BOOL WINAPI HeapFree(
              HANDLE heap, /* [in] Handle of heap */
              DWORD flags,   /* [in] Heap freeing flags */
              LPVOID ptr     /* [in] Address of memory to free */
) {
    ARENA_INUSE *pInUse;
    SUBHEAP *subheap;
    HEAP *heapPtr = HEAP_GetPtr( heap );

    /* Validate the parameters */

    if (!heapPtr) return FALSE;
    if (!ptr)  /* Freeing a NULL ptr is doesn't indicate an error in Win2k */
    {
	WARN("(%08x,%08lx,%08lx): asked to free NULL\n",
                   heap, flags, (DWORD)ptr );
	return TRUE;
    }

    flags &= HEAP_NO_SERIALIZE;
    flags |= heapPtr->flags;
    if (!(flags & HEAP_NO_SERIALIZE)) EnterCriticalSection( &heapPtr->critSection );
    if (!HEAP_IsRealArena( heap, HEAP_NO_SERIALIZE, ptr, QUIET ))
    {
        if (!(flags & HEAP_NO_SERIALIZE)) LeaveCriticalSection( &heapPtr->critSection );
        SetLastError( ERROR_INVALID_PARAMETER );
        TRACE("(%08x,%08lx,%08lx): returning FALSE\n",
                      heap, flags, (DWORD)ptr );
        return FALSE;
    }

    /* Turn the block into a free block */

    pInUse  = (ARENA_INUSE *)ptr - 1;
    subheap = HEAP_FindSubHeap( heapPtr, pInUse );
    HEAP_MakeInUseBlockFree( subheap, pInUse );

    if (!(flags & HEAP_NO_SERIALIZE)) LeaveCriticalSection( &heapPtr->critSection );

    TRACE("(%08x,%08lx,%08lx): returning TRUE\n",
                  heap, flags, (DWORD)ptr );
    return TRUE;
}


/***********************************************************************
 *           HeapReAlloc   (KERNEL32.340)
 * RETURNS
 *	Pointer to reallocated memory block
 *	NULL: Failure
 */
LPVOID WINAPI HeapReAlloc(
              HANDLE heap, /* [in] Handle of heap block */
              DWORD flags,   /* [in] Heap reallocation flags */
              LPVOID ptr,    /* [in] Address of memory to reallocate */
              DWORD size     /* [in] Number of bytes to reallocate */
) {
    ARENA_INUSE *pArena;
    DWORD oldSize;
    HEAP *heapPtr;
    SUBHEAP *subheap;

    if (!ptr) return HeapAlloc( heap, flags, size );  /* FIXME: correct? */
    if (!(heapPtr = HEAP_GetPtr( heap ))) return FALSE;

    /* Validate the parameters */

    flags &= HEAP_GENERATE_EXCEPTIONS | HEAP_NO_SERIALIZE | HEAP_ZERO_MEMORY |
             HEAP_REALLOC_IN_PLACE_ONLY;
    flags |= heapPtr->flags;
    size = (size + 3) & ~3;
    if (size < HEAP_MIN_BLOCK_SIZE) size = HEAP_MIN_BLOCK_SIZE;

    if (!(flags & HEAP_NO_SERIALIZE)) EnterCriticalSection( &heapPtr->critSection );
    if (!HEAP_IsRealArena( heap, HEAP_NO_SERIALIZE, ptr, QUIET ))
    {
        if (!(flags & HEAP_NO_SERIALIZE)) LeaveCriticalSection( &heapPtr->critSection );
        SetLastError( ERROR_INVALID_PARAMETER );
        TRACE("(%08x,%08lx,%08lx,%08lx): returning NULL\n",
                      heap, flags, (DWORD)ptr, size );
        return NULL;
    }

    /* Check if we need to grow the block */

    pArena = (ARENA_INUSE *)ptr - 1;
    pArena->threadId = GetCurrentTask();
    subheap = HEAP_FindSubHeap( heapPtr, pArena );
    oldSize = (pArena->size & ARENA_SIZE_MASK);
    if (size > oldSize)
    {
        char *pNext = (char *)(pArena + 1) + oldSize;
        if ((pNext < (char *)subheap + subheap->size) &&
            (*(DWORD *)pNext & ARENA_FLAG_FREE) &&
            (oldSize + (*(DWORD *)pNext & ARENA_SIZE_MASK) + sizeof(ARENA_FREE) >= size))
        {
            /* The next block is free and large enough */
            ARENA_FREE *pFree = (ARENA_FREE *)pNext;
            pFree->next->prev = pFree->prev;
            pFree->prev->next = pFree->next;
            pArena->size += (pFree->size & ARENA_SIZE_MASK) + sizeof(*pFree);
            if (!HEAP_Commit( subheap, (char *)pArena + sizeof(ARENA_INUSE)
                                               + size + HEAP_MIN_BLOCK_SIZE))
            {
                if (!(flags & HEAP_NO_SERIALIZE)) LeaveCriticalSection( &heapPtr->critSection );
                SetLastError( ERROR_OUTOFMEMORY );
                return NULL;
            }
            HEAP_ShrinkBlock( subheap, pArena, size );
        }
        else  /* Do it the hard way */
        {
            ARENA_FREE *pNew;
            ARENA_INUSE *pInUse;
            SUBHEAP *newsubheap;

            if ((flags & HEAP_REALLOC_IN_PLACE_ONLY) ||
                !(pNew = HEAP_FindFreeBlock( heapPtr, size, &newsubheap )))
            {
                if (!(flags & HEAP_NO_SERIALIZE)) LeaveCriticalSection( &heapPtr->critSection );
                SetLastError( ERROR_OUTOFMEMORY );
                return NULL;
            }

            /* Build the in-use arena */

            pNew->next->prev = pNew->prev;
            pNew->prev->next = pNew->next;
            pInUse = (ARENA_INUSE *)pNew;
            pInUse->size     = (pInUse->size & ~ARENA_FLAG_FREE)
                               + sizeof(ARENA_FREE) - sizeof(ARENA_INUSE);
            pInUse->threadId = GetCurrentTask();
            pInUse->magic    = ARENA_INUSE_MAGIC;
            HEAP_ShrinkBlock( newsubheap, pInUse, size );
            memcpy( pInUse + 1, pArena + 1, oldSize );

            /* Free the previous block */

            HEAP_MakeInUseBlockFree( subheap, pArena );
            subheap = newsubheap;
            pArena  = pInUse;
        }
    }
    else HEAP_ShrinkBlock( subheap, pArena, size );  /* Shrink the block */

    /* Clear the extra bytes if needed */

    if (size > oldSize)
    {
        if (flags & HEAP_ZERO_MEMORY)
            memset( (char *)(pArena + 1) + oldSize, 0,
                    (pArena->size & ARENA_SIZE_MASK) - oldSize );
        else if (TRACE_ON(heap))
            memset( (char *)(pArena + 1) + oldSize, ARENA_INUSE_FILLER,
                    (pArena->size & ARENA_SIZE_MASK) - oldSize );
    }

    /* Return the new arena */

    pArena->callerEIP = GET_EIP();
    if (!(flags & HEAP_NO_SERIALIZE)) LeaveCriticalSection( &heapPtr->critSection );

    TRACE("(%08x,%08lx,%08lx,%08lx): returning %08lx\n",
                  heap, flags, (DWORD)ptr, size, (DWORD)(pArena + 1) );
    return (LPVOID)(pArena + 1);
}


/***********************************************************************
 *           HeapCompact   (KERNEL32.335)
 */
DWORD WINAPI HeapCompact( HANDLE heap, DWORD flags )
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}


/***********************************************************************
 *           HeapLock   (KERNEL32.339)
 * Attempts to acquire the critical section object for a specified heap.
 *
 * RETURNS
 *	TRUE: Success
 *	FALSE: Failure
 */
BOOL WINAPI HeapLock(
              HANDLE heap /* [in] Handle of heap to lock for exclusive access */
) {
    HEAP *heapPtr = HEAP_GetPtr( heap );
    if (!heapPtr) return FALSE;
    EnterCriticalSection( &heapPtr->critSection );
    return TRUE;
}


/***********************************************************************
 *           HeapUnlock   (KERNEL32.342)
 * Releases ownership of the critical section object.
 *
 * RETURNS
 *	TRUE: Success
 *	FALSE: Failure
 */
BOOL WINAPI HeapUnlock(
              HANDLE heap /* [in] Handle to the heap to unlock */
) {
    HEAP *heapPtr = HEAP_GetPtr( heap );
    if (!heapPtr) return FALSE;
    LeaveCriticalSection( &heapPtr->critSection );
    return TRUE;
}


/***********************************************************************
 *           HeapSize   (KERNEL32.341)
 * RETURNS
 *	Size in bytes of allocated memory
 *	0xffffffff: Failure
 */
DWORD WINAPI HeapSize(
             HANDLE heap, /* [in] Handle of heap */
             DWORD flags,   /* [in] Heap size control flags */
             LPVOID ptr     /* [in] Address of memory to return size for */
) {
    DWORD ret;
    HEAP *heapPtr = HEAP_GetPtr( heap );

    if (!heapPtr) return FALSE;
    flags &= HEAP_NO_SERIALIZE;
    flags |= heapPtr->flags;
    if (!(flags & HEAP_NO_SERIALIZE)) EnterCriticalSection( &heapPtr->critSection );
    if (!HEAP_IsRealArena( heap, HEAP_NO_SERIALIZE, ptr, QUIET ))
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        ret = 0xffffffff;
    }
    else
    {
        ARENA_INUSE *pArena = (ARENA_INUSE *)ptr - 1;
        ret = pArena->size & ARENA_SIZE_MASK;
    }
    if (!(flags & HEAP_NO_SERIALIZE)) LeaveCriticalSection( &heapPtr->critSection );

    TRACE("(%08x,%08lx,%08lx): returning %08lx\n",
                  heap, flags, (DWORD)ptr, ret );
    return ret;
}


/***********************************************************************
 *           HeapValidate   (KERNEL32.343)
 * Validates a specified heap.
 *
 * NOTES
 *	Flags is ignored.
 *
 * RETURNS
 *	TRUE: Success
 *	FALSE: Failure
 */
BOOL WINAPI HeapValidate(
              HANDLE heap, /* [in] Handle to the heap */
              DWORD flags,   /* [in] Bit flags that control access during operation */
              LPCVOID block  /* [in] Optional pointer to memory block to validate */
) {

    return HEAP_IsRealArena( heap, flags, block, QUIET );
}


/***********************************************************************
 *           HeapWalk   (KERNEL32.344)
 * Enumerates the memory blocks in a specified heap.
 * See HEAP_Dump() for info on heap structure.
 *
 * TODO
 *   - handling of PROCESS_HEAP_ENTRY_MOVEABLE and
 *     PROCESS_HEAP_ENTRY_DDESHARE (needs heap.c support)
 *
 * RETURNS
 *	TRUE: Success
 *	FALSE: Failure
 */
BOOL WINAPI HeapWalk(
              HANDLE heap,               /* [in]  Handle to heap to enumerate */
              LPPROCESS_HEAP_ENTRY entry /* [out] Pointer to structure of enumeration info */
) {
    HEAP *heapPtr = HEAP_GetPtr(heap);
    SUBHEAP *sub, *currentheap = NULL;
    BOOL ret = FALSE;
    char *ptr;
    int region_index = 0;

    if (!heapPtr || !entry)
    {
	SetLastError(ERROR_INVALID_PARAMETER);
	return FALSE;
    }

    if (!(heapPtr->flags & HEAP_NO_SERIALIZE)) EnterCriticalSection( &heapPtr->critSection );

    /* set ptr to the next arena to be examined */

    if (!entry->lpData) /* first call (init) ? */
    {
	TRACE("begin walking of heap 0x%08x.\n", heap);
	/*HEAP_Dump(heapPtr);*/
	currentheap = &heapPtr->subheap;
	ptr = (char*)currentheap + currentheap->headerSize;
    }
    else
    {
	ptr = entry->lpData;
	sub = &heapPtr->subheap;
	while (sub)
	{
	    if (((char *)ptr >= (char *)sub) &&
		((char *)ptr < (char *)sub + sub->size))
	    {
		currentheap = sub;
		break;
	    }
	    sub = sub->next;
	    region_index++;
	}
	if (currentheap == NULL)
	{
	    ERR("no matching subheap found, shouldn't happen !\n");
	    SetLastError(ERROR_NO_MORE_ITEMS);
	    goto HW_end;
	}

	ptr += entry->cbData; /* point to next arena */
	if (ptr > (char *)currentheap + currentheap->size - 1)
	{   /* proceed with next subheap */
	    if (!(currentheap = currentheap->next))
	    {  /* successfully finished */
		TRACE("end reached.\n");
		SetLastError(ERROR_NO_MORE_ITEMS);
		goto HW_end;
	    }
	    ptr = (char*)currentheap + currentheap->headerSize;
	}
    }

    entry->wFlags = 0;
    if (*(DWORD *)ptr & ARENA_FLAG_FREE)
    {
	ARENA_FREE *pArena = (ARENA_FREE *)ptr;

	/*TRACE("free, magic: %04x\n", pArena->magic);*/

	entry->lpData = pArena + 1;
	entry->cbData = pArena->size & ARENA_SIZE_MASK;
	entry->cbOverhead = sizeof(ARENA_FREE);
	entry->wFlags = PROCESS_HEAP_UNCOMMITTED_RANGE;
    }
    else
    {
	ARENA_INUSE *pArena = (ARENA_INUSE *)ptr;

	/*TRACE("busy, magic: %04x\n", pArena->magic);*/
	
	entry->lpData = pArena + 1;
	entry->cbData = pArena->size & ARENA_SIZE_MASK;
	entry->cbOverhead = sizeof(ARENA_INUSE);
	entry->wFlags = PROCESS_HEAP_ENTRY_BUSY;
	/* FIXME: can't handle PROCESS_HEAP_ENTRY_MOVEABLE
	and PROCESS_HEAP_ENTRY_DDESHARE yet */
    }

    entry->iRegionIndex = region_index;

    /* first element of heap ? */
    if (ptr == (char *)(currentheap + currentheap->headerSize))
    {
	entry->wFlags |= PROCESS_HEAP_REGION;
	entry->u.Region.dwCommittedSize = currentheap->commitSize;
	entry->u.Region.dwUnCommittedSize =
		currentheap->size - currentheap->commitSize;
	entry->u.Region.lpFirstBlock = /* first valid block */
		currentheap + currentheap->headerSize;
	entry->u.Region.lpLastBlock  = /* first invalid block */
		currentheap + currentheap->size;
    }
    ret = TRUE;

HW_end:
    if (!(heapPtr->flags & HEAP_NO_SERIALIZE)) LeaveCriticalSection( &heapPtr->critSection );

    return ret;
}


/***********************************************************************
 *           HEAP_CreateSystemHeap
 *
 * Create the system heap.
 */
BOOL HEAP_CreateSystemHeap(void)
{
    SYSTEM_HEAP_DESCR *descr;
    HANDLE heap;
    HEAP *heapPtr;
    int created;

    HANDLE map = CreateFileMappingA( INVALID_HANDLE_VALUE, NULL, SEC_COMMIT | PAGE_READWRITE,
                                     0, HEAP_DEF_SIZE, "__SystemHeap" );
    if (!map) return FALSE;
    created = (GetLastError() != ERROR_ALREADY_EXISTS);

    if (!(heapPtr = MapViewOfFileEx( map, FILE_MAP_ALL_ACCESS, 0, 0, 0, SYSTEM_HEAP_BASE )))
    {
        /* pre-defined address not available, use any one */
        fprintf( stderr, "Warning: system heap base address %p not available\n",
                 SYSTEM_HEAP_BASE );
        if (!(heapPtr = MapViewOfFile( map, FILE_MAP_ALL_ACCESS, 0, 0, 0 )))
        {
            CloseHandle( map );
            return FALSE;
        }
    }
    heap = (HANDLE)heapPtr;

    if (created)  /* newly created heap */
    {
        HEAP_InitSubHeap( heapPtr, heapPtr, HEAP_SHARED, 0, HEAP_DEF_SIZE );
        HeapLock( heap );
        descr = heapPtr->private = HeapAlloc( heap, HEAP_ZERO_MEMORY, sizeof(*descr) );
        assert( descr );
    }
    else
    {
        /* wait for the heap to be initialized */
        while (!heapPtr->private) Sleep(1);
        HeapLock( heap );
        /* remap it to the right address if necessary */
        if (heapPtr->subheap.heap != heapPtr)
        {
            void *base = heapPtr->subheap.heap;
            HeapUnlock( heap );
            UnmapViewOfFile( heapPtr );
            if (!(heapPtr = MapViewOfFileEx( map, FILE_MAP_ALL_ACCESS, 0, 0, 0, base )))
            {
                fprintf( stderr, "Couldn't map system heap at the correct address (%p)\n", base );
                CloseHandle( map );
                return FALSE;
            }
            heap = (HANDLE)heapPtr;
            HeapLock( heap );
        }
        descr = heapPtr->private;
        assert( descr );
    }
    SystemHeap = heap;
    SystemHeapDescr = descr;
    HeapUnlock( heap );
    CloseHandle( map );
    return TRUE;
}


/***********************************************************************
 *           GetProcessHeap    (KERNEL32.259)
 */
HANDLE WINAPI GetProcessHeap(void)
{
    return (HANDLE)processHeap;
}


/***********************************************************************
 *           GetProcessHeaps    (KERNEL32.376)
 */
DWORD WINAPI GetProcessHeaps( DWORD count, HANDLE *heaps )
{
    DWORD total;
    HEAP *ptr;

    if (!processHeap) return 0;  /* should never happen */
    total = 1;  /* main heap */
    EnterCriticalSection( &processHeap->critSection );
    for (ptr = firstHeap; ptr; ptr = ptr->next) total++;
    if (total <= count)
    {
        *heaps++ = (HANDLE)processHeap;
        for (ptr = firstHeap; ptr; ptr = ptr->next) *heaps++ = (HANDLE)ptr;
    }
    LeaveCriticalSection( &processHeap->critSection );
    return total;
}


/***********************************************************************
 *           HEAP_strdupA
 */
LPSTR HEAP_strdupA( HANDLE heap, DWORD flags, LPCSTR str )
{
    LPSTR p = HeapAlloc( heap, flags, strlen(str) + 1 );
    if(p) {
        SET_EIP(p);
        strcpy( p, str );
    }
    return p;
}


/***********************************************************************
 *           HEAP_strdupW
 */
LPWSTR HEAP_strdupW( HANDLE heap, DWORD flags, LPCWSTR str )
{
    INT len = strlenW(str) + 1;
    LPWSTR p = HeapAlloc( heap, flags, len * sizeof(WCHAR) );
    if(p) {
        SET_EIP(p);
        strcpyW( p, str );
    }
    return p;
}


/***********************************************************************
 *           HEAP_strdupAtoW
 */
LPWSTR HEAP_strdupAtoW( HANDLE heap, DWORD flags, LPCSTR str )
{
    LPWSTR ret;
    INT len;

    if (!str) return NULL;
    len = MultiByteToWideChar( CP_ACP, 0, str, -1, NULL, 0 );
    ret = HeapAlloc( heap, flags, len * sizeof(WCHAR) );
    if (ret) {
        SET_EIP(ret);
        MultiByteToWideChar( CP_ACP, 0, str, -1, ret, len );
    }
    return ret;
}


/***********************************************************************
 *           HEAP_strdupWtoA
 */
LPSTR HEAP_strdupWtoA( HANDLE heap, DWORD flags, LPCWSTR str )
{
    LPSTR ret;
    INT len;

    if (!str) return NULL;
    len = WideCharToMultiByte( CP_ACP, 0, str, -1, NULL, 0, NULL, NULL );
    ret = HeapAlloc( heap, flags, len );
    if(ret) {
        SET_EIP(ret);
        WideCharToMultiByte( CP_ACP, 0, str, -1, ret, len, NULL, NULL );
    }
    return ret;
}



/***********************************************************************
 * 32-bit local heap functions (Win95; undocumented)
 */

#define HTABLE_SIZE      0x10000
#define HTABLE_PAGESIZE  0x1000
#define HTABLE_NPAGES    (HTABLE_SIZE / HTABLE_PAGESIZE)

#include "pshpack1.h"
typedef struct _LOCAL32HEADER
{
    WORD     freeListFirst[HTABLE_NPAGES];
    WORD     freeListSize[HTABLE_NPAGES];
    WORD     freeListLast[HTABLE_NPAGES];

    DWORD    selectorTableOffset;
    WORD     selectorTableSize;
    WORD     selectorDelta;

    DWORD    segment;
    LPBYTE   base;

    DWORD    limit;
    DWORD    flags;

    DWORD    magic;
    HANDLE heap;

} LOCAL32HEADER;
#include "poppack.h"

#define LOCAL32_MAGIC    ((DWORD)('L' | ('H'<<8) | ('3'<<16) | ('2'<<24)))

/***********************************************************************
 *           Local32Init   (KERNEL.208)
 */
HANDLE WINAPI Local32Init16( WORD segment, DWORD tableSize,
                             DWORD heapSize, DWORD flags )
{
    DWORD totSize, segSize = 0;
    LPBYTE base;
    LOCAL32HEADER *header;
    HEAP *heap;
    WORD *selectorTable;
    WORD selectorEven, selectorOdd;
    int i, nrBlocks;

    /* Determine new heap size */

    if ( segment )
    {
        if ( (segSize = GetSelectorLimit16( segment )) == 0 )
            return 0;
        else
            segSize++;
    }

    if ( heapSize == -1L )
        heapSize = 1024L*1024L;   /* FIXME */

    heapSize = (heapSize + 0xffff) & 0xffff0000;
    segSize  = (segSize  + 0x0fff) & 0xfffff000;
    totSize  = segSize + HTABLE_SIZE + heapSize;


    /* Allocate memory and initialize heap */

    if ( !(base = VirtualAlloc( NULL, totSize, MEM_RESERVE, PAGE_READWRITE )) )
        return 0;

    if ( !VirtualAlloc( base, segSize + HTABLE_PAGESIZE, 
                        MEM_COMMIT, PAGE_READWRITE ) )
    {
        VirtualFree( base, 0, MEM_RELEASE );
        return 0;
    }

    heap = (HEAP *)(base + segSize + HTABLE_SIZE);
    if ( !HEAP_InitSubHeap( heap, (LPVOID)heap, 0, 0x10000, heapSize ) )
    {
        VirtualFree( base, 0, MEM_RELEASE );
        return 0;
    }


    /* Set up header and handle table */
    
    header = (LOCAL32HEADER *)(base + segSize);
    header->base    = base;
    header->limit   = HTABLE_PAGESIZE-1;
    header->flags   = 0;
    header->magic   = LOCAL32_MAGIC;
    header->heap    = (HANDLE)heap;

    header->freeListFirst[0] = sizeof(LOCAL32HEADER);
    header->freeListLast[0]  = HTABLE_PAGESIZE - 4;
    header->freeListSize[0]  = (HTABLE_PAGESIZE - sizeof(LOCAL32HEADER)) / 4;

    for (i = header->freeListFirst[0]; i < header->freeListLast[0]; i += 4)
        *(DWORD *)((LPBYTE)header + i) = i+4;

    header->freeListFirst[1] = 0xffff;


    /* Set up selector table */
  
    nrBlocks      = (totSize + 0x7fff) >> 15; 
    selectorTable = (LPWORD) HeapAlloc( header->heap,  0, nrBlocks * 2 );
    selectorEven  = SELECTOR_AllocBlock( base, totSize, 
                                         SEGMENT_DATA, FALSE, FALSE );
    selectorOdd   = SELECTOR_AllocBlock( base + 0x8000, totSize - 0x8000, 
                                         SEGMENT_DATA, FALSE, FALSE );
    
    if ( !selectorTable || !selectorEven || !selectorOdd )
    {
        if ( selectorTable ) HeapFree( header->heap, 0, selectorTable );
        if ( selectorEven  ) SELECTOR_FreeBlock( selectorEven, totSize >> 16 );
        if ( selectorOdd   ) SELECTOR_FreeBlock( selectorOdd, (totSize-0x8000) >> 16 );
        HeapDestroy( header->heap );
        VirtualFree( base, 0, MEM_RELEASE );
        return 0;
    }

    header->selectorTableOffset = (LPBYTE)selectorTable - header->base;
    header->selectorTableSize   = nrBlocks * 4;  /* ??? Win95 does it this way! */
    header->selectorDelta       = selectorEven - selectorOdd;
    header->segment             = segment? segment : selectorEven;

    for (i = 0; i < nrBlocks; i++)
        selectorTable[i] = (i & 1)? selectorOdd  + ((i >> 1) << __AHSHIFT)
                                  : selectorEven + ((i >> 1) << __AHSHIFT);

    /* Move old segment */

    if ( segment )
    {
        /* FIXME: This is somewhat ugly and relies on implementation
                  details about 16-bit global memory handles ... */

        LPBYTE oldBase = (LPBYTE)GetSelectorBase( segment );
        memcpy( base, oldBase, segSize );
        GLOBAL_MoveBlock( segment, base, totSize );
        HeapFree( SystemHeap, 0, oldBase );
    }
    
    return (HANDLE)header;
}

/***********************************************************************
 *           Local32_SearchHandle
 */
static LPDWORD Local32_SearchHandle( LOCAL32HEADER *header, DWORD addr )
{
    LPDWORD handle;

    for ( handle = (LPDWORD)((LPBYTE)header + sizeof(LOCAL32HEADER));
          handle < (LPDWORD)((LPBYTE)header + header->limit);
          handle++)
    {
        if (*handle == addr)
            return handle;
    }

    return NULL;
}

/***********************************************************************
 *           Local32_ToHandle
 */
static VOID Local32_ToHandle( LOCAL32HEADER *header, INT16 type, 
                              DWORD addr, LPDWORD *handle, LPBYTE *ptr )
{
    *handle = NULL;
    *ptr    = NULL;

    switch (type)
    {
        case -2:    /* 16:16 pointer, no handles */
            *ptr    = PTR_SEG_TO_LIN( addr );
            *handle = (LPDWORD)*ptr;
            break;

        case -1:    /* 32-bit offset, no handles */
            *ptr    = header->base + addr;
            *handle = (LPDWORD)*ptr;
            break;

        case 0:     /* handle */
            if (    addr >= sizeof(LOCAL32HEADER) 
                 && addr <  header->limit && !(addr & 3) 
                 && *(LPDWORD)((LPBYTE)header + addr) >= HTABLE_SIZE )
            {
                *handle = (LPDWORD)((LPBYTE)header + addr);
                *ptr    = header->base + **handle;
            }
            break;

        case 1:     /* 16:16 pointer */
            *ptr    = PTR_SEG_TO_LIN( addr );
            *handle = Local32_SearchHandle( header, *ptr - header->base );
            break;

        case 2:     /* 32-bit offset */
            *ptr    = header->base + addr;
            *handle = Local32_SearchHandle( header, *ptr - header->base );
            break;
    }
}

/***********************************************************************
 *           Local32_FromHandle
 */
static VOID Local32_FromHandle( LOCAL32HEADER *header, INT16 type, 
                                DWORD *addr, LPDWORD handle, LPBYTE ptr )
{
    switch (type)
    {
        case -2:    /* 16:16 pointer */
        case  1:
        {
            WORD *selTable = (LPWORD)(header->base + header->selectorTableOffset);
            DWORD offset   = (LPBYTE)ptr - header->base;
            *addr = MAKELONG( offset & 0x7fff, selTable[offset >> 15] ); 
        }
        break;

        case -1:    /* 32-bit offset */
        case  2:
            *addr = ptr - header->base;
            break;

        case  0:    /* handle */
            *addr = (LPBYTE)handle - (LPBYTE)header;
            break;
    }
}

/***********************************************************************
 *           Local32Alloc   (KERNEL.209)
 */
DWORD WINAPI Local32Alloc16( HANDLE heap, DWORD size, INT16 type, DWORD flags )
{
    LOCAL32HEADER *header = (LOCAL32HEADER *)heap;
    LPDWORD handle;
    LPBYTE ptr;
    DWORD addr;

    /* Allocate memory */
    ptr = HeapAlloc( header->heap, 
                     (flags & LMEM_MOVEABLE)? HEAP_ZERO_MEMORY : 0, size );
    if (!ptr) return 0;


    /* Allocate handle if requested */
    if (type >= 0)
    {
        int page, i;

        /* Find first page of handle table with free slots */
        for (page = 0; page < HTABLE_NPAGES; page++)
            if (header->freeListFirst[page] != 0)
                break;
        if (page == HTABLE_NPAGES)
        {
            WARN("Out of handles!\n" );
            HeapFree( header->heap, 0, ptr );
            return 0;
        }

        /* If virgin page, initialize it */
        if (header->freeListFirst[page] == 0xffff)
        {
            if ( !VirtualAlloc( (LPBYTE)header + (page << 12), 
                                0x1000, MEM_COMMIT, PAGE_READWRITE ) )
            {
                WARN("Cannot grow handle table!\n" );
                HeapFree( header->heap, 0, ptr );
                return 0;
            }
            
            header->limit += HTABLE_PAGESIZE;

            header->freeListFirst[page] = 0;
            header->freeListLast[page]  = HTABLE_PAGESIZE - 4;
            header->freeListSize[page]  = HTABLE_PAGESIZE / 4;
           
            for (i = 0; i < HTABLE_PAGESIZE; i += 4)
                *(DWORD *)((LPBYTE)header + i) = i+4;

            if (page < HTABLE_NPAGES-1) 
                header->freeListFirst[page+1] = 0xffff;
        }

        /* Allocate handle slot from page */
        handle = (LPDWORD)((LPBYTE)header + header->freeListFirst[page]);
        if (--header->freeListSize[page] == 0)
            header->freeListFirst[page] = header->freeListLast[page] = 0;
        else
            header->freeListFirst[page] = *handle;

        /* Store 32-bit offset in handle slot */
        *handle = ptr - header->base;
    }
    else
    {
        handle = (LPDWORD)ptr;
        header->flags |= 1;
    }


    /* Convert handle to requested output type */
    Local32_FromHandle( header, type, &addr, handle, ptr );
    return addr;
}

/***********************************************************************
 *           Local32ReAlloc   (KERNEL.210)
 */
DWORD WINAPI Local32ReAlloc16( HANDLE heap, DWORD addr, INT16 type,
                             DWORD size, DWORD flags )
{
    LOCAL32HEADER *header = (LOCAL32HEADER *)heap;
    LPDWORD handle;
    LPBYTE ptr;

    if (!addr)
        return Local32Alloc16( heap, size, type, flags );

    /* Retrieve handle and pointer */
    Local32_ToHandle( header, type, addr, &handle, &ptr );
    if (!handle) return FALSE;

    /* Reallocate memory block */
    ptr = HeapReAlloc( header->heap, 
                       (flags & LMEM_MOVEABLE)? HEAP_ZERO_MEMORY : 0, 
                       ptr, size );
    if (!ptr) return 0;

    /* Modify handle */
    if (type >= 0)
        *handle = ptr - header->base;
    else
        handle = (LPDWORD)ptr;

    /* Convert handle to requested output type */
    Local32_FromHandle( header, type, &addr, handle, ptr );
    return addr;
}

/***********************************************************************
 *           Local32Free   (KERNEL.211)
 */
BOOL WINAPI Local32Free16( HANDLE heap, DWORD addr, INT16 type )
{
    LOCAL32HEADER *header = (LOCAL32HEADER *)heap;
    LPDWORD handle;
    LPBYTE ptr;

    /* Retrieve handle and pointer */
    Local32_ToHandle( header, type, addr, &handle, &ptr );
    if (!handle) return FALSE;

    /* Free handle if necessary */
    if (type >= 0)
    {
        int offset = (LPBYTE)handle - (LPBYTE)header;
        int page   = offset >> 12;

        /* Return handle slot to page free list */
        if (header->freeListSize[page]++ == 0)
            header->freeListFirst[page] = header->freeListLast[page]  = offset;
        else
            *(LPDWORD)((LPBYTE)header + header->freeListLast[page]) = offset,
            header->freeListLast[page] = offset;

        *handle = 0;

        /* Shrink handle table when possible */
        while (page > 0 && header->freeListSize[page] == HTABLE_PAGESIZE / 4)
        {
            if ( VirtualFree( (LPBYTE)header + 
                              (header->limit & ~(HTABLE_PAGESIZE-1)),
                              HTABLE_PAGESIZE, MEM_DECOMMIT ) )
                break;

            header->limit -= HTABLE_PAGESIZE;
            header->freeListFirst[page] = 0xffff;
            page--;
        }
    }

    /* Free memory */
    return HeapFree( header->heap, 0, ptr );
}

/***********************************************************************
 *           Local32Translate   (KERNEL.213)
 */
DWORD WINAPI Local32Translate16( HANDLE heap, DWORD addr, INT16 type1, INT16 type2 )
{
    LOCAL32HEADER *header = (LOCAL32HEADER *)heap;
    LPDWORD handle;
    LPBYTE ptr;

    Local32_ToHandle( header, type1, addr, &handle, &ptr );
    if (!handle) return 0;

    Local32_FromHandle( header, type2, &addr, handle, ptr );
    return addr;
}

/***********************************************************************
 *           Local32Size   (KERNEL.214)
 */
DWORD WINAPI Local32Size16( HANDLE heap, DWORD addr, INT16 type )
{
    LOCAL32HEADER *header = (LOCAL32HEADER *)heap;
    LPDWORD handle;
    LPBYTE ptr;

    Local32_ToHandle( header, type, addr, &handle, &ptr );
    if (!handle) return 0;

    return HeapSize( header->heap, 0, ptr );
}

/***********************************************************************
 *           Local32ValidHandle   (KERNEL.215)
 */
BOOL WINAPI Local32ValidHandle16( HANDLE heap, WORD addr )
{
    LOCAL32HEADER *header = (LOCAL32HEADER *)heap;
    LPDWORD handle;
    LPBYTE ptr;

    Local32_ToHandle( header, 0, addr, &handle, &ptr );
    return handle != NULL;
}

/***********************************************************************
 *           Local32GetSegment   (KERNEL.229)
 */
WORD WINAPI Local32GetSegment16( HANDLE heap )
{
    LOCAL32HEADER *header = (LOCAL32HEADER *)heap;
    return header->segment;
}

/***********************************************************************
 *           Local32_GetHeap
 */
static LOCAL32HEADER *Local32_GetHeap( HGLOBAL16 handle )
{
    WORD selector = GlobalHandleToSel16( handle );
    DWORD base  = GetSelectorBase( selector ); 
    DWORD limit = GetSelectorLimit16( selector ); 

    /* Hmmm. This is a somewhat stupid heuristic, but Windows 95 does
       it this way ... */

    if ( limit > 0x10000 && ((LOCAL32HEADER *)base)->magic == LOCAL32_MAGIC )
        return (LOCAL32HEADER *)base;

    base  += 0x10000;
    limit -= 0x10000;

    if ( limit > 0x10000 && ((LOCAL32HEADER *)base)->magic == LOCAL32_MAGIC )
        return (LOCAL32HEADER *)base;

    return NULL;
}

/***********************************************************************
 *           Local32Info   (KERNEL.444)  (TOOLHELP.84)
 */
BOOL16 WINAPI Local32Info16( LOCAL32INFO *pLocal32Info, HGLOBAL16 handle )
{
    SUBHEAP *heapPtr;
    LPBYTE ptr;
    int i;

    LOCAL32HEADER *header = Local32_GetHeap( handle );
    if ( !header ) return FALSE;

    if ( !pLocal32Info || pLocal32Info->dwSize < sizeof(LOCAL32INFO) )
        return FALSE;

    heapPtr = (SUBHEAP *)HEAP_GetPtr( header->heap );
    pLocal32Info->dwMemReserved = heapPtr->size;
    pLocal32Info->dwMemCommitted = heapPtr->commitSize;
    pLocal32Info->dwTotalFree = 0L;
    pLocal32Info->dwLargestFreeBlock = 0L;

    /* Note: Local32 heaps always have only one subheap! */
    ptr = (LPBYTE)heapPtr + heapPtr->headerSize;
    while ( ptr < (LPBYTE)heapPtr + heapPtr->size )
    {
        if (*(DWORD *)ptr & ARENA_FLAG_FREE)
        {
            ARENA_FREE *pArena = (ARENA_FREE *)ptr;
            DWORD size = (pArena->size & ARENA_SIZE_MASK);
            ptr += sizeof(*pArena) + size;

            pLocal32Info->dwTotalFree += size;
            if ( size > pLocal32Info->dwLargestFreeBlock )
                pLocal32Info->dwLargestFreeBlock = size;
        }
        else
        {
            ARENA_INUSE *pArena = (ARENA_INUSE *)ptr;
            DWORD size = (pArena->size & ARENA_SIZE_MASK);
            ptr += sizeof(*pArena) + size;
        }
    }

    pLocal32Info->dwcFreeHandles = 0;
    for ( i = 0; i < HTABLE_NPAGES; i++ )
    {
        if ( header->freeListFirst[i] == 0xffff ) break;
        pLocal32Info->dwcFreeHandles += header->freeListSize[i];
    }
    pLocal32Info->dwcFreeHandles += (HTABLE_NPAGES - i) * HTABLE_PAGESIZE/4;

    return TRUE;
}

/***********************************************************************
 *           Local32First   (KERNEL.445)  (TOOLHELP.85)
 */
BOOL16 WINAPI Local32First16( LOCAL32ENTRY *pLocal32Entry, HGLOBAL16 handle )
{
    FIXME("(%p, %04X): stub!\n", pLocal32Entry, handle );
    return FALSE;
}

/***********************************************************************
 *           Local32Next   (KERNEL.446)  (TOOLHELP.86)
 */
BOOL16 WINAPI Local32Next16( LOCAL32ENTRY *pLocal32Entry )
{
    FIXME("(%p): stub!\n", pLocal32Entry );
    return FALSE;
}

