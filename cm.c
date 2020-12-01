#include <sys/types.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <linux/types.h>
#include "list.h"
#include "tree.h"  

#define PORT 1551
int send_socket;
int rec_socket;
int page_size;

struct block{
    void* ptr;
    void* addr;
    size_t size;
    int rw; 
    RB_ENTRY(block) node;
};

RB_HEAD(datatree, block) tree;

//test code
int up = 100;
uint64_t rec_keys[100];
int key_index = 0;
//test code

static int compare_size(struct block * a, struct block * b)
{
    if (a->ptr < b->ptr)
        return -1;
    else if (a->ptr > b->ptr)
	    return 1;
    else if (a->ptr == b->ptr)
        return 0;
    else
	    return a < b ? -1 : 1;    
}

RB_GENERATE_STATIC(datatree, block, node, compare_size); 

void send_mem(void* org_ptr, void* com_addr, size_t size){
    struct block cm;
    cm.ptr = org_ptr;
    cm.addr = com_addr;
    cm.size = size;
    cm.rw = 1;
    send(send_socket, &cm, sizeof(struct block), 0);
    send(send_socket, com_addr, size, 0);
}

void* back_mem(void* ptr){
    struct block cm;
    cm.ptr = ptr;
    cm.addr = NULL;
    cm.size = 0;
    cm.rw = 0;
    size_t data_size;
    send(send_socket, &cm, sizeof(struct block), 0);
    if (read(send_socket, &data_size, sizeof(size_t)) < 0){
        perror("read");
    }
    if (data_size == -1){
        perror("no match data");
        exit(0);
    }
    void* newptr = NULL;
    char back_data[data_size];
    if (read(send_socket, back_data, data_size) < 0){
        perror("read");
    }
    //printf("Back Data: %s\n", back_data);
    newptr = back_data;
    return newptr;
}

void receive_mem(){
    RB_INIT(&tree);
    for (;;){
        struct block * rcm = (struct block *)malloc(sizeof(struct block));
        rcm->rw = -1;
        if (read(rec_socket, rcm, sizeof(struct block)) < 0){
            perror("read");
        }
        if(rcm->rw == 1){
            void * data = (void*)malloc(page_size);
            if (read(rec_socket, data, rcm->size) < 0){
                perror("read");
            }
            rcm->addr = data;
            //test code
            rec_keys[key_index] = (uint64_t)rcm->ptr;
            printf("key %d: %p\n", key_index, rcm->ptr);
            key_index++;
            //test code
            RB_INSERT(datatree, &tree, rcm);
        }
        else if(rcm->rw == 0){
            struct block tblk;
            tblk.ptr = rcm->ptr;
            struct block * p = RB_NFIND(datatree, &tree, &tblk);
            size_t data_size = -1;
            if (p != NULL){
                data_size = p->size;
                send(rec_socket, &data_size, sizeof(data_size), 0);
                send(rec_socket, p->addr, data_size, 0);
                free(p->addr);
                RB_REMOVE(datatree, &tree, p);
            }
            else{
                send(send_socket, &data_size, sizeof(size_t), 0);
            }
        }
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

uint64_t rand_uint64_slow(void) {
  uint64_t r = 0;
  for (int i=0; i<64; i++) {
    r = r*2 + rand()%2;
  }
  return r;
}

void test(){
    
    uint64_t keys[up];
    char* datalist[up];
    for (int i = 0; i < up; i++){
        uint64_t ptr_key = rand_uint64_slow();
        keys[i] = ptr_key;
        int randomData = open("/dev/urandom", O_RDONLY);
        char myRandomData[page_size];
        if (read(randomData, myRandomData, page_size) < 0){
            printf("Send Error");
        }
        
        char * newdata = (char*)malloc(page_size);
        strcpy(newdata, myRandomData);
        datalist[i] = newdata;
        printf("key %d: %p\n", i, (void*)ptr_key);
        send_mem((void*)ptr_key, myRandomData, page_size);
    }

    
    for (int i = 0; i < up; i++){
        char* backData = (char*)back_mem((void*)keys[i]);
        if (strcmp(backData, (char*)datalist[i]) == 0){
            free(datalist[i]);
            continue;
        }
        else{
            printf("PAGE: %d error\n", i);
            exit(0);
        }
    }
    printf("all data back!\n");
    
}

int main(int argc, char *argv[]){
    page_size = sysconf(_SC_PAGE_SIZE);
    //char* test_page;
    if (argc == 1){
        server();
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
        
    }

    //free_list();
    return 1;
}