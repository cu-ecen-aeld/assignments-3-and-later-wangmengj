CC?=$(CROSS_COMPILE)gcc
CFLAGS?= -g -Wall -Werror -pthread
LDFLAGS?= -pthread

default: aesdsocket
all: aesdsocket 
aesdsocket: aesdsocket.o
	$(CC) ${LDFLAGS} aesdsocket.o -o aesdsocket
aesdsocket.o: aesdsocket.c
	$(CC) -c ${CFLAGS}  aesdsocket.c -o aesdsocket.o
clean:   
	rm -f aesdsocket aesdsocket.o
