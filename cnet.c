/* Copyright (c) 2008, Zachery Hostens <zacheryph@gmail.com>
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
#define CNET_LINEMODE 0x40

typedef struct {
  int fd;
  int poll;
  int flags;

  /* connection information */
  char *lhost;
  int lport;
  char *rhost;
  int rport;

  /* buffer data */
  char *in_buf, *out_buf;
  int in_len, out_len;

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
  sock->poll = npollfds;

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
  char buf[1024], *beg, *end;
  int len, ret = 0;

  for (;;) {
    if (-1 == (len = read(sock->fd, buf, 1024))) return cnet_close(sid);
    sock->in_buf = sock->in_len ? realloc(sock->in_buf, sock->in_len+len+1) : calloc(1, len+1);
    memcpy (sock->in_buf + sock->in_len, buf, len);
    sock->in_len += len;
    if (1024 > len) break;
  }
  sock->in_buf[sock->in_len] = '\0';

  if (0 == (sock->flags & CNET_LINEMODE)) {
    sock->handler->on_read (sid, sock->data, sock->in_buf, sock->in_len);
    free (sock->in_buf);
    sock->in_buf = NULL;
    ret = sock->in_len;
    sock->in_len = 0;
    return ret;
  }

  for (end = sock->in_buf; NULL != (beg = strsep(&end, "\r\n"));) {
    if (NULL == end)  break;
    if ('\0' == *beg) continue;
    sock->handler->on_read (sid, sock->data, beg, end-beg);
    ret += end-beg;
  }

  if ('\0' == *beg) {
    free (sock->in_buf);
    sock->in_len = 0;
  }
  else {
    sock->in_len -= (beg - sock->in_buf);
    memmove (sock->in_buf, beg, sock->in_len);
    sock->in_buf = realloc(sock->in_buf, sock->in_len);
  }
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
  struct sockaddr lsa, *rsa;
  struct addrinfo hints, *res = NULL;

  memset (&hints, '\0', sizeof(hints));
  hints.ai_family = PF_UNSPEC;
  if (-1 == (fd = cnet_bind (lhost, lport))) return -1;

  /* if we have lhost we need to get hints for connect */
  if (lhost) {
    getsockname (fd, &lsa, NULL);
    hints.ai_family = lsa.sa_family;
  }

  snprintf (port, 6, "%d", rport);
  if (getaddrinfo (rhost, port, &hints, &res)) goto cleanup;
  rsa = res->ai_addr;
  salen = res->ai_addrlen;

  ret = connect (fd, rsa, sizeof(*rsa));
  if (-1 == ret && EINPROGRESS != errno) goto cleanup;
  return cnet_register (fd, CNET_CLIENT|CNET_CONNECT, POLLIN|POLLOUT|POLLERR|POLLHUP|POLLNVAL);

  cleanup:
    close (fd);
    if (res) freeaddrinfo (res);
    return -1;
}

int cnet_close (int sid)
{
  cnet_socket_t *sock;
  if (NULL == (sock = cnet_fetch(sid))) return -1;
  if (0 > sock->fd) return -1;

  /* remove socket from pollfds is wise.... */
  npollfds--;
  if (sock->poll < npollfds) {
    memcpy (&pollfds[sock->poll], &pollfds[npollfds], sizeof(*pollfds));
    pollsids[sock->poll] = pollsids[npollfds];
    socks[pollsids[sock->poll]].poll = sock->poll;
  }
  memset (&pollfds[npollfds], '\0', sizeof(*pollfds));

  close (sock->fd);
  if (sock->handler->on_close) sock->handler->on_close (sid, sock->data);
  free (sock->lhost);
  free (sock->rhost);
  if (sock->out_len) free (sock->out_buf);
  if (sock->in_len) free (sock->in_buf);
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
        socks->flags &= ~CNET_CONNECT;
      }
      if (sock->flags & CNET_BLOCKED) {
        sock->flags &= ~CNET_BLOCKED;
        cnet_write (sid, NULL, 0);
      }
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

int cnet_linemode (int sid, int toggle)
{
  cnet_socket_t *sock;
  if (NULL == (sock = cnet_fetch(sid))) return 0;
  if (toggle) sock->flags |= CNET_LINEMODE;
  else sock->flags &= ~(CNET_LINEMODE);
  return 1;
}

int cnet_write (int sid, const void *data, int len)
{
  int written = 0, ret = 0;
  cnet_socket_t *sock;
  if (NULL == (sock = cnet_fetch(sid))) return -1;
  if (0 >= len && !sock->out_len) return 0;
  if (sock->flags & (CNET_BLOCKED|CNET_CONNECT)) goto buffer;

  if (sock->out_len) {
    if (sock->out_len != (written = write(sock->fd, sock->out_buf, sock->out_len))) goto buffer;
    free (sock->out_buf);
    sock->out_len = 0;
  }
  if (len == (ret = write(sock->fd, data, len))) return ret+written;

  buffer:
    pollfds[sock->poll].events |= POLLOUT;
    sock->flags |= CNET_BLOCKED;

    if (sock->out_len) memmove(sock->out_buf, sock->out_buf + written, sock->out_len - written);
    sock->out_buf = realloc(sock->out_buf, (sock->out_len - written) + (len - ret));
    memcpy(sock->out_buf+(sock->out_len-written), data, len);
    sock->out_len = (sock->out_len - written) + (len - ret);
    return written + ret;
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
