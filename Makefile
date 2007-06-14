CC = gcc
OBJS = cnet.o
SRCS = cnet.c
CFLAGS = -W -Wall -pedantic -fPIC
OS := $(shell uname -s)
ifeq ($(OS), Darwin)
	LDFLAGS = -dynamiclib
	LIBNAME = libcnet.dylib
else
	LDFLAGS = -shared
	LIBNAME = libcnet.so
endif

default: $(OBJS)
	$(CC) $(LDFLAGS) -o $(LIBNAME) $(OBJS)

clean:
	@rm -f *.o $(LIBNAME)

.c.o:
	$(CC) $(CFLAGS) -c $<
