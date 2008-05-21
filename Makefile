# Copyright (c) 2007 Zachery Hostens <zacheryph@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
CC = gcc
OBJS = cnet.o
SRCS = cnet.c
CFLAGS = -W -Wextra -Wall -Wstrict-aliasing -pedantic -fPIC -g -D_XOPEN_SOURCE=500
TSFLAGS = -DTESTSERVER -o server
TCFLAGS = -DTESTCLIENT -o client
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

test: default
	$(CC) $(CFLAGS) $(TSFLAGS) cnet.c test.c
	$(CC) $(CFLAGS) $(TCFLAGS) cnet.c test.c

bintest: default
	$(CC) $(CFLAGS) $(T_LDFLAGS) $(TSFLAGS) binary_test.c
	$(CC) $(CFLAGS) $(T_LDFLAGS) $(TCFLAGS) binary_test.c

runtest: test
	MallocGaurdEdges=1 MallocStackLogging=1 MallocPreScribble=1 MallocScribble=1 MallocErrorAbort=1 gdb ./server

clean:
	@rm -rf *.o $(LIBNAME) server client server.dSYM client.dSYM

.c.o:
	$(CC) $(CFLAGS) -c $<
