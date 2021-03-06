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


  static unsigned short
in_cksum(unsigned short *addr, int len)
{
  int nleft, sum;
  unsigned short *w;
  union {
    unsigned short us;
    unsigned char  uc[2];
  } last;
  unsigned short answer;

  nleft = len;
  sum = 0;
  w = addr;

  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  if (nleft == 1) {
    last.uc[0] = *(unsigned char *)w;
    last.uc[1] = 0;
    sum += last.us;
  }

  sum = (sum >> 16) + (sum & 0xffff);     /* add hi 16 to low 16 */
  sum += (sum >> 16);                     /* add carry */
  answer = ~sum;                          /* truncate to 16 bits */
  return(answer);
}

void create_etherhdr(char* buf) {
  struct ether_header *eth;

  eth = (struct ether_header *)buf;
  //90:e2:ba:92:cb:d5
  eth->ether_shost[0] = 0x90;
  eth->ether_shost[1] = 0xe2;
  eth->ether_shost[2] = 0xba;
  eth->ether_shost[3] = 0x92;
  eth->ether_shost[4] = 0xcb;
  eth->ether_shost[5] = 0xd5;
  // 90:e2:ba:5d:8f:cd
  eth->ether_dhost[0] = 0x90;
  eth->ether_dhost[1] = 0xe2;
  eth->ether_dhost[2] = 0xba;
  eth->ether_dhost[3] = 0x5d;
  eth->ether_dhost[4] = 0x8f;
  eth->ether_dhost[5] = 0xcd;

  eth->ether_type = htons(ETHERTYPE_IP);
}

void create_iphdr(char* buf, struct in_addr *src, struct in_addr *dst, size_t len) {
  struct ip *ip;

  ip = (struct ip *)(buf + sizeof(struct ether_header));
  ip->ip_v = 4;
  ip->ip_hl = 5;
  ip->ip_tos = 1;
  ip->ip_len = htons(len);
  ip->ip_id = htons(213);
  ip->ip_off = 0;
  ip->ip_ttl = 0x40;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = *src;
  ip->ip_dst = *dst;
  ip->ip_sum = 0;
  ip->ip_sum = in_cksum((unsigned short*)ip, ip->ip_hl << 2);
}

void create_udphdr(char* buf, unsigned short dport, char* data) {
  struct ip *ip;
  struct udphdr *udp;

  ip = (struct ip *)(buf + sizeof(struct ether_header));
  udp = (struct udphdr *)(buf + sizeof(struct ether_header) + (ip->ip_hl << 2));

  udp->uh_sport = htons(65001);
  udp->uh_dport = htons(dport);
  udp->uh_ulen = htons(sizeof(struct udphdr) + strlen(data));
  udp->uh_sum = 0;

  memcpy((char *)udp + sizeof(struct udphdr), data, strlen(data));
}

int main(int argc, char* argv[]) {
  int pktsizelen;
  unsigned int cur, i;
  char *pkt, *tbuf, *data, *counted_data;
  struct in_addr src, dst;
  struct netmap_ring *txring;
  struct pollfd pollfd[1];

  data = argv[1];
  counted_data = malloc(strlen(data) + 2);
  if(counted_data == NULL)
    perror("malloc");
  src.s_addr = inet_addr("10.2.2.2");
  dst.s_addr = inet_addr("10.2.2.3");

  nm_desc = nm_open("netmap:ix1", NULL, 0, NULL);
  txring = NETMAP_TXRING(nm_desc->nifp, nm_desc->first_tx_ring);
  cur = txring->cur;
  i = 0;

  while(1){
    pollfd[0].fd = nm_desc->fd;
    pollfd[0].events = POLLOUT;
    poll(pollfd, 1, -1);

    for(; i < 30 || nm_ring_empty(txring); ++i) {
      cur = txring->cur;
      tbuf = NETMAP_BUF(txring, txring->slot[cur].buf_idx);
      sprintf(counted_data, "%s%d", data, i);

      pktsizelen = sizeof(struct ether_header) + sizeof(struct ip) + sizeof(struct udphdr) + strlen(counted_data);

      pkt = malloc(pktsizelen);

      if(pkt == NULL)
        perror("malloc");

      create_etherhdr(pkt);
      create_iphdr(pkt, &src, &dst, pktsizelen - sizeof(struct ether_header));
      create_udphdr(pkt, 12345, counted_data);

      nm_pkt_copy(pkt, tbuf, pktsizelen);
      txring->slot[cur].len = pktsizelen;
      printHex(NETMAP_BUF(txring, txring->slot[cur].buf_idx), txring->slot[cur].len);
      txring->slot[cur].flags |= NS_BUF_CHANGED;

      txring->head = txring->cur = nm_ring_next(txring, cur);
      free(pkt);
    }
  }
}
