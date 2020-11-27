#include <sys/types.h>
#include <stdio.h>
#include <linux/userfaultfd.h>
#include <pthread.h>
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
#define PORT 5984

int client();
int server();
static void* uffd_handler_thred(void*);
static void* listen_thred(void*);

typedef struct{
	int r_w;
	int page_index;
	//char* content;
} req_p;

typedef enum {
	M = 0, S, I
} MSI;

MSI * page_info;
int p_size_int = 0;
char * page_ptr = NULL;

int main(int argc, char *argv[]){
    int flag = 0;

    if(argc == 1){
        flag = 1; //server
    }
    else if(argc == 2){
        flag = 2; //client
    }

    if (flag == 1){
        printf("Server Connecting\n");
        server();
    }
    else if(flag == 2){
        printf("Client Connecting\n");
        client();
    }
    else{
        printf("Not server or client\n");
    }

    return 0;
}

int client(){
    int sock = 0;
	struct sockaddr_in serv_addr;
	char buffer[1024] = {0};
	char *page = NULL;
	pthread_t thr1, thr2; 
	int s;
	int l;
	long uffd;  
	struct uffdio_api uffdio_api;
	struct uffdio_register uffdio_register;


    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("\n Socket creation error \n");
		return -1;
	}

	memset(&serv_addr, '0', sizeof(serv_addr));
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

	read(sock , buffer, 1024);
	p_size_int = atoi(buffer);
	char * addr =NULL;
	read(sock , &addr, sizeof(void *));
	if (addr == NULL){
		return 0;
	}
	printf("The size of page: %d \nThe address of page: %p\n",  p_size_int, addr);

	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd == -1)
		perror("userfaultfd");

	uffdio_api.api = UFFD_API;
	uffdio_api.features = 0;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1)
		perror("ioctl-UFFDIO_API");


	int ps = sysconf(_SC_PAGE_SIZE);
	if (page == NULL) {
		page = mmap(NULL, p_size_int * ps, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (page == MAP_FAILED)
			perror("mmap");
	}
	page_ptr = page;
	page_info = malloc(p_size_int *  sizeof (MSI));
	for(int i = 0; i < p_size_int; i++)
		page_info[i] = I;
	
	uffdio_register.range.start = (unsigned long) page;
	uffdio_register.range.len = p_size_int * ps;
	uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;

	if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1)
		perror("[*]ioctl-UFFDIO_REGISTER");

	s = pthread_create(&thr1, NULL, uffd_handler_thred, (void *) uffd);
	if(s != 0) {
		errno = s;
		perror("pthread_create");
	}

	l = pthread_create(&thr2, NULL, listen_thred, (void *) (&sock));
	if(l != 0) {
		errno = l;
		perror("pthread_create");
	}
	
	for(;;){
		char command[1024] = {0};
		printf("> Which command should I run? (r:read, w:write, v:view msi list): ");
		scanf("%s", command);
		
		if(strcmp(command, "exit") == 0)
			break;
		
		if(strcmp(command, "exit") == 0){
			break;
		}
		else{
			char page_stdin[1024] = {0};
			printf("> For which page? (0-%d, or -1 for all): ", (p_size_int - 1));
			scanf("%s", page_stdin);
			int page_size = atoi(page_stdin);
			if (page_size < -1 || page_size > p_size_int)
				perror("page error");

			if(command[0] == 'v'){
				if(page_size == -1){
					for(int i = 0; i < p_size_int; i++){
						printf("[$] MSI for page %d: ", i);
						if (page_info[i] == M){
							printf("M\n");
						}
						else if(page_info[i] == S){
							printf("S\n");
						}
						else if(page_info[i] == I){
							printf("I\n");
						}
					}
				}
				else{
					printf("[$] MSI for page %d: ", page_size);
					if (page_info[page_size] == M){
						printf("M\n");
					}
					else if(page_info[page_size] == S){
						printf("S\n");
					}
					else if(page_info[page_size] == I){
						printf("I\n");
					}
				}
			}
			else if(command[0] == 'w'){ //write
				char msg[sysconf(_SC_PAGE_SIZE)];
				printf("> Type your new message: ");
				fflush(stdin);
				scanf(" %[^\n]s\n", msg);
			
				if(page_size == -1){
					for (int i = 0; i < p_size_int; i++){
						char * write_page = i * sysconf(_SC_PAGE_SIZE) + page;
						strcpy(write_page, msg);
						printf("[%d] Page:\n", i);
						printf("%s\n\n", msg);
						page_info[i] = M;
					}
				}
				else{
					char * write_page = page_size * sysconf(_SC_PAGE_SIZE) + page;
					strcpy(write_page, msg);
					printf("[%d] Page:\n", page_size);
					printf("%s\n\n", msg);
					page_info[page_size] = M;
				}
				req_p request_page;
				request_page.r_w = 0;
				request_page.page_index = page_size;
				send(sock, &request_page, sizeof(req_p), 0);
			}
			else if(command[0] == 'r'){//read
				if(page_size == -1){
					for (int i = 0; i < p_size_int; i++){
						char * read_page = i * sysconf(_SC_PAGE_SIZE) + page;
						char page_msg[sysconf(_SC_PAGE_SIZE)];
						if(page_info[i] != I){						
							strcpy(page_msg, read_page);
							printf("[%d] Page:\n", i);
							printf("%s\n", page_msg);
						}
						else{
							req_p request_page;
							request_page.r_w = 1;
							request_page.page_index = i;
							send(sock, &request_page, sizeof(req_p), 0);

							char page_msg_s[sysconf(_SC_PAGE_SIZE)];
							read(sock , page_msg_s, sysconf(_SC_PAGE_SIZE));
							strcpy(read_page, page_msg_s);
							printf("[%d] Page:\n", i);
							printf("%s\n", page_msg_s);
							page_info[i] =S;
						}
					}
				}
				else{
					char * read_page = page_size * sysconf(_SC_PAGE_SIZE) + page;
					char page_msg[sysconf(_SC_PAGE_SIZE)];
					if(page_info[page_size] != I){
						strcpy(page_msg, read_page);
						printf("[%d] Page:\n", page_size);
						printf("%s\n", page_msg);
					}
					else{
						req_p request_page;
						request_page.r_w = 1;
						request_page.page_index = page_size;
						send(sock, &request_page, sizeof(req_p), 0);

						char page_msg_s[sysconf(_SC_PAGE_SIZE)];
						read(sock , page_msg_s, sysconf(_SC_PAGE_SIZE));
						strcpy(read_page, page_msg_s);
						printf("[%d] Page:\n", page_size);
						printf("%s\n", page_msg_s);
						page_info[page_size] =S;
					}
				}
			}
			else{
				printf("Command Error\n");
				break;
			}
		}
	}
	free(page_info);
	printf("Client closed");
	char end_c[1024] = {0};
	read(sock , end_c, 1024);
	printf("\n%s\n", end_c);
	return 0;
}

int server(){
    int server_fd, new_socket;
	struct sockaddr_in address;
	int opt = 1;
	int addrlen = sizeof(address);
	char *page = NULL;
	int s;
	pthread_t thr, thr1;
	long uffd;  
	struct uffdio_api uffdio_api;
	struct uffdio_register uffdio_register;
	int listener;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
		perror("socket failed");
		exit(EXIT_FAILURE);
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
	char p_size[1024];
	printf("> Enter the size of page you want: ");
	scanf("%s", p_size);

	send(new_socket , p_size, sizeof(p_size), 0);
	p_size_int = atoi(p_size);

	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd == -1)
		perror("userfaultfd");

	uffdio_api.api = UFFD_API;
	uffdio_api.features = 0;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1)
		perror("ioctl-UFFDIO_API");

	int ps = sysconf(_SC_PAGE_SIZE);
	if (page == NULL) {
		page = mmap(NULL, p_size_int * ps, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (page == MAP_FAILED)
			perror("mmap");
	}
	printf("> Page address %lx\n", (uint64_t)page);
	page_ptr = page;
	page_info = malloc(p_size_int *  sizeof (MSI));
	for(int i = 0; i < p_size_int; i++)
		page_info[i] = I;
	
	uffdio_register.range.start = (unsigned long) page;
	uffdio_register.range.len = p_size_int * ps;
	uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;

	if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1)
		perror("[*]ioctl-UFFDIO_REGISTER");

	send(new_socket , &page, sizeof(void*) , 0);

	s = pthread_create(&thr, NULL, uffd_handler_thred, (void *) uffd);
	if(s != 0) {
		errno = s;
		perror("pthread_create");
	}
	
	listener = pthread_create(&thr1, NULL, listen_thred, (void *) (&new_socket));
	if(listener != 0) {
		errno = listener;
		perror("pthread_create");
	}
	
	for(;;){
		char command[1024] = {0};
		printf("> Which command should I run? (r:read, w:write, v:view msi list): ");
		scanf("%s", command);
		
		if(strcmp(command, "exit") == 0){
			break;
		}
		else{
			char page_stdin[1024] = {0};
			printf("> For which page? (0-%d, or -1 for all): ", (p_size_int - 1));
			scanf("%s", page_stdin);
			int page_size = atoi(page_stdin);
			if (page_size < -1 || page_size > p_size_int)
				perror("page error");
			
			if(command[0] == 'v'){
				if(page_size == -1){
					for(int i = 0; i < p_size_int; i++){
						printf("[$] MSI for page %d: ", i);
						if (page_info[i] == M){
							printf("M\n");
						}
						else if(page_info[i] == S){
							printf("S\n");
						}
						else if(page_info[i] == I){
							printf("I\n");
						}
					}
				}
				else{
					printf("[$] MSI for page %d: ", page_size);
					if (page_info[page_size] == M){
						printf("M\n");
					}
					else if(page_info[page_size] == S){
						printf("S\n");
					}
					else if(page_info[page_size] == I){
						printf("I\n");
					}
				}
			}
			else if(command[0] == 'w'){ //write
				char msg[sysconf(_SC_PAGE_SIZE)];
				printf("> Type your new message: ");
				fflush(stdin);
				scanf(" %[^\n]s\n", msg);
				if(page_size == -1){
					for (int i = 0; i < p_size_int; i++){
						char * write_page = i * sysconf(_SC_PAGE_SIZE) + page;
						strcpy(write_page, msg);
						printf("[%d] Page:\n", i);
						printf("%s\n\n", msg);
						page_info[i] = M;
					}
				}
				else{
					char * write_page = page_size * sysconf(_SC_PAGE_SIZE) + page;
					strcpy(write_page, msg);
					printf("[%d] Page:\n", page_size);
					printf("%s\n\n", msg);
					page_info[page_size] = M;
				}
				req_p request_page;
				request_page.r_w = 0;
				request_page.page_index = page_size;
				send(new_socket, &request_page, sizeof(req_p), 0);
			}
			else if(command[0] == 'r'){ //read
				if(page_size == -1){
					for (int i = 0; i < p_size_int; i++){
						char * read_page = i * sysconf(_SC_PAGE_SIZE) + page;
						char page_msg[sysconf(_SC_PAGE_SIZE)];
						if(page_info[i] != I){						
							strcpy(page_msg, read_page);
							printf("[%d] Page:\n", i);
							printf("%s\n", page_msg);
						}
						else{
							req_p request_page;
							request_page.r_w = 1;
							request_page.page_index = i;
							send(new_socket, &request_page, sizeof(req_p), 0);

							char page_msg_s[sysconf(_SC_PAGE_SIZE)];
							read(new_socket , page_msg_s, sysconf(_SC_PAGE_SIZE));
							strcpy(read_page, page_msg_s);
							printf("[%d] Page:\n", i);
							printf("%s\n", page_msg_s);
							page_info[i] =S;
						}
					}
				}
				else{
					char * read_page = page_size * sysconf(_SC_PAGE_SIZE) + page;
					char page_msg[sysconf(_SC_PAGE_SIZE)];
					if(page_info[page_size] != I){
						strcpy(page_msg, read_page);
						printf("[%d] Page:\n", page_size);
						printf("%s\n", page_msg);
					}
					else{
						req_p request_page;
						request_page.r_w = 1;
						request_page.page_index = page_size;
						send(new_socket, &request_page, sizeof(req_p), 0);

						char page_msg_s[sysconf(_SC_PAGE_SIZE)];
						read(new_socket , page_msg_s, sysconf(_SC_PAGE_SIZE));
						strcpy(read_page, page_msg_s);
						printf("[%d] Page:\n", page_size);
						printf("%s\n", page_msg_s);
						page_info[page_size] =S;
					}
				}
			}
			else{
				printf("Command Error\n");
				break;
			}
		}
	}
	free(page_info);
	char * quit = "Server Terminated.";
	send(new_socket , quit, strlen(quit) + 1, 0);
	return 0;
}

static void* uffd_handler_thred(void* arg){
	static struct uffd_msg msg;   /* Data read from userfaultfd */
    long uffd;                    /* userfaultfd file descriptor */
    ssize_t nread;
    uffd = (long) arg;
	static char *page = NULL;
	struct uffdio_copy uffdio_copy;
	int page_size = sysconf(_SC_PAGE_SIZE);

	if (page == NULL) {
		page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (page == MAP_FAILED)
			perror("mmap");
	}

	for(;;){
		struct pollfd pollfd;
		int nready;

		pollfd.fd = uffd;
		pollfd.events = POLLIN;
		nready = poll(&pollfd, 1, -1);
		if (nready == -1)
			perror("poll");

		nread = read(uffd, &msg, sizeof(msg));
		if (nread == 0) {
			printf("EOF on userfaultfd!\n");
			exit(EXIT_FAILURE);
		}

		if (nread == -1)
			perror("read");

		if (msg.event != UFFD_EVENT_PAGEFAULT) {
			fprintf(stderr, "Unexpected event on userfaultfd\n");
			exit(EXIT_FAILURE);
    	}
		printf("[x]PAGEFAULT\n");

		uffdio_copy.src = (unsigned long) page;
		uffdio_copy.dst = (unsigned long) msg.arg.pagefault.address &
			~(page_size - 1);
		uffdio_copy.len = page_size;
		uffdio_copy.mode = 0;
		uffdio_copy.copy = 0;

		if (ioctl(uffd, UFFDIO_COPY, &uffdio_copy) == -1)
			perror("ioctl-UFFDIO_COPY");

	}
	
}

static void* listen_thred(void* arg){
	int sock = *(int*)arg;
	//int sys_page_size = sysconf(_SC_PAGE_SIZE);
	for(;;){
		req_p recv_page;
		read(sock, (void*)&recv_page, sizeof(req_p));
		if(recv_page.r_w == 0){ //wirte
			if(recv_page.page_index == -1){
				for(int i = 0; i < p_size_int; i++){
					page_info[i] = I;
				}
			}
			else{
				page_info[recv_page.page_index] = I;
			}
		}
		else if(recv_page.r_w == 1){ //read
			char * write_page = recv_page.page_index * sysconf(_SC_PAGE_SIZE) + page_ptr;
			char page_msg[sysconf(_SC_PAGE_SIZE)];
			if(page_info[recv_page.page_index] != I){
				strcpy(page_msg, write_page);
			}
			send(sock, page_msg, sizeof(page_msg), 0);
			page_info[recv_page.page_index] = S;
		}

	}
	return NULL;
}