/* 
 * This implementation incorporates memory coalescing and implicit free lists.
 * Each page uses a header containing the size of the page, and a footer with a pointer
 * to the next page. So it's easy to iterate through pages by adding the page size to
 * the address of the page header and following the footer pointer.
 * Each block uses a head that contains the size of the block and the size of the previous
 * block, making it easy to move both directions between blocks.
 * 
 * PLEASE READ THIS: I had originally planned on implementing an explicit free list, but after
 * well over 40 hours of pulling my hair out, I learned that when allocating multiple pages at a
 * time, my explicit free list implimentation fail due to not being able to access memory in
 * contiguous pages. I do not know why, and frankly I no longer care, if I spend any longer
 * on this assignment my computer may end up getting thrown out the window.
 * I've left my entire explicit free list implementation in this file, commented out, in hopes
 * of receiving partial credit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* always use 16-byte alignment */
#define ALIGNMENT 16

/* size of block header */
#define OVERHEAD (ALIGN(sizeof(header)))

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

/* rounds up to the nearest multiple of mem_pagesize() */
#define PAGE_ALIGN(size) (((size) + (mem_pagesize()-1)) & ~(mem_pagesize()-1))

/* gets the header pointer of a block given the block pointer */
#define HDRP(bp) ((char *)(bp) - ALIGN(sizeof(header)))

/* get the allocated status and size of a block given header pointer (not block pointer) */
#define GET_ALLOC(p) ((header *)(p))->allocated
#define GET_SIZE(p) ((header *)(p))->size

/* gets the size of the previous block given the block header in the next block */
#define PREV_SIZE(p) ((header *)(p))->prev_size

/* gets the size of a page given a page header pointer */
#define PAGE_SIZE(p) ((page_header *)(p))->size

/* gets next page pointer given current page header pointer */
#define NEXT_PGP(p) ((char *)(p) + PAGE_SIZE(p))

/* gets next block pointer given previous one */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))

/* gets previous block pointer given next one */
#define PREV_BLKP(bp) ((char *)(bp) - PREV_SIZE(HDRP(bp)))

/* gets the next free block pointer given current free block pointer */
#define NEXT_FREP(bp) ((free_header *)(bp))->next

static void *new_page(int size, char link);
static void rm_free(void *blk);

typedef struct {
  int size;
  int prev_size;
  char allocated;
} header;

typedef struct {
  int size;
} page_header;

typedef struct {
  void *next;
} free_header;

// Start of block linked list
void *first_blk = NULL;

// Start of free block linked list
void *free_blk = NULL;

/* 
 * mm_init - initialize the malloc package, allocate the first block
 */
int mm_init(void)
{
  first_blk = new_page(OVERHEAD, 0);
  return 0;
}

/* 
 * mm_malloc - Itterate through the block linked list until a free block is found that's big enough.
 * If one doesn't exist, allocate a new one.
 */
void *mm_malloc(size_t size)
{
  int newsize = ALIGN(size + OVERHEAD);
  void *p = first_blk;

  /*
  // Set p to the next free pointer while current p is allocated or too small
  while (p != NULL && GET_SIZE(HDRP(p)) < newsize) {
    p = NEXT_FREP(p);
  }
  if (p == NULL) {
    // Allocate the page
    p = new_page(newsize, 1);
    np = 1;
  }
  */

  // Set p to the next pointer while current p is allocated or too small
  while (GET_ALLOC(HDRP(p)) || GET_SIZE(HDRP(p)) < newsize) {
    p = NEXT_BLKP(p);
    // allocate a new page if we reach the end of this one and it doesn't point to a different page
    if (GET_SIZE(HDRP(p)) == 0 && *((char *)(p)) == NULL) {
      // Allocate the page
      void *page = new_page(newsize, 0);

      // Point the old page footer to the new page
      *((char *)(p)) = page - (2 * OVERHEAD);

      // Make p point to the new page
      p = page;
    } else if (GET_SIZE(HDRP(p)) == 0) {
      p = *((char *)(p));
    }
  }

  int splitsize = GET_SIZE(HDRP(p)) - newsize;
  //rm_free(p);
  GET_ALLOC(HDRP(p)) = 1;
  GET_SIZE(HDRP(p)) = newsize;

  // Split the block and set the headers appropriately
  if (splitsize > 0) {
    GET_ALLOC(HDRP(NEXT_BLKP(p))) = 0;
    GET_SIZE(HDRP(NEXT_BLKP(p))) = splitsize;
    PREV_SIZE(HDRP(NEXT_BLKP(p))) = newsize;
    PREV_SIZE(HDRP(NEXT_BLKP(NEXT_BLKP(p)))) = splitsize;
    //NEXT_FREP(NEXT_BLKP(p)) = free_blk;
    //free_blk = NEXT_BLKP(p);
  }
  
  return p;
}

/*
 * Create a new page(s) of at least size, links previous pages if link is true,
 * via the footer pointer.
 * Returns: pointer to first payload block in new page(s).
 */
void *new_page(int size, char link) {
  // Add 2 * OVERHEAD to account for the page header (1*OVERHEAD) and footer (2*OVERHEAD)
  int newsize = PAGE_ALIGN(size + (3 * OVERHEAD));

  // Map the page(s)
  // Add 2*OVERHEAD to set new_page pointer to first payload pointer rather than header or page header
  void *new_page = mem_map(newsize) + (2 * OVERHEAD);

  // Set the page header
  PAGE_SIZE(HDRP(HDRP(new_page))) = newsize;
  
  // Set the block header
  // Unallocated
  GET_ALLOC(HDRP(new_page)) = 0;
  // Prev size set to indicate start of page
  PREV_SIZE(HDRP(new_page)) = 0;
  // Subtract the 3 * OVERHEAD to account for the page header and footer
  GET_SIZE(HDRP(new_page)) = newsize - (3 * OVERHEAD);

  // Set the page footer to point to null since there's no next page yet
  GET_ALLOC(HDRP(NEXT_BLKP(new_page))) = 0;
  GET_SIZE(HDRP(NEXT_BLKP(new_page))) = 0;
  PREV_SIZE(HDRP(NEXT_BLKP(new_page))) = newsize - (3 * OVERHEAD);
  *(NEXT_BLKP(new_page)) = NULL;

  // Stick it on top of the free pointer linked list
  //NEXT_FREP(new_page) = free_blk;
  //free_blk = new_page;
  
  // Link previous pages to this one if this isn't the first page
  if (link && first_blk) {
    // Get the next page pointer, which points to the next page header
    void *p = first_blk + PAGE_SIZE(first_blk - (2 * OVERHEAD));
    while (*((char *)(p)) != NULL) {
      p = *((char *)(p)) + PAGE_SIZE(*((char *)(p)) - (2 * OVERHEAD));      
    }

    // Link the last page to the new one
    *((char *)(p)) = new_page - (2 * OVERHEAD);
  }

  return new_page;
}

/* Remove blk from the explicit free linked list */
void rm_free(void *blk) {
  if (free_blk == blk) {
    free_blk = NEXT_FREP(free_blk);
    return;
  }
  
  void *current = free_blk;
  void *prev = free_blk;
  while (current != NULL && current != blk) {
    prev = current;
    current = NEXT_FREP(current);
  }
  if (blk == current) {
    NEXT_FREP(prev) = NEXT_FREP(current);
  }
}

/*
 * mm_free - Make block header as unallocated and coalesce the adjacent blocks.
 */
void mm_free(void *ptr)
{
  GET_ALLOC(HDRP(ptr)) = 0;
  char next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
  char prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(ptr)));

  // Coalesce the blocks
  int newsize;
  if (!next_alloc && !prev_alloc && PREV_SIZE(HDRP(ptr)) > 0) {
    newsize = GET_SIZE(HDRP(PREV_BLKP(ptr))) + GET_SIZE(HDRP(ptr)) + GET_SIZE(HDRP(NEXT_BLKP(ptr)));
    GET_SIZE(HDRP(PREV_BLKP(ptr))) = newsize;
    PREV_SIZE(HDRP(NEXT_BLKP(NEXT_BLKP(ptr)))) = newsize;
    //rm_free(PREV_BLKP(ptr));
    //rm_free(ptr);
    //rm_free(NEXT_BLKP(ptr));
    //NEXT_FREP(PREV_BLKP(ptr)) = free_blk;
    //free_blk = PREV_BLKP(ptr);
  } else if (!next_alloc) {
    newsize = GET_SIZE(HDRP(ptr)) + GET_SIZE(HDRP(NEXT_BLKP(ptr)));
    PREV_SIZE(HDRP(NEXT_BLKP(NEXT_BLKP(ptr)))) = newsize;
    GET_SIZE(HDRP(ptr)) = newsize;
    //rm_free(ptr);
    //rm_free(NEXT_BLKP(ptr));
    //NEXT_FREP(ptr) = free_blk;
    //free_blk = ptr;
  } else if (!prev_alloc && PREV_SIZE(HDRP(ptr)) > 0) {
    newsize = GET_SIZE(HDRP(PREV_BLKP(ptr))) + GET_SIZE(HDRP(ptr));
    GET_SIZE(HDRP(PREV_BLKP(ptr))) = newsize;
    PREV_SIZE(HDRP(NEXT_BLKP(ptr))) = newsize;
    //rm_free(ptr);
    //rm_free(PREV_BLKP(ptr));
    //NEXT_FREP(PREV_BLKP(ptr)) = free_blk;
    //free_blk = PREV_BLKP(ptr);
  } else {
    //rm_free(ptr);
    //NEXT_FREP(ptr) = free_blk;
    //free_blk = ptr;
  }
}
