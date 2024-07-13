CC=gcc
CFLAGS=-lcurl -lz

http_server: http_server.c
	$(CC) -o http_server http_server.c $(CFLAGS)
