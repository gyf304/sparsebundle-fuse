CFLAGS ?= -std=c99 -O3 -Wall

.PHONY: all clean install

all: sparse-fuse sparse-nbd.so

yxml.o: yxml/yxml.c
	${CC} -c -fPIC ${CFLAGS} -Iyxml yxml/yxml.c -o yxml.o

sparse.o: sparse.c
	${CC} -c -fPIC ${CFLAGS} -Iyxml -Iuthash/include sparse.c -o sparse.o

sparse-fuse.o: sparse-fuse.c
	${CC} -c -fPIC ${CFLAGS} sparse-fuse.c `pkg-config fuse --cflags` -o sparse-fuse.o

sparse-nbd.o: sparse-nbd.c
	${CC} -c -fPIC ${CFLAGS} sparse-nbd.c -o sparse-nbd.o

sparse-fuse: sparse-fuse.o yxml.o sparse.o
	${CC} sparse-fuse.o yxml.o sparse.o -lpthread `pkg-config fuse --libs` -o sparse-fuse

sparse-nbd.so: sparse-nbd.o yxml.o sparse.o
	${CC} sparse-nbd.o yxml.o sparse.o -lpthread -shared -o sparse-nbd.so

clean:
	rm -f sparse-fuse *.o
