#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>



void error(const char *msg)
{
    perror(msg);
    exit(0);
}

int main(int argc, char *argv[])
{
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char buffer[256];

    char *line = NULL;
    size_t len = 0;
    ssize_t rs;

    if (argc < 3) {
       fprintf(stderr,"usage %s hostname port\n", argv[0]);
       exit(0);
    }
    portno = atoi(argv[2]);
    FILE *ifp;
    ifp = fopen(argv[3], "r");
    if (ifp == NULL) {
      fprintf(stderr, "Can't open input file %s!\n", argv[3]);
      exit(1);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    server = gethostbyname(argv[1]);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);
    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0)
        error("ERROR connecting");

    strcpy(buffer, "modelID\n");
    n = write(sockfd, buffer, strlen(buffer));
    if (n < 0)
         error("ERROR writing to socket");

    bzero(buffer, 256);
    n = read(sockfd, buffer, 255);
    if (n < 0)
         error("ERROR reading from socket");
    printf("%s\n", buffer);

    while ((rs = getline(&line, &len, ifp)) != -1) {
      bzero(buffer, 256);
      if(rs < 256) {
    strcpy(buffer, line);
    //  strcat(buffer, "\n");
    //  printf("%s", buffer);
     }
      else {
    error("ERROR: Length of line read is over 255");
    fprintf(stderr, ">>%s<< len = %ld", line, rs);
      }

      n = write(sockfd, buffer, strlen(buffer));
      if (n < 0)
    error("ERROR writing to socket");

      bzero(buffer, 256);
      n = read(sockfd, buffer, 255);
      if (n < 0)
    error("ERROR reading from socket");
      printf("%s", buffer);
    }

    fclose(ifp);
    if (line)
      free(line);

    close(sockfd);

    return 0;
}
