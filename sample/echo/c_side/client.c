#include <poll.h>
#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>

struct nm_desc *nm_desc;

char *ip_ntoa2(u_char *d){
  static char str[15];
  sprintf(str,"%d.%d.%d.%d",d[0],d[1],d[2],d[3]);
  return str;
}

int get_mtu() {
  struct ifreq *ifr;

  strncpy(ifr->ifr_name, "ix1", IFNAMSIZ-1);

  if (ioctl(nm_desc->fd, SIOCGIFMTU, ifr) != 0) {
    perror("ioctl");
    return 1;
  }

  return ifr->ifr_mtu;
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

void swapto(int to_hostring, struct netmap_slot *rxslot) {
  struct netmap_ring *txring;
  int i, first, last;
  uint32_t t, cur;

  if (to_hostring) {
    fprintf(stderr, "NIC to HOST\n");
    //txring -> host ring
    first = last = nm_desc->last_tx_ring;
  } else {
    fprintf(stderr, "HOST to NIC\n");
    first = nm_desc->first_tx_ring;
    last = nm_desc->last_tx_ring - 1;
  }

  for (i = first; i <= last; ++i) {
    txring = NETMAP_TXRING(nm_desc->nifp, i);
    while(!nm_ring_empty(txring)) {
      cur = txring->cur;

      t = txring->slot[cur].buf_idx;
      txring->slot[cur].buf_idx = rxslot->buf_idx;
      rxslot->buf_idx = t;

      txring->slot[cur].len = rxslot->len;

      txring->slot[cur].flags |= NS_BUF_CHANGED;
      rxslot->flags |= NS_BUF_CHANGED;

      txring->head = txring->cur = nm_ring_next(txring, cur);

      break;
    }
  }
}

int main(int argc, char* argv[]) {
  int pktsizelen, pkthdrlen;
  unsigned int cur, i, is_hostring;
  char *pkt, *buf, *tbuf, *data, *payload;
  struct in_addr src, dst;
  char r_src[32];
  char r_dst[32];
  struct pollfd pollfd[1];
  struct netmap_ring *txring, *rxring;
  struct ether_header *ether;
  struct ether_arp *arp;
  struct ip *ip;
  struct udphdr *udp;

  src.s_addr = inet_addr("10.2.2.2");
  dst.s_addr = inet_addr("10.2.2.3");

  nm_desc = nm_open("netmap:ix1*", NULL, 0, NULL);
  printf("INPUT: ");
  scanf("%s", data);

  if(strlen(data) >= 512) {
    printf("error. over size\n");
  }

  txring = NETMAP_TXRING(nm_desc->nifp, nm_desc->first_tx_ring);
  pkthdrlen = sizeof(struct ether_header) + sizeof(struct ip) + sizeof(struct udphdr);
  pktsizelen = pkthdrlen + strlen(data);

  if((pkt = malloc(pktsizelen)) == NULL) {
    perror("malloc");
  }

  create_etherhdr(pkt);
  create_iphdr(pkt, &src, &dst, pktsizelen);
  create_udphdr(pkt, 12345, data);

  while(1) {
    pollfd[0].fd = nm_desc->fd;
    pollfd[0].events = POLLOUT;
    poll(pollfd, 1, -1);


    if(pollfd[0].revents & POLLOUT) {
      tbuf = NETMAP_BUF(txring, txring->slot[txring->cur].buf_idx);
      nm_pkt_copy(pkt, tbuf, pktsizelen);
      txring->slot[cur].len = pktsizelen;
      txring->slot[cur].flags |= NS_BUF_CHANGED;
      free(pkt);
      break;
    }
  }

  while(1) {
    pollfd[0].fd = nm_desc->fd;
    pollfd[0].events = POLLIN;
    poll(pollfd, 1, 100);

    for (i = nm_desc->first_rx_ring; i <= nm_desc->last_rx_ring; i++) {

      is_hostring = (i == nm_desc->last_rx_ring);

      rxring = NETMAP_RXRING(nm_desc->nifp, i);

      while(!nm_ring_empty(rxring)) {
        cur = rxring->cur;
        buf = NETMAP_BUF(rxring, rxring->slot[cur].buf_idx);
        ether = (struct ether_header *)buf;
        if(ntohs(ether->ether_type) == ETHERTYPE_ARP) {
          printf("This is ARP.\n");
          arp = (struct ether_arp *)(buf + sizeof(struct ether_header));

          printf("arp_spa=%s\n",ip_ntoa2(arp->arp_spa));
          printf("arp_tpa=%s\n",ip_ntoa2(arp->arp_tpa));

          swapto(!is_hostring, &rxring->slot[cur]);
          rxring->head = rxring->cur = nm_ring_next(rxring, cur);
          continue;
        }
        ip = (struct ip *)(buf + sizeof(struct ether_header));
        payload = (char *)ip + (ip->ip_hl<<2);

        if (ip->ip_p == IPPROTO_UDP) {
          udp = (struct udphdr *)payload;
          printf("UDP Src port: %u\n", ntohs(udp->uh_sport));
          printf("UDP Dst port: %u\n", ntohs(udp->uh_dport));
          printf("Recieved: %s\n", payload + sizeof(struct udphdr *));
        }

        swapto(!is_hostring, &rxring->slot[cur]);
        rxring->head = rxring->cur = nm_ring_next(rxring, cur);
      }
    }
    if (ioctl(nm_desc->fd, NIOCRXSYNC, NULL) != 0)
      perror("sync ioctl");
  }
}
