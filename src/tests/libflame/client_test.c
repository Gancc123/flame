/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-07-12 17:21:45
 * @LastEditors: lwg
 * @LastEditTime: 2019-08-23 19:57:34
 */
#include <stdio.h>

#include "include/libflame_api.h"
#include "util/spdk_common.h"

#define READ_SIZE 8192

void cb_func(uint64_t buf, void* context, int status){
    char* mm = (char*)buf;
    for(int i = 0; i < READ_SIZE; i++){
        if(mm[i] != 0) putchar(mm[i]);
        else putchar('2');
    }
    printf("read completed\n");
    return ;
}

void cb_func2(uint64_t buf, void* context, int status){
    printf("write completed\n");
    return ;
}

#define CFG_PATH "flame_client.cfg"

static void test_gateway(void *arg1, void *arg2){
    if(flame_context_init_cfg(CFG_PATH)){
        return ;
    }
    if(flame_context_init_log("", "TRACE", "client")){
        return ;
    }
    set_cluster_name("flame");
    const char* cluster_name = get_cluster_name();
    set_node_name("test_node");
    const char* node_name = get_node_name();
    printf("cluster_name = %s\n", cluster_name);
    printf("node_name = %s\n", node_name);

    flame_handlers_connect_mgr("192.168.3.112:6677");
    int rc;
    rc = flame_handlers_open_volume("vg1", "vol1");
    BufferInfo_t write_buf_info, read_buf_info;
    write_buf_info.size = 1 << 22;
    read_buf_info.size  = 1 << 22;
    void* write_buffer = NULL, *read_buffer = NULL;
    rc = allocate_buffer(&write_buf_info, &write_buffer);
    rc = allocate_buffer(&read_buf_info, &read_buffer);
    uint64_t GigaByte = 1 << 30;
    char *m = (char *)write_buf_info.addr;
    for(int i = 0; i < READ_SIZE/4; i++)
        *(m + i) = 'a' + i % 26;
    for(int i = READ_SIZE/2; i < READ_SIZE; i++)
        *(m + i) = 'a' + i % 26;
    rc = flame_write("vg1", "vol1", write_buffer, 0, READ_SIZE, cb_func2, NULL);
    getchar();
    rc = flame_read("vg1", "vol1", read_buffer, 0, READ_SIZE, cb_func, (void*)read_buf_info.addr);
    getchar();
    spdk_app_stop(0);
}

int main(int argc, char *argv[]){
    int rc = 0;
    struct spdk_app_opts opts = {};
    spdk_app_opts_init(&opts);
    opts.name = "gateway_test";
    opts.reactor_mask = "0xf0";
    opts.rpc_addr = "/var/tmp/spdk_gateway_c.sock";
    opts.mem_size = 2048;

    rc = spdk_app_start(&opts, test_gateway, NULL, NULL);
    if(rc) {
        SPDK_NOTICELOG("spdk app start: ERROR!\n");
    } else {
        SPDK_NOTICELOG("SUCCESS!\n");
    }

    spdk_app_fini();

    return 0;
}