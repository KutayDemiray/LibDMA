#include "dma.h"
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

// test switches
//#define TIME_TEST
//#define INT_FRAG_TEST
#define EXT_FRAG_TEST

int main() {
	/*
	void *p1;
	void *p2;
	void *p3;
	void *p4;
	int ret;

	ret = dma_init (20); // create a segment of 1 MB
	if (ret != 0) {
		printf ("something was wrong\n");
		exit(1);
	}
	printf("%d\n", ret);
	
	p1 = dma_alloc(100); // allocate space for 100 bytes
	
	p2 = dma_alloc(1024); 
	
	p3 = dma_alloc(64); //always check the return value
	p4 = dma_alloc(220);
	
	
	dma_free(p3); 
	
	p3 = dma_alloc(2048); 
	
	dma_print_blocks(); 
	dma_free(p1);
	dma_free(p2);
	dma_free(p3);
	dma_free(p4);
	*/
	
	#ifdef INT_FRAG_TEST
	int m = 20;
	dma_init(m);
	int allocable = (0x1 << m) - (0x1 << (m - 6)) - 256;
	printf("allocatable space: %d\n", allocable);
	time_t t;
	srand((unsigned) time(&t));
	
	int allocs = 0;
	
	// fill heap with random allocations up to 256 bytes
	int sumsize = 0;
	int actualsize = 0;
	
	void *p;
	
	do {
		int size = rand() % 256 + 256;
		p = dma_alloc(size);
		int words = size % 8 == 0 ? (size >> 3) : (size >> 3) + 1;
		words = words % 2 == 0 ? words : words + 1;
		if (p != NULL) {
			sumsize += size;
			actualsize += 8 * words;
			allocs++;
			printf("allocated %d requested bytes (total requested: %d bytes (%d actual) in %d allocs, total internal frag: %d bytes)\n", size, sumsize, actualsize, allocs, dma_give_intfrag());
		}
		else {
			printf("test ended after fail on alloc %d of size %d bytes (actual: %d bytes)\n", allocs + 1, size, words * 8);
		}
	} while (p != NULL);
	
	dma_print_blocks();
	//dma_print_bitmap();
	#endif
	
	#ifdef EXT_FRAG_TEST
	int m = 16;
	dma_init(m);
	int allocable = (0x1 << m) - (0x1 << (m - 6)) - 256;
	printf("allocatable space: %d\n", allocable);
	time_t t;
	srand((unsigned) time(&t));
	
	int allocs = 0;
	int frees = 0;
	// fill heap with random allocations up to 256 bytes
	int sumsize = 0;
	int actualsize = 0;
	
	void* ptrs[500];
	int sizes[500];
	int words[500];
	
	void *p;
	do {
		if (allocs == 0 || rand() % 4 != 3) {
			int size = rand() % 256 + 256;
			p = dma_alloc(size);
			if (p != NULL) {
				sumsize += size;
				sizes[allocs] = size;
				words[allocs] = size % 8 == 0 ? (size >> 3) : (size >> 3) + 1;
				words[allocs] = words[allocs] % 2 == 0 ? words[allocs] : words[allocs] + 1;
				actualsize += words[allocs] * 8;
				ptrs[allocs] = p;
				allocs++;
				printf("allocated %d bytes (total: %d bytes (%d actual) in %d allocs %d frees, total internal frag: %d bytes)\n", size, sumsize, actualsize, allocs, frees, dma_give_intfrag());
			}
			else {
				printf("test over at alloc %d of size %d (current allocated (actual) space: %d, max allocatable space: %d)\n", allocs + 1, size, actualsize, allocable);
				dma_print_blocks();
				dma_print_bitmap();
			}
		}
		else {
			int index = rand() % allocs;
			if (ptrs[index] != NULL) {
				dma_free(ptrs[index]);
				ptrs[index] = NULL;
				sumsize -= sizes[index];
				actualsize -= words[index] * 8;
				frees++;
				printf("freed ptr from alloc %d pointing to %d bytes\n", index, words[index] * 8);
			}
		}
		
	} while (p != NULL);
	#endif

	return 0;
}
