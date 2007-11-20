/* Copyright (c) 2007, Zachery Hostens <zacheryph@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef CONTEXT_NET_H_
#define CONTEXT_NET_H_

#ifndef __GNUC__
# define __attribute__(x) /* NOTHING */
#endif

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
int cnet_linemode (int sid, int toggle);
int cnet_write (int sid, const void *data, int len);
int cnprintf (int sid, const char *format, ...) __attribute__((format(printf,2,3)));
cnet_handler_t *cnet_handler (int sid, cnet_handler_t *handler);
void *cnet_conndata (int sid, void *conn_data);

#endif /* CONTEXT_NET_H_ */
