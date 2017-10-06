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

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    last.uc[0] = *(unsigned char *)w;
    last.uc[1] = 0;
    sum += last.us;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum >> 16) + (sum & 0xffff);     /* add hi 16 to low 16 */
  sum += (sum >> 16);                     /* add carry */
  answer = ~sum;                          /* truncate to 16 bits */
  return(answer);
}

void swap_ether(struct ether_header *ether) {
  unsigned char temp_host[6];

  memcpy((char*)temp_host, ether->ether_dhost, 6);
  memcpy((char*)ether->ether_dhost, ether->ether_shost, 6);
  memcpy((char*)ether->ether_shost, temp_host, 6);
}

void swap_ip(struct ip *ip, char* data) {
  struct in_addr temp_addr;

  temp_addr = ip->ip_dst;
  ip->ip_dst = ip->ip_src;
  ip->ip_src = temp_addr;
  ip->ip_sum = 0;

  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udphdr) + strlen(data));
  ip->ip_sum = in_cksum((unsigned short*)ip, ip->ip_hl << 2);
}

void swap_udp(struct udphdr *udp, char* data) {
  unsigned short temp_port;

  temp_port = ntohs(udp->uh_sport);
  udp->uh_sport = udp->uh_dport;
  udp->uh_dport = htons(temp_port);
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
  struct pollfd pollfd[1];
  struct netmap_ring *txring, *rxring;
  struct ether_header *ether;
  struct ether_arp *arp;
  struct ip *ip;
  struct udphdr *udp;
  int recieved = 0;
  int sent = 0;

  nm_desc = nm_open("netmap:ix1*", NULL, 0, NULL);

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
          udp = (struct udphdr *)payload;
          data = payload + sizeof(struct udphdr);
          printf("recieved: %s\n", data);
          recieved = 1;
          break;
        }
      }
      if(recieved)
        break;
    }
    if(recieved)
      break;
  }

  for(;;) {
    pollfd[0].fd = nm_desc->fd;
    pollfd[0].events = POLLOUT;
    poll(pollfd, 1, -1);

    if(sent) {
      txring = NETMAP_TXRING(nm_desc->nifp, nm_desc->first_tx_ring);
      pktsizelen = sizeof(struct ether_header) + sizeof(struct ip) + sizeof(struct udphdr) + strlen(data);

      swap_ether(ether);
      swap_ip(ip, data);
      swap_udp(udp, data);

      cur = txring->cur;
      buf = NETMAP_BUF(txring, txring->slot[cur].buf_idx);
      nm_pkt_copy(pkt, buf, pktsizelen);
      txring->slot[cur].len = pktsizelen;
      txring->slot[cur].flags |= NS_BUF_CHANGED;

      txring->head = txring->cur = nm_ring_next(txring, cur);
      sent = 1;
    } else {
      break;
    }
  }
}
