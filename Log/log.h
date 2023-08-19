#ifndef _LOG_H_
#define _LOG_H_

#include <stdio.h>
#include<iostream>
#include<string>
#include<stdarg.h>
#include<pthread.h>
#include "block_queue.h"

class log
{
    /* data */
    char dir_name[128];   //路径名
    char log_name[128];   //文件名
    int m_split_line;       //日志的最大行数
    int m_log_buf_size;     //日志缓冲区大小
    long long m_count;      //日志行数记录
    int m_today;            //按天分类，记录当前时间是哪一天
    FILE *m_fp;             //打开log文件的指针
    char *m_buf;
    block_queue<std::string> *m_log_queue;   //阻塞队列
    bool m_is_async;        //是否同步标志位
    locker m_locker;
    int m_close_log;        //关闭日志
    
    public:
        static log *get_instance(){
            static log instance;
            return &instance;
        }

        static void *flush_log_thread(void *args){
            log::get_instance()->async_write_log();
        }
        bool init(const char* filename,int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);
        void write_log(int level, const char *format,...);
        void flush();
    private:
        log();
    virtual ~log();
    void *async_write_log()
    {
        std::string single_log;
        while(m_log_queue->pop(single_log)){
            m_locker.lock();
            fputs(single_log.c_str(),m_fp);
            m_locker.unlock();
        }
    }
};
#define LOG_DEBUG(format,...) if(0 == m_close_log) {log::get_instance()->write_log(0,format,##__VA_ARGS__); log::get_instance()->flush();}
#define LOG_INFO(format,...) if(0 == m_close_log) {log::get_instance()->write_log(1,format,##__VA_ARGS__); log::get_instance()->flush();}
#define LOG_WARN(format,...) if(0 == m_close_log) {log::get_instance()->write_log(2,format,##__VA_ARGS__); log::get_instance()->flush();}
#define LOG_ERROR(format,...) if(0 == m_close_log) {log::get_instance()->write_log(3,format,##__VA_ARGS__); log::get_instance()->flush();}

#endif