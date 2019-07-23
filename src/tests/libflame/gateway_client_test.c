#include <stdio.h>

#include "include/libflame_api.h"
#include "util/spdk_common.h"

// #include "libflame/libchunk/libchunk.h"
// #include "include/csdc.h"
// #include "libflame/libchunk/log_libchunk.h"
// #include "common/context.h"
// #include "common/log.h"


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

    // flame_stub_connect_mgr("192.168.3.112:6677");
    VolumeMeta_t meta;
    int rc;
    rc = flame_stub_open_volume("vg1", "vol1", &meta);
    BufferInfo_t write_buf_info, read_buf_info;
    void* write_buffer = NULL, *read_buffer = NULL;
    rc = get_buffer_addr(&write_buf_info, &write_buffer);
    rc = get_buffer_addr(&read_buf_info, &read_buffer);
    uint64_t GigaByte = 1 << 30;
    char *m = (char *)write_buf_info.addr;
    for(int i = 0; i < 8192 * 2; i++)
        *(m + i) = 'a' + i % 26;
    rc = flame_write(write_buffer, GigaByte - 8192, 8192 * 2, NULL, NULL);
    getchar();
    rc = flame_read(read_buffer, GigaByte - 8192, 8192 * 2, NULL, NULL);
    getchar();
    m = (char *)read_buf_info.addr;
    for(int i = 0; i < 8192 * 2; i++){
        putchar(m[i]);
    }
        
    printf("lwg\n");
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