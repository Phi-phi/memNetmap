#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef REPEAT
#define REPEAT 100
#endif

int main() {
  int ld;
  int recieved = 0;
  struct sockaddr_in skaddr, remote;
  unsigned int len,n;
  char bufin[512];

  printf("repeat: %d\n", REPEAT);

  if ((ld = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP )) < 0) {
    printf("Problem creating socket\n");
    exit(1);
  }

  memset(&skaddr, 0, sizeof(skaddr));
  skaddr.sin_family = AF_INET;
  skaddr.sin_addr.s_addr = INADDR_ANY;
  skaddr.sin_port = htons(11233);

  if (bind(ld, (struct sockaddr *) &skaddr, sizeof(skaddr))<0) {
    printf("bind");
    exit(1);
  }

  memset(&remote, 0, sizeof(remote));
  memset(bufin, 0, sizeof(bufin));

  len = sizeof(struct sockaddr_in);
  while (1) {
    while (recieved < REPEAT) {
      n = recvfrom(ld, bufin, 512, 0, (struct sockaddr *)&remote, &len);
      ++recieved;
      if (n < 0) {
        perror("recieved");
        break;
      }
    }
    recieved = 0;

    printf("Got a datagram from %s port %d\n",
        inet_ntoa(remote.sin_addr), ntohs(remote.sin_port));

    printf("GOT %d BYTES. %s\n",n, bufin);
    sendto(ld, bufin, n, 0, (struct sockaddr *)&remote, len);
  }

  return(0);
}
