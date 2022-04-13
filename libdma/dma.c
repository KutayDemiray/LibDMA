#include "dma.h"
#include <pthread.h>
#include <sys/mman.h>

/* The "heap" managed by this library.
 * The beginning of it contains the bitmap. The bitmap contains 1 bit for every word (which is 8 bytes = 2^6 bits)
 * Thus, if the heap size is 2^m bytes, the size of the bitmap will be 2^(m - 6) bytes.
 * Bitmap is followed by 256 bytes of reserved space.
 */
void *heap = NULL; 

/*
 * Initializes the library. This function will allocate a continuous segment of free memory
 * from the virtual memory of the process it's being called from using the mmap() syscall.
 * The library will then manage the memory in that "heap". The size of that heap will be 
 * 2^m bytes. This function will also initialize other structs of the library such as bitmaps.
 * Constraints: 14 <= m <= 22 (heap size is between 16KB and 4MB)
 * Returns: 0 on success, -1 on failure. 
 */
 
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
	
	// initialize bitmap
	// first, make it all 0 (already might be, but make sure)
	int i;
	for (i = 0; i < (unsigned int) (0x1 << (m - 8)); i++) {
		((unsigned int *) heap)[i] = 0x00000000;
	}
	
	// allocate bitmap memory and reserved memory on the heap
	// 01000000 00000000 ... 00000000 (2^(m - 3) bits)
	// bitmap size is 2^(m - 6) bytes = 2^(m - 9) words => 2^(m - 9) bits are allocated for bitmap
	// 01000000 00000000 ... 00000000 (2^(m - 9) bits = 2^(m - 12) bytes = 2^(m - 14) strides of type int)
	// 14 <= m condition guarantees that the bitmap is at least 1 int wide so that we can safely do the statement below
	((unsigned int *) heap)[0] = 0x40000000; // 4 bytes = 1 int
	
	((unsigned int *) heap)[(unsigned int) (0x1 << (m - 14))] = 0x40000000; // allocate the 256 bytes of reserved space after bitmap 
	
	// rest of the heap is free, mark the bitmap as such
	
	for (i = (unsigned int) (0x1 << (m - 14)) + 1; i < (unsigned int) (0x1 << (m - 8)); i++) { // the bitmap spans 2^(m - 6) bytes = 2^(m - 8) ints
		((unsigned int *) heap)[i] = 0xFFFFFFFF;
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
	
}

/*
 * Frees the allocated memory block pointed by p.
 */
void dma_free(void *p) {

}
