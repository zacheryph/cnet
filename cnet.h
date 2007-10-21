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
#ifndef _CONTEXT_NET_H_
#define _CONTEXT_NET_H_

typedef struct {
  int (*on_connect) (int sid, void *conn_data);
  int (*on_read) (int sid, void *conn_data, char *data, int len);
  int (*on_eof) (int sid, void *conn_data, int err);
  int (*on_close) (int sid, void *conn_data);
  int (*on_newclient) (int sid, void *conn_data, int newsid, char *host, int port);
} cnet_handler_t;

int cnet_listen (const char *host, int port);
int cnet_connect (const char *rhost, int rport, const char *lhost, int lport);
int cnet_close (int sid);
int cnet_select (int timeout);
int cnet_ip_type (const char *ip);
int cnet_valid (int sid);
int cnet_write (int sid, const char *data, int len);
int cnprintf (int sid, const char *format, ...);
cnet_handler_t *cnet_handler (int sid, cnet_handler_t *handler);
void *cnet_conndata (int sid, void *conn_data);

#endif /* _CONTEXT_NET_H_ */
