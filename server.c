#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>
#include <stdatomic.h>
#include <pthread.h>

#define MAX_CLIENTS 100
#define MAX_USERS 200
#define BUFFER_SIZE 2048
#define PORT 4444
#define HELP_STR "To send a message type the receiver, than two points ':', than the message\nTo show the list of who is online type 'list'\nto show the list of users type 'users'\n\n"
#define OK_STRING "Succesfully logged in!\n\n"
#define FILE_NAME "database.txt"
#define NEW_MSG_LINE "-----MESSAGES-FROM-LAST-ONLINE-----\n"

static atomic_int clientNumber = 0;
static atomic_int usersNumber = 0;
static int uid = 10;
static char msgSeparator[] = ":";
static char bufSeparator[] = "|";
//user structure - la userò per il log-in
typedef struct userStr{
  char name[32];
  char surname[32];
  char username[32];
  char password[32];
}userStr;

//client struct
typedef struct clientStr{
  struct sockaddr_in address;
  int sockfd;
  int uid;
  int logged;
  struct userStr *user;
}clientStr;


FILE *db_file_ptr;
FILE *client_file_ptr;
FILE *buffer_file;
clientStr *clients[MAX_CLIENTS];
userStr *users[MAX_USERS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

//declaration of functions
void save_msg(char *msg, char *usr);
void send_to_all(char *msg);
void save_all();
void free_all();

void my_exit(){
  send_to_all("ERROR: server is down\n");
  save_all();
  free_all();
  
  exit(0);  
}

void free_all(){
  pthread_mutex_lock(&clients_mutex);

  for(int i=0; i<MAX_CLIENTS; i++){
    if(clients[i]){
	  if(clients[i]->logged == 1){
        free(clients[i]->user);        
	  }
	  free(clients[i]);
    }
  }
   
  pthread_mutex_unlock(&clients_mutex);
}

void add_client(clientStr *client){
  pthread_mutex_lock(&clients_mutex);

  for(int i=0; i<MAX_CLIENTS; i++){
    if(!clients[i]){
      clients[i] = client;
      break;
    }
  }  
  pthread_mutex_unlock(&clients_mutex);
}

void remove_client(clientStr *client){
  pthread_mutex_lock(&clients_mutex);

  for(int i=0; i<MAX_CLIENTS; i++){
    if(clients[i]){
      if(clients[i]->uid == client->uid){
		clients[i] = NULL;
        break;
      }
    }
  }  
  pthread_mutex_unlock(&clients_mutex);
}

void send_online_list(int uid){
  pthread_mutex_lock(&clients_mutex);
  char listBuffer[((MAX_CLIENTS+2) * 32)+21];
  char name[34]; //32+2
  strcpy(listBuffer,"currently online: ");
  int sockfd;
  for(int i=0; i<MAX_CLIENTS; i++){
    if(clients[i]){
      if(clients[i]->uid != uid){		
		if(clients[i]->logged == 1){
		  sprintf(name, "%s, ", clients[i]->user->username);
		  strcat(listBuffer, name);
	    }
      }
      else{
		sockfd = clients[i]->sockfd;
      }
    }
  }
  strcat(listBuffer, "\n");
  write(sockfd, listBuffer, strlen(listBuffer));
  bzero(listBuffer, ((MAX_CLIENTS+2) * 32));
  pthread_mutex_unlock(&clients_mutex);
}

void send_to_all_except(char *msg, int uid){ //not used except for debug
  pthread_mutex_lock(&clients_mutex);

  for(int i=0; i<MAX_CLIENTS; i++){
    if(clients[i]){
      if(clients[i]->uid != uid){
		write(clients[i]->sockfd, msg, strlen(msg));
      }
    }
  }
  
  pthread_mutex_unlock(&clients_mutex);
}

void send_to_all(char *msg){
  pthread_mutex_lock(&clients_mutex);

  for(int i=0; i<MAX_CLIENTS; i++){
    if(clients[i]){
	  write(clients[i]->sockfd, msg, strlen(msg));
    }
  }
  
  pthread_mutex_unlock(&clients_mutex);
}

void send_to_uid(char *msg, int uid){
  pthread_mutex_lock(&clients_mutex);

  for(int i=0; i<MAX_CLIENTS; i++){
    if(clients[i]){
      if(clients[i]->uid == uid){
	write(clients[i]->sockfd, msg, strlen(msg));
      }
    }
  }
  pthread_mutex_unlock(&clients_mutex);
}

int send_to_from(char *msg, char *dest, char *sender){
  int found = 0;
  pthread_mutex_lock(&clients_mutex);

  for(int i=0; i<MAX_CLIENTS; i++){
    if(clients[i]){
      if(strcmp(clients[i]->user->username, dest) == 0){
		char buffer[BUFFER_SIZE + 32];
		sprintf(buffer, "%s->%s\n", sender, msg);
		write(clients[i]->sockfd, buffer, strlen(buffer));
		//save message
		save_msg(buffer, dest);
		
		found = 1;
		break;
      }
    }
  }
  pthread_mutex_unlock(&clients_mutex);
  return found;
}

void send_users_list(clientStr *client){
  pthread_mutex_lock(&users_mutex);
  char listBuffer[((MAX_USERS+2) * 32)+21];
  char name[34]; //32+2
  strcpy(listBuffer,"users in database: ");
  for(int i=0; i<MAX_USERS; i++){
    if(users[i]){
	  sprintf(name, "%s, ", users[i]->username);
	  strcat(listBuffer, name);
    }
  }
  strcat(listBuffer, "\n");
  write(client->sockfd, listBuffer, strlen(listBuffer));
  bzero(listBuffer, ((MAX_USERS+2) * 32)+21);
  pthread_mutex_unlock(&users_mutex);
}

int send_offline(char *msg, char *dest, char *sender){
  int found = 0;
  pthread_mutex_lock(&users_mutex);

  for(int i=0; i<MAX_USERS; i++){
    if(users[i]){
      if(strcmp(users[i]->username, dest) == 0){
		char buffer[BUFFER_SIZE + 32];
		sprintf(buffer, "%s->%s\n", sender, msg);
		//save message
		save_msg(buffer, dest);
		
		found = 1;
		break;
      }
    }
  }
  pthread_mutex_unlock(&users_mutex);
  return found;
}

void save_all(){
  pthread_mutex_lock(&clients_mutex);

  for(int i=0; i<MAX_CLIENTS; i++){
    if(clients[i]){
	  if(clients[i]->logged == 1){
	    save_msg(NEW_MSG_LINE, clients[i]->user->username);
	  }
    }
  }
  
  pthread_mutex_unlock(&clients_mutex);
}

void save_msg(char *msg, char *usr){
  char file[32+4];
    
  sprintf(file, "%s.txt", usr);
  client_file_ptr = fopen(file, "a");
  fputs(msg, client_file_ptr);
  fclose(client_file_ptr);
}

void populate_users_db(){
	//declaring buffer for getline
	int userCtr = 0;
	int usrSize = 134;
	char *line;
	line = (char*) malloc(usrSize*sizeof(char));
	size_t maxLineSize = usrSize;
	size_t currentLineSize;
	
	//file mutex
	pthread_mutex_lock(&file_mutex);  
	
    db_file_ptr = fopen (FILE_NAME, "r");
    //if file does not exist, create it
    if(db_file_ptr == NULL){
	  db_file_ptr = fopen(FILE_NAME, "wb");
	  fclose(db_file_ptr);
	  db_file_ptr = fopen (FILE_NAME, "r");
	}
	
	while((currentLineSize = getline(&line, &maxLineSize, db_file_ptr)) != -1){
	  line[currentLineSize-1] = 0;
	  userStr *newUser = (userStr *)malloc(sizeof(userStr));
	  char *token = strtok(line,bufSeparator);
	  strcpy(newUser->username,token);
	  
	  token = strtok(NULL,bufSeparator);
	  strcpy(newUser->password,token);
	  
	  token = strtok(NULL,bufSeparator);
	  strcpy(newUser->name,token);
	  
	  token = strtok(NULL,bufSeparator);
	  strcpy(newUser->surname,token);
	  
	  users[userCtr] = newUser;
	  userCtr++;
	  usersNumber++;
	  bzero(line,usrSize);
	  if(usersNumber == MAX_USERS){
		  printf("ERROR: too many users in database!\n");
		  my_exit();	  
	  }
    }
    
    fclose(db_file_ptr);
    free(line);
    pthread_mutex_unlock(&file_mutex);
}

void add_to_users_db(char *usr, char *psw, char *nme, char *sur){
  usersNumber++;
  char file_string[134]; //(32*4)+6
  sprintf(file_string, "%s|%s|%s|%s\n",usr,psw,nme,sur);
  	  
  //file mutex
  pthread_mutex_lock(&file_mutex);  
  
  db_file_ptr = fopen (FILE_NAME, "a"); 
  fputs(file_string, db_file_ptr);
  fclose(db_file_ptr);
  
  pthread_mutex_unlock(&file_mutex);
  
  
  userStr *newUser;
  newUser = (userStr *)malloc(sizeof(userStr));
  
  strcpy(newUser->username , usr);
  strcpy(newUser->password , psw);
  strcpy(newUser->name , nme);
  strcpy(newUser->surname , sur);
  //users mutex
  pthread_mutex_lock(&users_mutex);
  
  for(int i=0; i<MAX_USERS; i++){
    if(!users[i]){
      users[i] = newUser;
      break;
    }
  }  
  pthread_mutex_unlock(&users_mutex); 
}

int username_already_taken(char *usr){
  int taken = 0;
  pthread_mutex_lock(&users_mutex);
  
  for(int i=0; i<MAX_USERS; i++){
    if(users[i]){
      if(strcmp(users[i]->username, usr) == 0){		
		taken = 1;
		break;		
      }
    }
  }
  
  pthread_mutex_unlock(&users_mutex);
  return taken;
}

void populate_user_field(clientStr *client, char *usr){
  pthread_mutex_lock(&users_mutex);
  
  int index;
  int flag = 0;
  for(int i=0; i<MAX_USERS; i++){
    if(users[i]){
      if(strcmp(users[i]->username, usr) == 0){
		index = i;
		flag = 1;
		break;
      }
    }
  }
  
  pthread_mutex_unlock(&users_mutex);
  
  if(flag){
	strcpy(client->user->username, users[index]->username);
	strcpy(client->user->password, users[index]->password);
	strcpy(client->user->name, users[index]->name);
	strcpy(client->user->surname, users[index]->surname);
  }
}

struct userStr * assign_user(char *usr){ //deprecated
  //use only when sure the user exist
  pthread_mutex_lock(&users_mutex);
  
  int index;
  int flag = 0;
  for(int i=0; i<MAX_USERS; i++){
    if(users[i]){
      if(strcmp(users[i]->username, usr) == 0){
		index = i;
		flag = 1;
		break;
      }
    }
  }
  
  pthread_mutex_unlock(&users_mutex);
  
  if(flag){
	return users[index];
  }
  return NULL;
}

int check_credentials(char *usr, char *psw){
  int found = 0;
  pthread_mutex_lock(&users_mutex);

  for(int i=0; i<MAX_USERS; i++){	
    if(users[i]){
      if(strcmp(users[i]->username, usr) == 0){
		if(strcmp(users[i]->password, psw) == 0){
		  found = 1;
		  break;
		}
      }
    }
  }  
  
  pthread_mutex_unlock(&users_mutex);
  return found;
}

int already_logged(char *usr){
  int flag = 0;
  
  pthread_mutex_lock(&clients_mutex);
  
  for(int i=0; i<MAX_CLIENTS; i++){
    if(clients[i]){      
	  if(clients[i]->logged == 1){
		if(strcmp(clients[i]->user->username, usr) == 0){
		  flag = 1;
		  break;
		}
	  }
    }
  }
  
  pthread_mutex_unlock(&clients_mutex); 
   
  return flag;
}

void load_old_msgs(clientStr *client){
  char file[32+4];
  char bufferFile[32+4+8];  
  char *line;
  line = (char*) malloc(BUFFER_SIZE*sizeof(char));
  size_t maxLineSize = BUFFER_SIZE;
  
  sprintf(file, "%s.txt", client->user->username);
  sprintf(bufferFile, "%s_buffer.txt", client->user->username);
  
  client_file_ptr = fopen(file, "r");  
  if(client_file_ptr == NULL){
	printf("ERROR: somehow client file is missing!");
	client_file_ptr = fopen(file, "wb");
	fclose(client_file_ptr);
	return;
  }
  buffer_file = fopen(bufferFile, "w");
  
  while((getline(&line, &maxLineSize, client_file_ptr)) != -1){
	//avoid the NEW_MSG_LINE
	if(strcmp(line, NEW_MSG_LINE) != 0){
	  fputs(line, buffer_file);
	}
	//send the old msg to the client
	write(client->sockfd, line, strlen(line));	  
	bzero(line, BUFFER_SIZE);	
  }  
  fclose(client_file_ptr);
  fclose(buffer_file);
  
  free(line);
  
  remove(file);  
  rename(bufferFile, file);
}

int sign_client(clientStr *client){
  char uName[32];
  char password[32];
  char name[32];
  char surname[32];
  char file[32+4];

  int uName_flag = 0;
  int password_flag = 0;
  int name_flag = 0;
  int surname_flag = 0;
  
  while(1){
	if(usersNumber == MAX_USERS){
	  printf("ERROR: max users in database reached!\n");
	  return 0;	  
	}
	if(!uName_flag){
	  send_to_uid("chose your username:\n",client->uid);
	  if(recv(client->sockfd, uName, 32, 0) <= 0){
	    printf("ERROR: RECEIVE.\n");
	    return 0;
	    continue;
	  }
	  else if(strlen(uName) <  2 || strlen(uName) >= 32-1){
	    printf("ERROR: Invalid username.\n");
	    send_to_uid("ERROR: Invalid username.\n",client->uid);
	    bzero(uName, 32);
	    continue;
	  }
	  else if(username_already_taken(uName)){
	    printf("ERROR: username already taken.\n");
	    send_to_uid("ERROR: username already taken.\n",client->uid);
	    bzero(uName, 32);
	    continue;
	  }
	  else{
	    uName_flag = 1;
	  }
	}
	if(!password_flag){
	  send_to_uid("chose your password:\n",client->uid);
		if(recv(client->sockfd, password, 32, 0) <= 0){
		  printf("ERROR: RECEIVE.\n");
		  return 0;
		  continue;
		}
	    else if(strlen(password) <  2 || strlen(password) >= 32-1){
		  printf("ERROR: Invalid password.\n");
		  send_to_uid("ERROR: Invalid password.\n",client->uid);
	      bzero(password, 32);
		  continue;
		}
		else{
		  password_flag = 1;
		}
	}
	if(!name_flag){
	  send_to_uid("enter your name:\n",client->uid);
		if(recv(client->sockfd, name, 32, 0) <= 0){
		  printf("ERROR: RECEIVE.\n");
		  return 0;
		  continue;
		}
		else if(strlen(name) <  2 || strlen(name) >= 32-1){
		  printf("ERROR: Invalid name.\n");
		  send_to_uid("ERROR: Invalid name.\n",client->uid);
		  bzero(name, 32);
		  continue;
		}
		else{
		  name_flag = 1;
		}
    }
	if(!surname_flag){
	  send_to_uid("enter your surname:\n",client->uid);
	  if(recv(client->sockfd, surname, 32, 0) <= 0){
		printf("ERROR: RECEIVE.\n");
		return 0;
	    continue;
	  }
	  else if(strlen(surname) <  2 || strlen(surname) >= 32-1){
		printf("ERROR: Invalid surname.\n");
		send_to_uid("ERROR: Invalid surname.\n",client->uid);
		bzero(surname, 32);
		continue;
	  }
      else{
		surname_flag = 1;
		break;
	  }
	}
  }
  add_to_users_db(uName,password,name,surname);
  
  populate_user_field(client, uName);
  
  //create offline message database
  sprintf(file, "%s.txt", uName);
  client_file_ptr = fopen(file, "wb");
  fclose(client_file_ptr);
  
  return 1;
}

int log_client(clientStr *client){
  char uName[32];
  char password[32];
  while(1){
	  //username and password
	  send_to_uid("insert your username:\n",client->uid);
	  bzero(uName,32);
	  bzero(password,32);
	  if(recv(client->sockfd, uName, 32, 0) <= 0){
	    printf("ERROR: RECEIVE.\n");
	    return 0;
	    continue;
	  }	  
	  send_to_uid("insert your password:\n",client->uid);
	  if(recv(client->sockfd, password, 32, 0) <= 0){
	    printf("ERROR: RECEIVE.\n");
	    return 0;
	    continue;
	  }
	  else if(strlen(uName) <  2 || strlen(uName) >= 32-1  || strlen(password) <  2 || strlen(password) >= 32-1){
	    printf("ERROR: Invalid username or password formatting.\n");
	    send_to_uid("ERROR: Invalid username or password formatting.\n",client->uid);
	    //bzero(uName, 32);
	    //bzero(password, 32);
	    continue;
	  }
	  else{
		if(already_logged(uName)){
		  printf("ERROR: User already logged!\n");
	      send_to_uid("ERROR: User already logged!\n",client->uid);
		  return 0;
		}
		if(check_credentials(uName, password)){
		  populate_user_field(client, uName);
		  return 1;
		}
		else{
		  printf("ERROR: Invalid username or password.\n");
	      send_to_uid("ERROR: Invalid username or password.\n",client->uid);
	      return 0;
		}
	  }	  
  }
  return 0;
}

int log_in_db(clientStr *client){
	char logOrSign[8];
	int done_flag = 0;
	
	send_to_uid("Welcome! ",client->uid);
	
	while(1){
	  if(done_flag){
        break;
      }
      send_to_uid("do you want to sign or log?\n",client->uid);
      bzero(logOrSign, 8);
      //int receive = recv(client->sockfd, buffer, BUFFER_SIZE, 0);
      if(recv(client->sockfd, logOrSign, 8, 0) <= 0){
        printf("ERROR: RECEIVE.\n");
        return 0;
      }
      else if(strlen(logOrSign) <  2 || strlen(logOrSign) >= 8-1){
        printf("ERROR: Incorrect comand.\n");
        send_to_uid("ERROR: Incorrect comand.\n",client->uid);
      }
      else if(strcmp(logOrSign,"sign")==0 || strcmp(logOrSign,"Sign")==0 || strcmp(logOrSign,"signup")==0 || strcmp(logOrSign,"Signup")==0){
        done_flag = sign_client(client);
      }
      else if(strcmp(logOrSign,"log")==0 || strcmp(logOrSign,"Log")==0 || strcmp(logOrSign,"login")==0 || strcmp(logOrSign,"Login")==0){
        done_flag = log_client(client);
      }
      else {
		printf("ERROR: Incorrect comand.\n");
        send_to_uid("ERROR: Incorrect comand.\n",client->uid);
	  }
    }
    return done_flag;
}

void *handle_client(void *arg){
  clientStr *client = (clientStr*)arg;
  client->user = (userStr *)malloc(sizeof(userStr));
  
  int logInFlag = log_in_db(client);
  int connected_flag = 0;
  char buffer[BUFFER_SIZE];
  char msgBuffer[BUFFER_SIZE];
  char destBuffer[32];
  char saveBuffer[BUFFER_SIZE+32+1];

  if(logInFlag){
    connected_flag = 1;
    client->logged = 1;
    send_to_uid("Successfully logged in!\n",client->uid);
    load_old_msgs(client);
  }
  clientNumber++;
  while(1){
    if(connected_flag != 1){
      break;
    }
    bzero(destBuffer, 32);
    //Receive:
    int receive = recv(client->sockfd, buffer, BUFFER_SIZE, 0);
    if (receive > 0){      
      if(strlen(buffer) > 0){		
		char *ptr = strtok(buffer, msgSeparator);
		if(strlen(ptr) < 32){		  
		  bzero(destBuffer,32);
		  strcpy(destBuffer, ptr);
		  if(strcmp(destBuffer, "exit") == 0 || strcmp(destBuffer, "exit\n") == 0 ){
			sprintf(msgBuffer, "%s has left\n", client->user->username);
			printf("%s", msgBuffer);
			//send_to_all_except(msgBuffer, client->uid);
			save_msg(NEW_MSG_LINE, client->user->username);
			connected_flag = 0;
			break;
			//continue;
		  }
		  else if(strcmp(destBuffer, "help") == 0 || strcmp(destBuffer, "help\n") == 0 ){
			sprintf(msgBuffer, HELP_STR);
			send_to_uid(msgBuffer, client->uid);
			bzero(ptr,32);
			continue;
		  }
		  else if(strcmp(destBuffer, "list") == 0 || strcmp(destBuffer, "list\n") == 0 ){
			send_online_list(client->uid);
			bzero(ptr,32);
			continue;
		  }
		  else if(strcmp(destBuffer, "users") == 0 || strcmp(destBuffer, "users\n") == 0 ){
			send_users_list(client);
			bzero(ptr,32);
			continue;
		  }
		}
		else{
		  send_to_uid("ERROR: invalid receiver\n",client->uid);
		  bzero(ptr,32);
		  continue;
		}
		ptr = strtok(NULL, "");
		if(ptr==NULL){
		  send_to_uid("ERROR: invalid formatting\n",client->uid);
		  bzero(buffer,BUFFER_SIZE);
		  continue;
		}
		strcpy(msgBuffer, ptr);
		if(strlen(msgBuffer) > 0){
		  if(send_to_from(msgBuffer,destBuffer,client->user->username) == 0){
			if(send_offline(msgBuffer,destBuffer,client->user->username) == 0){
			  send_to_uid("ERROR: receiver not in database\n",client->uid);
			}
			else{
			  sprintf(saveBuffer, "%s:%s\n", destBuffer, msgBuffer);
			  save_msg(saveBuffer, client->user->username);
			}
		  }
		  else{
			sprintf(saveBuffer, "%s:%s\n", destBuffer, msgBuffer);
			save_msg(saveBuffer, client->user->username);
		  }
		}
      }
    }
    else if (receive == 0){
      sprintf(msgBuffer, "%s has left\n", client->user->username);
      printf("%s", msgBuffer);
      save_msg(NEW_MSG_LINE, client->user->username);
      //send_to_all_except(msgBuffer, client->uid);
      connected_flag = 0;
    }
    else {
      printf("ERROR: -1\n");
      connected_flag = 0;
    }
    
    bzero(buffer, BUFFER_SIZE);
    bzero(msgBuffer, BUFFER_SIZE);
    bzero(destBuffer, 32);
  }
  
  close(client->sockfd);
  remove_client(client);
  free(client->user);
  free(client);
  clientNumber--;
  pthread_detach(pthread_self());

  return NULL;
}

int main(void){
  //variables
  int option = 1;
  int listenfd = 0;
  int connfd = 0;
  struct sockaddr_in server_addr;
  struct sockaddr_in client_addr;
  pthread_t tid;
  
  //socket
  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  server_addr.sin_addr.s_addr = INADDR_ANY; 
  server_addr.sin_port = htons(PORT);
  server_addr.sin_family = AF_INET;

  //Signals
  signal(SIGPIPE, SIG_IGN);
  
  //bind & listen
  if(setsockopt(listenfd, SOL_SOCKET,(SO_REUSEPORT | SO_REUSEADDR),(char*)&option,sizeof(option)) < 0){
	  printf("ERROR: pthread_create.\n");
	  exit(0);
  }
  bind(listenfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
  listen(listenfd, 10);  

  //server active
  printf("---SERVER ACTIVE---\n");

  //opening database file
  populate_users_db();
  //overwriting ctrl+c  
  signal(SIGINT, my_exit);

  //main loop
  while(1){
    socklen_t cliLen = sizeof(client_addr);
    connfd = accept(listenfd,(struct sockaddr*)&client_addr, &cliLen);
    //check client numbers,	
	if(clientNumber < MAX_CLIENTS){
		//setting up client
		clientStr *client = (clientStr *)malloc(sizeof(clientStr));
		client->address = client_addr;
		client->sockfd = connfd;
		client->uid = uid++;
		client->logged = 0;

		add_client(client);
		if(pthread_create(&tid, NULL, &handle_client, (void*)client) != 0){
		  printf("ERROR: pthread_create.\n");
		}
	}
    
    sleep(1);    
  }

  fclose(db_file_ptr);
  return EXIT_SUCCESS;
}
