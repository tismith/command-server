all: client server

USE_TPL=1

ifdef USE_TPL
CFLAGS += -DUSE_TPL
TPL_SOURCES += tpl.c
TPL_HEADERS += tpl.h
endif

server: server.c $(TPL_SOURCES) $(TPL_HEADERS)
	$(CC) $(CFLAGS) -o server server.c $(TPL_SOURCES)

client: client.c $(TPL_SOURCES) $(TPL_HEADERS)
	$(CC) $(CFLAGS) -o client client.c $(TPL_SOURCES)

.PHONY: clean
clean:
	-rm *.o client server

