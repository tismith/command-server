all: client server

USE_TPL=1
USE_LUA=1

ifdef USE_TPL
CFLAGS += -DUSE_TPL
TPL_SOURCES += tpl.c
TPL_HEADERS += tpl.h
endif

ifdef USE_LUA
CFLAGS += -DUSE_LUA
LUA_LIBS += -llua
endif

EXTRA_SOURCES = $(TPL_SOURCES)
EXTRA_HEADERS = $(TPL_HEADERS)
EXTRA_LIBS = $(LUA_LIBS)
EXTRA_DEPS = $(EXTRA_SOURCES) $(EXTRA_HEADERS)

server: server.c $(EXTRA_DEPS)
	$(CC) $(CFLAGS) -o server server.c $(EXTRA_SOURCES) $(EXTRA_LIBS)

client: client.c $(EXTRA_DEPS)
	$(CC) $(CFLAGS) -o client client.c $(EXTRA_SOURCES)

.PHONY: clean
clean:
	-rm *.o client server

