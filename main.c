#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#define CHECK_ERROR(cond,msg)                               \
                    if (cond){                              \
                    fprintf(stderr,"Line:%d\n", __LINE__ ); \
                    perror(msg);                            \
                    exit(-1);}

#define CHECK_FUNC_ERROR(cond,msg)                          \
                    if (cond){                              \
                    fprintf(stderr,"Line:%d\n", __LINE__ ); \
                    perror(msg);                            \
                    return -1;}

int rcv_port = 4000;
int snd_port = 5000;
struct interval
{
    double begin;
    double end;
    double dx;
};
const double dx = 1E-9;
const double interval_beginning = 0;
const double interval_ending = 1;
struct server
{
    struct sockaddr_in snd_addr;
    struct interval interval;
    int threads_num;
    int socket;

};

void send_broadcast();
int wait_workers(int tcp_socket, struct server* servers, int max_workers, int* threads_num);
int split_interval(struct server* servers, int workers_num, int threads_num);

int main(int argc, char* argv[]) {

    CHECK_ERROR(argc < 2, "Please, enter the number of workers\n");
    int max_workers = atoi(argv[1]);
    CHECK_ERROR(max_workers <= 0, "The number of workers must be positive integer\n");
    send_broadcast();
    struct server* servers = (struct server*)calloc(max_workers, sizeof(struct server));
    int tcp_socket =  socket(PF_INET, SOCK_STREAM, 0);
    fcntl(tcp_socket,F_SETFL, O_NONBLOCK);
    int threads_num = 0;
    int workers = wait_workers(tcp_socket, servers, max_workers, &threads_num);
    printf("Total number of threads: %d\n",threads_num);
    if (workers <=0 ) {
        perror("Error during wait\n");
        exit(-1);
    };
    CHECK_ERROR(split_interval(servers, workers,threads_num) == -1, "Splitting error\n");
    for (int i = 0; i < workers; i++) {
       CHECK_ERROR(send(servers[i].socket, &servers[i].interval, sizeof(struct interval),0) == -1, strerror(errno));
    }
    fd_set read_set;
    int max_socket = -1;
    int select_ret = 0;
    struct timeval timeout = {.tv_sec = 10};
    int received_answers = 0;
    double result = 0;
    double recv_res = 0;
    ssize_t recv_ret = 0;
    while(received_answers < workers) {
        FD_ZERO(&read_set);
        for (int i = 0; i < workers; i++) {
            if (servers[i].socket != -1) {
                FD_SET(servers[i].socket, &read_set);
                if (servers[i].socket > max_socket)
                    max_socket = servers[i].socket;
            }

        }
        select_ret = select(max_socket+1, &read_set, NULL, NULL, &timeout);
        CHECK_ERROR(select_ret == -1, strerror(errno));
        if (select_ret == 0)
        {
            printf("Check connections...\n");
            for (int i = 0; i < workers; i++) {
                if (servers[i].socket != -1) {
                    CHECK_ERROR(send(servers[i].socket, &max_socket, sizeof(int), 0) == -1, strerror(errno));
                }
            }
            continue;
        }
        for (int i = 0; i < workers; i++) {
            if (servers[i].socket == -1)
            {
                continue;
            }
            else {
                printf("%d\t",servers[i].socket);
                if (FD_ISSET(servers[i].socket, &read_set)) {
                    printf("%d\t",servers[i].socket);
                    recv_ret = recv(servers[i].socket, &recv_res, sizeof(double), 0);
                    printf("recv_ret: %ld\n", recv_ret);
                    CHECK_ERROR(recv_res == -1, strerror(errno));
                    CHECK_ERROR(recv_res == 0, "Worker disconnected\n")
                    result += recv_res;
                    received_answers++;
                    close(servers[i].socket);
                    servers[i].socket = -1;
                }
            }
        }
    }
    
    printf("Result: %lg",result);

    close(tcp_socket);
    free(servers);
    exit (0);
}
void send_broadcast(struct server* servers )
{
    int sk = socket(PF_INET, SOCK_DGRAM, 0);
    CHECK_ERROR(sk == -1, strerror(errno));
    int val = 1;
    struct sockaddr_in snd_addr;
    bzero(&snd_addr, sizeof(snd_addr));
    snd_addr.sin_family = AF_INET;
    snd_addr.sin_port = htons(snd_port);
    snd_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    CHECK_ERROR(setsockopt (sk, SOL_SOCKET,SO_BROADCAST, (const void*)&val, sizeof(val)) == -1, strerror(errno));
    int MSG = 0;
    CHECK_ERROR(sendto(sk, &MSG, sizeof(int), 0, (struct sockaddr*) &snd_addr, sizeof(snd_addr)) == -1, strerror(errno));
    close(sk);
}
int wait_workers(int tcp_socket, struct server* servers, int max_workers, int* threads_num)
{
    int workers = 0;
    struct sockaddr_in rcv_addr;
    bzero(&rcv_addr, sizeof(rcv_addr));
    rcv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    rcv_addr.sin_family = AF_INET;
    rcv_addr.sin_port = htons(rcv_port);
    unsigned int len = sizeof(rcv_addr);

    CHECK_FUNC_ERROR(bind(tcp_socket, (struct sockaddr *) &rcv_addr, sizeof(rcv_addr)) != 0 , strerror(errno));
    CHECK_FUNC_ERROR(listen(tcp_socket, 256) != 0 , strerror(errno));
    struct timeval timeout = {.tv_usec = 250000};
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(tcp_socket,&read_set);
    int sk = -1;
    while ((select(tcp_socket+1, &read_set,NULL, NULL, &timeout) > 0)& (workers < max_workers)){
        sk = accept(tcp_socket, (struct sockaddr *) &servers[workers].snd_addr, &len);
        printf("Server answer from %s on port %d\n",
               inet_ntoa(servers[workers].snd_addr.sin_addr), ntohs(servers[workers].snd_addr.sin_port));
        CHECK_FUNC_ERROR(sk == -1, strerror(errno));
        recv(sk, &servers[workers].threads_num, sizeof(int),0);
        *threads_num += servers[workers].threads_num;

        servers[workers].socket = sk;
        CHECK_FUNC_ERROR(fcntl(sk, F_SETFL, O_NONBLOCK), strerror(errno));
        int val = 1;
        CHECK_FUNC_ERROR(setsockopt(sk,SOL_SOCKET, SO_KEEPALIVE,&val, sizeof(val)) == -1, strerror(errno));
        FD_ZERO(&read_set);
        FD_SET(tcp_socket,&read_set);
        workers +=1;
    }
    if (workers < max_workers)
    {
        fprintf(stderr, "Only %d of %d workers successfully connected\n", workers, max_workers);
    }
    return workers;
}
int split_interval(struct server* servers, int workers_num, int threads_num)
{
    if (servers == NULL)
        return -1;
    double step = (interval_ending - interval_beginning)/threads_num;
    servers[0].interval.begin = interval_beginning;
    servers[0].interval.dx = dx;
    for (int i = 1; i < workers_num; i++) {
        servers[i-1].interval.end = servers[i].interval.begin = servers[i-1].interval.begin + step*servers[i-1].threads_num;
        servers[i].interval.dx = dx;
    }
    servers[workers_num-1].interval.end = interval_ending;
    return 0;
}
