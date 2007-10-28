/* Copyright (c) 2007 Zachery Hostens <zacheryph@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cnet.h"

#define TESTSERVER_HOST "127.0.0.1"
#define TESTSERVER_PORT 64930

/* test server: echo's anything it recieves to client & output */
#ifdef TESTSERVER
int ts_on_read (int sid, void *conn_data, char *data, int len);
int ts_on_eof (int sid, void *conn_data, int err);
int ts_on_close (int sid, void *conn_data);
int ts_on_newclient (int sid, void *conn_data, int newsid, char *host, int port);

cnet_handler_t testserver_handler = {
  NULL, ts_on_read, ts_on_eof, ts_on_close, ts_on_newclient
};

int ts_on_read (int sid, void *conn_data, char *data, int len)
{
  printf ("SERVER: on_read sid:%d len:%d --> %s\n", sid, len, data);
  return 0;
}

int ts_on_eof (int sid, void *conn_data, int err)
{
  printf ("SERVER: on_eof sid:%d\n", sid);
  return 0;
}

int ts_on_close (int sid, void *conn_data)
{
  printf ("SERVER: on_close sid:%d\n", sid);
  return 0;
}

int ts_on_newclient (int sid, void *conn_data, int newsid, char *host, int port)
{
  printf ("SERVER: new_client sid:%d host:%s port:%d\n", newsid, host, port);
  cnet_handler (newsid, &testserver_handler);
  return 0;
}

int main (const int argc, const char **argv)
{
  int ssid;
  ssid = cnet_listen (TESTSERVER_HOST, TESTSERVER_PORT);
  if (-1 == ssid) {
    printf ("SERVER: ERROR failed to connect errno:%d\n", errno);
    return errno;
  }

  cnet_handler (ssid, &testserver_handler);
  printf ("SERVER: started server sid:%d\n", ssid);

  for (;;) {
    printf ("SERVER: calling cnet_select()\n");
    cnet_select (5000);
  }

  return 0;
}
#endif


/* test client: starts x clients and echo's random text to server */
#ifdef TESTCLIENT
int tc_on_connect (int sid, void *conn_data)
{
  printf ("CLIENT: on_connect sid:%d\n", sid);
  return 0;
}

int tc_on_read (int sid, void *conn_data, char *data, int len)
{
  printf ("CLIENT: on_read sid:%d len:%d --> %s\n", sid, len, data);
  return 0;
}

int tc_on_eof (int sid, void *conn_data, int err)
{
  printf ("CLIENT: on_eof sid:%d\n", sid);
  return 0;
}

int tc_on_close (int sid, void *conn_data)
{
  printf ("CLIENT: on_close sid:%d\n", sid);
  return 0;
}

cnet_handler_t testclient_handler = {
  tc_on_connect, tc_on_read, tc_on_eof, tc_on_close, NULL
};

int main (const int argc, const char **argv)
{
  int i, sid;

  sid = cnet_connect (TESTSERVER_HOST, TESTSERVER_PORT, NULL, 0);
  cnet_handler (sid, &testclient_handler);
  if (0 > sid) {
    printf ("CLIENT: ERROR cnet_connect failed: %d\n", sid);
    printf ("perror: %s\n", strerror(errno));
    return 0;
  }

  for (i = 0; i < 4; i++) {
    cnprintf (sid, "my formatted: %d --> %s\n", i, "Format String");
    cnet_select(2000);
    sleep (2);
  }

  return 0;
}
#endif
