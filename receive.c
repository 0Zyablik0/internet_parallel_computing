#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include <pthread.h>
#include <math.h>

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
struct interval
{
    double begin;
    double end;
    double dx;  
};

struct thread_arg
{
    double begin;
    double end;
    double sum;
};


char cpu_core_id_path[] = "/sys/bus/cpu/devices/cpu#/topology/core_id";
const unsigned int str_place = 24; // position of # in string cpu_core_id_path

int MSG = 0;
int rcv_port = 5000;
int snd_port = 4000;

double dx = 0;
double interval_beginning = 0;
double interval_ending = 0;


double  f(double x);
void*   routine(void* arg);
int     split_interval(struct thread_arg* thread_arg, unsigned long threads_num);
void    *anti_boost_routine(void* arg);
int     parse_cpu_info(unsigned* cpu_buff, unsigned cpu_num);


enum
{
    wrong_str = -1,
    system_err = -2,
    wrong_call = -3
};


int main(int argc, char* argv[]) {


    CHECK_ERROR(argc < 2, "Please, enter the number of threads\n");
    int threads_num = atoi(argv[1]);
    CHECK_ERROR(threads_num < 0, "The number of threads must be positive integer\n");

    unsigned int cpu_num = (unsigned int)sysconf(_SC_NPROCESSORS_ONLN);


    unsigned * cpu_buff = (unsigned*)calloc(cpu_num,sizeof(unsigned));
    CHECK_ERROR(cpu_buff == NULL, strerror(errno));

    pthread_t* threads = (pthread_t*)calloc((unsigned)threads_num, sizeof(pthread_t));
    CHECK_ERROR(threads == NULL, strerror(errno));

    pthread_t* anti_boost_threads = (pthread_t*)calloc((unsigned)cpu_num, sizeof(pthread_t));
    CHECK_ERROR(anti_boost_threads == NULL, strerror(errno));

    struct thread_arg* thread_arg = (struct thread_arg*)calloc((unsigned)threads_num, sizeof(struct thread_arg));
    CHECK_ERROR(thread_arg == NULL, strerror(errno));
 
    int real_cpus_num = parse_cpu_info(cpu_buff, cpu_num);
    if (real_cpus_num <= 0) exit(system_err);

    int sk = socket(PF_INET, SOCK_DGRAM, 0);
    int tcp_socket =  socket(PF_INET, SOCK_STREAM, 0);
    CHECK_ERROR(sk == -1, strerror(errno));
    int val = 1;
    struct sockaddr_in rcv_addr;
    rcv_addr.sin_port = htons(rcv_port);
    rcv_addr.sin_family = AF_INET;
    rcv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    CHECK_ERROR(bind(tcp_socket, (struct sockaddr *) &rcv_addr, sizeof(rcv_addr)) != 0 , strerror(errno));
    CHECK_ERROR(bind(sk, (struct sockaddr *) &rcv_addr, sizeof(rcv_addr)) != 0 , strerror(errno));

    unsigned int len = sizeof(rcv_addr);
    CHECK_ERROR(recvfrom(sk, &MSG, sizeof(int), 0, (struct sockaddr*)&rcv_addr, &len) == -1, strerror(errno));
    close(sk);
    printf("Server answer from %s on port %d\n",
           inet_ntoa(rcv_addr.sin_addr), ntohs(rcv_addr.sin_port));
    rcv_addr.sin_port = htons(snd_port);
    CHECK_ERROR(connect(tcp_socket, (struct sockaddr*) &rcv_addr,len) == -1, strerror(errno));
    CHECK_ERROR(fcntl(tcp_socket,F_SETFL, O_NONBLOCK) == -1, strerror(errno));
    CHECK_ERROR(send(tcp_socket,&threads_num, sizeof(int),0) == -1, strerror(errno));
    struct interval interval = {0,0,0};

    struct pollfd pollfd = {.fd = tcp_socket, .events = POLLIN};
    CHECK_ERROR(poll(&pollfd,1, -1) <= 0, strerror(errno));
    printf("poll completed\n");
    CHECK_ERROR(recv(tcp_socket,&interval, sizeof(interval),0) <= 0, strerror(errno));
    
    printf("b:%lg e:%lg dx:%lg\n", interval.begin, interval.end, interval.dx);
    dx = interval.dx;
    interval_beginning = interval.begin;
    interval_ending = interval.end;

    split_interval(thread_arg,(unsigned) threads_num);
    pthread_attr_t pthread_attr;
    CHECK_ERROR(pthread_attr_init(&pthread_attr) != 0, strerror(errno));


    cpu_set_t cpu_set;

    for (int i = 0; i < threads_num; i++) {
        CPU_ZERO(&cpu_set);
        CPU_SET(cpu_buff[i%cpu_num],&cpu_set);
        pthread_attr_setaffinity_np(&pthread_attr,sizeof(cpu_set_t),&cpu_set);
        pthread_create(threads+i, &pthread_attr, routine, thread_arg+i);

    }

    for (int i = threads_num; i < real_cpus_num ; i++) {
        CPU_ZERO(&cpu_set);
        CPU_SET(cpu_buff[i%cpu_num],&cpu_set);
        pthread_attr_setaffinity_np(&pthread_attr,sizeof(cpu_set_t),&cpu_set);
        pthread_create(anti_boost_threads+i, &pthread_attr, anti_boost_routine, NULL);
    }


    double res = 0;
    for (int i = 0; i < threads_num; i++) {
        pthread_join(threads[i], NULL);
        res += thread_arg[i].sum;
    }
    CHECK_ERROR(setsockopt(tcp_socket,SOL_SOCKET, SO_KEEPALIVE,&val, sizeof(val)) == -1, strerror(errno));   
    send(tcp_socket, &res, sizeof(double),0);

    printf("Result: %lg\n", res);


    pthread_attr_destroy(&pthread_attr);
    free(threads);
    free(thread_arg);
    free(anti_boost_threads);
    free(cpu_buff);


    close(tcp_socket);
    exit (0);
}

#undef CHECK_ERROR


void *routine(void* arg)
{
    struct thread_arg elem = *(struct thread_arg *)arg;
    double begin = elem.begin;
    double end = elem.end;
    while (begin < end)
    {
        elem.sum += (f(begin)+f(begin+dx));
        begin += dx;
    }
    (*(struct thread_arg *)arg).sum = elem.sum*dx/2;
    pthread_exit(0);

}
double f(double x)
{
    return 1/sqrt(1+x*x);
}

int split_interval(struct thread_arg* thread_arg, unsigned long threads_num)
{
    if (thread_arg == NULL)
        return -1;
    double step = (interval_ending - interval_beginning)/threads_num;
    thread_arg[0].begin = interval_beginning;
    for (int i = 1; i < threads_num; i++) {
            thread_arg[i-1].end = thread_arg[i].begin = thread_arg[i-1].begin + step;
    }
    thread_arg[threads_num-1].end = interval_ending;
    return 0;
}
void *anti_boost_routine(void* arg)
{
    int i = 0;
    while (1)
    {
        i++;
    }
}

int parse_cpu_info(unsigned* cpu_buff, unsigned cpu_num)
{
    if (cpu_buff == NULL)
        return wrong_call;
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    int core = 0;
    int pos = 0;
    int real_cpus = 0;
    for (unsigned int i = 0; i < cpu_num; i++) {
        cpu_core_id_path[str_place] = (char)(i+48);
        FILE* cpu_info = fopen(cpu_core_id_path, "r");
        if (cpu_info == NULL)
        {
            perror(strerror(errno));
            return system_err;
        }

        core = fgetc(cpu_info) - 48;
        fclose(cpu_info);
        if(!CPU_ISSET(core,&cpu_set))
        {
            CPU_SET(core,&cpu_set);
            cpu_buff[i] = i;
            real_cpus ++;
        }
        else
        {
            cpu_buff[cpu_num-pos-1] = i;
            pos ++;
        }

    }
    return real_cpus;
}
#undef CHECK_FUNC_ERROR
