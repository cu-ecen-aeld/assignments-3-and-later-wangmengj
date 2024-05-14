C=$(CROSS_COMPILE)gcc
CFLAGS= -g -Wall -Werror

default: aesdsocket
all: aesdsocket 
aesdsocket: aesdsocket.o
	$(CC) aesdsocket.o -o aesdsocket
writer.o: aesdsocket.c
	$(CC) -c aesdsocket.c -o aesdsocket.o
clean:   
	rm -f aesdsocket aesdsocket.o
