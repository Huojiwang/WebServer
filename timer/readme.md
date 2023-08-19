
定时器处理非活动连接
===============
由于非活跃连接占用了连接资源，严重影响服务器的性能，通过实现一个服务器定时器，处理这种非活跃连接，释放连接资源。利用alarm函数周期性地触发SIGALRM信号,该信号的信号处理函数利用管道通知主循环执行定时器链表上的定时任务.
> * 统一事件源
> * 基于升序链表的定时器
> * 处理非活动连接

##各个函数的意义
>* http_conn::initmysql_result(connection_pool *connpool)  数据库连接池中获取链接然后赋值给SQL

>*  client_data中包含有ip地址，以及在本机上的sockfd，还有一个util_itmer计时器
>* util_timer 包含过期时间，以及回调函数， 还有client_data数据
>* sort_timer_lst是一个util_timer的链表，链表中有添加新的client到来后的timer的方法 调整时间的方法 删除在链表中的util_timer的方法。
>* utils包含了静态的u_pipedfd, util_timer的链表,以及u_epollfd和时间间隔TIMESLOT
>* alarm(m_TIMESLOT); 系统调用，用于设置定时器，以在指定的时间间隔后发送SIGALRM信号