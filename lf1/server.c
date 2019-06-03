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

typedef struct connection_st {
    int sock;
    int using;
#define BUF_SIZE 4096
    int roff;
    char rbuf[BUF_SIZE];
    int woff;
    char wbuf[BUF_SIZE];
}*connection_t;

#define CONN_MAXFD 65536
struct connection_st g_conn_table[CONN_MAXFD] = {0};

static sig_atomic_t shut_server = 0;

void shut_server_handler(int signo) {
    shut_server = 1;
}

int epfd;
int lisSock;
#define NUM_WORKER 1
pthread_t worker[NUM_WORKER];

int sendData(connection_t conn, char *data, int len) {
    if (conn->woff){
        if (conn->woff + len > BUF_SIZE) {
            return -1;
        }
        memcpy(conn->wbuf + conn->woff, data, len);
        conn->woff += len;
        return 0;
    } else {

        int ret = send(conn->sock, data, len, 0);
        if (ret > 0){
            if (ret == len) {
                return 0;
            }
            int left = len - ret;
            if (left > BUF_SIZE) return -1;
            
            memcpy(conn->wbuf, data + ret, left);
            conn->woff = left;
        } else {
            if (errno != EINTR && errno != EAGAIN) {
                return -1;
            }
            if (len > BUF_SIZE) {
                return -1;
            }
            memcpy(conn->wbuf, data, len);
            conn->woff = len;
        }
    }

    return 0;
}

int handleReadEvent(connection_t conn) {
    if (conn->roff == BUF_SIZE) {
        return -1;
    }
    
    int ret = read(conn->sock, conn->rbuf + conn->roff, BUF_SIZE - conn->roff);

    if (ret > 0) {
        conn->roff += ret;
        
        int beg, end, len;
        beg = end = 0;
        while (beg < conn->roff) {
            char *endPos = (char *)memchr(conn->rbuf + beg, '\n', conn->roff - beg);
            if (!endPos) break;
            end = endPos - conn->rbuf;
            len = end - beg + 1;
            
            /*echo*/
            if (sendData(conn, conn->rbuf + beg, len) == -1) return -1;
            beg = end + 1;
            printf("request_finish_time=%ld\n", (time_t)time(NULL));
        }
        int left = conn->roff - beg;
        if (beg != 0 && left > 0) {
            memmove(conn->rbuf, conn->rbuf + beg, left);
        }
        conn->roff = left;
    } else if (ret == 0) {
        return -1;
    } else {
        if (errno != EINTR && errno != EAGAIN) {
            return -1;
        }
    }

    return 0;
}

int handleWriteEvent(connection_t conn) {
    if (conn->woff == 0) return 0;

    int ret = send(conn->sock, conn->wbuf, conn->woff, 0);

    if (ret == -1) {
        if (errno != EINTR && errno != EAGAIN) {
            return -1;
        }
    } else {
        int left = conn->woff - ret;

        if (left > 0) {
            memmove(conn->wbuf, conn->wbuf + ret, left);
        }

        conn->woff = left;
    }

    return 0;
}

void closeConnection(connection_t conn) {
    struct epoll_event evReg;

    conn->using = 0;
    conn->woff = conn->roff = 0;
    epoll_ctl(epfd, EPOLL_CTL_DEL, conn->sock, &evReg);
    close(conn->sock);
}

void *workerThread(void *arg) {
    struct epoll_event event;
    struct epoll_event evReg;

    while (!shut_server) {
        int numEvents = epoll_wait(epfd, &event, 1, 1000);
        
        if (numEvents > 0) {
            if (event.data.fd == lisSock) {
                int sock = accept(lisSock, NULL, NULL);
                if (sock > 0) {
                    g_conn_table[sock].using = 1;
                    
                    int flag;
                    fcntl(sock, F_GETFL, &flag);
                    fcntl(sock, F_SETFL, flag | O_NONBLOCK);
                    
                    evReg.data.fd = sock;
                    evReg.events = EPOLLIN | EPOLLONESHOT;
                            
                    epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &evReg);
                }
                evReg.data.fd = lisSock;
                evReg.events = EPOLLIN | EPOLLONESHOT;
                
                epoll_ctl(epfd, EPOLL_CTL_MOD, lisSock, &evReg);
            } else {
                int sock = event.data.fd;
                connection_t conn = &g_conn_table[sock];
                
                if (event.events & EPOLLOUT) {
                    if (handleWriteEvent(conn) == -1) {
                        closeConnection(conn);
                        continue;
                    }
                }

                if (event.events & EPOLLIN) {
                    if (handleReadEvent(conn) == -1) {
                        closeConnection(conn);
                        continue;
                    }
                }
                
                evReg.events = EPOLLIN | EPOLLONESHOT;
                if (conn->woff > 0) evReg.events |= EPOLLOUT;
                evReg.data.fd = sock;
                epoll_ctl(epfd, EPOLL_CTL_MOD, conn->sock, &evReg);
            }
        }
    }
    return ((void*)0);
}

int main(int argc, char *const argv[]) {
    //memset(g_conn_table, 0 , sizeof(g_conn_table));
    int c;
    for (c = 0; c < CONN_MAXFD; ++c) {
        g_conn_table[c].sock = c;
    }
    struct sigaction act;
    memset(&act, 0, sizeof(act));

    act.sa_handler = shut_server_handler;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);

    epfd = epoll_create(20);
    
    lisSock = socket(AF_INET, SOCK_STREAM, 0); 
    
    int reuse = 1;
    setsockopt(lisSock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    int flag;
    fcntl(lisSock, F_GETFL, &flag);
    fcntl(lisSock, F_SETFL, flag | O_NONBLOCK);

    struct sockaddr_in lisAddr;
    lisAddr.sin_family = AF_INET;
    lisAddr.sin_port = htons(9876);
    lisAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(lisSock, (struct sockaddr *)&lisAddr, sizeof(lisAddr)) == -1) {
        perror("bind");
        return -1;
    }

    listen(lisSock, 4096);

    struct epoll_event evReg;
    evReg.events = EPOLLIN | EPOLLONESHOT;
    evReg.data.fd = lisSock;

    epoll_ctl(epfd, EPOLL_CTL_ADD, lisSock, &evReg);

    int i;
    for (i = 0; i < NUM_WORKER; ++i) {
        pthread_create(worker + i, NULL, workerThread, NULL);
    }

    for (i = 0; i < NUM_WORKER; ++i) {
        pthread_join(worker[i], NULL);
    }
    
    for (c = 0; c < CONN_MAXFD; ++c) {
        connection_t conn = g_conn_table + c;
        if (conn->using) { 
            epoll_ctl(epfd, EPOLL_CTL_DEL, conn->sock, &evReg);
            close(conn->sock);
        }
    }    

    return 0;
}
