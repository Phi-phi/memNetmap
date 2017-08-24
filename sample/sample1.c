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
  unsigned int cur, n, i;
  char *buf, *payload;
  struct netmap_ring *rxring;
  struct pollfd pollfd[1];
  struct ether_header *ether;
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

    for(i = nm_desc->first_rx_ring; i <= nm_desc->last_rx_ring; ++i){
      rxring = NETMAP_RXRING(nm_desc->nifp, i);
      cur = rxring->cur;
      for(n = nm_ring_space(rxring); n > 0; n--, cur = nm_ring_next(rxring, cur)) {
        memset( src, 0x00, sizeof(src));
        memset( dst, 0x00, sizeof(dst));

        buf = NETMAP_BUF(rxring, rxring->slot[cur].buf_idx);
        printf("FULL: %x\n", *buf);
        ether = (struct ether_header *)buf;
        ip = (struct ip *)(buf + sizeof(struct ether_header));
        payload = (char *)(ip + ip->ip_hl * 4);

        inet_ntop(AF_INET, &ip->ip_src, src, sizeof(src));
        inet_ntop(AF_INET, &ip->ip_dst, dst, sizeof(dst));

        printf("ip ver: %u\n", ip->ip_v);
        printf("saddr: %s\n", src);
        printf("daddr: %s\n", dst);

        if (ip->ip_p == IPPROTO_TCP) {
          tcp = (struct tcphdr *)payload;
          printf("TCP Src port: %d\n", ntohs(tcp->th_sport));
          printf("TCP Dst port: %d\n", ntohs(tcp->th_dport));
        } else if (ip->ip_p == IPPROTO_UDP) {
          udp = (struct udphdr *)payload;
          printf("UDP Src port: %d\n", ntohs(udp->uh_sport));
          printf("UDP Dst port: %d\n", ntohs(udp->uh_dport));
        }

        printf("payload: %x\n", *payload);
        printf("payload: %s\n", payload);
      }
    }
  }
}
