#include <stdio.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <memory.h>

#ifdef USE_TPL
#include "tpl.h"
#endif

#define MAX_COMMAND 256
#define COMMAND_START '$'
#define COMMAND_END '*'

#define DEBUG 1

void error(char *msg)
{
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[])
{
    int sockfd, newsockfd, portno, clilen, pid;
    struct sockaddr_in serv_addr, cli_addr;
    char buf[1024];
    int n = 0;
#ifdef USE_TPL
    tpl_node *tn;
    struct struct_type {
        int x;
        char *string;
        char c;
    } local_struct;
#endif
    struct timeval timeout;

    bzero(buf, sizeof(buf));

    if (argc < 3) {
        fprintf(stderr,"usage: %s <server> <port>\n", argv[0]);
        exit(1);
    }

    portno = atoi(argv[2]);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("Error opening socket");

    bzero((char *) &serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(portno);

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    if (setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
                sizeof(timeout)) < 0)
        error("setsockopt failed\n");

    if (setsockopt (sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,
                sizeof(timeout)) < 0)
        error("setsockopt failed\n");

    if (0 != connect(sockfd, 
                (const struct sockaddr *) &serv_addr, sizeof(serv_addr))) {
        error("connect failed\n");
    }

    
    write(sockfd, "$testbinary*\n", sizeof("$testbinary*\n"));
    n = read(sockfd, buf, sizeof(buf));
    printf("Read %d from server: %s\n", n, buf);

#ifdef USE_TPL
    tn = tpl_map("S(isc)", &local_struct);
    tpl_load(tn, TPL_MEM|TPL_EXCESS_OK, buf, n);
    tpl_unpack(tn, 0);
    printf("S.i = %d, S.s = %s, S.c = %c\n", local_struct.x, local_struct.string, local_struct.c);
    tpl_free(tn);
#endif

    bzero(buf, sizeof(buf));
    return 0;
}
