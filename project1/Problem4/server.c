#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <ctype.h>

void *entrance(void *sockfd);
void *serve(void* newsockfd);
void encode(char buffer[], int length);

void* PTHREAD_EXIT_SUCCESS;
void* PTHREAD_EXIT_FAILURE;

int serving_num;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char *argv[])
{
	int sockfd, newsockfd, portno, clilen, n;
	char buffer[256];
	struct sockaddr_in serv_addr, cli_addr;
	pthread_t newthread;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		printf("ERROR opening socket\n");
		exit(1);
	}
	bzero((char*) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	portno = 2050;
	serv_addr.sin_port = htons(portno);
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		printf("Error on binding\n");
		exit(1);
	}
	listen(sockfd, 5);
	clilen = sizeof(cli_addr);
	printf("Server initiating...\n");

	while (1) {
		newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
		if (newsockfd < 0) {
			printf("[ Error ]\terror on accept!\n");
			continue;
		}
		pthread_create(&newthread, NULL, entrance, &newsockfd);
	}

	return 0;
}

void encode(char buffer[], int length)
{
	for (int i = 0; i < length; i++) {
		if (buffer[i] >= 'a' && buffer[i] <= 'z') {
			buffer[i] = 'a' + (buffer[i] - 'a' + 3) % 26;
		}
		else if (buffer[i] >= 'A' && buffer[i] <= 'Z') {
			buffer[i] = 'A' + (buffer[i] - 'A' + 3) % 26;
		}
	}
}

void *serve(void *sockfd)
{
	int newsockfd = (int)(*((int*)sockfd));
	char buffer[256];

	while(1) {
		bzero(buffer, 256);
		if (read(newsockfd, buffer, 255) < 0) {
			printf("[ ERROR ]\terror on reading message from %d", newsockfd);
			pthread_exit(PTHREAD_EXIT_FAILURE);
		} else if (strcmp(buffer, ":q\n") == 0) {
			pthread_mutex_lock(&mutex);
			serving_num--;
			pthread_mutex_unlock(&mutex);

			printf("Server thread closing...\n");
			pthread_exit(PTHREAD_EXIT_SUCCESS);
		} else {
			printf("Receiving message: %s", buffer);
			encode(buffer, strlen(buffer));
			write(newsockfd, buffer, strlen(buffer));
		}
	}
}

void *entrance(void *sockfd)
/* entrance of serve function
   reject requests when two clients are being served 
   deal with the first successful request */
{
	int newsockfd = (int)(*((int*)sockfd));
	pthread_t newthread;
	char buffer[256];
	int enter = 0;

	strcpy(buffer, "Please wait...\n");

	while(1) {
		bzero(buffer, 256);
		if (read(newsockfd, buffer, 255) < 0) {
			pthread_exit(PTHREAD_EXIT_FAILURE);
		}
		if (strcmp(buffer, ":q\n") == 0) {
			pthread_exit(PTHREAD_EXIT_SUCCESS);
		} else {
			pthread_mutex_lock(&mutex);
			if (serving_num >= 2) {
				strcpy(buffer, "Please wait...\n");
				write(newsockfd, buffer, strlen(buffer));
			} else {	
				printf("Receiving message: %s", buffer);
				encode(buffer, strlen(buffer));
				write(newsockfd, buffer, strlen(buffer));

				serving_num++;		
				pthread_create(&newthread, NULL, serve, &newsockfd);
				enter = 1;
			}
			pthread_mutex_unlock(&mutex);

			if (enter) {
				pthread_exit(PTHREAD_EXIT_SUCCESS);
			}
		}
	}
}
