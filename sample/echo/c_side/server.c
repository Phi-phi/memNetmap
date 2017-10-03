#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main() {
  int ld;
  struct sockaddr_in skaddr, remote;
  int length;
  int len,n;
  char bufin[512];

  if ((ld = socket( PF_INET, SOCK_DGRAM, 0 )) < 0) {
    printf("Problem creating socket\n");
    exit(1);
  }

  skaddr.sin_family = AF_INET;
  skaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  skaddr.sin_port = htons(0);

  if (bind(ld, (struct sockaddr *) &skaddr, sizeof(skaddr))<0) {
    printf("Problem binding\n");
    exit(0);
  }

  length = sizeof( skaddr );
  if (getsockname(ld, (struct sockaddr *) &skaddr, &length)<0) {
    printf("Error getsockname\n");
    exit(1);
  }

  while (1) {
    n = recvfrom(ld, bufin, 512, 0, (struct sockaddr *)&remote, &len);

    printf("Got a datagram from %s port %d\n",
        inet_ntoa(remote.sin_addr), ntohs(remote.sin_port));

    if (n < 0) {
      perror("Error receiving data");
    } else {
      printf("GOT %d BYTES\n",n);
      sendto(ld, bufin, n, 0, (struct sockaddr *)&remote, len);
    }
  }
  return(0);
}
