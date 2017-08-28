/**
 * Networking
 * CS 241 - Spring 2017
 */
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
#include <sys/wait.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <string.h>

#include "common.h"
#include "format.h"
#include "dictionary.h"
#include "vector.h"
#define MAX_CLIENTS 10
#define ACCEPT 0
#define HEADER_PARSED 1
#define HEADER_NOTPARSED 2

static volatile int endSession;
//static volatile int currIndex;
static volatile int clients[MAX_CLIENTS];
int read_fd_count = 10;
int write_fd_count = 10;

//global vars for state dictionary and file vector 
dictionary *dict;
vector *vec;
char *temp_dir;
 
struct addrinfo hints, *result;
struct epoll_event *events;
int sock_fd;
int serverSocket;
int curr_event;
int epfd;

ssize_t read_all_from_socket(int socket, char *buffer, size_t count);
ssize_t write_all_to_socket(int socket, const char *buffer, size_t count);
void *my_char_copy_constructor(void *elem);
void my_char_destructor(void *elem);

void read_from_client(int client_fd);
void parse_header(int client_fd);
void list_helper1(int client_fd);
void list_helper2(int client_fd);
void put_helper1(int client_fd, char *header, size_t header_len);
void put_helper2(int client_fd);
int parse_size(int client_fd);
void send_ok(int client_fd);
void send_bad_request(int client_fd);
void send_bad_file_size(int client_fd);
void send_no_such_file(int client_fd);
void get_helper1(int client_fd, char *filename);
void get_helper2(int client_fd);
void delete_helper(int client_fd, char *filename);
void remove_values(int client_fd);

void close_server() {
    // Your code here.

	//remove all memories from dict
	vector *values = dictionary_values(dict);
	for(size_t i=0;i<vector_size(values);i++){
		char **value = vector_get(values,i);
		for(int i=0;i<9;i++){
			if(i!=6){
				free(value[i]);
				printf("freed %d\n",i);
			}
		}
		free(value);
	}
	dictionary_destroy(dict);

	

	//remove all files in the directory
	for(size_t i=0;i<vector_size(vec);i++){
		char *file = vector_get(vec,i);
		if(access(file, F_OK)==0){
			if(unlink(file)==-1){
				perror("Failed to remove file upon exiting server.\n");
				fprintf(stderr, "unlink,errno:%s\n",strerror(errno));
				continue;
			}
			printf("Successfully removed file: %s\n",file);
		}
	}
	
	
	//remove directory we created
	if(chdir("..")==-1){
		perror("Change directory failed.\n");
		exit(1);
	}
	int ret = rmdir(temp_dir);
	if(ret==-1){
		fprintf(stderr,"errno:%s\n",strerror(errno));
		exit(1);
	}
	//remove all memories from vec
	vector_destroy(vec);
	vector_destroy(values);

	free(events);
	close(sock_fd);
	exit(0);
}
void add_client(client_fd){

	for(int i=0;i<MAX_CLIENTS;i++){
		if(clients[i]==-1){
			clients[i] = client_fd;
			return;
		}
	}
}
void make_nonblocking(int fd){
	
	int flags = fcntl(fd, F_GETFL,0);
	if(flags==-1){
		perror("fcntl");
		exit(1);
	}
	if(fcntl(fd, F_SETFL, flags | O_NONBLOCK)==-1){
		perror("fcntl");
		exit(1);
	}
}
void accept_connections(int epfd, struct epoll_event e){

	while(1){

		struct sockaddr_in new_addr;
		socklen_t new_len = sizeof(new_addr);
		int client_fd = accept(sock_fd, (struct sockaddr*)&new_addr, &new_len);

		if(client_fd==-1){
			//NO connections are present to be accepted
			if(errno==EAGAIN || errno==EWOULDBLOCK)break;
			else{
				perror("accept error\n");
				exit(1);
			}
		}
		
		//add the client into dict structure
		int *key = malloc(sizeof(int));
		*key = client_fd;
		char **value = malloc(9*sizeof(char*));

		//0: ACCEPT/PARSED   1:header  2: file_fd  3. file_size  4. file_name 5.read_total 6.buff 7. left(GET) 8. fd (LIST)
		for(int i=0;i<9;i++){
			value[i] = malloc(1024);
		}
		sprintf(value[5],"%d",0);
		strcpy(value[0],"ACCEPT");
		value[0][6] = '\0';
		strcpy(value[6],"");
		sprintf(value[7],"%d",0);
		
		//printf("In accept function: set value[0]:%s\n",value[0]);

		dictionary_set(dict,key,value);
		free(key);
		key=NULL;
		

		add_client(client_fd);
		printf("User %d join\n", client_fd);
		make_nonblocking(client_fd);
	
		//add client_fd to epoll
		e.data.fd = client_fd;
		e.events = EPOLLIN | EPOLLET;
		if(epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &e)==-1){
			perror("epoll_ctl");
			exit(1);
		}
	
	}

}

void run_server(char *port) {

	int s;
	sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK , 0);
	
	struct addrinfo hints, *result;
	memset(&hints, 0, sizeof(struct addrinfo));
	//set to reuse sock_fd
	int optval = 1;
	setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optval, sizeof(optval));
	
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	s = getaddrinfo(NULL, port, &hints, &result);
	if(s!=0){
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		close_server();
		exit(1);
	}
	
	if(bind(sock_fd, result->ai_addr, result->ai_addrlen)!=0){
		perror("Server bind() failed.\n");
		close_server();
		exit(1);
	}
	if(listen(sock_fd, MAX_CLIENTS)!=0){		//10 needs modified
		perror("Server listen() failed.\n");
		close_server();
		exit(1);
	}
	struct sockaddr_in *result_addr = (struct sockaddr_in*)result->ai_addr;
	printf("Listening on fd %d, port %d\n", sock_fd, ntohs(result_addr->sin_port));
	freeaddrinfo(result);
	
	epfd = epoll_create(MAX_CLIENTS);
	//event for sock_fd
	struct epoll_event event;
	event.data.fd = sock_fd;
	event.events = EPOLLIN |EPOLLET;
	if(epoll_ctl(epfd, EPOLL_CTL_ADD, sock_fd, &event)==-1){
		fprintf(stderr, "epoll_ctl failed.\n");
		exit(1);
	}

	//a pointer to all incoming clients events
	events = calloc(MAX_CLIENTS,sizeof(struct epoll_event));
	
	//event loop
	while(1){
		printf("Waiting for connection...\n");
		int num_ready = epoll_wait(epfd, events,  MAX_CLIENTS, -1);
		if(num_ready < 0)continue;
		for(int i=0;i<num_ready;i++){

			curr_event = i;

			//The listening socket for server is ready
			if(sock_fd==events[i].data.fd){
				//printf("enter accept function\n");
				accept_connections(epfd, events[i]);
				//printf("done accept function\n");
				continue;
			}
			else{
				char **value = dictionary_get(dict, &events[i].data.fd);

				if(strcmp(value[0],"ACCEPT")==0){
					parse_header(events[i].data.fd);
				}
				else if(strcmp(value[0],"HEADER")==0){
					parse_size(events[i].data.fd);
				}
				else if(strcmp(value[0],"SIZE")==0){
					if(strncmp(value[1],"PUT",3)==0)put_helper2(events[i].data.fd);
				}
				else if(strcmp(value[0],"CONTINUE GET")==0){
					get_helper2(events[i].data.fd);
				}
				else if(strcmp(value[0],"CONTINUE LIST")==0){
					list_helper2(events[i].data.fd);
				}
			}	
			
		}	
	}	
	
	
}

void parse_header(int client_fd){

	char **value = dictionary_get(dict, &client_fd);

	char buff[1024];
	char header[1024];

	memset(header, 0, 1024);
	memset(buff, 0, 1024);

	size_t header_len = 0;
	
	int len = read(client_fd, buff, 1024);
	//printf("read %d bytes in header\n",len);
		
	int flag = 0;
	int index = 0;
	while(index < 270){

		char curr = buff[index];
		header_len++;
		index++;
		if(curr=='\n'){
			flag = 1;
			break;
		}
	}
	
	//test for no '\n' in header
	if(flag==0){
		send_bad_request(client_fd);
		shutdown(client_fd, SHUT_RDWR);
		remove_values(client_fd);
		dictionary_remove(dict, &client_fd);
		close(client_fd);
		return;
	}
	strncpy(header,buff,index);
	header[header_len] = '\0';
	printf("header: %s\n",header);
	//printf("header_len: %zu\n",header_len);
	//change state to HEADER
	strcpy(value[0],"HEADER");
	strcpy(value[1],header);

	
	//test for extra sent header (list)
	if(strncmp(header, "LIST\n",5)==0){
		char buff[1];
		memset(buff,0,1);
		int ret = read_all_from_socket(client_fd,buff,1);
		if(ret>0){
			send_bad_request(client_fd);
			shutdown(client_fd, SHUT_RDWR);
			remove_values(client_fd);
			dictionary_remove(dict, &client_fd);
			close(client_fd);
			return;
		}
	}

	char *space = strchr(header,' ');

	if(strcmp(header,"LIST\n")==0)list_helper1(client_fd);
	else{
		//test for space between verb & filename
		if(space==NULL){
			send_bad_request(client_fd);
			shutdown(client_fd, SHUT_RDWR);
			remove_values(client_fd);
			dictionary_remove(dict, &client_fd);
			close(client_fd);
			return;
		}
		else{
			

			if(strncmp(header,"PUT",3)==0){

				char filename[255];
				memset(filename,0,255);
				strcpy(filename, header+4);
				filename[strlen(filename)-1] = '\0';
				sprintf(value[4],"%s", filename);
				//printf("filename:%s\n",filename);

				int file_fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU | O_APPEND);
				//save file_fd
				sprintf(value[2],"%d", file_fd);

				vector_push_back(vec,filename); 

				char *newline = strchr(buff,'\n');
				//need to read size
				//printf("newline-buff+1 : %ld\n",newline-buff+1);
				if((newline-buff+1)==len){
					int ret = parse_size(client_fd);
					if(ret ==-2 || ret==0)return;
				}	
	
				size_t file_size = 0;
				memcpy(&file_size, buff+header_len,sizeof(size_t));
				//printf("file_size:%zu\n",file_size);
				sprintf(value[3],"%zu", file_size);
				//change state to SIZE
				strcpy(value[0],"SIZE");

				size_t read_total = atoi(value[5]);

				int written = write(file_fd, buff+header_len+sizeof(size_t), len-header_len-8);
				//printf("written %d bytes to file in parse header\n",written);
				//if server receive SIGPIPE, close connection to client
				if(written==-2){
					remove_values(client_fd);
					dictionary_remove(dict, &client_fd);
					close(file_fd);
					close(client_fd);
				}
				read_total += written;
				//printf("read_total:%zu\n",read_total);	
				sprintf(value[5],"%zu", read_total);
				//parse_size(client_fd);
				put_helper2(client_fd);

			}
			else if(strncmp(header,"GET",3)==0){
			
				char filename[255];
				memset(filename,0,255);
				strcpy(filename, header+4);
				filename[strlen(filename)-1] = '\0';
				sprintf(value[4],"%s", filename);
				//printf("filename:%s\n",filename);

				get_helper1(client_fd, filename);

			}
			else if(strncmp(header,"DELETE",6)==0){
				
				char filename[255];
				memset(filename,0,255);
				strcpy(filename, header+7);
				filename[strlen(filename)-1] = '\0';
				sprintf(value[4],"%s", filename);
				//printf("filename:%s\n",filename);
				delete_helper(client_fd, filename);
			}
			//test for nonexistent verb
			else{
				send_bad_request(client_fd);
				shutdown(client_fd, SHUT_RDWR);
				//remove client_fd from dict and close connection
				remove_values(client_fd);
				dictionary_remove(dict, &client_fd);
				close(client_fd);
				return;
			}
		} 
	}
	return;
}

void get_helper2(int client_fd){

	char **value = dictionary_get(dict, &client_fd);
	int file_fd = atoi(value[2]);
	char *left_buff = value[6];
	int left = atoi(value[7]);
	int write_total = atoi(value[5]);
	
	char buff[1024];
	memset(buff,0,1024);

	while(1){
			
		memset(buff,0,1024);
		int written = 0;
		int have_read = 0;

		if(left>0){

			written = write(client_fd, left_buff, left);
		}
		else{

			have_read = read(file_fd, buff, 1024);
			printf("Helper2: read %d bytes from file\n", have_read);
			//if we read EOF, we're done and break.
			if(have_read==0)break;

			written = write(client_fd, buff, have_read);
		}
		write_total += written;
		printf("Helper2: Written %d bytes to client\n",written);

		if(written==-1){

			if(errno==EPIPE){
				
				remove_values(client_fd);
				dictionary_remove(dict, &client_fd);
				close(file_fd);
				shutdown(client_fd, SHUT_RDWR);
				close(client_fd);
				break;
			}
			else if(errno==EAGAIN || errno==EWOULDBLOCK){

				fprintf(stderr, "write to socket: %s\n",strerror(errno));
				strcpy(value[6], buff);
				strcpy(value[0],"CONTINUE GET");
				return;
			}
		}
		else if(written>0){
			
			if(written < have_read){
				fprintf(stdout, "Written < read, go backt to event loop\n");
				memcpy(value[6], buff+written, have_read - written);
				sprintf(value[7],"%d", have_read-written);
				strcpy(value[0],"CONTINUE GET");
				printf("have_read-written: %d\n",have_read-written);
				sprintf(value[5],"%d", write_total);
				return;	
			}
			else if(written < left){
				
				fprintf(stdout, "written < left, go backt to event loop\n");
				strcpy(value[6], left_buff+written);
				sprintf(value[7],"%d", left-written);
				strcpy(value[0],"CONTINUE GET");
				return;	
			}
			else if(written == left)left=0;
		}
			
		
		
	}
	printf("write_total:%d\n",write_total);
	strcpy(value[0],"DONE");
	close(file_fd);
	shutdown(client_fd, SHUT_WR);
	close(client_fd);
	remove_values(client_fd);
	dictionary_remove(dict, &client_fd);
	//free(left_buff);
}

void get_helper1(int client_fd, char *filename){

	//change the mode we want to monitor
	events[curr_event].events = EPOLLOUT;
	epoll_ctl(epfd, EPOLL_CTL_MOD, client_fd, &events[curr_event]);

	char **value = dictionary_get(dict, &client_fd);
	int write_total = atoi(value[5]);
	shutdown(client_fd, SHUT_RD);

	int file_fd = open(filename, O_RDONLY, S_IRWXU);
	if(file_fd == -1){

		fprintf(stderr, "open: %s\n",strerror(errno));
		send_no_such_file(client_fd);
		shutdown(client_fd, SHUT_WR);
		remove_values(client_fd);
		dictionary_remove(dict, &client_fd);
		close(client_fd);
		return;
	}
	else send_ok(client_fd);
	sprintf(value[2],"%d", file_fd);

	struct stat s;
	fstat(file_fd, &s);
	size_t file_size = s.st_size;
	//printf("file size: %zu\n",file_size);

	//send size
	write_all_to_socket(client_fd, (char*)&file_size, sizeof(size_t));	
	//printf("Written %d bytes of file_size to client\n",retval);

	char buff[1024];
	memset(buff,0,1024);

	while(1){
			
		memset(buff,0,1024);
		
		int have_read = read(file_fd, buff, 1024);
		printf("Helper1: read %d bytes from file\n", have_read);
		//if we read EOF, we're done and break.
		if(have_read==0)break;

		int written = write(client_fd, buff, have_read);
		write_total += written;
		printf("Helper1: Written %d bytes to client\n",written);

		if(written==-1){

			if(errno==EPIPE){

				remove_values(client_fd);
				dictionary_remove(dict, &client_fd);
				close(file_fd);
				shutdown(client_fd, SHUT_RDWR);
				close(client_fd);
				break;
			}
			else if(errno==EAGAIN || errno==EWOULDBLOCK){

				fprintf(stderr, "write to socket: %s\n",strerror(errno));
				strcpy(value[6], buff);
				strcpy(value[0],"CONTINUE GET");
				return;
			}
		}
		else if(written>0 && written < have_read){
			
			fprintf(stdout, "written < read, go backt to event loop\n");
			printf("have_read-written: %d\n",have_read-written);
			memcpy(value[6], buff+written, have_read - written);
			sprintf(value[7],"%d", have_read-written);
			sprintf(value[5],"%d", write_total);
			strcpy(value[0],"CONTINUE GET");
			return;	
		}			
		
	}
	printf("write_total:%d\n",write_total);
	strcpy(value[0],"DONE");
	close(file_fd);
	shutdown(client_fd, SHUT_WR);
	close(client_fd);
	remove_values(client_fd);
	dictionary_remove(dict, &client_fd);
}

void list_helper2(int client_fd){

	char **value = dictionary_get(dict, &client_fd);
	int file_fd = atoi(value[8]);
	char *left_buff = value[6];
	int left = atoi(value[7]);
	//printf("LIST HELPER2: left:%d\n",left);
	//printf("LIST HELPER2: left_buff:%s\n",left_buff);
	char buff[1024];
	memset(buff,0,1024);

	while(1){
			
		memset(buff,0,1024);
		int written = 0;
		int have_read = 0;

		if(left>0){

			written = write(client_fd, left_buff, left);
		}
		else{

			have_read = read(file_fd, buff, 1024);
			//printf("LIST Helper2: read %d bytes from file\n", have_read);
			//if we read EOF, we're done and break.
			if(have_read==0)break;

			written = write(client_fd, buff, have_read);
		}
		//write_total += written;
		//printf("LIST Helper2: Written %d bytes to client\n",written);

		if(written==-1){

			if(errno==EPIPE){

				remove_values(client_fd);
				dictionary_remove(dict, &client_fd);
				close(file_fd);
				shutdown(client_fd, SHUT_RDWR);
				close(client_fd);
				break;
			}
			else if(errno==EAGAIN || errno==EWOULDBLOCK){

				fprintf(stderr, "write to socket: %s\n",strerror(errno));
				strcpy(value[6], buff);
				strcpy(value[0],"CONTINUE LIST");
				return;
			}
		}
		else if(written>0){
			
			if(written < have_read){
				//fprintf(stdout, "Written < read, go backt to event loop\n");
				memcpy(value[6], buff+written, have_read - written);
				sprintf(value[7],"%d", have_read-written);
				strcpy(value[0],"CONTINUE LIST");
				//printf("have_read-written: %d\n",have_read-written);
				//sprintf(value[5],"%d", write_total);
				return;	
			}
			else if(written < left){
				
				//fprintf(stdout, "written < left, go backt to event loop\n");
				memcpy(value[6], left_buff+written, left-written);
				sprintf(value[7],"%d", left-written);
				strcpy(value[0],"CONTINUE LIST");
				return;	
			}
			else if(written == left)left=0;
		}
			
		
		
	}
	//printf("write_total:%d\n",write_total);
	strcpy(value[0],"DONE");
	close(file_fd);
	if(unlink(value[4])==-1){
		perror("Failed to remove filelist after List.\n");
		fprintf(stderr, "unlink,errno:%s\n",strerror(errno));
		exit(1);
	}
	shutdown(client_fd, SHUT_WR);
	close(client_fd);
	remove_values(client_fd);
	dictionary_remove(dict, &client_fd);

}

void list_helper1(int client_fd){

	events[curr_event].events = EPOLLOUT;
	epoll_ctl(epfd, EPOLL_CTL_MOD, client_fd, &events[curr_event]);

	char **value = dictionary_get(dict, &client_fd);

	shutdown(client_fd, SHUT_RD);
	
	char filename[9];
	memset(filename,0,9);
	strcpy(filename, "filelist");
	strcpy(value[4],filename);
	int file_fd = open(filename, O_CREAT | O_TRUNC | O_RDWR, S_IRWXU | O_APPEND);

	size_t size = 0;

	size_t size_cwd = 4096;
	char *cwd1 = getcwd(NULL,size_cwd);
	DIR *dir;
	struct dirent *ent;
	if ((dir = opendir(cwd1)) != NULL) {
		ent = readdir (dir);
		while (ent != NULL) {
	
			if(strcmp(ent->d_name,"..")==0 || strcmp(ent->d_name,".")==0 || strcmp(ent->d_name,filename)==0){
				ent = readdir (dir);
				continue;
			}
			size = size + strlen(ent->d_name)+1;
			write(file_fd, ent->d_name, strlen(ent->d_name));
			write(file_fd, "\n",1);
			ent = readdir (dir);
  		}
	}	
	closedir(dir);
	close(file_fd);
	size--;

	//if empty directory
	if((int)size==-1){

		send_ok(client_fd);
		size = 0;
		write_all_to_socket(client_fd, (char*)&size, sizeof(size_t));
		if(unlink(filename)==-1){
			perror("Failed to remove filelist after List.\n");
			fprintf(stderr, "unlink,errno:%s\n",strerror(errno));
			exit(1);
		}
		close(file_fd);
		shutdown(client_fd, SHUT_WR);
		close(client_fd);
		remove_values(client_fd);
		dictionary_remove(dict, &client_fd);
		free(cwd1);
		return;
	}

	//truncate last '\n' bit
	file_fd = open(filename, O_RDWR, S_IRWXU);
	
	int ret = ftruncate(file_fd,size);
	if(ret==-1){
		fprintf(stderr, "ftruncate: %s\n",strerror(errno));
	}
	
	close(file_fd);

	file_fd = open(filename, O_RDWR, S_IRWXU);
	sprintf(value[8],"%d", file_fd);
	
	//send OK
	send_ok(client_fd);

	//send file_size
	int written = write_all_to_socket(client_fd, (char*)&size, sizeof(size_t));
	//printf("Written %d bytes list size to client\n",written);
	if(written==-2){
		remove_values(client_fd);
		dictionary_remove(dict, &client_fd);
		close(client_fd);
	}

	char buff[1024];
	memset(buff,0,1024);
	while(1){
			
		memset(buff,0,1024);
		
		int have_read = read(file_fd, buff, 1024);
		//printf("LIST helper1: read %d bytes from file\n", have_read);
		//if we read EOF, we're done and break.
		if(have_read==0)break;

		int written = write(client_fd, buff, have_read);
		//write_total += written;
		//printf("LIST Helper1: Written %d bytes to client\n",written);
	
		if(written==-1){

			if(errno==EPIPE){

				remove_values(client_fd);
				dictionary_remove(dict, &client_fd);
				close(file_fd);
				shutdown(client_fd, SHUT_RDWR);
				close(client_fd);
				break;
			}
			else if(errno==EAGAIN || errno==EWOULDBLOCK){

				fprintf(stderr, "write to socket: %s\n",strerror(errno));
				strcpy(value[6], buff);
				strcpy(value[0],"DONE");
				return;
			}
		}
		else if(written>0 && written < have_read){
			
			//fprintf(stdout, "written < read, go backt to event loop\n");
			//printf("have_read-written: %d\n",have_read-written);
			memcpy(value[6], buff+written, have_read - written);
			sprintf(value[7],"%d", have_read-written);
			//sprintf(value[5],"%d", write_total);
			strcpy(value[0],"CONTINUE LIST");
			free(cwd1);
			return;	
		}			
		
	}
	//printf("write_total:%d\n",write_total);
	strcpy(value[0],"DONE");
	close(file_fd);
	if(unlink(filename)==-1){
		perror("Failed to remove filelist after List.\n");
		fprintf(stderr, "unlink,errno:%s\n",strerror(errno));
		exit(1);
	}
	shutdown(client_fd, SHUT_WR);
	close(client_fd);
	remove_values(client_fd);
	dictionary_remove(dict, &client_fd);
	free(cwd1);
}

void delete_helper(int client_fd, char *filename){

	FILE *file = fopen(filename, "r");
	if(file==NULL){
		send_no_such_file(client_fd);
		shutdown(client_fd, SHUT_RDWR);
		remove_values(client_fd);
		dictionary_remove(dict, &client_fd);
		close(client_fd);
		return;
	}
	else{
		send_ok(client_fd);

		//send stop reading/writing signal to client
		shutdown(client_fd, SHUT_RDWR);

		//remove file from vector
		for(size_t i=0;i<vector_size(vec);i++){
			char *name = vector_get(vec,i);
			if(strcmp(name, filename)==0){
				vector_erase(vec,i);
			}
		}

		fclose(file);
		if(unlink(filename)==-1){
			perror("Failed to remove file upon receive too much data.\n");
			fprintf(stderr, "unlink,errno:%s\n",strerror(errno));
			exit(1);
		}
	}
	
	close(client_fd);
	remove_values(client_fd);
	dictionary_remove(dict, &client_fd);
}

void put_helper1(int client_fd, char *header, size_t header_len){
 
	char **value = dictionary_get(dict, &client_fd);
	char *buff = value[6];

	//test for space between verb & filename
	if(strncmp(header,"PUT ",4)!=0){

		send_bad_request(client_fd);
		shutdown(client_fd, SHUT_RDWR);
		//remove client_fd from dict and close connection
		remove_values(client_fd);
		dictionary_remove(dict, &client_fd);
		close(client_fd);
		return;
	}
	//char **value = dictionary_get(dict, &client_fd);

	char filename[255];
	memset(filename,0,255);
	strcpy(filename, header+4);
	filename[strlen(filename)-1] = '\0';
	sprintf(value[4],"%s", filename);
	printf("filename:%s\n",filename);
	int file_fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU | O_APPEND);
	//save file_fd
	vector_push_back(vec,filename); 
	sprintf(value[2],"%d", file_fd);	
	
	size_t file_size = 0;
	memcpy(&file_size, buff+header_len,sizeof(size_t));
	printf("file_size:%zu\n",file_size);
	sprintf(value[3],"%zu", file_size);
	//change state to SIZE
	strcpy(value[0],"SIZE");

	size_t read_total = atoi(value[5]);

	int written = write_all_to_socket(file_fd, buff+header_len+sizeof(size_t), strlen(buff)-header_len-sizeof(size_t));
	//if server receive SIGPIPE, close connection to client
	if(written==-2){
		remove_values(client_fd);
		dictionary_remove(dict, &client_fd);
		close(file_fd);
		close(client_fd);
	}
	read_total += written;
	printf("read_total:%zu\n",read_total);	
	sprintf(value[5],"%zu", read_total);
	//parse_size(client_fd);
	put_helper2(client_fd);
	free(buff);
	return;
}

int parse_size(int client_fd){
	printf("In parse size......\n");
	char **value = dictionary_get(dict, &client_fd);
	size_t file_size = 0;
	while(file_size==0){

		int len = read(client_fd, (char*)&file_size, sizeof(size_t));
		printf("len:%d\n",len);
		if(len==-1){
			if(errno==EAGAIN){
				printf("failed read size, go back to event loop\n");
				return -2;
			}
		}
		//we've read size bytes already and probabaly data
		else if(len==0)return -1;
		//printf("file_size: %zu\n",file_size);
		sprintf(value[3],"%zu", file_size);
	}
	strcpy(value[0],"SIZE");
	put_helper2(client_fd);
	return 0;
}

void put_helper2(int client_fd){

	//get the file_fd of the file we want to write
	char **value = dictionary_get(dict, &client_fd);
	int file_fd = atoi(value[2]);	
	printf("in put_helper2, file_fd:%d\n",file_fd);
	char *file = value[4];
	size_t file_size = atoi(value[3]);
	size_t read_total = atoi(value[5]);
	
	while(1){

		char data[1024];
		memset(data,0,1024);

		int len = read(client_fd, data, 1024);
		printf("read %d bytes of data\n", len);

		if(len==-1){
			//we've read all data from this event, go back to event loop for next event
			if(errno==EAGAIN){
				printf("Read all from this event, go back to event loop\n");
				
				break;
			}	
			//other error occurrs
			else{
				printf("Closed connection on descriptor %d with error on read\n", client_fd);
				remove_values(client_fd);
				dictionary_remove(dict, &client_fd);
				close(file_fd);
				close(client_fd);
				break;
			}
		}
		//we've read EOF, the client close write end of the fd
		else if(len==0){
			
			//too little data
			if(read_total < file_size){
				//1. remove file
				if(unlink(file)==-1){
					perror("Failed to remove file upon receive too much data.\n");
					fprintf(stderr, "unlink,errno:%s\n",strerror(errno));
					exit(1);
				}
				printf("Removed file b/c too little data:%s\n",file);
				//2. clean up state in dict for client_fd
				remove_values(client_fd);
				dictionary_remove(dict, &client_fd);
				//3. send error msg:  ERROR\nBad file size
				send_bad_file_size(client_fd);
				//close connection
				close(client_fd);
				break;
			}
			//too much data
			else if(read_total > file_size){
				//1. remove file
				if(unlink(file)==-1){
					perror("Failed to remove file upon receive too much data.\n");
					fprintf(stderr, "unlink,errno:%s\n",strerror(errno));
					exit(1);
				}
				printf("Removed file b/c too much data:%s\n",file);
				//2. clean up state in dict for client_fd
				remove_values(client_fd);
				dictionary_remove(dict, &client_fd);
				//3. send error msg:  ERROR\nBad file size
				send_bad_file_size(client_fd);
				//close connection
				close(client_fd);
				break;
			}
			read_total = 0;//reset read_total
			printf("Closed connection on descriptor %d with EOF\n", client_fd);
			shutdown(client_fd, SHUT_RD);
			send_ok(client_fd);
			remove_values(client_fd);
			dictionary_remove(dict, &client_fd);
			close(client_fd);
			close(file_fd);
			break;
		}
		read_total += len;
		//printf("read_total:%zu\n",read_total);	
		sprintf(value[5],"%zu", read_total);
		
		
		int written = write_all_to_socket(file_fd, data, len);
		//printf("written %d bytes to file\n",written);

		//if server receive SIGPIPE, close connection to client
		if(written==-2){
			remove_values(client_fd);
			dictionary_remove(dict, &client_fd);
			close(file_fd);
			close(client_fd);
		}
	}//while
	
}
void remove_values(int client_fd){
	
	char **value = dictionary_get(dict, &client_fd);
	for(int i=0;i<9;i++){
		free(value[i]);
	}
	free(value);
}
void send_ok(int client_fd){

	char response[10];
	memset(response,0,10);
	strcpy(response,"OK\n");
	response[3] = '\0';
	int written = write_all_to_socket(client_fd, response, 3);//not 4!!!!!!!!!!!
	if(written==-2){
		remove_values(client_fd);
		dictionary_remove(dict, &client_fd);
		close(client_fd);
	}
	printf("Written %d bytes response(ok) to client: %d\n",written, client_fd);
	return;
}

void send_no_such_file(int client_fd){

	char response[21];
	memset(response, 0, 21);
	strcpy(response,"ERROR\nNo such file\n");
	response[20] = '\0';
	int written = write_all_to_socket(client_fd, response, 21);
	if(written==-2){
		remove_values(client_fd);
		dictionary_remove(dict, &client_fd);
		close(client_fd);
	}
	printf("Written %d bytes response(no such file) to client: %d\n",written, client_fd);

	return;
}
void send_bad_file_size(int client_fd){

	char response[21];
	memset(response, 0, 20);
	strcpy(response,"ERROR\nBad file size\n");
	response[20] = '\0';
	int written = write_all_to_socket(client_fd, response, 21);
	if(written==-2){
		remove_values(client_fd);
		dictionary_remove(dict, &client_fd);
		close(client_fd);
	}
	printf("Written %d bytes response(bad file size) to client: %d\n",written, client_fd);

	return;
}
void send_bad_request(int client_fd){
	
	char response[20];
	memset(response, 0, 20);
	strcpy(response,"ERROR\nBad request\n");
	//printf("response:%s",response);
	//printf("here\n");
	response[18] = '\0';
	int written = write_all_to_socket(client_fd, response, 19);
	if(written==-2){
		remove_values(client_fd);
		dictionary_remove(dict, &client_fd);
		close(client_fd);
	}
	printf("Written %d bytes response(bad request) to client: %d\n",written, client_fd);

	return;
}

int main(int argc, char **argv) {
  // Good luck!
	endSession=0;
	//verb myverb = check_args(argv);
	for(int i=0;i<MAX_CLIENTS;i++){
		clients[i] = -1;
	}

	dict = dictionary_create(int_hash_function, int_compare,
                              int_copy_constructor, /*key copy constructor*/
                              int_destructor,	    /*key destructor*/
                              shallow_copy_constructor, /*value copy contructor*/
                              shallow_destructor        /*value destructor*/);
	
	
	vec = vector_create(my_char_copy_constructor,  my_char_destructor, char_default_constructor);

	//set up the program to receive SIGINT
	struct sigaction act;
	memset(&act, '\0', sizeof(act));
    	act.sa_handler = close_server;
    	if (sigaction(SIGINT, &act, NULL) < 0) {
        	perror("sigaction");
        	return 1;
    	}
	//ignore SIGPIPE
	sigignore(SIGPIPE);

	//create a temp directory
	char template[] = "storageXXXXXX";
	temp_dir = mkdtemp(template);
	if(temp_dir==NULL){
		perror("Failed to create temporary directory.\n");
		exit(1);
	}
	else print_temp_directory(temp_dir);

	if(chdir(temp_dir)==-1){
		perror("Change directory failed.\n");
		exit(1);
	}
	run_server(argv[1]);

	return 0;	//needs modified
}

ssize_t read_all_from_socket(int socket, char *buffer, size_t count) {
    // Your Code Here
	//printf("count: %lu\n",count);
	size_t read_total = 0;
	while(read_total < count){

		int len = read(socket, buffer+read_total, count - read_total);

		//read EOF and socket is disconnected
		//if it's a "List\n" AND 5<270, should return 5
		if(len==0){
			if(read_total>0)return read_total;
			else return 0;
		}
		else if(len==-1){
			
			if(errno==EINTR)continue;
			else if(errno==EAGAIN)return -2;
			else return -1;
		}
		else if(len>0){
			read_total = read_total + len;
			//printf("buffer in read_all: %p\n",buffer);
		}
	}
	//buffer = temp_buff;
	//printf("buffer:%s\n buffer_read_total: %s\n",buffer,buffer+read_total);
    return read_total;
}
ssize_t write_all_to_socket(int socket, const char *buffer, size_t count) {
   
	size_t write_total = 0;;

	while(write_total < count){

		ssize_t len = write(socket, buffer + write_total, count - write_total);
		if(len==-1){
			if(errno==EPIPE){
				//close the file or client_fd we're writing to 
				print_connection_closed();
				close(socket);
				return -2;
			}	
			else{
				perror("write error in write_all\n");
				fprintf(stderr, "%s\n",strerror(errno));
				return -1;
			}
		}
		else if(len>0){
			write_total = write_total + len;
		}
	}
    return write_total;
}
void *my_char_copy_constructor(void *elem) {
  if(elem==NULL)return NULL;
  char *copy = malloc(256);
	strcpy(copy,(char*)elem);
  //*copy = *((char *)elem);
  return copy;
}
void my_char_destructor(void *elem) { free(elem); }
