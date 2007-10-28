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
CFLAGS = -W -Wall -pedantic -fPIC -g3
TSFLAGS = -DTESTSERVER -o server test.c
TCFLAGS = -DTESTCLIENT -o client test.c
T_LDFLAGS = -Wno-unused-parameter -L. -lcnet
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
	$(CC) $(CFLAGS) $(T_LDFLAGS) $(TSFLAGS)
	$(CC) $(CFLAGS) $(T_LDFLAGS) $(TCFLAGS)

clean:
	@rm -f *.o $(LIBNAME) server client

.c.o:
	$(CC) $(CFLAGS) -c $<
