all: server client

server: server.c
	gcc -o server server.c

client: client.c
	gcc -o client client.c

.PHONY: clean
clean:
	rm -rf server client
