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

int main(int argc, char* argv[]) {
  unsigned int rx_cur, tx_cur, n, i;
  char *rbuf, *tbuf, *payload;
  struct netmap_ring *rxring, *txring;
  struct pollfd pollfd[1];
  struct ether_header *ether;
  struct ip *ip;
  struct tcphdr *udp;

  nm_desc = nm_open("netmap:ix1*", NULL, 0, NULL);
  for(;;){
    pollfd[0].fd = nm_desc->fd;
    pollfd[0].events = POLLIN;
    poll(pollfd, 1, 500);

    rxring = NETMAP_RXRING(nm_desc->nifp, nm_desc->first_rx_ring);
    txring = NETMAP_TXRING(nm_desc->nifp, nm_desc->first_tx_ring);

    while(!nm_ring_empty(rxring)) {
      struct in_addr addr;
      rx_cur = rxring->cur;
      tx_cur = txring->cur;

      rbuf = NETMAP_BUF(rxring, rxring->slot[rx_cur].buf_idx);
      tbuf = NETMAP_BUF(txring, txring->slot[tx_cur].buf_idx);
      printf("packet: %x\n", *rbuf);

      ether = (struct ether_header *)rbuf;
      ip = (struct ip *)(rbuf + sizeof(struct ether_header));

      addr = ip->ip_src;
      ip->ip_src = ip->ip_dst;
      ip->ip_dst = addr;
      printf("modified %x\n", *rbuf);

      memcpy(tbuf, rbuf, sizeof(*rbuf));
      txring->slot[tx_cur].len = sizeof(*rbuf);
      txring->slot[tx_cur].flags |= NS_BUF_CHANGED;
      rxring->head = rxring->cur = nm_ring_next(rxring, rx_cur);
      txring->cur = nm_ring_next(txring, tx_cur);
    }


  }
}
