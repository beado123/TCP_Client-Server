/**
 * Networking
 * CS 241 - Spring 2017
 */
#include "common.h"
#include "format.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static const size_t MESSAGE_SIZE_DIGITS = 4;

static volatile int serverSocket;
static volatile verb myverb;
struct addrinfo hints, *result;
int sock_fd;

char **parse_args(int argc, char **argv);
verb check_args(char **args);
ssize_t get_message_size(int socket);
ssize_t write_message_size(size_t size, int socket);
ssize_t read_all_from_socket(int socket, char *buffer, size_t count);
ssize_t write_all_to_socket(int socket, const char *buffer, size_t count);

void close_server_connection() {
    // Your code here
	freeaddrinfo(result);
	close(sock_fd);
	exit(0);
}

int connect_to_server(const char *host, const char *port){

	int s;
	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;	

	s = getaddrinfo(host, port, &hints, &result);
	if(s!=0){
		fprintf(stderr, "getsddrinfo: %s\n", gai_strerror(s));
		close_server_connection();
		exit(1);
	}
	if(connect(sock_fd, result->ai_addr, result->ai_addrlen)==-1){
		perror("connect");
		print_error_message("Client connect failed.\n");
		close_server_connection();
		exit(1);
	}
	return sock_fd;
}
void write_to_server(char **args){
	
	if(myverb==GET){
		
		char request[100];
		memset(request,0,100);
		sprintf(request, "%s %s\n", args[2],args[3]);
		size_t size_request = strlen(request);
		
		write_all_to_socket(serverSocket, request, size_request);
		
		//close the write end of serverSocket
		shutdown(serverSocket, SHUT_WR);

		int file_fd = open(args[4], O_CREAT | O_TRUNC | O_RDWR, S_IRWXU);
		if(file_fd <0){print_client_help();exit(1);}

		size_t read = 0;
		size_t filesize = 0;
		char buff_verb[3];
		char *buff = NULL;
		read = read_all_from_socket(serverSocket, buff_verb,3);
		if(buff_verb[0]=='O'){
			read_all_from_socket(serverSocket, (char*)&filesize,sizeof(size_t));
			buff = calloc(1,filesize);
			read = read_all_from_socket(serverSocket, buff, filesize);
			
			if(read==filesize){
				char *temp = calloc(1,1);
				int read_temp = read_all_from_socket(serverSocket, temp,1);
				if(read_temp>0){
					print_recieved_too_much_data();
					free(temp);
					free(buff);
					buff=NULL;
					temp=NULL;
					close(file_fd);
					shutdown(serverSocket,SHUT_RD);
					close(serverSocket);
					return;
				}
				free(temp);
				temp=NULL;
				write_all_to_socket(file_fd, buff, filesize);
			}
			else if(read<filesize)print_too_little_data();
			else print_recieved_too_much_data();
			free(buff);
		}
		else if(buff_verb[0]=='E'){
			printf("%s",err_no_such_file);
		}
		else{
			print_invalid_response();
			
		}
	
		close(file_fd);
		shutdown(serverSocket,SHUT_RD);
		close(serverSocket);
		
		return;
	}
	else if(myverb==PUT){

		size_t retval=0;
		//The file we upload not exist
		if(access(args[4],F_OK)==-1){
			print_error_message("No such file.\n");
			exit(1);
		}
		
		char request[100];
		memset(request,0,100);
		sprintf(request, "%s %s\n", args[2],args[3]);
		size_t size_request = strlen(request);

		retval = write_all_to_socket(serverSocket, request, size_request);

		
		FILE *file = fopen(args[4],"r");
		fseek(file,0,SEEK_END);
		size_t size = ftell(file);
		fclose(file);
		int file_fd = open(args[4],O_RDWR, S_IRWXU);

		//send size
		retval = write_all_to_socket(serverSocket, (char*)&size, sizeof(size_t));
		
		char buff[1024];
		memset(buff,0,1024);
		while(1){
				
			memset(buff,0,1024);
			int have_read = read(file_fd,buff,1024);
			
			//read EOF done
			if(have_read==0)break;
			int written = write_all_to_socket(serverSocket, buff, have_read);
			
			if(written==-1){
				print_connection_closed();
				close(file_fd);
				exit(1);
			}
		}
		shutdown(serverSocket, SHUT_WR);
		close(file_fd);
		
		//start reading from server
		char response[2048];
		memset(response,0,2048);
		read_all_from_socket(serverSocket, response, 2048);
		if(strncmp(response,"OK\n",3)==0)print_success();
		else{
			print_error_message(response);
		}
		shutdown(serverSocket, SHUT_RD);
		close(serverSocket);
		return;
		
	}
	else if(myverb==DELETE){	
		
		char request[100];
		memset(request,0,100);
		sprintf(request, "%s %s\n", args[2],args[3]);
		size_t size_request = strlen(request);
		
		//write_message_size(size_request, serverSocket);
		write_all_to_socket(serverSocket, request, size_request);

		//close the write end of serverSocket
		shutdown(serverSocket, SHUT_WR);

		char response[2048];
		memset(response,0,2048);
		read_all_from_socket(serverSocket, response, 2048);
		if(strcmp(response,"OK\n")==0)print_success();
		else{
			print_error_message(response);
		}
		shutdown(serverSocket, SHUT_RD);
		close(serverSocket);
		return;
		
	}
	else if(myverb==LIST){

		
		//char *request = malloc(size_request);
		char request[10];
		memset(request,0,10);
		sprintf(request, "%s", "LIST\n");
		size_t size_request = strlen(request);
		
		//write_message_size(size_request, serverSocket);
		write_all_to_socket(serverSocket, request, size_request);

		//close the write end of serverSocket
		shutdown(serverSocket, SHUT_WR);

		char ok_buff[3];
		int len = read(serverSocket, ok_buff, 3);
		if(len==-1){
			print_connection_closed();
			shutdown(serverSocket,SHUT_RD);
			close(serverSocket);
			return;
		}
		if(strncmp(ok_buff,"OK\n", 3)!=0){
			print_invalid_response();
			shutdown(serverSocket,SHUT_RD);
			close(serverSocket);
			return;
		}

		size_t list_size = 0;
		len = read(serverSocket, (char*)&list_size, sizeof(size_t));
		if(len==-1){
			print_connection_closed();
			shutdown(serverSocket,SHUT_RD);
			close(serverSocket);
			return;
		}
		printf("Expect %zu bytes from server.\n", list_size);

		char buff[1024];
		memset(buff,0,1024);
		size_t read_total = 0;
		while(1){

			memset(buff,0,1024);
			len =read(serverSocket, buff, 1024);
			if(len==-1){
				print_connection_closed();
				shutdown(serverSocket,SHUT_RD);
				close(serverSocket);
				return;
			}
			read_total += len;
			//read EOF, done
			if(len==0)break;
			write(1, buff,len);
		}
		printf("Received %zu bytes from server.\n", read_total);

		shutdown(serverSocket,SHUT_RD);
		close(serverSocket);
		return;
	}
}


int main(int argc, char **argv) {
  // Good luck!

	myverb = check_args(argv);
	char **args = parse_args(argc,argv);

	//connect to server
	serverSocket = connect_to_server(args[0], args[1]);
	//printf("serverSocket:%d\n",serverSocket);
	
	write_to_server(args);
	free(args);
	close_server_connection();
	
	
	return 0;	//need modified
}

ssize_t get_message_size(int socket) {
    int32_t size;
    ssize_t read_bytes =
        read_all_from_socket(socket, (char *)&size, MESSAGE_SIZE_DIGITS );
    if (read_bytes == 0 || read_bytes == -1)
        return read_bytes;
	//printf("read_bytes:%zu\n",read_bytes);
	//printf("size: %u\n",size);
	//printf("ntohl(size):%u\n",ntohl(size));
    return size;
}
ssize_t write_message_size(size_t size, int socket) {
    // Your code here
	//printf("write_message_size: size: %lu\n",size);
	int32_t newsize = (size); 
	//printf("write_message_size: newsize: %lu\n",newsize);
	ssize_t len = write_all_to_socket(socket, (char*)&newsize, MESSAGE_SIZE_DIGITS );
	
    return len;
}
ssize_t read_all_from_socket(int socket, char *buffer, size_t count) {
    // Your Code Here
	
	size_t read_total = 0;
	while(read_total < count){

		int len = read(socket, buffer+read_total, count - read_total);

		//read EOF and socket is disconnected
		if(len==0)return 0;
		else if(len==-1){
			//printf("enter len==-1");
			printf("%s\n",strerror(errno));
			if(errno==EINTR)continue;
			else if(errno==EBADF){printf("EBADF\n");return -1;}
			else return 0;
		}
		else if(len>0){
			read_total = read_total + len;
			//printf("buffer in read_all: %p\n",buffer);
		}
	}
	
    return read_total;
}
ssize_t write_all_to_socket(int socket, const char *buffer, size_t count) {
    // Your Code Here
	size_t write_total = 0;;
	//printf("write_all_to_socket: count: %lu\n",count);
	while(write_total < count){

		ssize_t len = write(socket, buffer + write_total, count - write_total);
		if(len==-1){
			printf("%s\n",strerror(errno));
			if(errno==EPIPE)return 0;
			else if(errno==EBADF)return -1;
			else if(errno!=EINTR) return -1;
		}
		else if(len>0){
			write_total = write_total + len;
		}
	}
    return write_total;
}

/**
 * Given commandline argc and argv, parses argv.
 *
 * argc argc from main()
 * argv argv from main()
 *
 * Returns char* array in form of {host, port, method, remote, local, NULL}
 * where `method` is ALL CAPS
 */
char **parse_args(int argc, char **argv) {
  if (argc < 3) {
    return NULL;
  }

  char *host = strtok(argv[1], ":");
  char *port = strtok(NULL, ":");
  if (port == NULL) {
    return NULL;
  }

  char **args = calloc(1, 5 * sizeof(char*));
  args[0] = host;
  args[1] = port;
  args[2] = argv[2];
  char *temp = args[2];
  while (*temp) {
    *temp = toupper((unsigned char)*temp);
    temp++;
  }
  if (argc > 3) {
    args[3] = argv[3];
  } else {
    //args[3] = NULL;
  }
  if (argc > 4) {
    args[4] = argv[4];
  } else {
    //args[4] = NULL;
	
  }

  return args;
}

/**
 * Validates args to program.  If `args` are not valid, help information for the
 * program is printed.
 *
 * args     arguments to parse
 *
 * Returns a verb which corresponds to the request method
 */
verb check_args(char **args) {
  if (args == NULL) {
    print_client_usage();
    exit(1);
  }

  char *command = args[2];

  if (strcmp(command, "LIST") == 0) {
    return LIST;
  }

  if (strcmp(command, "GET") == 0) {
    if (args[3] != NULL && args[4] != NULL) {
      return GET;
    }
    print_client_help();
    exit(1);
  }

  if (strcmp(command, "DELETE") == 0) {
    if (args[3] != NULL) {
      return DELETE;
    }
    print_client_help();
    exit(1);
  }

  if (strcmp(command, "PUT") == 0) {
    if (args[3] == NULL || args[4] == NULL) {
      print_client_help();
      exit(1);
    }
    return PUT;
  }

  // Not a valid Method
  print_client_help();
  exit(1);
}
