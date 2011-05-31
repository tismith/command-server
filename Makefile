all: client server

server: server.c tpl.c tpl.h 
	$(CC) -o server server.c tpl.c

client: client.c tpl.c tpl.h
	$(CC) -o client client.c tpl.c

.PHONY: clean
clean:
	-rm *.o client server

