#include <poll.h>
#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <arpa/inet.h>

struct nm_desc *nm_desc;

void printHex(char* buf, size_t len) {
  int i;
  for(i = 0; i < len; ++i) {
    printf("%02X", ((unsigned char*)buf)[i]);
  }
  printf("\n");
}

void create_iphdr(char* buf, struct in_addr *src, struct in_addr *dst, size_t len) {
  struct ip *ip;

  ip = (struct ip *)buf;
  ip->ip_v = 4;
  ip->ip_hl = 5;
  ip->ip_tos = 1;
  ip->ip_len = len;
  ip->ip_id = htons(213);
  ip->ip_off = 0;
  ip->ip_ttl = 0x40;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = *src;
  ip->ip_dst = *dst;
  ip->ip_sum = 0;
}

void create_udphdr(char* buf, unsigned short dport, char* data) {
  char* u_buf;
  struct ip *ip;
  struct udphdr *udp;

  udp = (struct udphdr *)u_buf;
  udp->uh_sport = htons(65001);
  udp->uh_dport = htons(dport);
  udp->uh_ulen = htons(sizeof(struct udphdr) + strlen(data));
  udp->uh_sum = 0;

  ip = (struct ip *)buf;
  memcpy(buf + (ip->ip_hl << 2), udp, udp->uh_ulen);
}

int main(int argc, char* argv[]) {
  int sent = 0, pktsizelen;
  unsigned int cur, n, i;
  char *pkt, *tbuf, *data, *counted_data;
  struct in_addr src, dst;
  struct netmap_ring *txring;
  struct pollfd pollfd[1];
  struct ether_header *ether;
  struct ip *ip;
  struct udphdr *udp;

  data = argv[1];
  src.s_addr = inet_addr("10.2.2.3");
  dst.s_addr = inet_addr("10.2.2.3");

  nm_desc = nm_open("netmap:ix1", NULL, 0, NULL);
  for(;;){
    pollfd[0].fd = nm_desc->fd;
    pollfd[0].events = POLLOUT;
    poll(pollfd, 1, 500);

    if(!sent) {
      txring = NETMAP_TXRING(nm_desc->nifp, nm_desc->first_tx_ring);
      cur = txring->cur;

      for(i = 0; i < 30 || nm_ring_empty(txring); ++i) {
        cur = txring->cur;
        tbuf = NETMAP_BUF(txring, txring->slot[cur].buf_idx);
        sprintf(counted_data, "%s%d", data, i);

        printHex(tbuf, txring->slot[n].len);

        pktsizelen = sizeof(struct ip) + sizeof(struct udphdr) + strlen(counted_data);

        pkt = malloc(pktsizelen);

        if(pkt == NULL)
          perror("malloc");

        create_iphdr(pkt, &src, &dst, pktsizelen);
        create_udphdr(pkt, 12345, counted_data);

        printHex(pkt, pktsizelen);
        nm_pkt_copy(pkt, tbuf, pktsizelen);
        txring->slot[cur].flags |= NS_BUF_CHANGED;

        txring->head = txring->cur = nm_ring_next(txring, cur);
        free(pkt);
      }
      sent = 1;
    }
  }
}
