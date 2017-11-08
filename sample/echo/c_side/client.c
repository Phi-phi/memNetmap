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
#include <sys/time.h>
#define REPEAT 100
#define SEND_REPEAT 100
#define TIMEOUT 1500

struct nm_desc *nm_desc;

char *ip_ntoa2(u_char *d){
  static char str[15];
  sprintf(str,"%d.%d.%d.%d",d[0],d[1],d[2],d[3]);
  return str;
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

void swapto(int to_hostring, struct netmap_slot *rxslot) {
  struct netmap_ring *txring;
  int i, first, last;
  uint32_t t, cur;

  if (to_hostring) {
    //txring -> host ring
    first = last = nm_desc->last_tx_ring;
  } else {
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

double get_interval(struct timeval *begin, struct timeval *end){
  double b_sec, e_sec;

  b_sec = begin->tv_sec + (double)begin->tv_usec * 1e-6;
  e_sec = end->tv_sec + (double)end->tv_usec * 1e-6;

  return e_sec - b_sec;
}

int main(int argc, char* argv[]) {
  int pktsizelen, pkthdrlen;
  int before_idx, idx = 0;
  int pkt_loss = 0;
  double intvl;
  unsigned int cur, i, sent = 0, is_hostring;
  char *pkt, *buf, *tbuf, *payload;
  char data[512];
  char *counted_data;
  struct in_addr src, dst;
  struct pollfd pollfd[1];
  struct netmap_ring *txring, *rxring;
  struct ether_header *ether;
  struct ip *ip;
  struct udphdr *udp;
  struct timeval begin, end;

  src.s_addr = inet_addr("10.2.2.2");
  dst.s_addr = inet_addr("10.2.2.3");

  printf("INPUT: ");
  scanf("%s", data);

  if(strlen(data) >= 512) {
    printf("error. over size\n");
  }

  pkthdrlen = sizeof(struct ether_header) + sizeof(struct ip) + sizeof(struct udphdr);
  pktsizelen = pkthdrlen + strlen(data) + 3;

  nm_desc = nm_open("netmap:ix1*", NULL, 0, NULL);

  ioctl(nm_desc->fd, NIOCTXSYNC, NULL);
  ioctl(nm_desc->fd, NIOCRXSYNC, NULL);

  if((counted_data = malloc(strlen(data) + 3)) == NULL){
    perror("malloc");
  }

  if((pkt = malloc(pktsizelen)) == NULL) {
    perror("malloc");
  }

  create_etherhdr(pkt);
  create_iphdr(pkt, &src, &dst, pktsizelen - sizeof(struct ether_header));

  gettimeofday(&begin, NULL);
  while(idx < REPEAT){
    sprintf(counted_data, "%s%03d", data, idx);

    create_udphdr(pkt, 11233, counted_data);

    while(1) {
      pollfd[0].fd = nm_desc->fd;
      pollfd[0].events = POLLOUT;
      poll(pollfd, 1, -1);

      for(i = nm_desc->first_tx_ring; i <= nm_desc->last_tx_ring - 1 && sent < REPEAT; ++i) {
        txring = NETMAP_TXRING(nm_desc->nifp, i);

        while(!nm_ring_empty(txring) && sent < REPEAT) {
          cur = txring->cur;
          tbuf = NETMAP_BUF(txring, txring->slot[cur].buf_idx);
          nm_pkt_copy(pkt, tbuf, pktsizelen);
          txring->slot[cur].len = pktsizelen;
          txring->slot[cur].flags |= NS_BUF_CHANGED;

          txring->head = txring->cur = nm_ring_next(txring, cur);
          ++sent;
        }
      }

      if(pollfd[0].revents & POLLOUT) {
        break;
      }
    }
    sent = 0;

    before_idx = idx;
    while(1) {
      pollfd[0].fd = nm_desc->fd;
      pollfd[0].events = POLLIN;
      if(poll(pollfd, 1, TIMEOUT) > 0) {

        for (i = nm_desc->first_rx_ring; i <= nm_desc->last_rx_ring && before_idx == idx; i++) {
          is_hostring = (i == nm_desc->last_rx_ring);
          rxring = NETMAP_RXRING(nm_desc->nifp, i);

          while(!nm_ring_empty(rxring)) {
            cur = rxring->cur;
            buf = NETMAP_BUF(rxring, rxring->slot[cur].buf_idx);
            ether = (struct ether_header *)buf;
            if(ntohs(ether->ether_type) == ETHERTYPE_ARP) {
              swapto(!is_hostring, &rxring->slot[cur]);
              rxring->head = rxring->cur = nm_ring_next(rxring, cur);
              continue;
            }
            ip = (struct ip *)(buf + sizeof(struct ether_header));
            payload = (char*)ip + (ip->ip_hl<<2);

            if (ip->ip_p == IPPROTO_UDP) {
              udp = (struct udphdr *)payload;
              printf("UDP Src port: %u\n", ntohs(udp->uh_sport));
              printf("UDP Dst port: %u\n", ntohs(udp->uh_dport));
              printf("Recieved: %s\n", payload + sizeof(struct udphdr *));
              if(strcmp(counted_data, payload + sizeof(struct udphdr*)) == 0) {
                printf("ok.\n");
                ++idx;
                break;
              }
            }
            if(before_idx < idx) {
              break;
            }

            swapto(!is_hostring, &rxring->slot[cur]);
            rxring->head = rxring->cur = nm_ring_next(rxring, cur);
          }
        }
        if(before_idx < idx) {
          break;
        }
      } else {
        printf("timeout\n");
        ++idx;
        ++pkt_loss;
        break;
      }
    }
  }

  gettimeofday(&end, NULL);
  //sever側でsendを1sec待っているため、１００秒マイナス
  intvl = get_interval(&begin, &end) - 100.0;
  printf("interval-> %f\n", intvl);
  printf("average-> %f\n", intvl/REPEAT);
  printf("packet loss: %d\n", pkt_loss);

  nm_close(nm_desc);

  return 0;
}
