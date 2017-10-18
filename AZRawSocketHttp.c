//
//  http.c
//  CommonDemo
//
//  Created by arronzhu on 2017/10/18.
//  Copyright © 2017年 arronzhu. All rights reserved.
//

#include "AZRawSocketHttp.h"
#import <sys/socket.h>
#import <netinet/in.h>
#import <arpa/inet.h>
#import <netdb.h>
#import <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#define BUFFER_SIZE 1024
#define HTTP_POST "POST /%s HTTP/1.1\r\nHost: %s:%d\r\nAccept: */*\r\n"\
"Content-Type:application/x-www-form-urlencoded\r\nContent-Length: %lu\r\n\r\n%s"
#define HTTP_GET "GET /%s HTTP/1.1\r\nHost: %s:%d\r\nAccept: */*\r\n\r\n"

char* itoa(int val, int base){
    static char buf[32] = {0};
    int i = 30;
    for(; val && i ; --i, val /= base)
        buf[i] = "0123456789abcdef"[val % base];
    return &buf[i+1];
}

static int http_tcpclient_create(const char *host, int port, bool block) {
    struct addrinfo hints = { 0 }, *res;
    int socket_fd;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(host, itoa(port, 10), &hints, &res) != 0 || res->ai_addr == NULL) return -1;
    
    if((socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) return -1;
    if (!block) {
        int flags = fcntl(socket_fd, F_GETFL, 0);
        fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
    }
    
    //阻塞connect
    if (block) {
        if (connect(socket_fd, res->ai_addr, res->ai_addrlen) != 0) { return -1; }
        return socket_fd;
    }
    
    //非阻塞connect
    int ret = connect(socket_fd, res->ai_addr, res->ai_addrlen);
    if (ret != 0) {
        if (errno != EINPROGRESS) return -1;
        // 等待连接完成，errno == EINPROGRESS表示正在建立链接
        fd_set set;
        FD_ZERO(&set);
        FD_SET(socket_fd, &set);  //相反的是FD_CLR(_sock_fd,&set)
        
        time_t timeout = 100;     //(超时时间设置为100毫秒)
        struct timeval timeo;
        timeo.tv_sec = timeout / 1000;
        timeo.tv_usec = (timeout % 1000) * 1000;
        int retval = select(socket_fd + 1, NULL, &set, NULL, &timeo);
        if (retval < 0) {
            return -1;
        };
        if (retval == 0) {
            return -1;
        }
        
        //将检测到_socket_fd读事件或写事件，并不能说明connect成功
        if(FD_ISSET(socket_fd, &set)) {
            int error = 0;
            socklen_t len = sizeof(error);
            if(getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
                return -1;
            }
            if(error != 0) {//失败
                return -1;
            } else {//建立链接成功
                return socket_fd;
            }
        }
    }
    return socket_fd;
}

static void http_tcpclient_close(int socket){
    close(socket);
}

static int http_parse_url(const char *url,char *host,char *file,int *port) {
    char *ptr1,*ptr2;
    unsigned long len = 0;
    if(!url || !host || !file || !port){
        return -1;
    }
    
    ptr1 = (char *)url;
    
    if(!strncmp(ptr1,"http://",strlen("http://"))){
        ptr1 += strlen("http://");
    }else{
        return -1;
    }
    
    ptr2 = strchr(ptr1,'/');
    if(ptr2){
        len = strlen(ptr1) - strlen(ptr2);
        memcpy(host,ptr1,len);
        host[len] = '\0';
        if(*(ptr2 + 1)){
            memcpy(file,ptr2 + 1,strlen(ptr2) - 1 );
            file[strlen(ptr2) - 1] = '\0';
        }
    }else{
        memcpy(host,ptr1,strlen(ptr1));
        host[strlen(ptr1)] = '\0';
    }
    //get host and ip
    ptr1 = strchr(host,':');
    if(ptr1){
        *ptr1++ = '\0';
        *port = atoi(ptr1);
    }else{
        *port = HTTP_DEFAULT_PORT;
    }
    return 0;
}

static ssize_t http_tcpclient_send(int socket, char *buff, ssize_t size, bool block) {
    ssize_t sent = 0, tmpres = 0;
    int flag = block ? 0 : MSG_DONTWAIT;
    
    while(sent < size){
        tmpres = send(socket, buff+sent, size-sent, flag);
        if(tmpres == -1){
            return -1;
        }
        sent += tmpres;
    }
    return sent;
}

static ssize_t http_tcpclient_recv(int socket, char *lpbuff, bool block) {
    ssize_t recvnum = -1;
    int flag = block ? 0 : MSG_DONTWAIT;
    
    while (recvnum < 0) {
        recvnum = recv(socket, lpbuff, BUFFER_SIZE*4, flag);
        if (recvnum < 0 && errno == EAGAIN) {
            continue;
        } else {
            break;
        }
    }
    if (recvnum < 0) {
        printf("%s\n", strerror(errno));
    }
    
    return recvnum;
}

static char *http_parse_result(const char*lpbuf) {
    char *ptmp = NULL;
    char *response = NULL;
    ptmp = (char*)strstr(lpbuf,"HTTP/1.1");
    if(!ptmp){
        printf("http/1.1 not faind\n");
        return NULL;
    }
    if(atoi(ptmp + 9)!=200){
        printf("result:\n%s\n",lpbuf);
        return NULL;
    }
    
    ptmp = (char*)strstr(lpbuf,"\r\n\r\n");
    if(!ptmp){
        printf("ptmp is NULL\n");
        return NULL;
    }
    response = (char *)malloc(strlen(ptmp)+1);
    if(!response){
        printf("malloc failed \n");
        return NULL;
    }
    strcpy(response,ptmp+4);
    return response;
}

char * http_get(const char *url) {
    struct sock_block_type type = {true, true, true};
    return http_get_with_config(url, &type);
}

char *http_get_with_config(const char *url, struct sock_block_type *type) {
    int socket_fd = -1;
    char lpbuf[BUFFER_SIZE*4] = {'\0'};
    char host_addr[BUFFER_SIZE] = {'\0'};
    char file[BUFFER_SIZE] = {'\0'};
    int port = 0;
    
    if(!url){
        printf("url cann't be nil!\n");
        return NULL;
    }
    
    if(http_parse_url(url,host_addr,file,&port)){
        printf("http_parse_url failed!\n");
        return NULL;
    }
    
    socket_fd = http_tcpclient_create(host_addr, port, type->socket_block_flag);
    if(socket_fd < 0){
        printf("http_tcpclient_create failed\n");
        return NULL;
    }
    
    sprintf(lpbuf, HTTP_GET, file, host_addr, port);
    
    if(http_tcpclient_send(socket_fd,lpbuf,strlen(lpbuf), type->send_block_flag) < 0){
        printf("http_tcpclient_send failed..\n");
        return NULL;
    }
    
    if(http_tcpclient_recv(socket_fd, lpbuf, type->recv_block_flag) <= 0){
        printf("http_tcpclient_recv failed\n");
        return NULL;
    }
    
    http_tcpclient_close(socket_fd);
    
    char *response = http_parse_result(lpbuf);
    printf("%s\n", response);
    return response;
}
