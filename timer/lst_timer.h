#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../Log/log.h"

class util_timer;

struct client_data{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

class util_timer{
    public:
        time_t expire;          //过期时间
        void (* cb_func)(client_data *);    //回调函数
        client_data *user_data;
        util_timer *prev;
        util_timer *next;
    public:
        util_timer():prev(NULL),next(NULL){}
};

class sort_timer_lst{
    void add_timer(util_timer *timer, util_timer *lst_head);
    util_timer *head;
    util_timer *tail;

    public:
        sort_timer_lst();
        ~sort_timer_lst();

        void add_timer(util_timer *timer);
        void adjust_timer(util_timer *timer);
        void del_timer(util_timer *timer);
        void tick();    //遍历定时器列表，检查并且处理已经过期的定时器。在每次处理过期定时器时，会执行其回调函数并删除相应的定时器结点
};

class utils{
    public:
        utils() {}
        ~utils() {}
        void init(int timeslot);

        //对文件描述符设置非阻塞
        int setnonblocking(int fd);

        //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
        void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

        //信号处理函数
        static void sig_handler(int sig);

        //设置信号函数
        void addsig(int sig, void (handler)(int), bool restart = true);

        //定时处理任务，重新定时以不断触发SIGALRM信号
        void timer_handler();

        void show_error(int connfd,const char * info);

    public:
        static int *u_pipefd;
        sort_timer_lst m_timer_lst;
        static int u_epollfd;
        int m_TIMESLOT;
};  

void cb_func(client_data *user_data);

#endif