#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>

#define REQUEST_NUM 100000

int main(int argc, char *const arvg[]) {
    int i;
    char buff[1024];
    memset(buff, 0, sizeof(buff));
    const char *hello = "hello world\n";
    int helloLen = strlen(hello);
    
    clock_t begin,end;
    begin=clock();

    for (i = 0; i < REQUEST_NUM; ++i) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
    
        struct sockaddr_in srvAddr;
        srvAddr.sin_family = AF_INET;
        srvAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        srvAddr.sin_port = htons(9876);
        
        connect(sock, (struct sockaddr *)&srvAddr, sizeof(srvAddr));
        
        int size = 0;
        while (size < helloLen) {
            int ret = write(sock, hello + size, helloLen - size);
            if (ret > 0) size += ret;
        }
        recv(sock, buff, helloLen, MSG_WAITALL);
        //printf("%s\n", buff);
        //memset(buff, 0, sizeof(buff));
        close(sock);
    }

    end=clock();

    double use_time = (double)(end-begin)/CLOCKS_PER_SEC;
    int QPS = REQUEST_NUM/use_time;

    FILE *fp = fopen("QPS_short.txt", "a+");
    if (fp==0) { 
        printf("can't open file\n"); 
        return 0;
    }
    fseek(fp, 0, SEEK_END);

    char str[32] = {0};
    sprintf(str, "QPS:%d\ttime:%lfs\n", QPS, use_time);
    fwrite(str, strlen(str), 1, fp);
    fclose(fp);

    return 0;
}
