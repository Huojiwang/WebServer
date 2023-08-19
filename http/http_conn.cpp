#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>

using namespace std;

const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;

map<string,string> users;

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;


void http_conn::initmysql_result(connection_pool *connpool){
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql,connpool);
    
    //在user表中检索username，passwd数据，浏览器端输入
    if(mysql_query(mysql,"SELECT username,passwd FROM user")){
        LOG_ERROR("SELECT error: %s\n",mysql_error(mysql));
    }
    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);
    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);
    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_field(result);
    //从结果集中获取下一行，将对应的用户名和密码存入map中
    while(MYSQL_ROW row = mysql_fetch_row(result)){
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

//将文件描述符设置为非阻塞
int setnoblocking( int fd){
    int old_option = fcntl(fd,F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}
//将内核事件表注册读事件，ET模式，选择开启EPOLLONSHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMODE){
    epoll_event event;
    event.data.fd = fd;

    if(1 == TRIGMODE){
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    }else{
        event.events = EPOLLIN | EPOLLRDHUP;
    }

    if(one_shot){
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnoblocking(fd);
}

//从内核时间表删除描述符
void removefd(int epollfd, int fd){
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}

//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMODE){
    epoll_event event;
    event.data.fd = fd;
    if(1 == TRIGMODE){
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLHUP;
    }else{
        event.events = ev | EPOLLONESHOT | EPOLLHUP;
    }
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

//初始化连接
void http_conn::init(int sockfd, const sockaddr_in &addr,char *root, int TRIGMODE, int close_log ,string user,string passwd, string sqlname){
    m_sockfd = sockfd;
    m_address = addr;
    addfd(m_epollfd,sockfd,true,m_TRIGMode);
    m_user_count++;

    //当浏览器出现链接重置时，可能是网站根目录出错或者http响应格式出错或访问的文件内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMODE;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}
//初始化新接受的连接
//check_state默认为分析请求行状态
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}



//循环读取客户数据，直到无数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once(){
    if(m_read_idx >= READ_BUFFER_SIZE){
        return false;
    }
    int bytes_read = 0;
    //LT模式读取数据
    if( 0 ==  m_TRIGMode){
        bytes_read = recv(m_sockfd,m_read_buf + m_read_idx,READ_BUFFER_SIZE - m_read_idx,0);
        m_read_idx += bytes_read;

        if(bytes_read <= 0){
            return false;
        }

        return true;
    }else{//ET读数据
        for(;;){
            bytes_read = recv(m_sockfd,m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx,0);
            if(bytes_read == -1){
                if(errno == EAGAIN || errno == EWOULDBLOCK){
                    break;
                }
                return false;
            }else if(bytes_read == 0){
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}

//解析http请求行，获得请求方法，目标url及http版本号 
http_conn::HTTP_CODE http_conn::parse_request_line(char *text){
    m_url = strpbrk(text," \t");//首先使用strpbrk()函数在文本中查找第一个空格或制表符的位置，将其保存在m_url指针中。
    if(!m_url){
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char *method = text;
    if(strcasecmp(method,"GET" ) == 0){
        m_method = GET;
    }else if(strcasecmp(method,"POST") == 0){
        m_method = POST;
        cgi = 1;
    }else{
        return BAD_REQUEST;
    }

    m_url += strspn(m_url," \t");   //使用strspn()函数跳过连续的空格或制表符，将m_url指针指向URL起始位置。
    m_version = strpbrk(m_url," \t");

    if(!m_version){
        return BAD_REQUEST;
    }

    *m_version++ = '\0';
    m_version += strspn(m_version," \t");
    
    if(strcasecmp(m_version,"http/1.1") != 0){
        return BAD_REQUEST;
    }
    
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if(strncasecmp(m_url,"https://",8) == 0){
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if(!m_url || m_url[0] != '/'){
        return BAD_REQUEST;
    }

    if(strlen(m_url) == 1){
        strcat(m_url,"judge.html");
    }

    m_check_state = CHECK_STATE_HEADER; //主状态机改变状态  接下来解析请求头
    return NO_REQUEST;
}
//从状态机，用于分析出一行的内容
//返回值为行的读取状态，有LINE_OK,LINE_BAD<LINE_OPEN

http_conn::LINE_STATUS http_conn::parseline(){
    char temp;
    for(;m_checked_idx < m_read_idx; ++m_checked_idx){
        temp = m_read_buf[m_checked_idx];
        if(temp == '\r'){
            if((m_checked_idx +1) == m_read_idx){
                return LINE_OPEN;
            }else if(m_read_buf[m_checked_idx+1] == '\n'){
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }else if(temp == '\n'){
            if( m_checked_idx > 1 && m_read_buf[m_checked_idx-1] == '\r'){
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}
//解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text){
    if(text[0] == '\0'){
        if(m_content_length != 0){
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }else if(strncasecmp(text,"connection:",11) == 0){
        text += 11;
        text += strspn(text," \t");
        if(strcasecmp(text,"keep-alive") == 0){
            m_linger = true;
        }
    }else if(strncasecmp(text,"content-length",15) == 0){
        text += 15;
        text += strspn(text," \t");
        m_content_length = atol(text);
    }else if(strncasecmp(text,"Host:",5) == 0){
        text += 15;
        text += strspn(text," \t");
        m_host = text;
    }else{
        LOG_INFO("oop! unknow header:%s",text);
    }
    return NO_REQUEST;
}

//判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text){
    if(m_read_idx >= (m_content_length +m_checked_idx)){
        text[m_content_length] = '\0';
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;
    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || (line_status == parseline()) == LINE_OK){
        text = get_line();
        m_start_line = m_checked_idx;
        LOG_INFO("%s",text);
        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:{
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:{
                ret = parse_headers(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }else if(ret == GET_REQUEST){
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:{
                ret = parse_content(text);
                if(ret == GET_REQUEST){
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request(){
    /*   home/nowcoder/webserver/resourse  */
    strcpy(m_real_file,doc_root);
    int len = strlen(doc_root);

    const char *p = strrchr(m_url,'/');

    if(cgi == 1 && (*(p+1) == '2' || *(p+1) == '3')){
        //判断标志位时登录检测还是注册检测
        char flag = m_url[1];
        char *m_url_real = (char*)malloc(sizeof(char) * 200);

        strcpy(m_url_real,"/");
        strcat(m_url_real,m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len -1);
        free(m_url_real);

        //将用户名和密码提取出来
        char name[100],password[100];
        int i;
        for(i = 5;m_string[i] != '&';++i){
            name[i-5] = m_string[i];
        }
        name[i-5] = '\0';

        int j = 0;
        for(i = i+10;m_string[i] != '\0';++i,++j){
            password[j] = m_string[i];
        }
        password[j] = '\0';

        if(*(p+1)  == '3'){
            //如果是注册，先检测数据库中是否有重名的，没有重名的，进行增加数据
            char *sql_insert = (char*)malloc(sizeof(char)*200);
            strcpy(sql_insert,"INSERT INTO user(username,passwd) VALUES(");
            strcat(sql_insert,"'");
            strcat(sql_insert,name);
            strcat(sql_insert,"','");
            strcat(sql_insert,password);
            strcat(sql_insert,"')'");

            if(users.find(name) ==  users.end()){
                m_lock.lock();
                int res = mysql_query(mysql,sql_insert);
                users.insert(pair<string,string>(name,password));
                m_lock.unlock();

                if(!res){
                    strcpy(m_url,"/log.html");
                }else{
                    strcpy(m_url,"registerError.html");
                }
            }else{
                strcpy(m_url,"/registerError.html");
            }
        }else if(*(p+1) == '2'){
            //如果是登录，直接判断，若浏览器输入的用户名和密码在表中可以查找到，返回1否则返回0
            if(users.find(name) != users.end() && users[name] == password){
                strcpy(m_url,"welcome.html");
            }else{
                strcpy(m_url,"logError.html");
            }
        }
        if (*(p + 1) == '0')
        {
            char *m_url_real = (char *)malloc(sizeof(char) * 200);
            strcpy(m_url_real, "/register.html");
            strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

            free(m_url_real);
        }
        else if (*(p + 1) == '1')
        {
            char *m_url_real = (char *)malloc(sizeof(char) * 200);
            strcpy(m_url_real, "/log.html");
            strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

            free(m_url_real);
        }
        else if (*(p + 1) == '5')
        {
            char *m_url_real = (char *)malloc(sizeof(char) * 200);
            strcpy(m_url_real, "/picture.html");
            strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

            free(m_url_real);
        }
        else if (*(p + 1) == '6')
        {
            char *m_url_real = (char *)malloc(sizeof(char) * 200);
            strcpy(m_url_real, "/video.html");
            strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

            free(m_url_real);
        }
        else if (*(p + 1) == '7')
        {
            char *m_url_real = (char *)malloc(sizeof(char) * 200);
            strcpy(m_url_real, "/fans.html");
            strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

            free(m_url_real);
        }
        else
            strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

        if(stat(m_real_file,&m_file_stat) < 0){
            return NO_REQUEST;
        }
        if(!(m_file_stat.st_mode & S_IROTH)){
            return FORBIDDEN_REQUEST;
        }
        if (S_ISDIR(m_file_stat.st_mode)){
            return BAD_REQUEST;
        }

        int fd = open(m_real_file, O_RDONLY);
        m_file_address = (char*)mmap(0,m_file_stat.st_size,PROT_WRITE,MAP_PRIVATE,fd,0);
        close(fd);
        return FILE_REQUEST;
    }
}

bool http_conn::write(){
    int temp = 0;
    if(bytes_to_send == 0){
        modfd(m_epollfd,m_sockfd,EPOLLIN,m_TRIGMode);
        init();
        return true;
    }
    for(;;){
        temp = writev(m_sockfd, m_iv ,m_iv_count);
        if(temp < 0){
            if(errno == EAGAIN){
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }
        bytes_have_send += temp;
        bytes_to_send -= temp;
        if(bytes_have_send >= m_iv[0].iov_len){
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_to_send - m_write_idx);// 缓冲区的起始地址
            m_iv[1].iov_len = bytes_to_send;     // 缓冲区的长度
        }else{
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if(bytes_have_send <= 0){
            unmap();
            modfd(m_epollfd,m_sockfd,EPOLLIN,m_TRIGMode);

            if(m_linger){
                init();
                return true;
            }else{
                return false;
            }
        }
    }
}
/*
首先，检查bytes_to_send是否为0，如果为0，则表示数据已经全部发送完成。此时，调用modfd函数修改文件描述符的事件，将其设置为可读，然后调用init函数进行初始化操作，并返回true表示发送成功。

如果bytes_to_send不为0，进入一个无限循环，每次循环中调用writev函数进行数据发送。如果writev返回值temp小于0，表示发送出错。如果errno等于EAGAIN，表示发送缓冲区已满，此时调用modfd函数将文件描述符的事件设置为可写，然后返回true表示发送缓冲区已满，需要等待下次可写事件。

如果writev返回值temp大于等于0，表示成功发送了一部分数据。更新bytes_have_send和bytes_to_send的值，然后根据发送情况更新m_iv结构体数组。

如果bytes_have_send大于等于m_iv[0].iov_len，表示已经发送完了m_iv[0]指定的数据块，此时将m_iv[0].iov_len置为0，然后将m_file_address + (bytes_to_send - m_write_idx)作为起始地址，bytes_to_send作为长度，存储到m_iv[1]中，以便后续发送。

如果bytes_have_send小于m_iv[0].iov_len，表示还未发送完整个m_iv[0]指定的数据块，此时将m_write_buf + bytes_have_send作为起始地址，m_iv[0].iov_len - bytes_have_send作为长度，存储到m_iv[0]中，以便后续发送。

接着，检查bytes_have_send是否小于等于0。如果是，则表示发送出错或发送完成。调用unmap函数解除映射关系，然后调用modfd函数将文件描述符的事件设置为可读。如果m_linger为真，表示需要保持连接，此时调用init函数进行初始化操作，并返回true表示发送成功。如果m_linger为假，表示不需要保持连接，直接返回false表示发送失败。

以上循环会一直执行，直到数据全部发送完成或发送出错为止。
*/

bool http_conn::add_response(const char *format,...){
    if(m_write_idx >= WRITE_BUFFER_SIZE){
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);

    int len = vsnprintf(m_write_buf+m_write_idx,WRITE_BUFFER_SIZE-1-m_write_idx,format,arg_list);
    if(len >= (WRITE_BUFFER_SIZE -1 -m_write_idx)){
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);

    LOG_INFO("request:%s",m_write_buf);
    return true;
}

bool http_conn::add_status_line(int status, const char* title){
    return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
}

bool http_conn::add_headers(int content_len){
    return add_content_length(content_len) && add_linger() && add_blank_line();
}

bool http_conn::add_content_length(int content_len){
    return add_response("Content-Length: %d\r\n",content_len);
}

bool http_conn::add_content_type(){
    return add_response("Content-Type: %s\r\n","text/html");
}
bool http_conn::add_linger(){
    return add_response("Connection:%s\r\n",(m_linger == true)? "keep-alive":"close");
}
bool http_conn::add_blank_line(){
    return add_response("%s","\r\n");
}
bool http_conn::add_content(const char* content){
    return add_response("%s",content);
}

bool http_conn::process_write(HTTP_CODE ret){
    switch (ret)
    {
    case INTERNAL_ERROR:{
        add_status_line(500,error_500_title);
        add_headers(strlen(error_500_form));
        if(!add_content(error_500_form)){
            return false;
        }
        break;
    }
    case BAD_REQUEST:{
        add_status_line(404,error_404_title);
        add_headers(strlen(error_404_form));
        if(!add_content(error_404_form)){
            return false;
        }
        break;
    }
    case FORBIDDEN_REQUEST:{
        add_status_line(403,error_403_title);
        add_headers(strlen(error_403_form));
        if(!add_content(error_403_form)){
            return false;
        }
        break;
    }
    case FILE_REQUEST:{
        add_status_line(200,ok_200_title);
        if(m_file_stat.st_size != 0){
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;

            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }else{
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if(!add_content(ok_string)){
                return false;
            }

        }
    }  
    default:
        return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

void http_conn::process(){
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd,m_sockfd,EPOLLIN,m_TRIGMode);
        return;
    }
    bool write_ret = process_write(read_ret);
    if(!write_ret){
        close_conn();
    }
    modfd(m_epollfd,m_sockfd,EPOLLOUT,m_TRIGMode);
}

void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}


//关闭连接，关闭一个连接客户总量减一
void http_conn::close_conn(bool realclose){
    if(realclose && (m_sockfd != -1)){
        printf("close %D\n",m_sockfd);
        removefd(m_epollfd,m_sockfd);
        m_sockfd = -1;
        --m_user_count;
    }
}