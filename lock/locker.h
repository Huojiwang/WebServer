#ifndef LOCKER_H
#define LOCKER_H

#include<exception>
#include<pthread.h>
#include<semaphore.h>
#include<condition_variable>

std::condition_variable cd;

class sem{
    sem_t m_sem;
    public:
        sem(){
            if(sem_init(&m_sem,0,0) != 0){  //sem_init初始化成功时返回0，失败返回-1，并设置errno变量  在posix线程和进程间共享情况下可用。中间参数 0 表示线程间共享，非0表示进程间共享  最后参数表示 信号量初始值。
                throw std::exception();
            }
        }
        sem(unsigned num){
            if(sem_init(&m_sem,0,num) != 0){
                throw std::exception();
            }
        }
        ~sem(){
            sem_destroy(&m_sem);
        }
        bool wait(){    //判断是否需要等待
            return sem_wait(&m_sem) == 0;   //将等待信号量的值大于0。如果当前信号量的值大于0，则将其递减1，并且立即返回
        }
        bool post(){    //将信号量的值+1，并通知等待该信号量的其他线程。如果有其他线程因为该等待信号量而被阻塞，则将一个线程唤醒并执行
            return sem_post(&m_sem) == 0;
        }
};
class locker{
    pthread_mutex_t m_mutex;
    public:
        locker(){
            if(pthread_mutex_init(&m_mutex,NULL) != 0){
                throw std::exception();
            }
        }
        ~locker(){
            pthread_mutex_destroy(&m_mutex);
        }
        bool lock(){
            return pthread_mutex_lock(&m_mutex) == 0;
        }
        bool unlock(){
            return pthread_mutex_unlock(&m_mutex) == 0;
        }
        pthread_mutex_t *get(){
            return &m_mutex;
        }
};

class cond{
    pthread_cond_t m_cond;
    public:
        cond(){
            if(pthread_cond_init(&m_cond,NULL) != 0){
                throw std::exception();
            }
        }
        ~cond(){
            pthread_cond_destroy(&m_cond);
        }
        bool wait(pthread_mutex_t  *m_mutex){
            int ret = 0;
            ret = pthread_cond_wait(&m_cond,m_mutex);
            return ret == 0;
        }
        bool timewait(pthread_mutex_t *m_mutex,timespec t){
            int ret = 0;
            ret = pthread_cond_timedwait(&m_cond,m_mutex,&t);
        }
        bool signal(){
            return pthread_cond_signal(&m_cond) == 0;
        }

        bool broadcast(){
            return pthread_cond_broadcast(&m_cond) == 0;
        }
};

#endif