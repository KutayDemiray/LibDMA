#include "dma.h"
#include "stdlib.h"
#include <stdio.h>

int main() {
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
	printf("%d", ret);
	
	p1 = dma_alloc(100); // allocate space for 100 bytes
	printf ("a1\n");
	p2 = dma_alloc(1024); 
	printf ("a2\n");
	p3 = dma_alloc(64); //always check the return value
	printf ("a3\n");
	p4 = dma_alloc(220);
	printf ("a4\n");
	dma_free(p3); 
	printf ("f1\n");
	p3 = dma_alloc(2048); 
	printf ("a5\n");
	dma_print_blocks(); 
	printf ("p1\n");
	dma_free(p1);
	printf ("f2\n");
	dma_free(p2);
	printf ("f3\n");
	dma_free(p3);
	printf ("f4\n");
	dma_free(p4);
	printf ("f5\n");
	
	return 0;
}
