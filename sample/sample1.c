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

struct nm_desc *nm_desc;

char *ip_ntoa2(u_char *d){
  static char str[15];
  sprintf(str,"%d.%d.%d.%d",d[0],d[1],d[2],d[3]);
  return str;
}

void printHex(char* buf, size_t len) {
  int i;
  for(i = 0; i < len; ++i) {
    printf("%02X", ((unsigned char*)buf)[i]);
  }
  printf("\n");
}

void swapto(int to_hostring, struct netmap_slot *rxslot) {
  struct netmap_ring *txring;
  int i, first, last;
  uint32_t t, cur;

  if (to_hostring) {
    fprintf(stderr, "NIC to HOST\n");
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
  unsigned int cur, i, is_hostring, recieved;
  char *buf, *payload;
  struct netmap_ring *rxring;
  struct pollfd pollfd[1];
  struct ether_header *ether;
  struct ether_arp *arp;
  struct ip *ip;
  struct tcphdr *tcp;
  struct udphdr *udp;

  char src[32];
  char dst[32];

  nm_desc = nm_open("netmap:ix1*", NULL, 0, NULL);
  for(;;){
    pollfd[0].fd = nm_desc->fd;
    pollfd[0].events = POLLIN;
    poll(pollfd, 1, 100);

    recieved = 0;

    for (i = nm_desc->first_rx_ring; i <= nm_desc->last_rx_ring; i++) {

      is_hostring = (i == nm_desc->last_rx_ring);

      rxring = NETMAP_RXRING(nm_desc->nifp, i);

      while(!nm_ring_empty(rxring)) {
        recieved = 1;
        cur = rxring->cur;
        buf = NETMAP_BUF(rxring, rxring->slot[cur].buf_idx);
        printHex(buf, rxring->slot[cur].len);
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

        inet_ntop(AF_INET, &ip->ip_src, src, sizeof(src));
        inet_ntop(AF_INET, &ip->ip_dst, dst, sizeof(dst));

        printf("ip ver: %u\n", ip->ip_v);
        printf("saddr: %s\n", src);
        printf("daddr: %s\n", dst);

        if (ip->ip_p == IPPROTO_TCP) {
          tcp = (struct tcphdr *)payload;
          printf("TCP Src port: %u\n", ntohs(tcp->th_sport));
          printf("TCP Dst port: %u\n", ntohs(tcp->th_dport));

          switch(tcp->th_flags) {
            case TH_FIN:
              printf("FIN\n");
              break;
            case TH_SYN:
              printf("SYN\n");
              break;
            case TH_ACK:
              printf("ACK\n");
              break;
            case TH_RST:
              printf("RST\n");
              break;
            case TH_PUSH:
              printf("PSH\n");
              break;
            case TH_URG:
              printf("URG\n");
              break;
            case 0x12:
              printf("SYN ACK\n");
              break;
            case 0x18:
              printf("PSH ACK\n");
              break;
            case 0x11:
              printf("FIN ACK\n");
              break;
            case 0x14:
              printf("RST ACK\n");
              break;
            default:
              printf("flag: %x\n", tcp->th_flags);
              break;
          }
        } else if (ip->ip_p == IPPROTO_UDP) {
          udp = (struct udphdr *)payload;
          printf("UDP Src port: %u\n", ntohs(udp->uh_sport));
          printf("UDP Dst port: %u\n", ntohs(udp->uh_dport));
        }

        swapto(!is_hostring, &rxring->slot[cur]);
        rxring->head = rxring->cur = nm_ring_next(rxring, cur);
      }
    }
    if (ioctl(nm_desc->fd, NIOCRXSYNC, NULL) != 0)
      perror("sync ioctl");
    if (recieved) {
      printf("\n--------------------------------------------------------------------------------\n");
    }
  }
  return 0;
}
