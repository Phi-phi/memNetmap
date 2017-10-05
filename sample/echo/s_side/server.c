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

void swap_ether(struct ether_header *ether) {
  unsigned char temp_host[6];

  memcpy(temp_host, ether->ether_dhost, 6);
  memcpy(ether->ether_dhost, ether->ether_shost, 6);
  memcpy(ether->ether_shost, temp_host, 6);
}

void swap_ip(struct ip *ip, char* data) {
  struct in_addr temp_addr;

  temp_addr = ip->ip_dst;
  ip->ip_dst = ip->ip_src;
  ip->ip_src = temp_addr;

  ip->ip_len = sizeof(struct ip) + sizeof(struct udphdr) + strlen(data);
}

void swap_udp(struct udphdr *udp, char* data) {
  unsigned short temp_port;

  temp_port = udp->uh_sport;
  udp->uh_sport = udp->uh_dport;
  udp->uh_dport = udp->uh_sport;
  udp->uh_ulen = sizeof(struct udphdr) + strlen(data);

  memcpy((char *)udp + sizeof(struct udphdr), data, strlen(data));
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
  int pktsizelen;
  unsigned int cur, i, is_hostring;
  char *pkt, *buf, *payload, *data;
  struct in_addr temp_addr;
  struct pollfd pollfd[1];
  struct netmap_ring *txring, *rxring;
  struct ether_header *ether;
  struct ether_arp *arp;
  struct ip *ip;
  struct udphdr *udp;
  int recieved = 0;

  nm_desc = ("netmap:ix1*", NULL, 0, NULL);

  for(;;) {
    pollfd[0].fd = nm_desc->fd;
    pollfd[0].events = POLLIN;
    poll(pollfd, 1, -1);

    for(i = nm_desc->first_rx_ring; i <= nm_desc->last_rx_ring; ++i) {
      is_hostring = (i == nm_desc->last_rx_ring);

      rxring = NETMAP_RXRING(nm_desc->nifp, i);

      while(!nm_ring_empty(rxring)) {
        cur = rxring->cur;
        pkt = NETMAP_BUF(rxring, rxring->slot[cur].buf_idx);
        ether = (struct ether_header *)pkt;
        if(ntohs(ether->ether_type) == ETHERTYPE_ARP) {
          printf("This is ARP.\n");
          arp = (struct ether_arp *)(pkt + sizeof(struct ether_header));

          swapto(!is_hostring, &rxring->slot[cur]);
          rxring->head = rxring->cur = nm_ring_next(rxring, cur);
          continue;
        }
        ip = (struct ip*)(pkt + sizeof(struct ether_header));
        payload = (char*)ip + (ip->ip_hl<<2);

        if(ip->ip_p == IPPROTO_UDP) {
          data = payload + sizeof(struct udphdr);
          printf("recieved: %s\n", data);
          recieved = 1;
          break;
        }
      }
      if(recieved)
        break;
    }
  }

  for(;;) {
    pollfd[0].fd = nm_desc->fd;
    pollfd[0].events = POLLOUT;
    poll(pollfd, 1, -1);

    txring = NETMAP_TXRING(nm_desc->nifp, nm_desc->first_tx_ring);
    pktsizelen = sizeof(struct ether_header) + sizeof(struct ip) + sizeof(struct udphdr) + strlen(data);

    udp = (struct udphdr*)ip + (ip->ip_hl<<2);
    swap_ether(ether);
    swap_ip(ip, data);
    swap_udp(udp, data);

    cur = txring->cur;
    buf = NETMAP_BUF(txring, txring->slot[cur].buf_idx);
    nm_pkt_copy(pkt, buf, pktsizelen);
    txring->slot[cur].len = pktsizelen;
    txring->slot[cur].flags |= NS_BUF_CHANGED;

    txring->head = txring->cur = nm_ring_next(txring, cur);
  }
}
