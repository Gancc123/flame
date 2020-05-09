#include <cstdio>
#include <cstdint>
#include <iostream>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <string>

#include "spdk/json.h"

static uint8_t g_buf[1024];
static uint8_t *g_write_pos;

static int
write_cb(void *cb_ctx, const void *data, size_t size)
{
	size_t buf_free = g_buf + sizeof(g_buf) - g_write_pos;

	if (size > buf_free) {
		return -1;
	}

	memcpy(g_write_pos, data, size);
	g_write_pos += size;

	return 0;
}

#define ERR_EXIT(m) \
	do{ \
		perror(m); \
		exit(EXIT_FAILURE); \
	} while(0)


int main(int argc, char** argv) {
    memset(g_buf, 0, sizeof(g_buf));
	g_write_pos = g_buf;

    struct spdk_json_write_ctx *w;
    w = spdk_json_write_begin(write_cb, NULL, 0);

    spdk_json_write_object_begin(w);
    spdk_json_write_named_string(w, "jsonrpc", "2.0");
    spdk_json_write_named_uint32(w, "id", 1);
	spdk_json_write_named_string(w, "method", "get_rpc_methods");
	spdk_json_write_name(w, "params");
	spdk_json_write_object_begin(w);
	spdk_json_write_named_bool(w, "current", true);
	spdk_json_write_object_end(w);
	spdk_json_write_object_end(w);

    spdk_json_write_end(w);
    printf("%s\n", g_buf);

    int sockfd;
    if((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
        ERR_EXIT("socket");
    }
    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(5566);
    serveraddr.sin_addr.s_addr = inet_addr("192.168.19.129");

    if((connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr))) < 0){
        ERR_EXIT("connect");
    }
    
    char recvbuf[1024] = {0};
    int rc = write(sockfd, g_buf, sizeof(g_buf));
    if(rc == -1) ERR_EXIT("write socket");
    rc = read(sockfd, recvbuf, sizeof(recvbuf));
    if(rc == -1) ERR_EXIT("read socket");

    fputs(recvbuf, stdout);
    
    close(sockfd);
    return 0;
}

