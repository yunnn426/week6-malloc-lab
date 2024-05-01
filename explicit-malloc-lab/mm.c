/*
 * mm-explicit.c - 
 * 
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

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
#define ALIGNMENT 16

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t))) // allign 4byte

/* ----------------------------- define macro ----------------------------- */
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

#define PRED(bp) (*(char **)(bp)) // 이전 가용 블록
#define SUCC(bp) (*(char **)(bp + WSIZE)) // 다음 가용 블록
/* ------------------------------------------------------------------------ */

/* -------------------------- function prototype -------------------------- */
static void *coalesce(void*);
static void *extend_heap(size_t);
static void *find_fit(size_t);
static void place(void*, size_t);

static void put_free_block(void*);
static void remove_free_block(void*);
/* ------------------------------------------------------------------------ */

/* ---------------------------- global variants --------------------------- */
static char *heap_listp = 0; // points prologue
static char *free_listp = 0; // 가용 리스트 첫 블록 pred
/* ------------------------------------------------------------------------ */

/* 
 * mm_init - initialize the malloc package.
 */

int mm_init(void)
{   
    // cannot allocate mandatory 6 blocks(padding, prologue, footer) ...
    // + pred, succ (각 4바이트)
    heap_listp = mem_sbrk(6 * WSIZE);
    if (heap_listp == (void *)-1)
        return -1;

    PUT(heap_listp, 0); // padding
    PUT(heap_listp + (1 * WSIZE), PACK(2 * DSIZE, 1)); // prologue header (4byte)
    PUT(heap_listp + (2 * WSIZE), NULL); // pred == NULL
    PUT(heap_listp + (3 * WSIZE), NULL); // succ == NULL
    PUT(heap_listp + (4 * WSIZE), PACK(2 * DSIZE, 1)); // prologue footer (4byte)
    PUT(heap_listp + (5 * WSIZE), PACK(0, 1)); // epilogue header (0byte)

    free_listp = heap_listp + 2 * WSIZE; // pred를 가리킴(payload 시작점)

    // extend until chunksize
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */

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
        return bp;
    }

    // no free space... extend heap
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) 
        return NULL;
    place(bp, asize);

    return bp;
}

static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); // allocation of prev block
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); // allocation of next block
    size_t size = GET_SIZE(HDRP(bp));

    // case 1: both allocated
    // 해당 블록만 가용 리스트에 추가

    // case 2: next is free
    if (prev_alloc && !next_alloc) {
        // printf("case 2\n");
        remove_free_block(NEXT_BLKP(bp)); // next 블록 가용 리스트에서 삭제

        size += GET_SIZE(HDRP(NEXT_BLKP(bp))); // coalesce next block
        PUT(HDRP(bp), PACK(size, 0)); // update header
        PUT(FTRP(bp), PACK(size, 0)); // update footer
    }

    // case 3: prev is free
    else if (!prev_alloc && next_alloc) {
        // printf("case 3\n");

        // printf("이전 블록: %p\n", PREV_BLKP(bp));
        remove_free_block(PREV_BLKP(bp)); // prev 블록 가용 리스트에서 삭제

        size += GET_SIZE(HDRP(PREV_BLKP(bp))); // coalesce prev block
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0)); // update header
        PUT(FTRP(bp), PACK(size, 0)); // update footer
    }

    // case 4: both free
    else if (!prev_alloc && !next_alloc) {
        // printf("case 4\n");

        remove_free_block(PREV_BLKP(bp)); // next 블록 가용 리스트에서 삭제
        remove_free_block(NEXT_BLKP(bp)); // prev 블록 가용 리스트에서 삭제

        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    // printf("case 1\n");
    put_free_block(bp);

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

// first fit
static void *find_fit(size_t asize) {
    void *bp;
    // printf("exec first fit...\n");
    // iterate heap space
    for (bp = free_listp; GET_ALLOC(HDRP(bp)) == 0; bp = SUCC(bp)) { // 가용 리스트의 마지막은 프롤로그 블록
        if (asize <= GET_SIZE(HDRP(bp))) // if free and allocatable
            return bp;
    }

    return NULL;
}

// divide block (minimum block is 8byte)
static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp)); // size of block now
    remove_free_block(bp); // 일단 가용리스트에서 삭제


    if ((csize - asize) >= (2 * DSIZE)) { // divide
        PUT(HDRP(bp), PACK(asize, 1)); // allocate asize
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp); // divide block
        PUT(HDRP(bp), PACK(csize - asize, 0)); // allocate csize - asize
        PUT(FTRP(bp), PACK(csize - asize, 0));

        put_free_block(bp); // 분할 가능하면 남은 부분 가용 리스트에 추가
    }
    else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

static void put_free_block(void* bp) {
    // 가용 리스트 앞에 추가 (LIFO)
    SUCC(bp) = free_listp;
    PRED(bp) = NULL;
    PRED(free_listp) = bp;
    free_listp = bp;
}

static void remove_free_block(void* bp) {
    if (bp == free_listp) { // 첫 블록 삭제
        PRED(SUCC(bp)) = NULL;
        free_listp = SUCC(bp);
    }
    else {
        SUCC(PRED(bp)) = SUCC(bp);
        PRED(SUCC(bp)) = PRED(bp);
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    // ignore spurious requests.
    if (ptr == NULL)
        return;

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

    // realloc from NULL == malloc
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
        remove_free_block(NEXT_BLKP(oldptr));

        // 블록에 여유 공간 있으면 분할 -> 82
        // if (bsize - asize < 2 * DSIZE) {
        //     PUT(HDRP(oldptr), PACK(bsize, 1)); // 1 is alloc, 0 is free
        //     PUT(FTRP(oldptr), PACK(bsize, 1));
        // }
        // else {
        //     PUT(HDRP(oldptr), PACK(asize, 1)); // 1 is alloc, 0 is free
        //     PUT(FTRP(oldptr), PACK(asize, 1));

        //     put_free_block(NEXT_BLKP(oldptr));
        //     PUT(HDRP(NEXT_BLKP(oldptr)), PACK(bsize - asize, 0));
        //     PUT(FTRP(NEXT_BLKP(oldptr)), PACK(bsize - asize, 0));
        // }

        // 분할 안하고 그냥 할당 -> 85
        PUT(HDRP(oldptr), PACK(bsize, 1)); // 1 is alloc, 0 is free
        PUT(FTRP(oldptr), PACK(bsize, 1));
        
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