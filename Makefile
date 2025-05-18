CC=gcc
CFLAGS=-Wall -Wextra -std=c11 -pthread -Iinclude

all: server client

server: src/server.c src/document.c src/protocol.c
	$(CC) $(CFLAGS) -o server src/server.c src/document.c src/protocol.c

client: src/client.c src/document.c src/protocol.c
	$(CC) $(CFLAGS) -o client src/client.c src/document.c src/protocol.c

clean:
	rm -f server client *.o doc.md FIFO_* *~