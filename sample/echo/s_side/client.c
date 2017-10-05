#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>

int main( int argc, char **argv ) {
  int sock, read_len;
  struct sockaddr_in server, remote;
  char data[512], recv_data[512];

  printf("INPUT: ");
  scanf("%s", data);

  if ((sock = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP )) < 0) {
    printf("socket");
    exit(1);
  }

  memset(&server, 0, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = inet_addr("10.2.2.3");
  server.sin_port = htons(65001);

  if(bind(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
    perror("bind");
    exit(1);
  }

  memset(&remote, 0, sizeof(remote));
  remote.sin_family = AF_INET;
  remote.sin_addr.s_addr = inet_addr("10.2.2.2");
  remote.sin_port = htons(11233);

  sendto(sock, data, strlen(data), 0, (struct sockaddr*) &remote, sizeof(remote));

  memset(recv_data, 0, sizeof(recv_data));
  if ((read_len = recvfrom(sock, recv_data, 512, 0, NULL, NULL)) < 0) {
    perror("recvfrom");
    exit(1);
  }

  printf("Got back %d bytes\n", read_len);
  printf("recieved: %s\n", recv_data);

  if(strcmp(data, recv_data) == 0) {
    printf("ok\n");
  }

  close(sock);
  return 0;
}
