#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

/* client side */

int main(int argc, char* argv[])
{
	int sockfd, portno, n;
	struct sockaddr_in serv_addr;
	struct hostent *server;
	char buffer[256];
	// port number of server
	portno = 2050;
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		printf("[ ERROR ]\terror on opening socket\n");
		exit(1);
	}
	server = gethostbyname("127.0.0.1");
	if (server == NULL) {
		printf("[ ERROR ]\tno such host\n");
		exit(0);
	}
	bzero((char *)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
	serv_addr.sin_port = htons(portno);

	if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		printf("[ ERROR ]\tconnection failed!\n");
		exit(1);
	}

	printf("Please enter the message:\n");
	while (1) {
		bzero(buffer, 256);
		fgets(buffer, 255, stdin);
		write(sockfd, buffer, strlen(buffer));
		if (strcmp(buffer, ":q\n") == 0) break;
		bzero(buffer, 256);
		read(sockfd, buffer, 255);
		printf("From server: %s", buffer);
	}
	printf("Client closing...\n");
	return 0;
}
