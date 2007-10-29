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
/* stupid linux & gnu */
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include "cnet.h"

#define _GNU_SOURCE

#define CNET_CLIENT   0x01
#define CNET_SERVER   0x02
#define CNET_AVAIL    0x04
#define CNET_DELETED  0x08
#define CNET_CONNECT  0x10    /* socket is connecting */
#define CNET_BLOCKED  0x20    /* write's will block   */

typedef struct {
  int fd;
  int flags;

  /* connection information */
  char *lhost;
  int lport;
  char *rhost;
  int rport;

  /* buffer data */
  char *buf;
  int len;

  /* client data */
  cnet_handler_t *handler;
  void *data;
} cnet_socket_t;

static cnet_socket_t *socks = NULL;
static int nsocks = 0;
static struct pollfd *pollfds = NULL;
static int *pollsids = NULL;
static int npollfds;

static char tmp_host[40];
static char tmp_serv[6];


/*** private helper methods ***/
static int cnet_new (void)
{
  int i, sid;

  for (sid = 0; sid < nsocks; sid++)
    if (socks[sid].flags & CNET_AVAIL) break;

  if (sid == nsocks) {
    socks = realloc (socks, (nsocks+4) * sizeof(*socks));
    memset (socks+nsocks, '\0', 4*sizeof(*socks));
    for (i = 0; i < 4; i++) {
      socks[nsocks+i].fd = -1;
      socks[nsocks+i].flags = CNET_AVAIL;
    }
    nsocks += 4;
  }
  return sid;
}

static int cnet_set_nonblock (int sid)
{
  int flags;
  if (-1 == (flags = fcntl (socks[sid].fd, F_GETFL, 0))) return -1;
  flags |= O_NONBLOCK;
  fcntl (socks[sid].fd, F_SETFL, flags);
  return 0;
}

/* returns fd. NOT sid */
static int cnet_bind (const char *host, int port)
{
  int salen, fd;
  char strport[6];
  struct sockaddr *sa;
  struct addrinfo hints;
  struct addrinfo *res = NULL;

  memset (&hints, '\0', sizeof(hints));
  hints.ai_family = PF_UNSPEC;
  if (cnet_ip_type(host)) hints.ai_flags = AI_NUMERICHOST;
  if (port) snprintf (strport, 6, "%d", port);
  if (getaddrinfo (host, (port ? strport : NULL), &hints, &res)) return -1;
  sa = res->ai_addr;
  salen = res->ai_addrlen;

  if (-1 == (fd = socket (AF_INET, SOCK_STREAM, 0))) goto cleanup;
  if (-1 == bind (fd, sa, sizeof(*sa))) goto cleanup;
  freeaddrinfo (res);
  return fd;

  cleanup:
    freeaddrinfo (res);
    return -1;
}

static int cnet_register (int sid, int flags)
{
  cnet_socket_t *sock;
  sock = &socks[sid];

  pollfds = realloc (pollfds, (npollfds+1) * sizeof(*pollfds));
  pollsids = realloc (pollsids, (npollfds+1) * sizeof(*pollsids));
  memset (pollfds+npollfds, '\0', sizeof(*pollfds));
  pollfds[npollfds].fd = sock->fd;
  pollfds[npollfds].events = flags;
  pollsids[npollfds] = sid;
  npollfds++;

  return 0;
}


/*** connection handlers ***/
static int cnet_on_connect (int sid)
{
  cnet_socket_t *sock;
  sock = &socks[sid];
  sock->flags &= ~(CNET_CONNECT);

  if (sock->handler->on_connect) sock->handler->on_connect (sid, sock->data);
  return 0;
}

static int cnet_on_newclient (int sid)
{
  int fd, newsid;
  socklen_t salen;
  cnet_socket_t *sock, *newsock;
  struct sockaddr sa;
  salen = sizeof(sa);
  memset (&sa, '\0', salen);
  sock = &socks[sid];

  fd = accept (sock->fd, &sa, &salen);
  if (0 > fd) return -1;
  newsid = cnet_new ();
  newsock = &socks[newsid];
  newsock->fd = fd;
  newsock->flags = CNET_CLIENT;
  cnet_register (newsid, POLLIN|POLLERR|POLLHUP|POLLNVAL);

  getnameinfo (&sa, salen, tmp_host, 40, tmp_serv, 6, NI_NUMERICHOST|NI_NUMERICSERV);
  newsock->rhost = strdup (tmp_host);
  newsock->rport = atoi (tmp_serv);

  return sock->handler->on_newclient (sid, sock->data, newsid, newsock->rhost, newsock->rport);
}

static int cnet_on_readable (int sid)
{
  cnet_socket_t *sock;
  char *buf;
  int len;
  sock = &socks[sid];
  buf = calloc (1, 1024);
  if (-1 == (len = read(sock->fd, buf, 1023))) return cnet_close(sid);
  return sock->handler->on_read (sid, sock->data, buf, len);
}

static int cnet_on_eof (int sid, int err)
{
  cnet_socket_t *sock;
  sock = &socks[sid];
  if (sock->handler->on_eof) sock->handler->on_eof (sid, sock->data, err);
  return cnet_close(sid);
}


/*** public functions ***/
int cnet_listen (const char *host, int port)
{
  int sid;
  cnet_socket_t *sock;

  if (-1 == (sid = cnet_new ())) return -1;
  sock = &socks[sid];
  if (-1 == (sock->fd = cnet_bind (host, port))) return -1;
  cnet_set_nonblock (sid);
  if (-1 == listen (sock->fd, 2)) goto cleanup;
  sock->flags = CNET_SERVER;
  cnet_register (sid, POLLIN|POLLERR|POLLHUP|POLLNVAL);
  return sid;

  cleanup:
    cnet_close (sid);
    return -1;
}

int cnet_connect (const char *rhost, int rport, const char *lhost, int lport)
{
  int salen, sid, ret;
  char port[6];
  cnet_socket_t *sock;
  struct sockaddr *sa;
  struct addrinfo hints, *res = NULL;

  if (-1 == (sid = cnet_new ())) return -1;
  sock = &socks[sid];
  memset (&hints, '\0', sizeof(hints));
  hints.ai_family = PF_UNSPEC;

  if (!lhost) {
    if (-1 == (sock->fd = socket (AF_INET, SOCK_STREAM, 0))) return -1;
  }
  else {
    if (-1 == (sock->fd = cnet_bind (lhost, lport))) return -1;
    /* now we need to get the socket family to hint the connect() */
    getsockname (sock->fd, sa, NULL);
    hints.ai_family = sa->sa_family;
  }
  cnet_set_nonblock (sid);

  snprintf (port, 6, "%d", rport);
  if (getaddrinfo (rhost, port, &hints, &res)) goto cleanup;
  sa = res->ai_addr;
  salen = res->ai_addrlen;

  ret = connect (sock->fd, sa, sizeof(*sa));
  if (-1 == ret && EINPROGRESS != errno) goto cleanup;
  cnet_register (sid, POLLOUT|POLLERR|POLLHUP|POLLNVAL);
  sock->flags = CNET_CLIENT|CNET_CONNECT;
  return sid;

  cleanup:
    cnet_close (sid);
    if (res) freeaddrinfo (res);
    return -1;
}

int cnet_close (int sid)
{
  int i;
  cnet_socket_t *sock;
  if (!cnet_valid(sid)) return -1;
  sock = &socks[sid];
  if (0 > sock->fd) return -1;

  /* remove socket from pollfds is wise.... */
  for (i = 0; i < npollfds; i++)
    if (sock->fd == pollfds[i].fd) break;
  npollfds--;
  if (i < npollfds) {
    memcpy (&pollfds[i], &pollfds[npollfds], sizeof(*pollfds));
    pollsids[i] = pollsids[npollfds];
  }
  pollfds = realloc (pollfds, npollfds * sizeof(*pollfds));
  pollsids = realloc (pollsids, npollfds * sizeof(*pollsids));


  close (sock->fd);
  if (sock->handler->on_close) sock->handler->on_close (sid, sock->data);
  memset (sock, '\0', sizeof(*sock));
  sock->fd = -1;
  sock->flags = CNET_AVAIL;
  return 0;
}

int cnet_select (int timeout)
{
  static int active = 0;
  int i, n, ret, sid;
  struct pollfd *p;
  cnet_socket_t *sock;
  if (active) return 0;
  active++;

  ret = n = poll (pollfds, npollfds, timeout);
  if (-1 == ret) return -1;
  if (0 == ret) n = npollfds;

  for (i = 0; n && i < npollfds; i++) {
    p = &pollfds[i];
    sid = pollsids[i];
    sock = &socks[sid];
    if (!sock->handler || !p->revents) continue;

    if (p->revents & (POLLERR|POLLHUP|POLLNVAL)) {
      cnet_on_eof (sid, 0);
      i--;
      n--;
      continue;
    }

    if (p->revents & POLLIN) {
      p->events &= POLLIN;
      if (sock->flags & CNET_SERVER) cnet_on_newclient (sid);
      else cnet_on_readable (sid);
    }
    if (p->revents & POLLOUT) {
      if (socks->flags & CNET_CONNECT) cnet_on_connect (sid);
      cnet_write (sid, NULL, 0);
      socks->flags &= ~(CNET_BLOCKED|CNET_CONNECT);
    }

    p->events |= (POLLIN|POLLERR|POLLHUP|POLLNVAL);
    n--;
  }
  active--;
  return ret;
}

/* pass NULL to get the current handler returned */
/* you can't 'unset' a handler, defeats the purpose of an open socket */
cnet_handler_t *cnet_handler (int sid, cnet_handler_t *handler)
{
  cnet_socket_t *sock;
  if (!cnet_valid(sid)) return NULL;
  sock = &socks[sid];
  if (handler)
    sock->handler = handler;
  return sock->handler;
}

void *cnet_conndata (int sid, void *conn_data)
{
  cnet_socket_t *sock;
  if (!cnet_valid(sid)) return NULL;
  sock = &socks[sid];
  if (conn_data)
    sock->data = conn_data;
  return sock->data;
}

int cnet_ip_type (const char *ip)
{
  struct in_addr in_buf;
  struct in6_addr in6_buf;
  if (0 < inet_pton(AF_INET, ip, &in_buf)) return 4;
  if (0 < inet_pton(AF_INET6, ip, &in6_buf)) return 6;
  return 0;
}

/* is this a valid & connected sid ? */
int cnet_valid (int sid)
{
  if (sid >= nsocks) return 0;
  if (socks[sid].flags & (CNET_AVAIL | CNET_DELETED)) return 0;
  return 1;
}

int cnet_write (int sid, const char *data, int len)
{
  int i;
  cnet_socket_t *sock;
  if (!cnet_valid(sid)) return -1;
  sock = &socks[sid];
  if (data && 0 > len) len = strlen (data);
  if (!len && !sock->len) return 0;

  /* check if there is data that still needs to be written */
  if (data) {
    sock->buf = sock->len ? realloc(sock->buf, sock->len + len) : calloc(1, len);
    memcpy (sock->buf + sock->len, data, len);
    sock->len += len;
  }

  if (sock->flags & (CNET_BLOCKED|CNET_CONNECT)) return 0;
  len = write (sock->fd, sock->buf, sock->len);
  if (0 > len && errno != EAGAIN) return cnet_on_eof (sid, errno);

  if (len == sock->len) {
    free (sock->buf);
    sock->len = 0;
  }
  else {
    memmove (sock->buf, sock->buf+len, sock->len-len);
    sock->len -= len;
    sock->buf = realloc (sock->buf, sock->len);

    /* we need to set block since we still have data */
    sock->flags |= CNET_BLOCKED;
    for (i = 0; i < npollfds; i++) {
      if (pollfds[i].fd != sock->fd) continue;
      pollfds[i].events |= POLLOUT;
      break;
    }
  }
  return len;
}

int cnprintf (int sid, const char *format, ...)
{
  va_list args;
  char *data;
  int len, ret;

  va_start (args, format);
  if (-1 == (len = vasprintf (&data, format, args))) return -1;
  va_end (args);
  ret = cnet_write (sid, data, len);
  free (data);
  return ret;
}
