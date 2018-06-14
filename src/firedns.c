/*
firedns.c - firedns library
Copyright (C) 2002 Ian Gulliver

This file has been gutted and mucked with for use in BOPM - see the
real library at http://ares.penguinhosting.net/~ian/ before you judge
firedns based on this..

This program is free software; you can redistribute it and/or modify
it under the terms of version 2 of the GNU General Public License as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "setup.h"

#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

#include "compat.h"
#include "memory.h"
#include "firedns.h"
#include "config.h"
#include "list.h"
#include "log.h"
#include "dnsbl.h"

#define FIREDNS_TRIES 3

int firedns_errno = FDNS_ERR_NONE;

/* Variables local to this file */
static unsigned int firedns_fdinuse;

/* up to FDNS_MAX nameservers; populated by firedns_init() */
static struct in_addr servers4[FDNS_MAX];
static struct in6_addr servers6[FDNS_MAX];

/* actual count of nameservers; set by firedns_init() */
static unsigned int i4;
static unsigned int i6;

/*
 * Linked list of open DNS queries; populated by firedns_add_query(),
 * decimated by firedns_getresult()
 */
static list_t CONNECTIONS;

/*
 * List of errors, in order of values used in FDNS_ERR_*, returned by
 * firedns_strerror
 */
static const char *const errors[] =
{
  [FDNS_ERR_NONE] = "Success",
  [FDNS_ERR_FORMAT] = "Format error",
  [FDNS_ERR_SERVFAIL] = "Server failure",
  [FDNS_ERR_NXDOMAIN] = "Name error",
  [FDNS_ERR_NOIMPT] = "Not implemented",
  [FDNS_ERR_REFUSED] = "Refused",
  [FDNS_ERR_TIMEOUT] = "Timeout",
  [FDNS_ERR_NETWORK] = "Network error",
  [FDNS_ERR_FDLIMIT] = "FD Limit reached",
  [FDNS_ERR_OTHER] = "Unknown error"
};

/* Structures */

/* open DNS query */
struct s_connection
{
  /*
   * unique ID (random number), matches header ID; both set by
   * firedns_add_query()
   */
  unsigned char id[2];
  uint16_t class;
  uint16_t type;

  /* file descriptor returned from sockets */
  int fd;
  void *info;
  time_t start;
  char lookup[256];
  int v6;
};

struct s_rr_middle
{
  uint16_t type;
  uint16_t class;

  /* XXX - firedns depends on this being 4 bytes */
  uint32_t ttl;
  uint16_t rdlength;
};

/* DNS query header */
struct s_header
{
  unsigned char id[2];
  unsigned char flags1;
#define FLAGS1_MASK_QR 0x80
/* bitshift right 3 */
#define FLAGS1_MASK_OPCODE 0x78
#define FLAGS1_MASK_AA 0x04
#define FLAGS1_MASK_TC 0x02
#define FLAGS1_MASK_RD 0x01

  unsigned char flags2;
#define FLAGS2_MASK_RA 0x80
#define FLAGS2_MASK_Z  0x70
#define FLAGS2_MASK_RCODE 0x0f

  uint16_t qdcount;
  uint16_t ancount;
  uint16_t nscount;
  uint16_t arcount;

  /* DNS question, populated by firedns_build_query_payload() */
  unsigned char payload[512];
};

/* Function prototypes */
static struct s_connection *firedns_add_query(void);
static int firedns_doquery(struct s_connection *);
static int firedns_build_query_payload(const char *const, uint16_t, uint16_t, unsigned char *);
static int firedns_send_requests(struct s_header *, struct s_connection *, int);


void
firedns_init(void)
{
  /*
   * populates servers4 (or -6) struct with up to FDNS_MAX nameserver IP
   * addresses from /etc/firedns.conf (or /etc/resolv.conf)
   */
  FILE *f;
  struct in_addr addr4;
  struct in6_addr addr6;
  char buf[1024];
  char *p = NULL;

  i6 = 0;
  i4 = 0;

  srand((unsigned int)time(NULL));
  memset(servers4, 0, sizeof(servers4));
  memset(servers6, 0, sizeof(servers6));

  /* read etc/firedns.conf if we've got it, otherwise parse /etc/resolv.conf */
  f = fopen(FDNS_CONFIG_PREF, "r");

  if (f == NULL)
  {
    f = fopen(FDNS_CONFIG_FBCK, "r");

    if (f == NULL)
    {
      log_printf("Unable to open %s", FDNS_CONFIG_FBCK);
      return;
    }

    while (fgets(buf, sizeof(buf), f))
    {
      if ((p = strchr(buf, '\n')))
        *p = '\0';

      if (strncmp(buf, "nameserver", 10) == 0)
      {
        unsigned int i = 10;

        while (buf[i] == ' ' || buf[i] == '\t')
          ++i;

        if (i6 < FDNS_MAX)
        {
          if (inet_pton(AF_INET6, &buf[i], &addr6) > 0)
          {
            memcpy(&servers6[i6++], &addr6, sizeof(struct in6_addr));
            continue;
          }
        }

        if (i4 < FDNS_MAX)
        {
          if (inet_pton(AF_INET, &buf[i], &addr4) > 0)
            memcpy(&servers4[i4++], &addr4, sizeof(struct in_addr));
        }
      }
    }
  }
  else
  {
    while (fgets(buf, sizeof(buf), f))
    {
      if ((p = strchr(buf, '\n')))
        *p = '\0';

      if (i6 < FDNS_MAX)
      {
        if (inet_pton(AF_INET6, buf, &addr6) > 0)
        {
          memcpy(&servers6[i6++], &addr6, sizeof(struct in6_addr));
          continue;
        }
      }

      if (i4 < FDNS_MAX)
      {
        if (inet_pton(AF_INET, buf, &addr4) > 0)
          memcpy(&servers4[i4++], &addr4, sizeof(struct in_addr));
      }
    }
  }

  fclose(f);
}

/*
 * These little hacks are here to avoid alignment and type sizing issues completely by doing manual copies
 */
static inline void
firedns_fill_rr(struct s_rr_middle *restrict const rr, const unsigned char *const restrict input)
{
  rr->type = input[0] * 256 + input[1];
  rr->class = input[2] * 256 + input[3];
  rr->ttl = input[4] * 16777216 + input[5] * 65536 + input[6] * 256 + input[7];
  rr->rdlength = input[8] * 256 + input[9];
}

static inline void
firedns_fill_header(struct s_header *const restrict header, const unsigned char *const restrict input, const int l)
{
  header->id[0] = input[0];
  header->id[1] = input[1];
  header->flags1 = input[2];
  header->flags2 = input[3];
  header->qdcount = input[4] * 256 + input[5];
  header->ancount = input[6] * 256 + input[7];
  header->nscount = input[8] * 256 + input[9];
  header->arcount = input[10] * 256 + input[11];
  memcpy(header->payload, &input[12], l);
}

static inline void
firedns_empty_header(unsigned char *const restrict output, const struct s_header *const restrict header, const int l)
{
  output[0] = header->id[0];
  output[1] = header->id[1];
  output[2] = header->flags1;
  output[3] = header->flags2;
  output[4] = header->qdcount / 256;
  output[5] = header->qdcount % 256;
  output[6] = header->ancount / 256;
  output[7] = header->ancount % 256;
  output[8] = header->nscount / 256;
  output[9] = header->nscount % 256;
  output[10] = header->arcount / 256;
  output[11] = header->arcount % 256;
  memcpy(&output[12], header->payload, l);
}

/* immediate A query */
struct in_addr *
firedns_resolveip4(const char *const name)
{
  static struct in_addr addr;

  if (inet_pton(AF_INET, name, &addr) > 0)
    return &addr;

  return firedns_resolveip(FDNS_QRY_A, name);
}

/* immediate AAAA query */
struct in6_addr *
firedns_resolveip6(const char *const name)
{
  static struct in6_addr addr;

  if (inet_pton(AF_INET6, name, &addr) > 0)
    return &addr;

  return firedns_resolveip(FDNS_QRY_AAAA, name);
}

/* resolve a query of a given type */
void *
firedns_resolveip(int type, const char *const name)
{
  struct firedns_result *result;
  struct timeval tv;
  fd_set s;

  for (unsigned int t = 0; t < FIREDNS_TRIES; ++t)
  {
    int fd = firedns_getip(type, name, NULL);
    if (fd == -1)
      return NULL;

    tv.tv_sec = 5;
    tv.tv_usec = 0;
    FD_ZERO(&s);
    FD_SET(fd, &s);
    select(fd + 1, &s, NULL, NULL, &tv);

    result = firedns_getresult(fd);

    if (firedns_errno == FDNS_ERR_NONE)
      /*
       * Return is from static memory in getresult, so there is no need to
       * copy it until the next call to firedns.
       */
      return result->text;
    else if (firedns_errno == FDNS_ERR_NXDOMAIN)
      return NULL;
  }

  if (firedns_errno == FDNS_ERR_NONE)
    firedns_errno = FDNS_ERR_TIMEOUT;

  return NULL;
}

/*
 * build, add and send specified query; retrieve result with
 * firedns_getresult()
 */
int
firedns_getip(int type, const char *const name, void *info)
{
  struct s_connection *s;
  node_t *node;

  s = firedns_add_query();
  s->class = 1;
  s->type = type;
  s->info = info;
  strlcpy(s->lookup, name, sizeof(s->lookup));

  if (firedns_fdinuse >= OptionsItem->dns_fdlimit)
  {
    firedns_errno = FDNS_ERR_FDLIMIT;

    /* Don't add to queue if there is no info */
    if (info == NULL)
      xfree(s);
    else
    {
      node = node_create(s);
      list_add(&CONNECTIONS, node);
    }

    return -1;
  }

  int fd = firedns_doquery(s);
  if (fd == -1)
  {
    xfree(s);
    return -1;
  }

  node = node_create(s);
  list_add(&CONNECTIONS, node);

  return fd;
}

/* build DNS query, add to list */
static struct s_connection *
firedns_add_query(void)
{
  struct s_connection *s;

  /* create new connection object */
  s = xcalloc(sizeof(*s));

  /* verified by firedns_getresult() */
  s->id[0] = rand() % 255;
  s->id[1] = rand() % 255;
  s->fd = -1;

  return s;
}

static int
firedns_doquery(struct s_connection *s)
{
  struct s_header h;

  int len = firedns_build_query_payload(s->lookup, s->type, 1, (unsigned char *)&h.payload);
  if (len == -1)
  {
    firedns_errno = FDNS_ERR_FORMAT;
    return -1;
  }

  return firedns_send_requests(&h, s, len);
}

/*
 * populate payload with query: name= question, rr= record type
 */
static int
firedns_build_query_payload(const char *const name, uint16_t rr, uint16_t class,
                            unsigned char *payload)
{
  int16_t payloadpos = 0;
  const char *tempchr, *tempchr2;
  uint16_t l;

  tempchr2 = name;

  /* split name up into labels, create query */
  while ((tempchr = strchr(tempchr2, '.')))
  {
    l = tempchr - tempchr2;

    if (payloadpos + l + 1 > 507)
      return -1;

    payload[payloadpos++] = l;
    memcpy(&payload[payloadpos], tempchr2, l);
    payloadpos += l;
    tempchr2 = &tempchr[1];
  }

  l = strlen(tempchr2);

  if (l)
  {
    if (payloadpos + l + 2 > 507)
      return -1;

    payload[payloadpos++] = l;
    memcpy(&payload[payloadpos], tempchr2, l);
    payloadpos += l;
    payload[payloadpos++] = '\0';
  }

  if (payloadpos > 508)
    return -1;

  l = htons(rr);
  memcpy(&payload[payloadpos], &l, 2);

  l = htons(class);
  memcpy(&payload[payloadpos + 2], &l, 2);

  return payloadpos + 4;
}

/* send DNS query */
static int
firedns_send_requests(struct s_header *h, struct s_connection *s, int l)
{
  int sent_ok = 0;
  struct sockaddr_in addr4;
  struct sockaddr_in6 addr6;
  unsigned char payload[sizeof(struct s_header)];

  /* set header flags */
  h->id[0] = s->id[0];
  h->id[1] = s->id[1];
  h->flags1 = 0 | FLAGS1_MASK_RD;
  h->flags2 = 0;
  h->qdcount = 1;
  h->ancount = 0;
  h->nscount = 0;
  h->arcount = 0;

  /* try to create ipv6 or ipv4 socket */
  s->v6 = 0;

  if (i6 > 0)
  {
    s->fd = socket(AF_INET6, SOCK_DGRAM, 0);

    if (s->fd != -1)
    {
      if (fcntl(s->fd, F_SETFL, O_NONBLOCK))
      {
        close(s->fd);
        s->fd = -1;
      }
    }

    if (s->fd != -1)
    {
      memset(&addr6, 0, sizeof(addr6));
      addr6.sin6_family = AF_INET6;

      if (bind(s->fd, (struct sockaddr *)&addr6, sizeof(addr6)) == 0)
        s->v6 = 1;
      else
        close(s->fd);
    }
  }

  if (s->v6 == 0)
  {
    s->fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (s->fd != -1)
    {
      if (fcntl(s->fd, F_SETFL, O_NONBLOCK))
      {
        close(s->fd);
        s->fd = -1;
      }
    }

    if (s->fd != -1)
    {
      memset(&addr4, 0, sizeof(addr4));
      addr4.sin_family = AF_INET;
      addr4.sin_port = 0;
      addr4.sin_addr.s_addr = INADDR_ANY;

      if (bind(s->fd, (struct sockaddr *)&addr4, sizeof(addr4)) != 0)
      {
        close(s->fd);
        s->fd = -1;
      }
    }

    if (s->fd == -1)
    {
      firedns_errno = FDNS_ERR_NETWORK;
      return -1;
    }
  }

  firedns_empty_header(payload, h, l);

  /* if we've got ipv6 support, an ip v6 socket, and ipv6 servers, send to them */
  if (i6 > 0 && s->v6 == 1)
  {
    for (unsigned int i = 0; i < i6; ++i)
    {
      memset(&addr6, 0, sizeof(addr6));
      memcpy(&addr6.sin6_addr, &servers6[i], sizeof(addr6.sin6_addr));

      addr6.sin6_family = AF_INET6;
      addr6.sin6_port = htons(FDNS_PORT);

      if (sendto(s->fd, payload, l + 12, 0, (struct sockaddr *)&addr6, sizeof(addr6)) > 0)
        sent_ok = 1;
    }
  }

  for (unsigned int i = 0; i < i4; ++i)
  {
    /* send via ipv4-over-ipv6 if we've got an ipv6 socket */
    if (s->v6 == 1)
    {
      memset(&addr6, 0, sizeof(addr6));
      memcpy(addr6.sin6_addr.s6_addr, "\0\0\0\0\0\0\0\0\0\0\xff\xff", 12);
      memcpy(&addr6.sin6_addr.s6_addr[12], &servers4[i].s_addr, 4);
      addr6.sin6_family = AF_INET6;
      addr6.sin6_port = htons(FDNS_PORT);

      if (sendto(s->fd, payload, l + 12, 0, (struct sockaddr *)&addr6, sizeof(addr6)) > 0)
        sent_ok = 1;

      continue;
    }

    /* otherwise send via standard ipv4 boringness */
    memset(&addr4, 0, sizeof(addr4));
    memcpy(&addr4.sin_addr, &servers4[i], sizeof(addr4.sin_addr));
    addr4.sin_family = AF_INET;
    addr4.sin_port = htons(FDNS_PORT);

    if (sendto(s->fd, payload, l + 12, 0, (struct sockaddr *)&addr4, sizeof(addr4)) > 0)
      sent_ok = 1;
  }

  if (!sent_ok)
  {
    close(s->fd);
    s->fd = -1;
    firedns_errno = FDNS_ERR_NETWORK;
    return -1;
  }

  time(&s->start);
  firedns_fdinuse++;
  firedns_errno = FDNS_ERR_NONE;

  return s->fd;
}

/* retrieve result of DNS query */
struct firedns_result *
firedns_getresult(const int fd)
{
  static struct firedns_result result;
  struct s_header h;
  struct s_connection *c;
  node_t *node;
  int l, i, q, curanswer;
  struct s_rr_middle rr = { .rdlength = 0 };
  unsigned char buffer[sizeof(struct s_header)];

  firedns_errno = FDNS_ERR_OTHER;
  result.info = NULL;

  memset(result.text, 0, sizeof(result.text));

  /* Find query in list of dns lookups */
  LIST_FOREACH(node, CONNECTIONS.head)
  {
    c = node->data;

    if (c->fd == fd)
      break;
    else
      c = NULL;
  }

  /* query not found */
  if (c == NULL)
    return &result;

  /* query found -- we remove in cleanup */
  l = recv(c->fd, buffer, sizeof(struct s_header), 0);

  result.info = c->info;
  strlcpy(result.lookup, c->lookup, sizeof(result.lookup));

  if (l == -1)
  {
    firedns_errno = FDNS_ERR_NETWORK;
    goto cleanup;
  }

  if (l < 12)
    goto cleanup;

  firedns_fill_header(&h, buffer, l - 12);

  if (c->id[0] != h.id[0] || c->id[1] != h.id[1])
    /*
     * ID mismatch: we keep the connection, as this could be an answer to
     * a previous lookup..
     */
    return NULL;

  if ((h.flags1 & FLAGS1_MASK_QR) == 0)
    goto cleanup;

  if ((h.flags1 & FLAGS1_MASK_OPCODE) != 0)
    goto cleanup;

  if ((h.flags2 & FLAGS2_MASK_RCODE) != 0)
  {
    firedns_errno = (h.flags2 & FLAGS2_MASK_RCODE);
    goto cleanup;
  }

  if (h.ancount < 1)
  {
    firedns_errno = FDNS_ERR_NXDOMAIN;
    /* no sense going on if we don't have any answers */
    goto cleanup;
  }

  /* skip queries */
  i = 0;
  q = 0;
  l -= 12;

  while (q < h.qdcount && i < l)
  {
    if (h.payload[i] > 63)
    {
      /* pointer */
      i += 6;  /* skip pointer, class and type */
      q++;
    }
    else
    {
      /* label */
      if (h.payload[i] == 0)
      {
        q++;
        i += 5;  /* skip nil, class and type */
      }
      else
        i += h.payload[i] + 1;  /* skip length and label */
    }
  }

  /* &h.payload[i] should now be the start of the first response */
  curanswer = 0;

  while (curanswer < h.ancount)
  {
    q = 0;

    while (q == 0 && i < l)
    {
      if (h.payload[i] > 63)
      {
        /* pointer */
        i += 2;  /* skip pointer */
        q = 1;
      }
      else
      {
        /* label */
        if (h.payload[i] == 0)
        {
          i++;
          q = 1;
        }
        else
          i += h.payload[i] + 1;  /* skip length and label */
      }
    }

    if (l - i < 10)
      goto cleanup;

    firedns_fill_rr(&rr, &h.payload[i]);
    i += 10;

    if (rr.type != c->type)
    {
      curanswer++;
      i += rr.rdlength;
      continue;
    }

    if (rr.class != c->class)
    {
      curanswer++;
      i += rr.rdlength;
      continue;
    }

    break;
  }

  if (curanswer == h.ancount)
    goto cleanup;
  if (i + rr.rdlength > l)
    goto cleanup;
  if (rr.rdlength > 1023)
    goto cleanup;

  firedns_errno = FDNS_ERR_NONE;
  memcpy(result.text, &h.payload[i], rr.rdlength);
  result.text[rr.rdlength] = '\0';

/* Clean-up */
cleanup:
  list_remove(&CONNECTIONS, node);
  node_free(node);

  close(c->fd);
  xfree(c);
  firedns_fdinuse--;

  return &result;
}

void
firedns_cycle(void)
{
  node_t *node, *node_next;
  struct s_connection *p;
  struct firedns_result *res, new_result;
  static struct pollfd *ufds = NULL;
  unsigned int size = 0;
  time_t timenow;

  if (LIST_SIZE(&CONNECTIONS) == 0)
    return;

  if (ufds == NULL)
    ufds = xcalloc(sizeof(*ufds) * OptionsItem->dns_fdlimit);

  time(&timenow);

  LIST_FOREACH_SAFE(node, node_next, CONNECTIONS.head)
  {
    if (size >= OptionsItem->dns_fdlimit)
      break;

    p = node->data;

    if (p->fd < 0)
      continue;

    if (p->fd > 0 && (p->start + OptionsItem->dns_timeout) < timenow)
    {
      /* Timed out - remove from list */
      list_remove(&CONNECTIONS, node);
      node_free(node);

      memset(new_result.text, 0, sizeof(new_result.text));
      new_result.info = p->info;
      strlcpy(new_result.lookup, p->lookup, sizeof(new_result.lookup));

      close(p->fd);
      xfree(p);
      firedns_fdinuse--;

      firedns_errno = FDNS_ERR_TIMEOUT;

      if (new_result.info)
        dnsbl_result(&new_result);

      continue;
    }

    ufds[size].events = 0;
    ufds[size].revents = 0;
    ufds[size].fd = p->fd;
    ufds[size].events = POLLIN;

    ++size;
  }

  switch (poll(ufds, size, 0))
  {
    case -1:
    case  0:
      return;
  }

  LIST_FOREACH_SAFE(node, node_next, CONNECTIONS.head)
  {
    p = node->data;

    if (p->fd > 0)
    {
      for (unsigned int i = 0; i < size; ++i)
      {
        if ((ufds[i].revents & POLLIN) && ufds[i].fd == p->fd)
        {
          res = firedns_getresult(p->fd);

          if (res && res->info)
            dnsbl_result(res);

          break;
        }
      }
    }
    else if (firedns_fdinuse < OptionsItem->dns_fdlimit)
      firedns_doquery(p);
  }
}

const char *
firedns_strerror(int error)
{
  if (error == FDNS_ERR_NETWORK)
    return strerror(errno);

  return errors[error];
}
