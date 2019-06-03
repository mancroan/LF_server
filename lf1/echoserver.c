#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define CONN_MAXFD 65536
#define NUM_WORKER 5
#define MAX_LEN 1024 

typedef struct conn_st{
	int sock;
	int using;
    char writebuf[MAX_LEN];
}*conn_t;


struct conn_st g_conn_table[CONN_MAXFD];

static sig_atomic_t shut_server = 0;

void shut_server_handler(){
	shut_server = 1;
}

int epfd;
int lisSock;
pthread_t worker[NUM_WORKER];

int setnonblocking(fd){
	int flag;
    fcntl(fd, F_GETFL, &flag);
    fcntl(fd, F_SETFL, flag | O_NONBLOCK);
    return 0;
}

//修改epoll事件,默认LT  
static void epoll_add(int efd, int sockfd) {  
    struct epoll_event ev;  
    ev.events = EPOLLIN | EPOLLONESHOT;  
    ev.data.fd = sockfd;  
    epoll_ctl(efd, EPOLL_CTL_ADD, sockfd, &ev);  
}  


//修改epoll事件,默认LT  
static void epoll_mod(int efd, int sockfd, int enable) {  
    struct epoll_event ev;  
    ev.events = EPOLLIN | EPOLLONESHOT | (enable ? EPOLLOUT : 0);  
    ev.data.fd = sockfd;  
    epoll_ctl(efd, EPOLL_CTL_MOD, sockfd, &ev);  
}  

ssize_t readn(int fd, void *buf, int n)  
{  
    size_t nleft = n; 
    char *bufptr = buf;  
    ssize_t nread;  
    
    
    while(nleft > 0)  
    {  
        if((nread = read(fd, bufptr, nleft)) < 0)  
        {  
            if(errno == EINTR)  // 遇到中断  
            {   
                continue;   // 或者用 nread = 0;  
            }  
            else  
            {  
                return -1;  // 真正错误  
            }  
        }  
        else if(nread == 0) // 对端关闭  
        {  
            break;  
        }  
        nleft -= nread;  
        bufptr += nread;  
    }  
    return (n - nleft);
}
  
//写,定长  
int writen(int fd, const void *vptr, size_t n) {  
    // see man 2 write  
    size_t nleft;  
    int nwritten;  
    const char *ptr;  
  
    ptr = (char*)vptr;  
    nleft = n;  
    while (nleft > 0) {  
        nwritten = write(fd, ptr, nleft);  
        if (nwritten <= 0) {  
            if (nwritten < 0 && errno == EINTR)  
                nwritten = 0; /* call write() again */  
            else  
                return(-1); /* error */  
        }  
        nleft -= nwritten;  
        ptr += nwritten;  
    }  
    return(n);  
}

int handleReadEvent(conn_t conn) {
	int connfd = conn->sock;
    int res = recv(connfd, conn->writebuf, MAX_LEN, 0);

    // 处理网络异常情况  
    if (res < 0 && errno != EAGAIN) {   
        return -1;  
    } 
    else if (0 == res) {  
        return -1;  
    }  
    printf("%s\n", conn->writebuf);
    //send(connfd, buffer, res, 0);

    return 0;
}

int handleWriteEvent(conn_t conn) {
    int connfd = conn->sock;
    int len = send(connfd, conn->writebuf, MAX_LEN, 0);
    
    memset(conn->writebuf, 0, sizeof(conn->writebuf));
    // 处理网络异常情况  
    if (len < 0 && errno != EAGAIN) {  
        return -1;  
    }    
    return 0;
}

void closeConnection(conn_t conn) {
    struct epoll_event evReg;

    conn->using = 0;
    epoll_ctl(epfd, EPOLL_CTL_DEL, conn->sock, &evReg);
    close(conn->sock);
}


void *workerThread(void *arg){
	struct epoll_event event;
    //struct epoll_event evReg;

	while(!shut_server){
		int numEvents = epoll_wait(epfd, &event, 1, 1000);
		if (numEvents > 0){
			if (event.data.fd == lisSock){
				int sock = accept(lisSock, NULL, NULL);
				if (sock > 0){
                    conn_t conn = &g_conn_table[sock];
					conn->using = 1;

					setnonblocking(sock);
                    epoll_add(epfd, sock);
				}
                epoll_mod(epfd, lisSock, 0);
			}
			else{
                int sock = event.data.fd;
                conn_t conn = &g_conn_table[sock];
                if (event.events & EPOLLIN) {
                    if (handleReadEvent(conn) == -1) {
                        closeConnection(conn);
                        continue;
                    }

                }
                else if (event.events & EPOLLOUT) {
                    if (handleWriteEvent(conn) == -1) {
                        closeConnection(conn);
                        continue;
                    }
                }

                if (strlen(conn->writebuf) > 0);
                    epoll_mod(epfd, sock, 1);

			}
		}
	}
    return((void *)0);

}

int main(int argc, char *argv[]){
    memset(&g_conn_table, 0, sizeof(g_conn_table));
	int c = 0;
	for(c=0; c<CONN_MAXFD; c++)
		g_conn_table[c].sock = c;

	struct sigaction act;
	memset(&act, 0, sizeof(act));

	act.sa_handler = shut_server_handler;
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGTERM, &act, NULL);

	epfd = epoll_create(20);
	lisSock = socket(AF_INET, SOCK_STREAM, 0);

	int reuse = 1;
	setsockopt(lisSock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	setnonblocking(lisSock);

	struct sockaddr_in lisAddr;
    lisAddr.sin_family = AF_INET;
    lisAddr.sin_port = htons(9876);
    lisAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(lisSock, (struct sockaddr *)&lisAddr, sizeof(lisAddr)) == -1) {
        perror("bind");
        return -1;
    }

    printf("bind port:%d\n", 9876);

    listen(lisSock, 4096);

    printf("start listen....\n");

    struct epoll_event evReg;
    evReg.events = EPOLLIN | EPOLLONESHOT;
    evReg.data.fd = lisSock;

    epoll_ctl(epfd, EPOLL_CTL_ADD, lisSock, &evReg);

    int i = 0;
    for(i = 0; i<NUM_WORKER; i++)
    	pthread_create(worker+i, NULL, workerThread, NULL);


    for (i = 0; i < NUM_WORKER; ++i) {
        pthread_join(worker[i], NULL);
    }

    for (c = 0; c < CONN_MAXFD; ++c) {
        conn_t conn = g_conn_table + c;
        if (conn->using) { 
            epoll_ctl(epfd, EPOLL_CTL_DEL, conn->sock, &evReg);
            close(conn->sock);
        }
    }  

	return 0;
}

