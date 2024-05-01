/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "team7",
    /* First member's full name */
    "Hwang Yunkyung",
    /* First member's email address */
    "yunnn99@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t))) // allign 4byte

/////////////////////////////////////////////
/*              define macro               */
#define FIT 2 // fit


#define WSIZE 4 // word
#define DSIZE 8 // double word
#define CHUNKSIZE (1<<12)

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define PACK(size, alloc) ((size) | (alloc)) // 헤더값 계산

#define GET(p) (* (unsigned int *)(p)) // 주소 p 읽기
#define PUT(p, val) (* (unsigned int *)(p) = (val)) // 주소 p에 쓰기

#define GET_SIZE(p) (GET(p) & ~0x7) // 주소 p에 할당된 사이즈 반환
#define GET_ALLOC(p) (GET(p) & 0x1) // 주소 p의 할당 여부 반환

#define HDRP(bp) ((char *)(bp) - WSIZE) // 헤더의 주소 계산
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) // 푸터의 주소 계산

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp))) // 다음 블록의 주소 계산
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE)) // 이전 블록의 주소 계산
/////////////////////////////////////////////

static char *heap_listp; // points prologue
char *lastp; // save previous search point for next fit

static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); // allocation of prev block
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); // allocation of next block
    size_t size = GET_SIZE(HDRP(bp));

    // case 1: both allocated
    if (prev_alloc && next_alloc) {
        lastp = bp;
        return bp;
    }
    
    // case 2: next is free
    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp))); // coalesce next block
        PUT(HDRP(bp), PACK(size, 0)); // update header
        PUT(FTRP(bp), PACK(size, 0)); // update footer
    }

    // case 3: prev is free
    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))); // coalesce prev block
        PUT(FTRP(bp), PACK(size, 0)); // update footer
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); // update header
        bp = PREV_BLKP(bp);
    }

    // case 4: both free
    else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    lastp = bp;
    return bp;    
}

static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    // for 8 byte allignment
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    // cant extend until size
    if ((long)(bp = mem_sbrk(size)) == -1) {
        return NULL;
    }
        
    PUT(HDRP(bp), PACK(size, 0)); // header for newly extended heap block
    PUT(FTRP(bp), PACK(size, 0)); // footer for newly extended heap block
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // new epilogue block

    // check allocation of previous block and coalesce 
    return coalesce(bp);
}

/* 
 * mm_init - initialize the malloc package.
 */

int mm_init(void)
{   
    // cannot allocate mandatory 4 blocks(padding, prologue, footer) ...
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0); // padding
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); // prologue header (4byte)
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); // prologue footer (4byte)
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1)); // epilogue header (0byte)
    heap_listp += (2 * WSIZE); // heap_listp points prologue footer
    lastp = heap_listp;

    // extend until chunksize
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */

#if FIT == 1
// first fit
static void *find_fit(size_t asize) {
    void *bp;
    // printf("exec first fit...\n");
    // iterate heap space
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        //printf("now block %d\n", GET(bp));
        if (!GET_ALLOC(HDRP(bp)) && asize <= GET_SIZE(HDRP(bp))) // if free and allocatable
            return bp;
    }

    return NULL;
}

#elif FIT == 2
// next fit
static void *find_fit(size_t asize) {
    void *bp;
    void *old_lastp = lastp;
    
    for (bp = lastp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && asize <= GET_SIZE(HDRP(bp))) { // if free and allocatable
            lastp = NEXT_BLKP(bp);
            return bp;
        }
    }

    // cant find 
    for (bp = heap_listp; bp < old_lastp; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && asize <= GET_SIZE(HDRP(bp))) { // if free and allocatable
            lastp = NEXT_BLKP(bp);
            return bp;
        }
    }
    
    return NULL;
}

#elif FIT == 3
// best fit
static void *find_fit(size_t asize) {
    void *bp;
    void *best = NULL;

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && asize <= GET_SIZE(HDRP(bp))) { // if free and allocatable
            if (best == NULL)
                best = bp;
            if (GET_SIZE(HDRP(bp)) <= GET_SIZE(HDRP(best)))
                best = bp;
        }
    }

    return best;
}
#endif

// divide block (minimum block is 8byte)
static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp)); // size of block now

    if ((csize - asize) >= (DSIZE)) { // divide
        PUT(HDRP(bp), PACK(asize, 1)); // allocate asize
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp); // divide block
        PUT(HDRP(bp), PACK(csize - asize, 0)); // allocate csize - asize
        PUT(FTRP(bp), PACK(csize - asize, 0));
    }
    else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *bp;

    // return if requesting size 0 block
    if (size == 0) 
        return NULL;
    
    // 8 byte allignment
    asize = ALIGN(size + SIZE_T_SIZE);

    // search free space
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        lastp = bp;
        return bp;
    }

    // no free space... extend heap
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) 
        return NULL;
    place(lsbp, asize);
    lastp = bp; // save last search point
    return bp;

    // basic malloc
    // int newsize = ALIGN(size + SIZE_T_SIZE);
    // void *p = mem_sbrk(newsize);
    // if (p == (void *)-1)
	// return NULL;
    // else {
    //     *(size_t *)p = size;
    //     return (void *)((char *)p + SIZE_T_SIZE);
    // }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));
    
    PUT(HDRP(ptr), PACK(size, 0)); // make block available
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */

// mm_malloc이 find fit, extend heap을 하고 있음
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize = GET_SIZE(HDRP(oldptr));

    // 1. realloc from NULL == malloc
    if (ptr == NULL) {
        // printf("Case 1\n");
        return mm_malloc(size);
    }

    // 2. change ptr block size to 0 == free block
    if (size == 0) { 
        // printf("Case 2\n");
        mm_free(ptr);
        return NULL;
    }

    // 3. adjusted size is smaller
    if (size + DSIZE <= copySize) { // 왜 size + DSIZE??
        // printf("Case 3\n");
        return ptr;
    }

    // 4. bigger size
    size_t bsize = copySize + GET_SIZE(HDRP(NEXT_BLKP(oldptr)));
    size_t asize = ALIGN(size + SIZE_T_SIZE);

    // next is available and size is enough
    if (!GET_ALLOC(HDRP(NEXT_BLKP(oldptr))) && (size + DSIZE <= bsize)) {
        // printf("Case 4\n");
        PUT(HDRP(ptr), PACK(bsize, 1)); // 1 is alloc, 0 is free
        PUT(FTRP(ptr), PACK(bsize, 1));
        place(ptr, asize); // divide
        lastp = ptr; // save last search point
        return ptr;
    }

    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;

    if (size < copySize)
      copySize = size;
    memmove(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;

}