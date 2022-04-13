#include "dma.h"
#include <pthread.h>
#include <sys/mman.h>

/* The "heap" managed by this library.
 * The beginning of it contains the bitmap. The bitmap contains 1 bit for every word (which is 8 bytes = 2^6 bits)
 * Thus, if the heap size is 2^m bytes = 2^(m - 3) words, the size of the bitmap will be 2^(m - 3) bits = 2^(m - 6) bytes.
 * Bitmap is followed by 256 bytes = 2^5 words of reserved space.
 */
void *heap = NULL; 
unsigned int heap_size; // in bytes
unsigned int bitmap_size; // in bytes
const void *heap_head; // points to allocatable memory of the heap (not bitmap or reserved space) 
/*
 * Initializes the library. This function will allocate a continuous segment of free memory
 * from the virtual memory of the process it's being called from using the mmap() syscall.
 * The library will then manage the memory in that "heap". The size of that heap will be 
 * 2^m bytes. This function will also initialize other structs of the library such as bitmaps.
 * Constraints: 14 <= m <= 22 (heap size is between 16KB and 4MB)
 * Returns: 0 on success, -1 on failure. 
 */

// mutex to make the library thread-safe
pthread_mutex_t mutex;
 
void bitmap_write()
 
int dma_init(int m) {
	// lock init
	pthread_mutex_init(&mutex, NULL);
	
	// heap init	
	pthread_mutex_lock(&mutex);
	
	// first, allocate and map a virtual memory region of length 2^m bytes
	dma_alloc(0x1 << m);
	heap = (char *) mmap(NULL, 0x1 << m, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, NULL, 0); // TODO MAP_SHARED instead of private?
	if (heap == (void *) (-1)) {
		printf("dma_init(): mmap() failed\n");
		return -1;
	}
	heap_size = 0x1 << m; // 2^m bytes
	bitmap_size = heap_size >> 6; // 2^(m - 6) bytes
	// initialize heap
	// first, make it all 0 (already might be, but make sure)
	int i;
	for (i = 0; i < (heap_size >> 2); i++) {
		((unsigned int *) heap)[i] = 0x00000000;
	}
	
	// then, initialize bitmap
	// allocate bitmap memory and reserved memory on the heap
	// the bitmap is 01000000 00000000 ... 00000000 (2^(m - 3) bits in total)
	// bitmap size is 2^(m - 6) bytes = 2^(m - 9) words => 2^(m - 9) bits are allocated on the bitmap for the bitmap itself
	// 01000000 00000000 ... 00000000 (2^(m - 9) bits = 2^(m - 12) bytes = 2^(m - 14) strides of int)
	// 14 <= m condition guarantees that the bitmap is always at least 1 int wide so that the statement below can be done safely
	((unsigned int *) heap)[0] = 0x40000000; // 4 bytes = 1 int, in binary 01000000 00000000 00000000 00000000
	
	((unsigned int *) heap)[bitmap_size >> 8] = 0x40000000; // allocate the 256 bytes = 32 words of reserved space after bitmap 
	
	// save start of allocatable memory as a pointer
	// it should point to the (bitmap size + 256)th byte
	heap_head = (void *) &(((unsigned int *) heap)[(bitmap_size + 256) >> 2]);
	
	// allocateable part of the heap is completely free, mark as such on the bitmap
	for (i = (bitmap_size >> 8) + 1; i < (bitmap_size >> 2); i++) { // the bitmap spans 2^(m - 6) bytes = 2^(m - 8) ints
		((unsigned int *) heap)[i] = 0xFFFFFFFF; // each 1 (in binary form) represents a free word
	}
	
	// release lock
	pthread_mutex_unlock(&mutex);
	
	return 0;
}

/*
 * Allocates memory with given size.
 * Returns NULL on invalid input (size is outside bounds) or another error.
 */
void *dma_alloc(int size) {
	int words = (size >> 4) + 2; // we always allocate in blocks of multiples of 16 bytes (2 words) regardless of actual size requested
	
	pthread_mutex_lock(&mutex);
	// find the first empty segment with sufficient size on the bit map
	int i;
	int streak = 0; // sequence of free space
	unsigned int *bitmapptr = (unsigned int *) heap; 
	for (i = (bitmap_size >> 8) + 1; i < bitmap_size >> 2; i++) {
		unsigned int cur = bitmapptr[i];
		// note that because we always allocate in multiples of 16 bytes (2 words)
		// the "allocated" flag (01) will never be split between two ints
		// and "free" bits will always be in the multiples of 2 in these ints
		
		// first filter out any (01) flags, if any
		unsigned int mask = 0xFF; // should be initially 11000000 00000000 00000000 00000000
		unsigned int tmp = cur;
		int shifts = 0;
		while (tmp != 0x0) {
			if (mask & tmp == 0xC0000000) { // found 01 flag, filter it out
				unsigned int filter = ~(mask >> shifts);
				cur = cur & filter;
			}
			tmp << 2;
			shifts += 2;
		}
		
		// remaining set bits all belong to free memory, count them
		while (cur != 0x0) {
			
		}
	}
	
	// get a pointer pointing to the corresponding location on the heap
	
	pthread_mutex_unlock(&mutex);
	return p;
}

/*
 * Frees the allocated memory block pointed by p.
 */
void dma_free(void *p) {
	pthread_mutex_lock(&mutex);
	
	pthread_mutex_unlock(&mutex);
}
