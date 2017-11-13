#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <sys/time.h>

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

double get_interval(struct timeval *begin, struct timeval *end){
  double b_sec, e_sec;

  b_sec = begin->tv_sec + (double)begin->tv_usec * 1e-6;
  e_sec = end->tv_sec + (double)end->tv_usec * 1e-6;

  return e_sec - b_sec;
}

int main(int argc, char* argv[]) {
  int pktsizelen, count_num = 0, max_num;
  unsigned int cur, i;
  double intvl;
  char *pkt, *tbuf, *count;
  char *message = "This message sent for testing netmap. Source code is written by phi.phiphiphiphiphi....................";
  struct in_addr src, dst;
  struct netmap_ring *txring;
  struct pollfd pollfd[1];
  struct timeval begin, end;

  count = argv[1];
  max_num = atoi(count);

  if (max_num <= 0) {
    printf("repeat num error.\n");
    exit(-1);
  }

  printf("sending %d packets\n", max_num);

  src.s_addr = inet_addr("10.2.2.2");
  dst.s_addr = inet_addr("10.2.2.3");

  nm_desc = nm_open("netmap:ix1", NULL, 0, NULL);

  ioctl(nm_desc->fd, NIOCTXSYNC, NULL);
  pktsizelen = sizeof(struct ether_header) + sizeof(struct ip) + sizeof(struct udphdr) + strlen(message);

  pkt = malloc(pktsizelen);

  if(pkt == NULL)
    perror("malloc");

  create_etherhdr(pkt);
  create_iphdr(pkt, &src, &dst, pktsizelen - sizeof(struct ether_header));
  create_udphdr(pkt, 12345, message);

  pollfd[0].fd = nm_desc->fd;
  pollfd[0].events = POLLOUT;

  gettimeofday(&begin, NULL);

  while(1) {
    poll(pollfd, 1, -1);

    while (count_num < max_num) {
      for (i = nm_desc->first_tx_ring; i <= nm_desc->last_tx_ring && count_num < max_num; ++i) {
        txring = NETMAP_TXRING(nm_desc->nifp, i);

        while(!nm_ring_empty(txring) && count_num < max_num) {
          cur = txring->cur;

          tbuf = NETMAP_BUF(txring, txring->slot[cur].buf_idx);

          nm_pkt_copy(pkt, tbuf, pktsizelen);
          txring->slot[cur].len = pktsizelen;
          txring->slot[cur].flags |= NS_BUF_CHANGED;

          txring->head = txring->cur = nm_ring_next(txring, cur);
          ++count_num;
        }
      }

      for (i = nm_desc->first_tx_ring; i <= nm_desc->last_tx_ring; ++i) {
        txring = NETMAP_TXRING(nm_desc->nifp, i);
        while(nm_tx_pending(txring)) {
          ioctl(nm_desc->fd, NIOCTXSYNC, NULL);
        }
      }
      if (pollfd[0].revents & POLLOUT && count_num >= max_num && poll_ret > 0) {
        gettimeofday(&end, NULL);
        break;
      }

    }
  }

  intvl = get_interval(&begin, &end);

  printf("packet size: %d bytes\n", pktsizelen);
  printf("interval-> %f [sec]\n", intvl);
  printf("average-> %f [sec]\n", intvl / count_num);
  printf("packets per seconds %f [pps]\n", count_num / intvl);

  nm_close(nm_desc);

  return 0;
}
