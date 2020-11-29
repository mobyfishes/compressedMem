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
#include <linux/types.h>
#include "list.h"

#define PORT 1551
int send_socket;
int rec_socket;
int page_size;

static LIST_HEAD(datalist);

typedef struct{
	void* ptr;
    void* addr;
    size_t size;
    int rw; 
    struct list_head ele;
} com_mem;


void send_mem(void* org_ptr, void* com_addr, size_t size){
    com_mem cm;
    cm.ptr = org_ptr;
    cm.addr = com_addr;
    cm.size = size;
    cm.rw = 1;
    send(send_socket, &cm, sizeof(com_mem), 0);
    send(send_socket, com_addr, size, 0);
}

void back_mem(void* ptr){
    com_mem cm;
    cm.ptr = ptr;
    cm.addr = NULL;
    cm.size = 0;
    cm.rw = 0;
    size_t data_size;
    send(send_socket, &cm, sizeof(com_mem), 0);
    if (read(send_socket, &data_size, sizeof(size_t)) < 0){
        perror("read");
    }
    if (data_size == -1){
        perror("no match data");
    }
    void* back_data; //= (void*)malloc(data_size);
    if (read(send_socket, &back_data, data_size) < 0){
        perror("read");
    }
    printf("Back mem: %s\n", (char*)&back_data);
}

void receive_mem(){
    for (;;){
        com_mem * rcm = ( com_mem *)malloc(sizeof(com_mem));
        rcm->rw = -1;
        if (read(rec_socket, rcm, sizeof(com_mem)) < 0){
            perror("read");
        }
        if(rcm->rw == 1){
            void * data = (void*)malloc(rcm->size);
            if (read(rec_socket, data, rcm->size) < 0){
                perror("read");
            }
            rcm->addr = data;

            list_add_tail(&rcm->ele, &datalist);
        }
        else if(rcm->rw == 0){
            void* target_ptr = rcm->ptr;
            com_mem *obj, *next;
            list_for_each_entry_safe(obj, next, &datalist, ele){
                size_t data_size = -1;
                if (obj->ptr == target_ptr){
                    data_size = obj->size;
                    send(rec_socket, &data_size, sizeof(data_size), 0);
                    send(rec_socket, obj->addr, obj->size, 0);
                    list_del(&obj->ele);
                    free(obj);
                    break;
                }
                send(send_socket, &data_size, sizeof(size_t), 0);
            }
        }
    }
    
}

void free_list(){
    com_mem *obj, *next;
    list_for_each_entry_safe(obj, next, &datalist, ele){
        list_del(&obj->ele);
        free(obj);
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

void test(){
    for (int i = 0; i < 8888; i++){
        char* page = "uuuu";
        char* page_ptr = page;
        send_mem(page_ptr, page, sizeof(page));
    }
}

int main(int argc, char *argv[]){
    page_size = sysconf(_SC_PAGE_SIZE);
    char* test_page;
    if (argc == 1){
        printf("Server Connecting\n");
        server();
        test();
        char* page = "DDDDDD";
        char* page_ptr = page;
        printf("Init data: %p\n", page);
        printf("Init addr: %p\n", page_ptr);
        test_page = page;
        send_mem(page_ptr, page, sizeof(page));
        test();
        
    }
    else if(argc == 2){
        printf("Client Connecting\n");
        client();
        receive_mem();    
    }
    else{
        printf("Error\n");
    }

    if (argc == 1){
        back_mem(test_page);
    }

    free_list();
    return 1;
}