/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-05 10:29:09
 */
#include "common/cmdline.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <memory>
#include <list>

#include <grpcpp/grpcpp.h>

#include "include/flame.h"
#include "proto/flame.grpc.pb.h"
#include "service/flame_client.h"
#include "util/utime.h"

#include "spdk/json.h"

using namespace std;
using namespace flame;
using namespace flame::cli;

#define DEFAULT_MGR_ADDR "192.168.3.112:6677"

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

static int success__() {
    printf("Success!\n");
    return 0;
}

static void check_faild__(int r, const std::string& msg) {
    if (r != 0) {
        printf("faild(%d): %s\n", r, msg.c_str());
        exit(-1);
    }
}

namespace flame {

static unique_ptr<FlameClientContext> make_flame_client_context(string mgr_addr) {
    FlameContext* fct = FlameContext::get_context();

    FlameClient* client = new FlameClientImpl(fct, grpc::CreateChannel(
        mgr_addr, grpc::InsecureChannelCredentials()
    ));
    
    if (!client) {
        printf("connect flame cluster faild.\n");
        exit(-1);
    }

    FlameClientContext* fcct = new FlameClientContext(fct, client);

    if (!fcct) {
        printf("create flame client context faild.\n");
        exit(-1);
    }

    return unique_ptr<FlameClientContext>(fcct);
}

class VGShowCli : public Cmdline {
public:
    VGShowCli(Cmdline* parent) 
    : Cmdline(parent, "show", "List all of the volume group") {}

    Argument<int> offset {this, 's', "offset", "offset", 0};
    Argument<int> number {this, 'n', "number", "the number of vg that need to be showed", 0};
    Argument<string> mgr_addr {this, 'm', "mgr_addr", "the ip and port of manager: 192.168.3.112:6677", DEFAULT_MGR_ADDR};

    HelpAction help {this};

    int def_run() {
        auto cct = make_flame_client_context(mgr_addr);
        list<volume_group_meta_t> res;
        int r = cct->client()->get_vol_group_list(res, offset.get(), number.get());
        check_faild__(r, "show volume group.\n");
        
        printf("Size: %d\n", res.size());
        printf("vg_id\tname\tvols\tsize\tctime\n");
        for (auto it = res.begin(); it != res.end(); it++) {
            printf("%llu\t%s\t%u\t%llu\t%s\n",
                it->vg_id,
                it->name.c_str(),
                it->volumes,
                it->size,
                utime_t::get_by_usec(it->ctime).to_str().c_str()
            );
        }
        return 0;
    }
}; // class VGShowCli

class VGCreateCli : public Cmdline {
public:
    VGCreateCli(Cmdline* parent) 
    : Cmdline(parent, "create", "create an volume group") {}

    Serial<string> name {this, 1, "name", "the name of volume group"};
    Argument<string> mgr_addr {this, 'm', "mgr_addr", "the ip and port of manager: 192.168.3.112:6677", DEFAULT_MGR_ADDR};

    HelpAction help {this};

    int def_run() {
        auto cct = make_flame_client_context(mgr_addr);
        
        int r = cct->client()->create_vol_group(name.get());
        check_faild__(r, "create volume group");
        
        return success__();
    }
}; // class VGCreateCli

class VGRemoveCli : public Cmdline {
public:
    VGRemoveCli(Cmdline* parent) 
    : Cmdline(parent, "remove", "remove an volume group") {}

    Serial<string> name {this, 1, "name", "the name of volume group"};
    Argument<string> mgr_addr {this, 'm', "mgr_addr", "the ip and port of manager: 192.168.3.112:6677", DEFAULT_MGR_ADDR};

    HelpAction help {this};

    int def_run() {
        auto cct = make_flame_client_context(mgr_addr);

        int r = cct->client()->remove_vol_group(name.get());
        check_faild__(r, "remove volume group");
        
        return success__();
    }
}; // class VGRemoveCli

class VGRenameCli : public Cmdline {
public:
    VGRenameCli(Cmdline* parent)
    : Cmdline(parent, "rename", "rename an volume group") {}

    Serial<string> old_name {this, 1, "old_name", "the old name of vg"};
    Serial<string> new_name {this, 2, "new_name", "the new name of vg"};
    Argument<string> mgr_addr {this, 'm', "mgr_addr", "the ip and port of manager: 192.168.3.112:6677", DEFAULT_MGR_ADDR};

    HelpAction help {this};

    int def_run() {
        auto cct = make_flame_client_context(mgr_addr);

        int r = cct->client()->rename_vol_group(old_name.get(), new_name.get());
        check_faild__(r, "rename volume group");
        
        return success__();
    }
}; // class VGRenameCli

class VolGroupCli : public Cmdline {
public:
    VolGroupCli(Cmdline* parent) 
    : Cmdline(parent, "vg", "Volume Group") {}

    VGShowCli   show    {this};
    VGCreateCli create  {this};
    VGRemoveCli remove  {this};
    VGRenameCli rename  {this};

    HelpAction help {this};
}; // class VolGroupCli

class VolShowCli : public Cmdline {
public:
    VolShowCli(Cmdline* parent)
    : Cmdline(parent, "show", "show volumes") {}

    Serial<string> vg_name {this, 1, "vg_name", "the name of volume group"};

    Argument<int> offset {this, 's', "offset", "offset", 0};
    Argument<int> number {this, 'n', "number", "the number of vg that need to be showed", 0};
    Argument<string> mgr_addr {this, 'm', "mgr_addr", "the ip and port of manager: 192.168.3.112:6677", DEFAULT_MGR_ADDR};

    HelpAction help {this};

    int def_run() {
        auto cct = make_flame_client_context(mgr_addr);
        list<volume_meta_t> res;
        int r = cct->client()->get_volume_list(res, vg_name.get(), offset.get(), number.get());
        check_faild__(r, "get volume list");

        printf("Size: %d\n", res.size());
        printf("vol_id\tvg_id\tname\tsize\tctime\n");
        for (auto it = res.begin(); it != res.end(); it++) {
            printf("%llu\t%llu\t%s\t%llu\t%s\n",
                it->vol_id,
                it->vg_id,
                it->name.c_str(),
                it->size,
                utime_t::get_by_usec(it->ctime).to_str().c_str()
            );
        }
        return 0;
    }

}; // class VolShowCli

class VolCreateCli : public Cmdline {
public:
    VolCreateCli(Cmdline* parent)
    : Cmdline(parent, "create", "create an volume") {}

    Serial<string>  vg_name  {this, 1, "vg_name", "the name of volume group"};
    Serial<string>  vol_name {this, 2, "vol_name", "the name of volume"};
    Serial<int>     size     {this, 3, "size", "the size of volume, unit(GB)"};
    Serial<string>  target_ip     {this, 4, "target_ip", "the ip of the target, example 192.168.3.110"};
    Serial<int>     target_rpc_port     {this, 5, "target_rpc_port", "the port of the SPDK rpc"};

    Argument<int>   chk_sz   {this, "chunk_size", "the size of chunk in this volume, unit(GB)", 1};
    Argument<string> spolicy {this, "store_policy", "the store policy of this volume", ""};
    Argument<string> mgr_addr {this, 'm', "mgr_addr", "the ip and port of manager: 192.168.3.112:6677", DEFAULT_MGR_ADDR};

    Switch  prealloc    {this, "preallocate", "pre allocating the physical space for volume"};

    HelpAction help {this};

    int def_run() {
        auto cct = make_flame_client_context(mgr_addr.get());
        vol_attr_t attr;
        attr.size = (uint64_t)size.get() << 30;
        attr.chk_sz = chk_sz.get() << 30;
        attr.spolicy = 0;
        
        int r = cct->client()->create_volume(vg_name.get(), vol_name.get(), attr);
        check_faild__(r, "create volume");
        _buildFBD();
        _buildTarget();
        return success__();
    }
private:
    int _buildFBD(){
        memset(g_buf, 0, sizeof(g_buf));
        g_write_pos = g_buf;

        char *create_fbd_method = "construct_flame_bdev";
        struct spdk_json_write_ctx *w;
        w = spdk_json_write_begin(write_cb, NULL, 0);

        spdk_json_write_object_begin(w);
        spdk_json_write_named_string(w, "jsonrpc", "2.0");
        spdk_json_write_named_uint32(w, "id", 1);
        spdk_json_write_named_string(w, "method", create_fbd_method);
        spdk_json_write_name(w, "params");

        spdk_json_write_object_begin(w);
        spdk_json_write_named_string(w, "vg_name", vg_name.get().c_str());
        spdk_json_write_named_string(w, "vol_name", vol_name.get().c_str());
        spdk_json_write_named_uint64(w, "size", size.get());
        spdk_json_write_named_string(w, "mgr_addr", mgr_addr.get().c_str());
        spdk_json_write_object_end(w);

        spdk_json_write_object_end(w);

        spdk_json_write_end(w);

        int sockfd;
        if((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
            ERR_EXIT("socket");
        }
        struct sockaddr_in serveraddr;
        memset(&serveraddr, 0, sizeof(serveraddr));
        serveraddr.sin_family = AF_INET;
        serveraddr.sin_port = htons(target_rpc_port);
        serveraddr.sin_addr.s_addr = inet_addr(target_ip.get().c_str());

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

    int _buildTarget(){
        memset(g_buf, 0, sizeof(g_buf));
        g_write_pos = g_buf;

        char *create_target_method = "construct_nvmf_subsystem";
        string nqn = "nqn.2016-06.io.spdk:" + vg_name.get() + "_" +  vol_name.get();
        struct spdk_json_write_ctx *w;
        w = spdk_json_write_begin(write_cb, NULL, 0);

        spdk_json_write_object_begin(w);
            spdk_json_write_named_string(w, "jsonrpc", "2.0");
            spdk_json_write_named_uint32(w, "id", 1);
            spdk_json_write_named_string(w, "method", create_target_method);
            spdk_json_write_name(w, "params");

            spdk_json_write_object_begin(w);
                spdk_json_write_named_string(w, "nqn", nqn.c_str());
                spdk_json_write_name(w, "listen_addresses");
                    spdk_json_write_array_begin(w);
                        spdk_json_write_object_begin(w);
                            spdk_json_write_named_string(w, "trtype", "RDMA");
                            spdk_json_write_named_string(w, "transport", "RDMA");
                            spdk_json_write_named_string(w, "traddr", target_ip.get().c_str());
                            spdk_json_write_named_string(w, "trsvcid", "4420");  //使用统一的4420不知道是否有问题
                        spdk_json_write_object_end(w);
                    spdk_json_write_array_end(w);
                // spdk_json_write_name(w, "hosts");
                // spdk_json_write_array_begin(w);
                // spdk_json_write_string(w, "nqn.2016-06.io.spdk:init");
                // spdk_json_write_array_end(w);
                spdk_json_write_named_bool(w, "allow_any_host", true);
                spdk_json_write_named_string(w, "serial_number", "SPDK00000000000001");
                spdk_json_write_name(w, "namespaces");  
                    spdk_json_write_array_begin(w);
                        spdk_json_write_object_begin(w);
                            spdk_json_write_named_string(w, "bdev_name", (vg_name.get() + "_" +  vol_name.get()).c_str());
                        spdk_json_write_object_end(w);
                    spdk_json_write_array_end(w);
            spdk_json_write_object_end(w);

        spdk_json_write_object_end(w);

        spdk_json_write_end(w);

        int sockfd;
        if((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
            ERR_EXIT("socket");
        }
        struct sockaddr_in serveraddr;
        memset(&serveraddr, 0, sizeof(serveraddr));
        serveraddr.sin_family = AF_INET;
        serveraddr.sin_port = htons(target_rpc_port);
        serveraddr.sin_addr.s_addr = inet_addr(target_ip.get().c_str());

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
}; // class VolCreateCli

class VolRemoveCli : public Cmdline {
public:
    VolRemoveCli(Cmdline* parent)
    : Cmdline(parent, "remove", "remove an volume") {}

    Serial<string>  vg_name  {this, 1, "vg_name", "the name of volume group"};
    Serial<string>  vol_name {this, 2, "vol_name", "the name of volume"};
    Argument<string> mgr_addr {this, 'm', "mgr_addr", "the ip and port of manager: 192.168.3.112:6677", DEFAULT_MGR_ADDR};

    HelpAction help {this};

    int def_run() {
        auto cct = make_flame_client_context(mgr_addr);
        int r = cct->client()->remove_volume(vg_name.get(), vol_name.get());
        check_faild__(r, "remove volume");

        return success__();
    }
}; // class VolRemoveCli

class VolRenameCli : public Cmdline {
public:
    VolRenameCli(Cmdline* parent)
    : Cmdline(parent, "rename", "rename an volume") {}

    Serial<string>  vg_name  {this, 1, "vg_name", "the name of volume group"};
    Serial<string>  vol_name {this, 2, "vol_name", "the name of volume"};
    Serial<string>  new_name {this, 3, "new_name", "the new name of volume"};
    
    Argument<string> mgr_addr {this, 'm', "mgr_addr", "the ip and port of manager: 192.168.3.112:6677", DEFAULT_MGR_ADDR};

    HelpAction help {this};

    int def_run() {
        auto cct = make_flame_client_context(mgr_addr);
        int r = cct->client()->rename_volume(vg_name.get(), vol_name.get(), new_name.get());
        check_faild__(r, "rename volume");

        return success__();
    }
}; // class VolRemoveCli

class VolInfoCli : public Cmdline {
public:
    VolInfoCli(Cmdline* parent)
    : Cmdline(parent, "info", "show the information of volume") {}

    Serial<string>  vg_name  {this, 1, "vg_name", "the name of volume group"};
    Serial<string>  vol_name {this, 2, "vol_name", "the name of volume"};

    Argument<string> mgr_addr {this, 'm', "mgr_addr", "the ip and port of manager: 192.168.3.112:6677", DEFAULT_MGR_ADDR};

    HelpAction help {this};

    int def_run() {
        auto cct = make_flame_client_context(mgr_addr);
        uint32_t retcode;
        volume_meta_t res;
        int r = cct->client()->get_volume_info(res, vg_name.get(), vol_name.get(), retcode);
        check_faild__(r, "get volume info");

        printf("retcode: %d\n", retcode);
        printf("vol_id\tvg_id\tname\tsize\tctime\n");
    
        printf("%llu\t%llu\t%s\t%llu\t%s\n",
            res.vol_id,
            res.vg_id,
            res.name.c_str(),
            res.size,
            utime_t::get_by_usec(res.ctime).to_str().c_str()
        );
        return 0;
    }
}; // class VolInfoCli

class VolResizeCli : public Cmdline {
public:
    VolResizeCli(Cmdline* parent)
    : Cmdline(parent, "resize", "change the size of volume") {}

    Serial<string>  vg_name  {this, 1, "vg_name", "the name of volume group"};
    Serial<string>  vol_name {this, 2, "vol_name", "the name of volume"};
    Serial<int>     size     {this, 3, "size", "the new size of volume, unit(GB)"};

    Argument<string> mgr_addr {this, 'm', "mgr_addr", "the ip and port of manager: 192.168.3.112:6677", DEFAULT_MGR_ADDR};

    HelpAction help {this};

    int def_run() {
        auto cct = make_flame_client_context(mgr_addr);
        int r = cct->client()->resize_volume(vg_name.get(), vol_name.get(), size.get());
        check_faild__(r, "resize volume");

        return success__();
    }
}; // class VolResizeCli

class VolumeCli : public Cmdline {
public:
    VolumeCli(Cmdline* parent)
    : Cmdline(parent, "volume", "Volume") {}

    VolShowCli      show    {this};
    VolCreateCli    create  {this};
    VolRemoveCli    remove  {this};
    VolRenameCli    rename  {this};
    VolInfoCli      info    {this};
    VolResizeCli    resize  {this};

    HelpAction help {this};
}; // class VolumeCli

class CltInfoCli : public Cmdline {
public:
    CltInfoCli(Cmdline* parent)
    : Cmdline(parent, "info", "show the information of cluster") {}

    Argument<string> mgr_addr {this, 'm', "mgr_addr", "the ip and port of manager: 192.168.3.112:6677", DEFAULT_MGR_ADDR};

    HelpAction help {this};

    int def_run() {
        auto cct = make_flame_client_context(mgr_addr);
        cluster_meta_t res;
        int r = cct->client()->get_cluster_info(res);
        check_faild__(r, "resize volume");

        printf("name\tmgrs\tcsds\tsize\talloced\tused\n");
        printf("%s\t%d\t%d\t%llu\t%llu\t%llu\n",
              res.name.c_str(),
              res.mgrs,
              res.csds,
              res.size,
              res.alloced,
              res.used);
        
        return 0;
    }
};

class ClusterCli : public Cmdline {
public:
    ClusterCli(Cmdline* parent)
    : Cmdline(parent, "cluster", "Flame Cluster") {}

    CltInfoCli info {this};

    HelpAction help {this};
}; // class ClusterCli

class FlameCli : public Cmdline {
public:
    FlameCli() : Cmdline("flame", "Flame Command Line Interface") {}

    ClusterCli  cluster {this};
    VolGroupCli vg      {this};
    VolumeCli   volume  {this};

    HelpAction  help    {this};

}; // class FlameCli

} // namespace flame

int main(int argc, char** argv) {
    FlameCli flame_cli;
    return flame_cli.run(argc, argv);
}