#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>
#include <time.h>
//定义http响应的一些状态信息
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

//urldecode
int hex2dec(char c)
{
    if ('0' <= c && c <= '9') 
    {
        return c - '0';
    } 
    else if ('a' <= c && c <= 'f')
    {
        return c - 'a' + 10;
    } 
    else if ('A' <= c && c <= 'F')
    {
        return c - 'A' + 10;
    } 
    else 
    {
        return -1;
    }
}

void urldecode(char url[])
{
    int i = 0;
    int len = strlen(url);
    int res_len = 0;
    char res[201];
    for (i = 0; i < len; ++i) 
    {
        char c = url[i];
        if (c != '%') 
        {
            res[res_len++] = c;
        }
        else 
        {
            char c1 = url[++i];
            char c0 = url[++i];
            int num = 0;
            num = hex2dec(c1) * 16 + hex2dec(c0);
            res[res_len++] = num;
        }
    }
    res[res_len] = '\0';
    strcpy(url, res);
}



// void http_conn::initmysql_result(connection_pool *connPool)
// {
//     //先从连接池中取一个连接
//     MYSQL *mysql = NULL;
//     connectionRAII mysqlcon(&mysql, connPool);


//     //设置字符集
//     mysql_set_character_set(mysql, "utf8");



//     //在user表中检索username，passwd数据，浏览器端输入
//     if (mysql_query(mysql, "SELECT username,passwd FROM user"))
//     {
//         LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
//     }

//     //从表中检索完整的结果集
//     MYSQL_RES *result = mysql_store_result(mysql);

//     //返回结果集中的列数
//     int num_fields = mysql_num_fields(result);

//     //返回所有字段结构的数组
//     MYSQL_FIELD *fields = mysql_fetch_fields(result);

//     //从结果集中获取下一行，将对应的用户名和密码，存入map中
//     while (MYSQL_ROW row = mysql_fetch_row(result))
//     {
//         string temp1(row[0]);
//         string temp2(row[1]);
//         users[temp1] = temp2;
//     }
// }

//对文件描述符设置非阻塞
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

//关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

//初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;

    //当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
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

//从状态机，用于分析出一行内容
//返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r')
        {
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

//循环读取客户数据，直到无数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

    //LT读取数据
    if (0 == m_TRIGMode)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;

        if (bytes_read <= 0)
        {
            return false;
        }

        return true;
    }
    //ET读数据
    else
    {
        while (true)
        {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            }
            else if (bytes_read == 0)
            {
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}

//解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    m_url = strpbrk(text, " \t");
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else
        return BAD_REQUEST;
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    //当url为/时，显示首页
    if (strlen(m_url) == 1)
        strcat(m_url, "index.html");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if (text[0] == '\0')
    {
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

//判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        //POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(text);
            if (ret == GET_REQUEST)
                return do_request();
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    //printf("m_url:%s\n", m_url);
    const char *p = strrchr(m_url, '/');

    //处理cgi
    if (cgi == 1 && (*(p + 1) == '4' || *(p + 1) == '5' || *(p + 1) == '6' || *(p + 1) == '0'))
    {

        //根据标志判断是登录检测还是注册检测
        // char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        //POST-DHF
        if (*(p + 1) == '4')
        {
            //author=123&stage=123&mark=123&type=123&filename=123
            char author[100], stage[100], mark[100], type[100], filename[451];
            int i;
            int j = 0;
            for (i = 7; m_string[i] != '&'; ++i, ++j)
                author[j] = m_string[i];
            author[j] = '\0';

            j = 0;
            for (i = i + 7; m_string[i] != '&'; ++i, ++j)
                stage[j] = m_string[i];
            stage[j] = '\0';

            j = 0;
            for (i = i + 6; m_string[i] != '&'; ++i, ++j)
                mark[j] = m_string[i];
            mark[j] = '\0';

            j = 0;
            for (i = i + 6; m_string[i] != '&'; ++i, ++j)
                type[j] = m_string[i];
            type[j] = '\0';

            // 一个中文9个字节
            j = 0;
            for (i = i + 10; j < 450 && m_string[i] != '\0'; ++i, ++j)
                filename[j] = m_string[i];
            filename[j] = '\0';

            urldecode(author); 
            urldecode(filename); 

            //insert  INSERT INTO DHF(author, stage, mark, type, filename, code, date) VALUES("朱耀辉", "test", "test", "test", "测试测试测试", "test", NOW());
            char *sql_insert = (char *)malloc(sizeof(char) * 600);
            strcpy(sql_insert, "INSERT INTO DHF(author, stage, mark, type, filename, code, date) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, author);
            strcat(sql_insert, "', '");
            strcat(sql_insert, stage);
            strcat(sql_insert, "', '");
            strcat(sql_insert, mark);
            strcat(sql_insert, "', '");
            strcat(sql_insert, type);
            strcat(sql_insert, "', '");
            strcat(sql_insert, filename);
            strcat(sql_insert, "', 'NULL', NOW())");

            //update1
            char *sql_update = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_update, "UPDATE DHF SET code=");

            //code1
            char *code = (char *)malloc(sizeof(char) * 30);
            strcpy(code, "RNV-");
            strcat(code, mark);
            strcat(code, "-");
            strcat(code, type);
            strcat(code, "-");


            m_lock.lock();
            int res = mysql_query(mysql, sql_insert);

            res = mysql_query(mysql, "SELECT LAST_INSERT_ID()");
            MYSQL_RES *result = mysql_store_result(mysql);
            MYSQL_ROW row;
            row = mysql_fetch_row(result);

            //code2
            for(i = strlen(row[0]); i < 3; i++){
                strcat(code, "0");
            }
            strcat(code, row[0]);

            //update2
            strcat(sql_update, "'");
            strcat(sql_update, code);
            strcat(sql_update, "' WHERE id=");
            strcat(sql_update, row[0]);
            
            res = mysql_query(mysql, sql_update);
            m_lock.unlock();

            FILE *fp;
            fp = fopen("root/codeforreturn.html", "w");
                fprintf(fp,"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>code</title></head><body><br/><br/><div align=\"center\"><font size=\"10\"> <strong>您的文件编码为:</strong></font><br/><br/></div><div align=\"center\"><font size=\"10\"> <strong>");
                fprintf(fp,"%s",code);
                fprintf(fp,"%s",filename);
                fprintf(fp,"</strong></font></div><br/><br/></body><html>");
            fclose(fp);
            strcpy(m_url, "/codeforreturn.html");   


            free(sql_insert);
            free(sql_update);
            free(code);
            // char *m_url_real = (char *)malloc(sizeof(char) * 200);
            // strcpy(m_url_real, "/codeforreturn.html");
            // strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

            // free(m_url_real);




            // if (!res)
            //     strcpy(m_url, "/log.html");
            // else
            //     strcpy(m_url, "/registerError.html");

        }
        //POST-DMR
        if (*(p + 1) == '5')
        {
            //author=123&mark=123&type=123&filename=123
            char author[100], mark[100], type[100], filename[451];
            int i;
            int j = 0;
            for (i = 7; m_string[i] != '&'; ++i, ++j)
                author[j] = m_string[i];
            author[j] = '\0';
            j = 0;
            for (i = i + 6; m_string[i] != '&'; ++i, ++j)
                mark[j] = m_string[i];
            mark[j] = '\0';

            j = 0;
            for (i = i + 6; m_string[i] != '&'; ++i, ++j)
                type[j] = m_string[i];
            type[j] = '\0';

            j = 0;
            for (i = i + 10; j < 450 && m_string[i] != '\0'; ++i, ++j)
                filename[j] = m_string[i];
            filename[j] = '\0';

            // 中文URL解码
            urldecode(author); 
            urldecode(filename); 

            //insert  INSERT INTO DMR(author, mark, type, filename, code, date) VALUES("朱耀辉", "test", "test", "测试测试测试", "test", NOW());
            char *sql_insert = (char *)malloc(sizeof(char) * 600);
            strcpy(sql_insert, "INSERT INTO DMR(author, mark, type, filename, code, date) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, author);
            strcat(sql_insert, "', '");
            strcat(sql_insert, mark);
            strcat(sql_insert, "', '");
            strcat(sql_insert, type);
            strcat(sql_insert, "', '");
            strcat(sql_insert, filename);
            strcat(sql_insert, "', 'NULL', NOW())");

            //update1
            char *sql_update = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_update, "UPDATE DMR SET code=");

            //code1
            char *code = (char *)malloc(sizeof(char) * 30);
            strcpy(code, mark);
            strcat(code, "-");
            strcat(code, type);
            strcat(code, "-");


            m_lock.lock();
            int res = mysql_query(mysql, sql_insert);

            res = mysql_query(mysql, "SELECT LAST_INSERT_ID()");
            MYSQL_RES *result = mysql_store_result(mysql);
            MYSQL_ROW row;
            row = mysql_fetch_row(result);

            //code2
            for(i = strlen(row[0]); i < 4; i++){
                strcat(code, "0");
            }
            strcat(code, row[0]);

            //update2
            strcat(sql_update, "'");
            strcat(sql_update, code);
            strcat(sql_update, "' WHERE id=");
            strcat(sql_update, row[0]);
            
            res = mysql_query(mysql, sql_update);
            m_lock.unlock();

            FILE *fp;
            fp = fopen("root/codeforreturn.html", "w");
                fprintf(fp,"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>code</title></head><body><br/><br/><div align=\"center\"><font size=\"10\"> <strong>您的文件编码为:</strong></font><br/><br/></div><div align=\"center\"><font size=\"10\"> <strong>");
                fprintf(fp,"%s  %s",code,filename);
                fprintf(fp,"</strong></font></div><br/><br/></body><html>");
            fclose(fp);
            strcpy(m_url, "/codeforreturn.html");   
            free(sql_insert);
            free(sql_update);
            free(code);
            // if (!res)
            //     strcpy(m_url, "/log.html");
            // else
            //     strcpy(m_url, "/registerError.html");
        }
        //POST-SQP
        if (*(p + 1) == '6')
        {
            //author=123&mask=123&unpassstage
            char author[100], mark[100], unpassstage[200];
            int i;
            int j = 0;
            for (i = 7; m_string[i] != '&'; ++i, ++j)
                author[j] = m_string[i];
            author[j] = '\0';
            j = 0;
            for (i = i + 6; m_string[i] != '&'; ++i, ++j)
                mark[j] = m_string[i];
            mark[j] = '\0';

            j = 0;
            for (i = i + 13; m_string[i] != '\0'; ++i, ++j)
                unpassstage[j] = m_string[i];
            unpassstage[j] = '\0';

            urldecode(author); 


            //获取当前年月
            int res = mysql_query(mysql, "SELECT CURDATE()");
            MYSQL_RES *result = mysql_store_result(mysql);
            MYSQL_ROW row_date;
            row_date = mysql_fetch_row(result);
            char yearmonth[5];
            yearmonth[0] = row_date[0][2];
            yearmonth[1] = row_date[0][3];
            yearmonth[2] = row_date[0][5];
            yearmonth[3] = row_date[0][6];
            yearmonth[4] = '\0';



            //获取最后一条记录年月
            char *sql_lastrecord = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_lastrecord, "SELECT code FROM SQP_");
            strcat(sql_lastrecord, unpassstage);
            strcat(sql_lastrecord, " WHERE id=(select MAX(id) FROM SQP_");
            strcat(sql_lastrecord, unpassstage);
            strcat(sql_lastrecord, ")");
            res = mysql_query(mysql, sql_lastrecord);
            printf("%d", res);
            result = mysql_store_result(mysql);
            MYSQL_ROW row_lastcode;
            row_lastcode = mysql_fetch_row(result);
            char lastyearmonth[5];
            lastyearmonth[0] = row_lastcode[0][8];
            lastyearmonth[1] = row_lastcode[0][9];
            lastyearmonth[2] = row_lastcode[0][10];
            lastyearmonth[3] = row_lastcode[0][11];
            lastyearmonth[4] = '\0';

            char nextnum_c[4];
            nextnum_c[0] = row_lastcode[0][12];
            nextnum_c[1] = row_lastcode[0][13];
            nextnum_c[2] = row_lastcode[0][14];            
            nextnum_c[3] = '\0';

            int nextnum = atoi(nextnum_c);
            nextnum++;
            string buff = to_string(nextnum);
            j = 0;
            for(; j < buff.size(); j++){
                nextnum_c[2-j] = buff[buff.size()-j-1];
            }
            for(;j < 3;j++){
                nextnum_c[2-j] = '0';
            }
            //code
            char *code = (char *)malloc(sizeof(char) * 30);
            strcpy(code, "NCF-");
            strcat(code, unpassstage);
            strcat(code, "-");
            strcat(code, yearmonth);
            bool sign = true;
            for(int k = 0; k < 4;k++){
                if(yearmonth[k] != lastyearmonth[k]){
                    sign = false;
                }
            }
            if(sign){
                strcat(code, nextnum_c);
            }
            else{
                strcat(code, "001");
            }

            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO SQP_");
            strcat(sql_insert, unpassstage);
            strcat(sql_insert, "(author, mark, unpassstage, code, date) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, author);
            strcat(sql_insert, "', '");
            strcat(sql_insert, mark);
            strcat(sql_insert, "', '");
            strcat(sql_insert, unpassstage);
            strcat(sql_insert, "', '");
            strcat(sql_insert, code);
            strcat(sql_insert, "', NOW())");
            res = mysql_query(mysql, sql_insert);

            //insert  INSERT INTO SQP(author, mark, unpassstage, code, date) VALUES("朱耀辉", "测试测试测试", "test", NOW());
            strcpy(sql_insert, "INSERT INTO SQP(author, mark, unpassstage, code, date) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, author);
            strcat(sql_insert, "', '");
            strcat(sql_insert, mark);
            strcat(sql_insert, "', '");
            strcat(sql_insert, unpassstage);
            strcat(sql_insert, "', '");
            strcat(sql_insert, code);
            strcat(sql_insert, "', NOW())");
            res = mysql_query(mysql, sql_insert);
            m_lock.unlock();

            FILE *fp;
            fp = fopen("root/codeforreturn.html", "w");
                fprintf(fp,"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>code</title></head><body><br/><br/><div align=\"center\"><font size=\"10\"> <strong>您的文件编码为:</strong></font><br/><br/></div><div align=\"center\"><font size=\"10\"> <strong>");
                fprintf(fp,"%s",code);
                fprintf(fp,"</strong></font></div><br/><br/></body><html>");
            fclose(fp);
            strcpy(m_url, "/codeforreturn.html");   
            free(sql_insert);
            // free(sql_update);
            free(code);

            // if (!res)
            //     strcpy(m_url, "/log.html");
            // else
            //     strcpy(m_url, "/registerError.html");
        }
        if (*(p + 1) == '0')
        {
           //author=123&stage=123&mark=123&type=123&filename=123
            char filecode[100];
            int i;
            int j = 0;
            for (i = 9; j < 90 && m_string[i] != '\0'; ++i, ++j)
                filecode[j] = m_string[i];
            filecode[j] = '\0';

            char *sql_delete = (char *)malloc(sizeof(char) * 200);

            if(filecode[0]=='R' && filecode[1]=='N' && filecode[2]=='V'){
                strcpy(sql_delete, "DELETE FROM DHF WHERE code='");
                strcat(sql_delete, filecode);
                strcat(sql_delete, "'");
            }
            else if(filecode[0]=='N' && filecode[1]=='C' && filecode[2]=='F'){
                strcpy(sql_delete, "DELETE FROM SQP WHERE code='");
                strcat(sql_delete, filecode);
                strcat(sql_delete, "'");
            }
            else{
                strcpy(sql_delete, "DELETE FROM DMR WHERE code='");
                strcat(sql_delete, filecode);
                strcat(sql_delete, "'");
            }

            m_lock.lock();
            int res = mysql_query(mysql, sql_delete);
            m_lock.unlock();

            FILE *fp;
            fp = fopen("root/deleteforreturn.html", "w");
                if(!res){
                    fprintf(fp,"文件号：%s",filecode);
                    fprintf(fp,"\n删除成功");
                }
                else{
                    fprintf(fp,"操作失败");
                }
            fclose(fp);
            strcpy(m_url, "/deleteforreturn.html");   
            free(sql_delete);
        }

    }

    if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/DHF.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '2')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/DMR.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '3')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/SQP.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    //table_DHF
    else if (*(p + 1) == '7')
    {
        //生成csv
        m_lock.lock();
        int res = mysql_query(mysql, "SELECT * FROM DHF ORDER BY id DESC");
        MYSQL_RES *result = mysql_store_result(mysql);
        m_lock.unlock();
        int num_rows = mysql_num_rows(result);//行
	    int num_fields = mysql_num_fields(result);//列

        FILE *fp;
        fp = fopen("root/data_from_mysql/DHF.csv", "w");
        //id, author, stage, mark, type, filename, code, date
        fprintf(fp,"\"id\",\"author\",\"stage\",\"mark\",\"type\",\"filename\",\"code\",\"date\"\r\n");
        MYSQL_ROW row;
        for(int i = 0; i < num_rows; i++){
            row = mysql_fetch_row(result);
            for(int j = 0; j < num_fields; j++){
                fprintf(fp,"\"%s\"",row[j]);
                if(j != num_fields-1){
                    fprintf(fp,",");
                }
            }
            fprintf(fp,"\r\n");
        }
        fclose(fp);


        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/table_DHF.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    //table_DMR
    else if (*(p + 1) == '8')
    {
        //生成csv
        m_lock.lock();
        int res = mysql_query(mysql, "SELECT * FROM DMR ORDER BY id DESC");
        MYSQL_RES *result = mysql_store_result(mysql);
        m_lock.unlock();
        int num_rows = mysql_num_rows(result);//行
	    int num_fields = mysql_num_fields(result);//列

        FILE *fp;
        fp = fopen("root/data_from_mysql/DMR.csv", "w");
        //id, author, mark, type, filename, code, date) 
        fprintf(fp,"\"id\",\"author\",\"mark\",\"type\",\"filename\",\"code\",\"date\"\r\n");
        MYSQL_ROW row;
        for(int i = 0; i < num_rows; i++){
            row = mysql_fetch_row(result);
            for(int j = 0; j < num_fields; j++){
                fprintf(fp,"\"%s\"",row[j]);
                if(j != num_fields-1){
                    fprintf(fp,",");
                }
            }
            fprintf(fp,"\r\n");
        }
        fclose(fp);

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/table_DMR.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    //table_SQP
    else if (*(p + 1) == '9')
    {
        //生成csv
        m_lock.lock();
        int res = mysql_query(mysql, "SELECT * FROM SQP ORDER BY id DESC");
        MYSQL_RES *result = mysql_store_result(mysql);
        m_lock.unlock();
        int num_rows = mysql_num_rows(result);//行
	    int num_fields = mysql_num_fields(result);//列

        FILE *fp;
        fp = fopen("root/data_from_mysql/SQP.csv", "w");
        //id, author, mask, unpassstage, code, date
        fprintf(fp,"\"id\",\"author\",\"mark\",\"unpassstage\",\"code\",\"date\"\r\n");
        MYSQL_ROW row;
        for(int i = 0; i < num_rows; i++){
            row = mysql_fetch_row(result);
            for(int j = 0; j < num_fields; j++){
                fprintf(fp,"\"%s\"",row[j]);
                if(j != num_fields-1){
                    fprintf(fp,",");
                }
            }
            fprintf(fp,"\r\n");
        }
        fclose(fp);


        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/table_SQP.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);


    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;

    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;


    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
bool http_conn::write()
{
    int temp = 0;

    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if (temp < 0)
        {
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0)
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}
bool http_conn::add_response(const char *format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() &&
           add_blank_line();
}
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
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
void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}
