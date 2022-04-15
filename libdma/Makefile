all: libdma.a app
libdma.a: dma.c
	gcc -Wall -g -c dma.c
	ar rcs libdma.a dma.o
app: libdma.a app.c
	gcc -Wall -g -o app app.c -L. -ldma
clean:
	rm -fr libdma.a dma.o app.o app *~
