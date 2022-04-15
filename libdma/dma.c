#include "dma.h"
#include <stdio.h>
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
 
int dma_init(int m) {
	// lock init
	pthread_mutex_init(&mutex, NULL);
	// no need to lock since we assume dma_init() will be called on the main thread before any other threads are created
	
	// heap init	
	
	// first, allocate and map a virtual memory region of length 2^m bytes
	dma_alloc(0x1 << m);
	heap = (char *) mmap(NULL, 0x1 << m, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); // TODO MAP_SHARED instead of private?
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
	
	// allocate the 256 bytes = 32 words of reserved space after bitmap (which will take 32 bits = 1 int in total)
	((unsigned int *) heap)[bitmap_size >> 8] = 0x40000000; // 01000000 00000000 00000000 00000000
	
	// save start of allocatable memory as a pointer
	// it should point to the (bitmap size + 256)th byte
	heap_head = (void *) &(((unsigned int *) heap)[(bitmap_size + 256) >> 2]);
	
	// allocateable part of the heap is completely free, mark as such on the bitmap
	for (i = (bitmap_size >> 8) + 1; i < (bitmap_size >> 2); i++) { // the bitmap spans 2^(m - 6) bytes = 2^(m - 8) ints
		((unsigned int *) heap)[i] = 0xFFFFFFFF; // each 1 (in binary form) represents a free word
	}
	
	return 0;
}

/*
 * Allocates memory with given size.
 * Returns NULL on invalid input (size is outside bounds) or another error.
 */
void *dma_alloc(int size) {
	// we always allocate in blocks of multiples of 16 bytes (2 words) regardless of actual size requested
	int words;
	
	if (size < 16) {
		words = 2;
	}
	else if (size % 16 == 0) {
		words = size >> 4;
	}
	else {
		words = (size >> 4) + 2;
	} 

	// get lock as we'll be accessing the heap
	pthread_mutex_lock(&mutex);
	
	// find the first empty segment with sufficient size on the bit map
	int i;
	int streak = 0; // length of current sequence of free space
	int bit_offset = 0; // calculated from the left, not right (offset wrt. msb)
	int int_offset = 0;
	for (i = (bitmap_size >> 8) + 1; i < bitmap_size >> 2; i++) {
		unsigned int cur = ((unsigned int *) heap)[i];
		// note that because we always allocate in multiples of 16 bytes (2 words)
		// the "allocated" flag (01) will never be split between two ints
		// and "free" bits will always be in the multiples of 2 in these ints
		
		// first filter out any (01) flags, if any
		unsigned int tmp = cur;
		int shifts = 0;
		while (tmp != 0x0) {
			if ((tmp & 0xC0000000) == 0x80000000) { // 0x80000000 = 01000000 00000000 00000000 00000000
				cur = cur & ~(0xC0000000 >> shifts); // found 01 flag, filter it out
			}
			tmp = tmp << 2;
			shifts += 2;
		}

		// remaining set bits should all point to free memory, count the length of the sequence of them
		bit_offset = 0;
		while (shifts < 32) {
			shifts = 0;
			if ((cur & 0xC0000000) == 0x0) {
				streak = 0;
			}
			else {
				if (streak == 0) {
					// save start position (on the bitmap) of the new streak
					bit_offset = shifts;
					int_offset = i;
				}
				streak += 2;
				if (streak == words) { // found a free block with sufficient size, allocate memory
					
					// set streak region as allocated on the bitmap
					int curint = int_offset;
					int curbit = bit_offset;
					
					// set first two bits as 01 to mark as allocated
					
					// clear first bit of allocated region in bitmap
					((unsigned int *) heap)[curint] = ((unsigned int *) heap)[curint] & ~(0x1 << (32 - curbit));
					
					// no need to set the second bit to obtain 01 (it is already set)
					
					// update curbit and curint
					curbit = (curbit + 2) % 32;
					if (curbit == 30) {
						curint++;
					}
					
					// now set rest of the bits as 0
					while (curint != i && curbit != shifts) {
						// set bits two at a time
						((unsigned int *) heap)[curint] = ((unsigned int *) heap)[curint] & ~(0x3 << (32 - curbit)); // all bits set except two
						// update curbit and curint
						curbit = (curbit + 2) % 32;
						if (curbit == 30) {
							curint++;
						}
					} 			
					
					// get a pointer pointing to the corresponding location on the heap
					// the streak starts at offset:
					// (32 * int_offset + bit_offset) * 8 bytes = (32 * int_offset + bit_offset) * 2 ints of the whole memory segment
					void *ptr = (void *) (&((unsigned int *) heap)[((int_offset << 5) + bit_offset) << 1]);
					
					pthread_mutex_unlock(&mutex);
					return ptr;
				}
			}
			shifts = (shifts + 2);
			cur = cur << 2;
		}
	}

	// failed to find a large enough contiguous memory segment in heap, return null
	pthread_mutex_unlock(&mutex);
	return NULL;
}

/*
 * Frees the allocated memory block pointed by p. Also marks the corresponding bits in bitmap as free.
 */
void dma_free(void *p) {
	
	pthread_mutex_lock(&mutex);
	printf ("ff0\n");
	unsigned int word_offset = (p - heap) >>  3; // bytes between the two addresses divided by 8 gives word offset of p from start of heap
	printf("%d", word_offset);
	printf ("ff1\n");
	
	unsigned int int_offset = word_offset >> 5;
	printf("berke");
	int curint = int_offset;
	int curbit = word_offset % 32;
	
	// set flag as 11 from 01
	
	((unsigned int *) heap)[curint] = ((unsigned int *) heap)[curint] | (0xC0000000 >> curbit);
	printf ("2\n");
	
	curbit = (curbit + 2) % 32;
	if (curbit == 0) {
		curint++;
	}
	
	while ((((((unsigned int *) heap)[curint] << curbit) & 0xC0000000) != 0x40000000) && (((((unsigned int *) heap)[curint] << curbit) & 0xC0000000) != 0xC0000000) ) { // bunu yazan adam kör oldu
		printf ("3\n");
		((unsigned int *) heap)[curint] = ((unsigned int *) heap)[curint] | (0xC0000000 >> curbit);
		printf ("4\n");
		curbit = (curbit + 2) % 32;
		if (curbit == 0) {
			curint++;
		}
	}
	
	pthread_mutex_unlock(&mutex);
}


void dma_print_page (int pno){
	pthread_mutex_lock(&mutex);
	
	unsigned long int page_size_words = 0x1 << 19;
	unsigned long int page_size_ints = page_size_words >> 6;
	
	int i;
	for (i = 0; i < page_size_ints; i++){
		printf("%lx",((unsigned long int*)heap)[pno*page_size_ints + i]);
		if (i % 4 == 3)
			printf("\n");
	}
	
	printf("\n");
	
	pthread_mutex_unlock(&mutex);
}

void dma_print_bitmap(){
	pthread_mutex_lock(&mutex);
	
	unsigned long int bitmap_size_words = bitmap_size >> 3;
	unsigned long int bitmap_size_ints = bitmap_size_words >> 6;
	
	int i; 
	for (i = 0; i < bitmap_size_ints; i++){
		unsigned long int tmp = ((unsigned long int*)heap)[i];
		int j;
		for (j = 0; j < 64; j++){
			printf("%ld", tmp >> (63-j));
			if (j % 8 == 7)
				printf(" ");
		}
		printf("\n");
	}
	
	pthread_mutex_unlock(&mutex);
}

void dma_print_blocks(){
	pthread_mutex_lock(&mutex);
	
	unsigned long int bitmap_size_words = bitmap_size >> 3;
	unsigned long int bitmap_size_ints = bitmap_size_words >> 6;
	unsigned long int c = 0xC000000000000000;
	unsigned long int tmp;
	unsigned long int content;
	unsigned long int content2;
	unsigned long int amount_alloc = 0;
	unsigned long int amount_free = 0;
	int traverse = 0;
	int alloc = 0;
	void *heap_top = heap_head;
	
	
	int i;
	for (i = 0; i < bitmap_size_ints; i++){
	
		content = ((unsigned long int*)heap)[i];
		content2 = content; // need for shift operations since at the end we need tmp again
				
		while (traverse < 256){
			traverse += 2;
			tmp = content2 & c;
			content2 = content2 << 2;
			tmp = tmp << 62; 
			
			if (tmp == 0x1){
				if (amount_free != 0){
					printf("F, %p, %lx (%ld) \n", heap_top, amount_free, amount_free);
					heap_top += amount_free;
				}
					
				amount_alloc = 2;
				amount_free = 0;
				alloc = 1;
			}
			else if (tmp == 0x0){
				amount_alloc += 2;
			}
			else if (tmp == 0x3){
				if (alloc && (amount_alloc != 0)){
					printf("A, %p, %lx (%ld) \n", heap_top, amount_alloc, amount_alloc);
					heap_top += amount_alloc;
				}
				amount_free += 2;
				amount_alloc = 0;
				alloc = 0;
			}
		}
			
			
		
		
	}
	
	
	pthread_mutex_unlock(&mutex);
}







