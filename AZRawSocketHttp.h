//
//  http.h
//  CommonDemo
//
//  Created by arronzhu on 2017/10/18.
//  Copyright © 2017年 arronzhu. All rights reserved.
//

#ifndef AZRawSocketHttp_h
#define AZRawSocketHttp_h

#include <stdio.h>
#include <stdbool.h>

#define HTTP_DEFAULT_PORT 80

struct sock_block_type {
    bool socket_block_flag;
    bool send_block_flag;
    bool recv_block_flag;
};

/*默认全部阻塞*/
char *http_get(const char *url);

char *http_get_with_config(const char *url, struct sock_block_type *type);

#endif /* AZRawSocketHttp_h */
