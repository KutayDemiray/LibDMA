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

// total internal fragmentation ever created in the heap
// note that deallocating memory blocks do not decrement this value, hence it is not a measure of
// total internal fragmentation in the memory at any given time
int total_intfrag = 0;

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
	// we require the region to start at a page boundary
	// but the linux manual says the kernel already guarantees that with mmap(), so no need to do anything extra
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
	
	// allocateable part of the heap is completely free, mark as such on the bitmap
	for (i = (bitmap_size >> 8) + 1; i < (bitmap_size >> 2); i++) { // the bitmap spans 2^(m - 6) bytes = 2^(m - 8) ints
		((unsigned int *) heap)[i] = 0xFFFFFFFF; // each 1 (in binary form) represents a free word
	}
	
	return 0;
}

/*
 * Allocates memory with given size (in bytes).
 * Returns NULL on invalid input (size is outside bounds) or another error.
 */
void *dma_alloc(int size) {

	printf("dma_alloc(%d)\n", size);
	// we always allocate in blocks of multiples of 16 bytes (2 words) regardless of actual size requested
	int words = size % 8 == 0 ? (size >> 3) : (size >> 3) + 1;
	
	// alloc size = 2 * ceil(words / 2);
	words = words % 2 == 0 ? words : words + 1;
	// 114
	/*
	if (size < 16) {
		words = 2;
	}
	else if (size % 16 == 0) {
		words = size >> 4;
	}
	else {
		words = (size >> 4) + 2;
	} 
	*/
	// get lock as we'll be accessing the heap
	pthread_mutex_lock(&mutex);
	
	// find the first empty segment with sufficient size on the bit map
	int curint = (bitmap_size >> 8) + 1;
	int curbit = 0;
	int streak = 0; // length of current sequence of free space
	/*int bit_offset = 0; // calculated from the left, not right (offset wrt. msb)
	int int_offset = 0;*/
	
	int startint, startbit;
	while (curint < (bitmap_size >> 2)) {
		unsigned int cur = ((unsigned int *) heap)[curint];
		curbit = 0;
		while (curbit < 32) {
			if ((cur & 0xC0000000) != 0xC0000000) { // if current 2 bits are not 11
				streak = 0;
			}
			else { // 11 found
				if (streak == 0) { // if at the start of the new streak, save position
					startint = curint;
					startbit = curbit;
				}
				
				streak += 2;
				
				if (streak == words) { // sufficient streak found
					printf("streak found starting at int %d bit %d\n", startint, startbit);
					int curint = startint;
					int curbit = startbit;
					
					// make flag bits 01
					((unsigned int *) heap)[curint] = ((unsigned int *) heap)[curint] & ~(0x80000000 >> (curbit));
					// no need to set the second bit to obtain 01 (it is already set)
					
					curbit = (curbit + 2) % 32;
					if (curbit == 0) {
						curint++;
					}
					
					// clear rest of the bits as 0
					int i;
					for (i = 0; i < words - 2; i += 2) {
						((unsigned int *) heap)[curint] = ((unsigned int *) heap)[curint] & ~(0xC0000000 >> (curbit));
						curbit = (curbit + 2) % 32;
						if (curbit == 0) {
							curint++;
						}
					}
					
					// return ptr to start address
					// the allocated region starts at offset:
					// (32 * startint + curint) * 1 word = (32 * int_offset + bit_offset) * 2 ints of the whole memory segment
					void *ptr = (void *) &(((unsigned int *) heap)[((startint << 5) + startbit) << 1]);
					
					pthread_mutex_unlock(&mutex);
					return ptr;
				}
				
			}
			cur = cur << 2; 
			curbit += 2;
		}
		curint++;
	}
	
	/*
	for (i = (bitmap_size >> 8) + 1; i < bitmap_size >> 2; i++) {
		unsigned int cur = ((unsigned int *) heap)[i];
		// note that because we always allocate in multiples of 16 bytes (2 words)
		// the "allocated" flag (01) will never be split between two ints
		// and "free" bits will always be in the multiples of 2 in these ints
		
		// first filter out any (01) flags, if any
		unsigned int tmp = cur;
		int shifts = 0;
		while (tmp != 0x0) {
			if ((tmp & 0xC0000000) == 0x40000000) { // 0x40000000 = 01000000 00000000 00000000 00000000
				cur = cur & ~(0xC0000000 >> shifts); // found 01 flag, filter it out
			}
			tmp = tmp << 2;
			shifts += 2;
		}

		// remaining set bits should all point to free memory, count the length of the sequence of them
		bit_offset = 0;
		shifts = 0;
		while (shifts < 32) {
			if ((cur & 0xC0000000) == 0x0) {
				streak = 0;
			}
			else {//if ((cur & 0xC0000000) == 0xC0000000) {
				if (streak == 0) {
					// save start position (on the bitmap) of the new streak
					bit_offset = shifts;
					int_offset = i;
				}
				streak += 2;
				if (streak == words) { // found a free block with sufficient size, allocate memory
					printf("streak found starting at int %d byte %d\n", int_offset, bit_offset);
					
					// set streak region as allocated on the bitmap
					int curint = int_offset;
					int curbit = bit_offset;
					
					// set first two bits as 01 to mark as allocated
					
					// clear first bit of allocated region in bitmap
					printf("flag set\n");
					((unsigned int *) heap)[curint] = ((unsigned int *) heap)[curint] & ~(0x80000000 >> (curbit));
					
					// no need to set the second bit to obtain 01 (it is already set)
					
					// update curbit and curint
					curbit = (curbit + 2) % 32;
					if (curbit == 0) {
						curint++;
					}
					
					// now set rest of the bits as 0
					int j;
					for (j = 0; j < words - 2; j = j + 2) {
						// set bits two at a time
						printf("curint %d curbit %d: %d%d\n", curint, curbit,
							((0x80000000 >> curbit) & ((unsigned int *) heap)[curint]) >> (31 - curbit),
							((0x40000000 >> curbit) & ((unsigned int *) heap)[curint]) >> (30 - curbit));
						((unsigned int *) heap)[curint] = ((unsigned int *) heap)[curint] & ~(0xC0000000 >> (curbit)); // all bits set except two
						// update curbit and curint
						curbit = (curbit + 2) % 32;
						if (curbit == 0) {
							curint++;
						}
					} 			
					
					
					// get a pointer pointing to the corresponding location on the heap
					// the streak starts at offset:
					// (32 * int_offset + bit_offset) * 8 bytes = (32 * int_offset + bit_offset) * 2 ints of the whole memory segment
					void *ptr = (void *) (&((unsigned int *) heap)[((int_offset << 5) + bit_offset) << 1]);
					
					// update total internal fragmentation
					total_intfrag += size % 16;
					
					pthread_mutex_unlock(&mutex);
					return ptr;
				}
			}
			
			shifts = (shifts + 2);
			cur = cur << 2;
		}
	}
	*/

	// failed to find a large enough contiguous memory segment in heap, return null
	pthread_mutex_unlock(&mutex);
	return NULL;
}

/*
 * Frees the allocated memory block pointed by p. Also marks the corresponding bits in bitmap as free.
 */
void dma_free(void *p) {
	
	pthread_mutex_lock(&mutex);
	printf("heap: %p\n", heap);
	printf("p: %p\n",p);
	printf("%ld\n", p - heap);
	unsigned int word_offset = (p - heap) >>  3; // bytes between the two addresses divided by 8 gives word offset of p from start of heap
	//printf("%d", word_offset);

	
	unsigned int int_offset = word_offset >> 5;

	int curint = int_offset;
	int curbit = word_offset % 32;
	
	// set flag as 11 from 01
	
	((unsigned int *) heap)[curint] = ((unsigned int *) heap)[curint] | (0xC0000000 >> curbit);
	
	curbit = (curbit + 2) % 32;
	if (curbit == 0) {
		curint++;
	}
	
	while ((((((unsigned int *) heap)[curint] << curbit) & 0xC0000000) != 0x40000000) && (((((unsigned int *) heap)[curint] << curbit) & 0xC0000000) != 0xC0000000) ) { 
		((unsigned int *) heap)[curint] = ((unsigned int *) heap)[curint] | (0xC0000000 >> curbit);

		curbit = (curbit + 2) % 32;
		if (curbit == 0) {
			curint++;
		}
	}
	
	pthread_mutex_unlock(&mutex);
}


void dma_print_page (int pno){
	pthread_mutex_lock(&mutex);
	
	unsigned long int page_size_bytes = 0x1 << 12;
	unsigned long int page_size_ints = page_size_bytes >> 2;
	unsigned int tmp;
	
	int i;
	for (i = 0; i < page_size_ints; i++){
		//printf(" %d -->", i);
		unsigned int content = ((unsigned int*)heap)[(pno)*page_size_ints + i];
		
		int j;
		for (j = 0; j < 2*sizeof(unsigned int);j++){
			tmp = content & 0xF0000000;
			tmp = tmp >> (4 * (2*sizeof(unsigned int)-1));
			
			if (tmp == 0)
				printf("0");
			else 
				printf("%x", tmp);
				
			content = content << 4;
		}
		
		if (i % 8 == 7)
			printf("\n");
	}
	
	printf("\n");
	
	pthread_mutex_unlock(&mutex);
}

void dma_print_bitmap(){
	pthread_mutex_lock(&mutex);
	
	// bitmap is 2^(m - 3) bits = 2^(m - 6) bytes = 2^(m - 8) ints = 2^(m - 9) words 
	unsigned long int bitmap_size_ints = bitmap_size >> 2;
	
	int i; 
	for (i = 0; i < bitmap_size_ints; i++){
		unsigned int tmp = ((unsigned int*)heap)[i];
		int j;
		for (j = 0; j < 32; j++){
			printf("%d", 0x1 & (tmp >> (31-j)));
			if (j % 8 == 7)
				printf(" ");
		}
		if (i % 2 == 1)
			printf("\n");
	}
	
	pthread_mutex_unlock(&mutex);
}

/*
 * Prints each allocated/free block in the heap region, its start address and size.
 * Note that apart from user-allocated blocks, the bitmap and "reserved" regions are also displayed (first and second rows, respectively)
 */
void dma_print_blocks(){
	pthread_mutex_lock(&mutex);
	//printf("Start of heap: %p \n", heap);
	unsigned long int bitmap_size_ints = bitmap_size >> 2;
	unsigned int c = 0xC0000000;
	unsigned int tmp;
	unsigned int content;
	unsigned int content2;
	unsigned long int amount_alloc = 0;
	unsigned long int amount_free = 0;
	int traverse = 0;
	void *heap_top = heap;
	char* heap_print;
	int i; 
	
	for (i = 0; i < bitmap_size_ints; i++){
		//printf("i: %d \n", i);
		traverse = 0;
		content = ((unsigned int*)heap)[i];
		content2 = content; 
				
		while (traverse < 32){
			if ((((unsigned long int)heap_top) << 16 )>> 16== (unsigned long int)heap_top){
				heap_print = "0x0000";
			}
				
			else{
				heap_print = "";
			}
				
					
			traverse += 2;
			//printf("traverse no: %d \n", traverse);
			tmp = content2 & c;
			content2 = content2 << 2;
			tmp = tmp >> 30; 
			
			//printf("tmp: %x \n", tmp);
			if (tmp == 0x1){
				if (amount_free != 0){
					printf("F, %s%lx, 0x%lx (%ld) \n", heap_print, (unsigned long int)heap_top, 8*amount_free, 8*amount_free);
					heap_top += 8*amount_free;
				}
				else if (amount_alloc != 0){
					printf("A, %s%lx, 0x%lx (%ld) \n", heap_print, (unsigned long int)heap_top, 8*amount_alloc, 8*amount_alloc);
					heap_top += 8*amount_alloc;
				}
					
				amount_alloc = 2;
				amount_free = 0;
			}
			else if (tmp == 0x0){
				amount_alloc += 2;
				amount_free = 0; // to be sure, not required
			}
			else if (tmp == 0x3){
				if (amount_alloc != 0){
					printf("A, %s%lx, 0x%lx (%ld) \n",heap_print, (unsigned long int)heap_top, 8*amount_alloc, 8*amount_alloc);
					heap_top += 8*amount_alloc;
				}
				amount_free += 2;
				amount_alloc = 0;
			}
		}
			
	}
	if (amount_alloc != 0){
		printf("A, %s%lx, 0x%lx (%ld) \n", heap_print, (unsigned long int)heap_top, 8*amount_alloc, 8*amount_alloc);
	}
	else {
		printf("F, %s%lx, 0x%lx (%ld) \n", heap_print, (unsigned long int)heap_top, 8*amount_free, 8*amount_free);
	}

	
	pthread_mutex_unlock(&mutex);
}

int dma_give_intfrag() {
	return total_intfrag;
}

