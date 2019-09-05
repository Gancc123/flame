/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-05 10:29:09
 */
#include "common/cmdline.h"

#include <grpcpp/grpcpp.h>

#include "include/flame.h"
#include "proto/flame.grpc.pb.h"
#include "service/flame_client.h"
#include "util/utime.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <memory>
#include <list>

using namespace std;
using namespace flame;
using namespace flame::cli;

#define DEFAULT_MGR_ADDR "192.168.3.112:6677"

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

    Argument<int>   chk_sz   {this, "chunk_size", "the size of chunk in this volume, unit(GB)", 1};
    Argument<string> spolicy {this, "store_policy", "the store policy of this volume", ""};
    Argument<string> mgr_addr {this, 'm', "mgr_addr", "the ip and port of manager: 192.168.3.112:6677", DEFAULT_MGR_ADDR};

    Switch  prealloc    {this, "preallocate", "pre allocating the physical space for volume"};

    HelpAction help {this};

    int def_run() {
        auto cct = make_flame_client_context(mgr_addr);
        vol_attr_t attr;
        attr.size = (uint64_t)size.get() << 30;
        attr.chk_sz = chk_sz.get() << 30;
        attr.spolicy = 0;
        
        int r = cct->client()->create_volume(vg_name.get(), vol_name.get(), attr);
        check_faild__(r, "create volume");

        return success__();
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