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
/* this define is for gnu/linux retardation! */
#define _GNU_SOURCE
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
static struct pollfd *pollfds = NULL;
static int *pollsids = NULL;
static int nsocks = 0;
static int npollfds = 0;


/*** private helper methods ***/
static int cnet_grow_sockets (void)
{
  int i, newsocks;
  newsocks = (nsocks / 3) + 16;
  socks = realloc (socks, (nsocks+newsocks) * sizeof(*socks));
  pollfds = realloc (pollfds, (nsocks+newsocks) * sizeof(*pollfds));
  pollsids = realloc (pollsids, (nsocks+newsocks) * sizeof(*pollsids));
  memset(socks+nsocks, '\0', newsocks*sizeof(*socks));
  memset (pollfds+newsocks, '\0', sizeof(*pollfds));
  for (i = 0; i < newsocks; i++) {
    socks[nsocks+i].fd = -1;
    socks[nsocks+i].flags = CNET_AVAIL;
  }

  nsocks += newsocks;
  return newsocks;
}

/* returns fd. NOT sid */
static int cnet_bind (const char *host, int port)
{
  int salen, fd;
  char strport[6];
  struct sockaddr *sa;
  struct addrinfo hints, *res = NULL;

  fd = socket (AF_INET, SOCK_STREAM, 0);
  if (!host || 0 > fd) return fd;

  memset (&hints, '\0', sizeof(hints));
  hints.ai_family = PF_UNSPEC;
  if (cnet_ip_type(host)) hints.ai_flags = AI_NUMERICHOST;
  if (port) snprintf (strport, 6, "%d", port);
  if (getaddrinfo (host, (port ? strport : NULL), &hints, &res)) return -1;
  sa = res->ai_addr;
  salen = res->ai_addrlen;

  if (-1 == bind (fd, sa, sizeof(*sa))) {
    close (fd);
    fd = -1;
  }

  freeaddrinfo (res);
  return fd;
}

/* create a sid / configure fd */
static int cnet_register (int fd, int sockflags, int fdflags)
{
  int sid, flags;
  cnet_socket_t *sock;

  /* get us our sid */
  if (0 == nsocks) cnet_grow_sockets();
  for (sid = 0; sid < nsocks; sid++)
    if (socks[sid].flags & CNET_AVAIL) break;
  if (sid == nsocks) return -1;
  sock = &socks[sid];
  sock->fd = fd;
  sock->flags = sockflags;

  /* configure the fd */
  pollfds[npollfds].fd = sock->fd;
  pollfds[npollfds].events = fdflags;
  pollsids[npollfds] = sid;
  npollfds++;

  /* set nonblocking */
  if (-1 == (flags = fcntl (sock->fd, F_GETFL, 0))) return -1;
  flags |= O_NONBLOCK;
  fcntl (sock->fd, F_SETFL, flags);

  return sid;
}

/* fetch the cnet_socket_t related to a sid if avail and not deleted */
static cnet_socket_t *cnet_fetch (int sid)
{
  if (sid >= nsocks) return NULL;
  if (socks[sid].flags & (CNET_AVAIL | CNET_DELETED)) return NULL;
  return &socks[sid];
}


/*** connection handlers ***/
static int cnet_on_connect (int sid, cnet_socket_t *sock)
{
  sock->flags &= ~(CNET_CONNECT);
  if (sock->handler->on_connect) sock->handler->on_connect (sid, sock->data);
  return 0;
}

static int cnet_on_newclient (int sid, cnet_socket_t *sock)
{
  int fd, newsid, accepted = 0;
  char host[40], serv[6];
  socklen_t salen;
  cnet_socket_t *newsock;
  struct sockaddr sa;
  salen = sizeof(sa);
  memset (&sa, '\0', salen);

  while (npollfds < nsocks) {
    fd = accept (sock->fd, &sa, &salen);
    if (0 > fd) break;
    accepted++;
    newsid = cnet_register (fd, CNET_CLIENT, POLLIN|POLLERR|POLLHUP|POLLNVAL);
    newsock = &socks[newsid];

    getnameinfo (&sa, salen, host, 40, serv, 6, NI_NUMERICHOST|NI_NUMERICSERV);
    newsock->rhost = strdup (host);
    newsock->rport = atoi (serv);
    sock->handler->on_newclient (sid, sock->data, newsid, newsock->rhost, newsock->rport);
  }
  return accepted;
}

static int cnet_on_readable (int sid, cnet_socket_t *sock)
{
  char *buf;
  int len, ret;
  buf = calloc (1, 1024);
  if (-1 == (len = read(sock->fd, buf, 1023))) return cnet_close(sid);
  ret = sock->handler->on_read (sid, sock->data, buf, len);
  free (buf);
  return ret;
}

static int cnet_on_eof (int sid, cnet_socket_t *sock, int err)
{
  if (sock->handler->on_eof) sock->handler->on_eof (sid, sock->data, err);
  return cnet_close(sid);
}


/*** public functions ***/
int cnet_listen (const char *host, int port)
{
  int fd;

  if (-1 == (fd = cnet_bind (host, port))) return -1;
  if (-1 == listen (fd, 2)) {
    close (fd);
    return -1;
  }
  return cnet_register (fd, CNET_SERVER, POLLIN|POLLERR|POLLHUP|POLLNVAL);
}

int cnet_connect (const char *rhost, int rport, const char *lhost, int lport)
{
  int salen, fd, ret;
  char port[6];
  struct sockaddr *sa;
  struct addrinfo hints, *res = NULL;

  memset (&hints, '\0', sizeof(hints));
  hints.ai_family = PF_UNSPEC;
  if (-1 == (fd = cnet_bind (lhost, lport))) return -1;

  /* if we have lhost we need to get hints for connect */
  if (lhost) {
    getsockname (fd, sa, NULL);
    hints.ai_family = sa->sa_family;
  }

  snprintf (port, 6, "%d", rport);
  if (getaddrinfo (rhost, port, &hints, &res)) goto cleanup;
  sa = res->ai_addr;
  salen = res->ai_addrlen;

  ret = connect (fd, sa, sizeof(*sa));
  if (-1 == ret && EINPROGRESS != errno) goto cleanup;
  return cnet_register (fd, CNET_CLIENT|CNET_CONNECT, POLLIN|POLLOUT|POLLERR|POLLHUP|POLLNVAL);

  cleanup:
    close (fd);
    if (res) freeaddrinfo (res);
    return -1;
}

int cnet_close (int sid)
{
  int i;
  cnet_socket_t *sock;
  if (NULL == (sock = cnet_fetch(sid))) return -1;
  if (0 > sock->fd) return -1;

  /* remove socket from pollfds is wise.... */
  for (i = 0; i < npollfds; i++)
    if (sock->fd == pollfds[i].fd) break;
  npollfds--;
  if (i < npollfds) {
    memcpy (&pollfds[i], &pollfds[npollfds], sizeof(*pollfds));
    pollsids[i] = pollsids[npollfds];
  }
  memset (&pollfds[i], '\0', sizeof(*pollfds));

  close (sock->fd);
  if (sock->handler->on_close) sock->handler->on_close (sid, sock->data);
  free (sock->lhost);
  free (sock->rhost);
  if (sock->len) free (sock->buf);
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
      cnet_on_eof (sid, sock, 0);
      i--;
      n--;
      continue;
    }

    if (p->revents & POLLIN) {
      if (sock->flags & CNET_SERVER) cnet_on_newclient (sid, sock);
      else cnet_on_readable (sid, sock);
    }
    if (p->revents & POLLOUT) {
      p->events &= ~(POLLOUT);
      if (sock->flags & CNET_CONNECT) {
        cnet_on_connect (sid, sock);
        socks->flags &= ~CNET_BLOCKED;
      }
      if (sock->flags & CNET_BLOCKED) cnet_write (sid, NULL, 0);
    }

    n--;
    if (!n) break;
  }
  active--;

  /* grow sockets if we must */
  if (npollfds > nsocks - (nsocks / 3)) cnet_grow_sockets();

  return ret;
}

/* pass NULL to get the current handler returned */
/* you can't 'unset' a handler, defeats the purpose of an open socket */
cnet_handler_t *cnet_handler (int sid, cnet_handler_t *handler)
{
  cnet_socket_t *sock;
  if (NULL == (sock = cnet_fetch(sid))) return NULL;
  if (handler) sock->handler = handler;
  return sock->handler;
}

void *cnet_conndata (int sid, void *conn_data)
{
  cnet_socket_t *sock;
  if (NULL == (sock = cnet_fetch(sid))) return NULL;
  if (conn_data) sock->data = conn_data;
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
  return cnet_fetch(sid) ? 1 : 0;
}

int cnet_write (int sid, const char *data, int len)
{
  int i;
  cnet_socket_t *sock;
  if (NULL == (sock = cnet_fetch(sid))) return -1;
  if (0 >= len && !sock->len) return 0;

  /* check if there is data that still needs to be written */
  if (len > 0) {
    sock->buf = sock->len ? realloc(sock->buf, sock->len + len) : calloc(1, len);
    memcpy (sock->buf + sock->len, data, len);
    sock->len += len;
  }

  if (sock->flags & (CNET_BLOCKED|CNET_CONNECT)) return 0;
  len = write (sock->fd, sock->buf, sock->len);
  if (0 > len && errno != EAGAIN) return cnet_on_eof (sid, sock, errno);

  for (i = 0; i < npollfds; i++)
    if (sock->fd == pollfds[i].fd) break;

  if (len == sock->len) {
    free (sock->buf);
    sock->len = 0;
    pollfds[i].events &= ~POLLOUT;
  }
  else {
    memmove (sock->buf, sock->buf+len, sock->len-len);
    sock->len -= len;
    sock->buf = realloc (sock->buf, sock->len);

    /* we need to set block since we still have data */
    sock->flags |= CNET_BLOCKED;
    pollfds[i].events |= POLLOUT;
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
