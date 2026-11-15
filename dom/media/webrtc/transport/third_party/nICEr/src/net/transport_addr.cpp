/*
Copyright (c) 2007, Adobe Systems, Incorporated
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

* Neither the name of Adobe Systems, Network Resonance nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <csi_platform.h>
#include <stdio.h>
#include <memory.h>
#include <sys/types.h>
#include <errno.h>
#ifdef WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <assert.h>
#include "nr_api.h"
#include "util.h"
#include "transport_addr.h"

#include <errno.h>
#ifdef WIN32
#include <Ws2ipdef.h>
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT            WSAEAFNOSUPPORT
#endif
#else
#include <sys/socket.h>
#endif
#define INET6

/* inet_ntop implementation from NetBSD */

/*
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1996-1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#if !defined(NS_INADDRSZ)
# define NS_INADDRSZ  4
#endif
#if !defined(NS_IN6ADDRSZ)
# define NS_IN6ADDRSZ  16
#endif
#if !defined(NS_INT16SZ)
# define NS_INT16SZ  2
#endif

/*
 * WARNING: Don't even consider trying to compile this on a system where
 * sizeof(int) < 4.  sizeof(int) > 4 is fine; all the world's not a VAX.
 */

static const char *inet_ntop4(const unsigned char *src, char *dst, size_t size);
#ifdef INET6
static const char *inet_ntop6(const unsigned char *src, char *dst, size_t size);
#endif /* INET6 */

/* char *
 * inet_ntop(af, src, dst, size)
 *  convert a network format address to presentation format.
 * return:
 *  pointer to presentation format address (`dst'), or NULL (see errno).
 * author:
 *  Paul Vixie, 1996.
 */
const char *
inet_ntop(int af, const void *src, char *dst, size_t size)
{

  switch (af) {
  case AF_INET:
    return (inet_ntop4(static_cast<const unsigned char*>(src), dst, size));
#ifdef INET6
  case AF_INET6:
    return (inet_ntop6(static_cast<const unsigned char*>(src), dst, size));
#endif /* INET6 */
  default:
    errno = EAFNOSUPPORT;
    return (NULL);
  }
  /* NOTREACHED */
}

/* const char *
 * inet_ntop4(src, dst, size)
 *  format an IPv4 address, more or less like inet_ntoa()
 * return:
 *  `dst' (as a const)
 * notes:
 *  (1) uses no statics
 *  (2) takes a unsigned char* not an in_addr as input
 * author:
 *  Paul Vixie, 1996.
 */
static const char *
inet_ntop4(const unsigned char *src, char *dst, size_t size)
{
  char tmp[sizeof "255.255.255.255"];
  int l;

  l = snprintf(tmp, sizeof(tmp), "%u.%u.%u.%u",
      src[0], src[1], src[2], src[3]);
  if (l <= 0 || (size_t) l >= size) {
    errno = ENOSPC;
    return (NULL);
  }
  strlcpy(dst, tmp, size);
  return (dst);
}

#ifdef INET6
/* const char *
 * inet_ntop6(src, dst, size)
 *  convert IPv6 binary address into presentation (printable) format
 * author:
 *  Paul Vixie, 1996.
 */
static const char *
inet_ntop6(const unsigned char *src, char *dst, size_t size)
{
  /*
   * Note that int32_t and int16_t need only be "at least" large enough
   * to contain a value of the specified size.  On some systems, like
   * Crays, there is no such thing as an integer variable with 16 bits.
   * Keep this in mind if you think this function should have been coded
   * to use pointer overlays.  All the world's not a VAX.
   */
  char tmp[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255"];
  char *tp, *ep;
  struct { int base, len; } best, cur;
  unsigned int words[NS_IN6ADDRSZ / NS_INT16SZ];
  int i;
  int advance;

  /*
   * Preprocess:
   *  Copy the input (bytewise) array into a wordwise array.
   *  Find the longest run of 0x00's in src[] for :: shorthanding.
   */
  memset(words, '\0', sizeof words);
  for (i = 0; i < NS_IN6ADDRSZ; i++)
    words[i / 2] |= (src[i] << ((1 - (i % 2)) << 3));
  best.base = -1;
  cur.base = -1;
  best.len = -1;  /* XXX gcc */
  cur.len = -1;  /* XXX gcc */
  for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++) {
    if (words[i] == 0) {
      if (cur.base == -1)
        cur.base = i, cur.len = 1;
      else
        cur.len++;
    } else {
      if (cur.base != -1) {
        if (best.base == -1 || cur.len > best.len)
          best = cur;
        cur.base = -1;
      }
    }
  }
  if (cur.base != -1) {
    if (best.base == -1 || cur.len > best.len)
      best = cur;
  }
  if (best.base != -1 && best.len < 2)
    best.base = -1;

  /*
   * Format the result.
   */
  tp = tmp;
  ep = tmp + sizeof(tmp);
  for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++) {
    /* Are we inside the best run of 0x00's? */
    if (best.base != -1 && i >= best.base &&
        i < (best.base + best.len)) {
      if (i == best.base)
        *tp++ = ':';
      continue;
    }
    /* Are we following an initial run of 0x00s or any real hex? */
    if (i != 0) {
      if (tp + 1 >= ep)
        return (NULL);
      *tp++ = ':';
    }
    /* Is this address an encapsulated IPv4? */
    if (i == 6 && best.base == 0 &&
        (best.len == 6 ||
        (best.len == 7 && words[7] != 0x0001) ||
        (best.len == 5 && words[5] == 0xffff))) {
      if (!inet_ntop4(src+12, tp, (size_t)(ep - tp)))
        return (NULL);
      tp += strlen(tp);
      break;
    }
    advance = snprintf(tp, (size_t)(ep - tp), "%x", words[i]);
    if (advance <= 0 || advance >= ep - tp)
      return (NULL);
    tp += advance;
  }
  /* Was it a trailing run of 0x00's? */
  if (best.base != -1 && (best.base + best.len) ==
      (NS_IN6ADDRSZ / NS_INT16SZ)) {
    if (tp + 1 >= ep)
      return (NULL);
    *tp++ = ':';
  }
  if (tp + 1 >= ep)
    return (NULL);
  *tp++ = '\0';

  /*
   * Check for overflow, copy, and we're done.
   */
  if ((size_t)(tp - tmp) > size) {
    errno = ENOSPC;
    return (NULL);
  }
  strlcpy(dst, tmp, size);
  return (dst);
}
#endif /* INET6 */

#ifdef WIN32
/* Not exactly, will forgive stuff like <addr>:<port> */
int inet_pton(int af, const char *src, void *dst)
{
  struct sockaddr_storage ss;
  int addrlen = sizeof(ss);

  if (af != AF_INET && af != AF_INET6) {
    return -1;
  }

  if (!WSAStringToAddressA((char *)src, af, NULL, (struct sockaddr*)&ss, &addrlen)) {
    if (af == AF_INET) {
      struct sockaddr_in *in = (struct sockaddr_in*)&ss;
      memcpy(dst, &in->sin_addr, sizeof(struct in_addr));
    } else {
      struct sockaddr_in6 *in6 = (struct sockaddr_in6*)&ss;
      memcpy(dst, &in6->sin6_addr, sizeof(struct in6_addr));
    }
    return 1;
  }
  return 0;
}
#endif /* WIN32 */

int nr_transport_addr_fmt_addr_string(nr_transport_addr *addr)
  {
    int _status;
    /* Max length for normalized IPv6 address string representation is 39 */
    char buffer[40];
    const char *protocol;

    switch(addr->protocol){
      case IPPROTO_TCP:
        if (addr->tls) {
          protocol = "TLS";
        } else {
          protocol = "TCP";
        }
        break;
      case IPPROTO_UDP:
        protocol = "UDP";
        break;
      default:
        ABORT(R_INTERNAL);
    }

    switch(addr->ip_version){
      case NR_IPV4:
        if (!inet_ntop(AF_INET, &addr->u.addr4.sin_addr,buffer,sizeof(buffer)))
          strcpy(buffer, "[error]");
        snprintf(addr->as_string,sizeof(addr->as_string),"IP4:%s:%d/%s",buffer,(int)ntohs(addr->u.addr4.sin_port),protocol);
        break;
      case NR_IPV6:
        if (!inet_ntop(AF_INET6, &addr->u.addr6.sin6_addr,buffer,sizeof(buffer)))
          strcpy(buffer, "[error]");
        snprintf(addr->as_string,sizeof(addr->as_string),"IP6:[%s]:%d/%s",buffer,(int)ntohs(addr->u.addr6.sin6_port),protocol);
        break;
      default:
        ABORT(R_INTERNAL);
    }

    _status=0;
  abort:
    return(_status);
  }

int nr_transport_addr_fmt_ifname_addr_string(const nr_transport_addr *addr, char *buf, int len)
  {
    int _status;
    /* leave room for a fully-expanded IPV4-mapped IPV6 address */
    char buffer[46];

    switch(addr->ip_version){
      case NR_IPV4:
        if (!inet_ntop(AF_INET, &addr->u.addr4.sin_addr,buffer,sizeof(buffer))) {
           strncpy(buffer, "[error]", sizeof(buffer));
        }
        break;
      case NR_IPV6:
        if (!inet_ntop(AF_INET6, &addr->u.addr6.sin6_addr,buffer,sizeof(buffer))) {
           strncpy(buffer, "[error]", sizeof(buffer));
        }
        break;
      default:
        ABORT(R_INTERNAL);
    }
    buffer[sizeof(buffer) - 1] = '\0';

    snprintf(buf,len,"%s:%s",addr->ifname,buffer);
    buf[len - 1] = '\0';

    _status=0;
  abort:
    return(_status);
  }

int nr_sockaddr_to_transport_addr(struct sockaddr *saddr, int protocol, int keep, nr_transport_addr *addr)
  {
    int r,_status;

    if(!keep) memset(addr,0,sizeof(nr_transport_addr));

    switch(protocol){
      case IPPROTO_TCP:
      case IPPROTO_UDP:
        break;
      default:
        ABORT(R_BAD_ARGS);
    }

    addr->protocol=protocol;

    if(saddr->sa_family==AF_INET){
      addr->ip_version=NR_IPV4;

      memcpy(&addr->u.addr4,saddr,sizeof(struct sockaddr_in));
    }
    else if(saddr->sa_family==AF_INET6){
      addr->ip_version=NR_IPV6;

      memcpy(&addr->u.addr6, saddr, sizeof(struct sockaddr_in6));
    }
    else
      ABORT(R_BAD_ARGS);

    if(r=nr_transport_addr_fmt_addr_string(addr))
      ABORT(r);

    _status=0;
  abort:
    return(_status);
  }


int nr_transport_addr_copy(nr_transport_addr *to, const nr_transport_addr *from)
  {
    memcpy(to,from,sizeof(nr_transport_addr));
    return 0;
  }

int nr_transport_addr_copy_keep_ifname(nr_transport_addr *to, const nr_transport_addr *from)
  {
    int r,_status;
    char save_ifname[MAXIFNAME];

    strncpy(save_ifname, to->ifname, MAXIFNAME);
    save_ifname[MAXIFNAME-1]=0;  /* Ensure null termination */

    if (r=nr_transport_addr_copy(to, from))
      ABORT(r);

    strncpy(to->ifname, save_ifname, MAXIFNAME);

    if (r=nr_transport_addr_fmt_addr_string(to))
      ABORT(r);

    _status=0;
 abort:
    return _status;
  }

int nr_transport_addr_copy_addrport(nr_transport_addr *to, const nr_transport_addr *from)
  {
    int r,_status;

    switch (from->ip_version) {
      case NR_IPV4:
        memcpy(&to->u.addr4, &from->u.addr4, sizeof(to->u.addr4));
        break;
      case NR_IPV6:
        memcpy(&to->u.addr6, &from->u.addr6, sizeof(to->u.addr6));
        break;
      default:
        ABORT(R_BAD_ARGS);
    }

    to->ip_version = from->ip_version;

    if (r=nr_transport_addr_fmt_addr_string(to)) {
      ABORT(r);
    }

    _status=0;
 abort:
    return _status;
  }

/* Convenience fxn. Is this the right API?*/
int nr_ip4_port_to_transport_addr(UINT4 ip4, UINT2 port, int protocol, nr_transport_addr *addr)
  {
    int r,_status;

    memset(addr, 0, sizeof(nr_transport_addr));

    addr->ip_version=NR_IPV4;
    addr->protocol=protocol;
#ifdef HAVE_SIN_LEN
    addr->u.addr4.sin_len=sizeof(struct sockaddr_in);
#endif
    addr->u.addr4.sin_family=PF_INET;
    addr->u.addr4.sin_port=htons(port);
    addr->u.addr4.sin_addr.s_addr=htonl(ip4);

    if(r=nr_transport_addr_fmt_addr_string(addr))
      ABORT(r);

    _status=0;
  abort:
    return(_status);
  }

int nr_str_port_to_transport_addr(const char *ip, UINT2 port, int protocol, nr_transport_addr *addr_out)
  {
    int r,_status;
    struct in_addr addr;
    struct in6_addr addr6;

    if (inet_pton(AF_INET, ip, &addr) == 1) {
      if(r=nr_ip4_port_to_transport_addr(ntohl(addr.s_addr),port,protocol,addr_out))
        ABORT(r);
    } else if (inet_pton(AF_INET6, ip, &addr6) == 1) {
      if(r=nr_ip6_port_to_transport_addr(&addr6,port,protocol,addr_out))
        ABORT(r);
    } else {
      ABORT(R_BAD_DATA);
    }

    _status=0;
  abort:
    return(_status);
  }

int nr_ip6_port_to_transport_addr(struct in6_addr* addr6, UINT2 port, int protocol, nr_transport_addr *addr)
  {
    int r,_status;

    memset(addr, 0, sizeof(nr_transport_addr));

    addr->ip_version=NR_IPV6;
    addr->protocol=protocol;
    addr->u.addr6.sin6_family=PF_INET6;
    addr->u.addr6.sin6_port=htons(port);
    memcpy(addr->u.addr6.sin6_addr.s6_addr, addr6->s6_addr, sizeof(addr6->s6_addr));

    if(r=nr_transport_addr_fmt_addr_string(addr))
      ABORT(r);

    _status=0;
  abort:
    return(_status);
  }

int nr_transport_addr_get_addrstring(const nr_transport_addr *addr, char *str, int maxlen)
  {
    int _status;

    if (addr->fqdn[0]) {
      if (maxlen <= static_cast<int>(strlen(addr->fqdn))) {
        ABORT(R_BAD_ARGS);
      }
      strncpy(str, addr->fqdn, maxlen);
    } else {
      const char* res;
      switch (addr->ip_version) {
        case NR_IPV4:
          res = inet_ntop(AF_INET, &addr->u.addr4.sin_addr, str, maxlen);
          break;
        case NR_IPV6:
          res = inet_ntop(AF_INET6, &addr->u.addr6.sin6_addr, str, maxlen);
          break;
        default:
          ABORT(R_INTERNAL);
      }

      if (!res) {
        if (errno == ENOSPC) {
          ABORT(R_BAD_ARGS);
        }
        ABORT(R_INTERNAL);
      }
    }

    _status=0;
  abort:
    return(_status);
  }

int nr_transport_addr_get_port(const nr_transport_addr *addr, int *port)
  {
    int _status;

    switch(addr->ip_version){
      case NR_IPV4:
        *port=ntohs(addr->u.addr4.sin_port);
        break;
      case NR_IPV6:
        *port=ntohs(addr->u.addr6.sin6_port);
        break;
      default:
        ABORT(R_INTERNAL);
    }

    _status=0;
  abort:
    return(_status);
  }

int nr_transport_addr_set_port(nr_transport_addr *addr, int port)
  {
    int _status;

    switch(addr->ip_version){
      case NR_IPV4:
        addr->u.addr4.sin_port=htons(port);
        break;
      case NR_IPV6:
        addr->u.addr6.sin6_port=htons(port);
        break;
      default:
        ABORT(R_INTERNAL);
    }

    _status=0;
  abort:
    return(_status);
  }

/* memcmp() may not work if, for instance, the string or interface
   haven't been made. Hmmm.. */
int nr_transport_addr_cmp(const nr_transport_addr *addr1,const nr_transport_addr *addr2,int mode)
  {
    assert(mode);

    if(addr1->ip_version != addr2->ip_version)
      return(1);

    if(mode < NR_TRANSPORT_ADDR_CMP_MODE_PROTOCOL)
      return(0);

    if(addr1->protocol != addr2->protocol)
      return(1);

    if(mode < NR_TRANSPORT_ADDR_CMP_MODE_ADDR)
      return(0);

    switch(addr1->ip_version){
      case NR_IPV4:
        if(addr1->u.addr4.sin_addr.s_addr != addr2->u.addr4.sin_addr.s_addr)
          return(1);
        if(mode < NR_TRANSPORT_ADDR_CMP_MODE_ALL)
          return(0);
        if(addr1->u.addr4.sin_port != addr2->u.addr4.sin_port)
          return(1);
        break;
      case NR_IPV6:
        if(memcmp(addr1->u.addr6.sin6_addr.s6_addr,addr2->u.addr6.sin6_addr.s6_addr,sizeof(struct in6_addr)))
          return(1);
        if(mode < NR_TRANSPORT_ADDR_CMP_MODE_ALL)
          return(0);
        if(addr1->u.addr6.sin6_port != addr2->u.addr6.sin6_port)
          return(1);
        break;
      default:
        abort();
    }

    return(0);
  }

int nr_transport_addr_is_loopback(const nr_transport_addr *addr)
  {
    switch(addr->ip_version){
      case NR_IPV4:
        switch(addr->u.addr4.sin_family){
          case AF_INET:
            if (((ntohl(addr->u.addr4.sin_addr.s_addr)>>24)&0xff)==0x7f)
              return 1;
            break;
          default:
            NR_UNIMPLEMENTED;
            break;
        }
        break;

      case NR_IPV6:
        if(!memcmp(addr->u.addr6.sin6_addr.s6_addr,in6addr_loopback.s6_addr,sizeof(struct in6_addr)))
          return(1);
        break;
      default:
        NR_UNIMPLEMENTED;
    }

    return(0);
  }

int nr_transport_addr_is_link_local(const nr_transport_addr *addr)
  {
    switch(addr->ip_version){
      case NR_IPV4:
        /* RFC3927: 169.254/16 */
        if ((ntohl(addr->u.addr4.sin_addr.s_addr) & 0xFFFF0000) == 0xA9FE0000)
          return(1);
        break;
      case NR_IPV6:
        {
          UINT4* addrTop = (UINT4*)(addr->u.addr6.sin6_addr.s6_addr);
          if ((*addrTop & htonl(0xFFC00000)) == htonl(0xFE800000))
            return(2);
        }
        break;
      default:
        NR_UNIMPLEMENTED;
    }

    return(0);
  }

int nr_transport_addr_is_mac_based(const nr_transport_addr *addr)
  {
    switch(addr->ip_version){
      case NR_IPV4:
        // IPv4 has no MAC based self assigned IP addresses
        return(0);
      case NR_IPV6:
        {
          // RFC 2373, Appendix A: lower 64bit 0x020000FFFE000000
          // indicates a MAC based IPv6 address
          UINT4* macCom = (UINT4*)(addr->u.addr6.sin6_addr.s6_addr + 8);
          UINT4* macExt = (UINT4*)(addr->u.addr6.sin6_addr.s6_addr + 12);
          if ((*macCom & htonl(0x020000FF)) == htonl(0x020000FF) &&
              (*macExt & htonl(0xFF000000)) == htonl(0xFE000000)) {
            return(1);
          }
        }
        break;
      default:
        NR_UNIMPLEMENTED;
    }
    return(0);
  }

int nr_transport_addr_is_teredo(const nr_transport_addr *addr)
  {
    switch(addr->ip_version){
      case NR_IPV4:
        return(0);
      case NR_IPV6:
        {
          UINT4* addrTop = (UINT4*)(addr->u.addr6.sin6_addr.s6_addr);
          if ((*addrTop & htonl(0xFFFFFFFF)) == htonl(0x20010000))
            return(1);
        }
        break;
      default:
        NR_UNIMPLEMENTED;
    }

    return(0);
  }

int nr_transport_addr_check_compatibility(const nr_transport_addr *addr1, const nr_transport_addr *addr2)
  {
    // first make sure we're comparing the same ip versions and protocols
    if ((addr1->ip_version != addr2->ip_version) ||
        (addr1->protocol != addr2->protocol)) {
      return(1);
    }

    if (!addr1->fqdn[0] && !addr2->fqdn[0]) {
      // now make sure the link local status matches
      if (nr_transport_addr_is_link_local(addr1) !=
          nr_transport_addr_is_link_local(addr2)) {
        return(1);
      }
    }
    return(0);
  }

int nr_transport_addr_is_wildcard(const nr_transport_addr *addr)
  {
    switch(addr->ip_version){
      case NR_IPV4:
        if(addr->u.addr4.sin_addr.s_addr==INADDR_ANY)
          return(1);
        if(addr->u.addr4.sin_port==0)
          return(1);
        break;
      case NR_IPV6:
        if(!memcmp(addr->u.addr6.sin6_addr.s6_addr,in6addr_any.s6_addr,sizeof(struct in6_addr)))
          return(1);
        if(addr->u.addr6.sin6_port==0)
          return(1);
        break;
      default:
        NR_UNIMPLEMENTED;
    }

    return(0);
  }

nr_transport_addr_mask nr_private_ipv4_addrs[] = {
  /* RFC1918: 10/8 */
  {0x0A000000, 0xFF000000},
  /* RFC1918: 172.16/12 */
  {0xAC100000, 0xFFF00000},
  /* RFC1918: 192.168/16 */
  {0xC0A80000, 0xFFFF0000},
  /* RFC6598: 100.64/10 */
  {0x64400000, 0xFFC00000}
};

int nr_transport_addr_get_private_addr_range(const nr_transport_addr *addr)
  {
    switch(addr->ip_version){
      case NR_IPV4:
        {
          UINT4 ip = ntohl(addr->u.addr4.sin_addr.s_addr);
          for (size_t i=0; i<(sizeof(nr_private_ipv4_addrs)/sizeof(nr_transport_addr_mask)); i++) {
            if ((ip & nr_private_ipv4_addrs[i].mask) == nr_private_ipv4_addrs[i].addr)
              return i + 1;
          }
        }
        break;
      case NR_IPV6:
        return(0);
      default:
        NR_UNIMPLEMENTED;
    }

    return(0);
  }

int nr_transport_addr_is_reliable_transport(const nr_transport_addr *addr)
  {
    return addr->protocol == IPPROTO_TCP;
  }
