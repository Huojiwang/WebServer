#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
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
#include <map>

#include "../lock/locker.h"
#include "../CGLmysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../Log/log.h"

class http_conn{
    public:
        static const int FILENAME_LEN = 200;
        static const int READ_BUFFER_SIZE = 2048;
        static const int WRITE_BUFFER_SIZE = 1024;
    enum METHOD /*解析客户端请求时，主状态机的状态*/
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

    enum CHECK_STATE{   ///从状态机的状态
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum LINE_STATUS{   //从状态机的三种状态
            LINE_OK = 0,
            LINE_BAD,
            LINE_OPEN
        };
    enum HTTP_CODE{ /*服务器处理http请求结果的可能结果，报文解析的结果*/
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_REQUEST,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
  
    private:
        int m_sockfd;
        sockaddr_in m_address;
        char m_read_buf[READ_BUFFER_SIZE]; //读缓冲区
        int m_read_idx;     //表示读缓冲区中以及读入的客户端数据的最后一个字节的下一个位置
        int m_checked_idx;  //当前正在分析的字符在都缓冲区的位置
        int m_start_line;   //当前正在解析的行的位置

        char m_write_buf[WRITE_BUFFER_SIZE];
        int m_write_idx;
        CHECK_STATE m_check_state;  //主状态机当前的状态
        METHOD m_method;
        char m_real_file[FILENAME_LEN];
        char *m_url;
        char *m_version;
        char *m_host;
        int m_content_length;
        bool m_linger;
        char *m_file_address;
        struct stat m_file_stat;
        struct iovec m_iv[2];   //iovec 结构体的作用是描述一个或多个连续的缓冲区，用于在输入输出操作中传递数据。它的设计目的是提供一种有效的方式，允许同时读取或写入多个不连续的缓冲区。
        int m_iv_count;
        int cgi;        //是否启用的POST
        char *m_string; //存储请求头数据
        int bytes_to_send;
        int bytes_have_send;
        char *doc_root;

        std::map<std::string, std::string> m_users;
        int m_TRIGMode;
        int m_close_log;

        char sql_user[100];
        char sql_passwd[100];
        char sql_name[100];
    public:
        static int m_epollfd;
        static int m_user_count;
        MYSQL *mysql;
        int m_state;  //读为0, 写为1
    public:
        http_conn(){}
        ~http_conn(){}
    public:
        void init(int sockfd, const sockaddr_in &addr, char*, int, int, std::string user,std::string passwd, std::string sqlname);
        void close_conn(bool real_close = true);
        void process();
        bool read_once();   //非阻塞的读
        bool write();   //非阻塞的写
        sockaddr_in *get_address(){
            return &m_address;
        }
        void initmysql_result(connection_pool *connpool);
        int timer_flag;
        int improv;

    private:
        void init();
        HTTP_CODE process_read(); //解析http请求
        bool process_write(HTTP_CODE ret);
        HTTP_CODE parse_request_line(char *text);   //解析请求首航
        HTTP_CODE parse_headers(char *text);
        HTTP_CODE parse_content(char *text);
        HTTP_CODE do_request();
        char *get_line(){ return m_read_buf + m_start_line;}
        LINE_STATUS parseline();        //解析一行
        void unmap();
        bool add_response(const char *format,...);
        bool add_content(const char *content);
        bool add_status_line(int status, const char *title);
        bool add_headers(int content_length);
        bool add_content_type();
        bool add_content_length(int content_length);
        bool add_linger();
        bool add_blank_line();
};




#endif