CC=gcc
CFLAGS=-Wall -std=c99 -D _BSD_SOURCE -D _XOPEN_SOURCE
LDFLAGS=-lpthread
DEPS=

all: eztunnel

eztunnel: eztunnel.c $(DEPS)
	$(CC) $(CFLAGS) eztunnel.c -o eztunnel $(LDFLAGS)

clean:
	rm eztunnel
