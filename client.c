#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define LENGTH 2048
#define USER_DATA_LENGTH 32
#define PORT 4444

#define stdin_clean(stdin) while ((getchar()) != '\n')

volatile sig_atomic_t flag = 0;
int sockfd = 0;
char uName[USER_DATA_LENGTH];

void my_exit(){
  //flag = 1;
  close(sockfd);
  exit(0);
}

void msg_trim (char* arr, int length) {
  int i;
  for (i = 0; i < length; i++) { 
    if (arr[i] == '\n') {
      arr[i] = '\0';
      break;
    }
  }
}

void recv_handler (){
  char message[LENGTH];
  while(1){
    int receive = recv(sockfd, message, LENGTH, 0);
    if (receive > 0){
      printf("%s", message);
      fflush(stdout);
    }
    else if(receive == 0){
      my_exit();
      break;
    }
    bzero(message, LENGTH);
  }
}

void send_handler(){
  char message[LENGTH];
  
  while(1){
    fgets(message, LENGTH, stdin);
    msg_trim(message, strlen(message));

    send(sockfd,message,strlen(message),0);
    bzero(message, LENGTH);
    
    if (strcmp(message, "exit") == 0) {
      my_exit();
    }
  }
}

int main(int argc, char **argv){

  char *ip = "127.0.0.1";

  signal(SIGINT, my_exit);

  //printf("%s",uName);
  struct sockaddr_in server_addr;

  //Socket settings
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  server_addr.sin_addr.s_addr = inet_addr(ip);
  server_addr.sin_port = htons(PORT);
  server_addr.sin_family = AF_INET;

  //connecting client to server
  if(connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1){
    printf("ERROR: connection failed!\n");
    return EXIT_FAILURE;
  }

  printf("CONNECTED!\n");

  pthread_t recv_thread;
  pthread_create(&recv_thread, NULL, (void *) recv_handler, NULL);
  
  pthread_t send_thread;
  pthread_create(&send_thread, NULL, (void *) send_handler, NULL);

  while(1){
    if(flag){
      my_exit();
    }
  }
  close(sockfd);

  return EXIT_SUCCESS;
}
