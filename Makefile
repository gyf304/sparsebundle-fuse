.PHONY: clean install

sparse: sparse.c
	gcc -std=c99 -O3 -Wall -Iyxml -Iuthash/include sparse.c yxml/yxml.c `pkg-config fuse --cflags --libs` -lpthread -o sparse

install: sparse
	cp sparse /usr/local/bin/sparsebundlefs

clean:
	rm -f sparse
