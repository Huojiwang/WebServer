#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<list>
#include<cstdio>
#include<exception>
#include<pthread.h>
#include"../lock/locker.h"
#include"../CGLmysql/sql_connection_pool.h"


template<typename T>
class threadpool{
    private:
        //工作线程运行的函数，它不断的从工作队列中取出任务并执行
        static void *worker(void *arg);
        void run();
    
    private:
        int m_thread_number;    //线程池中的线程数
        int m_max_request;      //请求队列中允许的最大请求数
        pthread_t *m_threads;   //描述线程池的数组，其大小位m_thread_number
        std::list<T *> m_workqueue; //请求队列
        locker m_queue_lock;    // 保护请求队列的互斥锁
        sem m_queuestat;        // 是否有任务需要处理
        connection_pool *m_connpool;    //数据库
        int m_actor_mode;       //模型切换

    public:
        threadpool(int actor_model, connection_pool *connpool, int thread_number = 8, int max_request = 10000);
        ~threadpool();
        bool append(T *request,int state);
        bool append_p(T *request);
};

template<typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connpool, int thread_number , int max_request):m_actor_mode(actor_model),m_connpool(connpool),m_thread_number(thread_number),m_max_request(max_request){
    if(thread_number <= 0 || max_request <= 0){
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];
    if(!m_threads){
        throw std::exception();
    }
    for( int i = 0; i < thread_number; ++i){
        if(pthread_create(m_threads+i,NULL,worker,this) != 0 ){
            delete []m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i])){
            delete []m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool(){
    delete []m_threads;
}

template<typename T>
bool threadpool<T>::append(T *request, int state){
    m_queue_lock.lock();
    if( m_workqueue.size() >= m_max_request){
        m_queue_lock.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queue_lock.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
bool threadpool<T>::append_p(T* request){
    m_queue_lock.lock();
    if(m_workqueue.size() >= m_max_request){
        m_queue_lock.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queue_lock.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
void* threadpool<T>::worker(void *arg){
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run(){
    while(true){
        m_queuestat.wait();
        m_queue_lock.lock();
        if(m_workqueue.empty()){
            m_queue_lock.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queue_lock.unlock();

        if(!request){
            continue;
        }
        //T类型包含有mysql函数，以及m_state还有一个readonce（）函数 
        if(1 == m_actor_mode){
            if(0 == request->m_state){
                if(request->read_once()){
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql,m_connpool);
                    request->process();
                }else{
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }else{
                if(request->write()){
                    request->improv = 1;
                }else{
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }else{
            connectionRAII mysqlcon(&request->mysql,m_connpool);
            request->process();
        }
    }
}


#endif