#include <sys/types.h>
#include <stdio.h>
#include <linux/userfaultfd.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#define PORT 1551
int send_socket;
int rec_socket;
int page_size;

typedef struct{
	void* ptr;
    void* addr;
    size_t size; 
} com_mem;

com_mem data_list[1024];

void send_mem(void* org_ptr, void* com_addr, size_t size){
    com_mem cm;
    cm.ptr = org_ptr;
    cm.addr = com_addr;
    cm.size = size;
    send(send_socket, &cm, sizeof(cm), 0);
    send(send_socket, &com_addr, size, 0);
}

void receive_mem(){
    for (;;){
        com_mem * rcm = ( com_mem *)malloc(sizeof(com_mem));
        
        if (read(rec_socket, rcm, sizeof(com_mem)) < 0){
            perror("read");
        }
        void * data = malloc(rcm->size);
        if (read(rec_socket, (void*)&data, rcm->size) < 0){
            perror("read");
        }
        rcm->addr = data;
        printf("data: %s\n", (char*)data);
        free(rcm);
        break;
    }
    
}

int server(){
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1; 
    int addrlen = sizeof(address); 

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0){
        printf("\n Socket creation error \n");
		return -1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
		       &opt, sizeof(opt))) {
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}

    address.sin_family = AF_INET; 
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons( PORT ); 

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		perror("bind failed");
		exit(EXIT_FAILURE);
	}

    if (listen(server_fd, 3) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

    if ((new_socket = accept(server_fd, (struct sockaddr *)&address,
				 (socklen_t*)&addrlen)) < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}
    send_socket = new_socket;
    printf("server connected\n");
    return 1;
}

int client(){
    int sock;
    struct sockaddr_in serv_addr; 

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("\n Socket creation error \n");
		return -1;
	}

	serv_addr.sin_family = AF_INET; 
    serv_addr.sin_port = htons(PORT); 

	if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
		printf("\nInvalid address/ Address not supported \n");
		return -1;
	}
	if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		printf("\nConnection Failed \n");
		return -1;
	}
    rec_socket = sock;
    printf("client connected\n");
    return 1;
}

int main(int argc, char *argv[]){
    page_size = sysconf(_SC_PAGE_SIZE);
    if (argc == 1){
        printf("Server Connecting\n");
        server();
        char* page = "DDDDDD";
        //memset(page, 'D', page_size);
        char* page_ptr = page;
        send_mem(page_ptr, page, sizeof(page));
    }
    else if(argc == 2){
        printf("Client Connecting\n");
        client();
        receive_mem();
        
    }
    else{
        printf("Error\n");
    }

    return 1;
}