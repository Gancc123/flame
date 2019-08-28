/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-06-14 21:24:50
 * @LastEditors: lwg
 * @LastEditTime: 2019-08-28 14:38:03
 */
#include "common/context.h"
#include "common/cmdline.h"
#include "common/convert.h"
#include "common/thread/thread.h"
#include "chunkstore/cs.h"

#include "csd/csd_context.h"
#include "csd/csd_admin.h"
#include "csd/config_csd.h"

#include "service/internal_client.h"
#include "include/retcode.h"

#include "cluster/clt_agent.h"
#include "cluster/clt_my/my_agent.h"

#include "csd/log_csd.h"

#include "libflame/libchunk/libchunk.h"
#include "libflame/libchunk/log_libchunk.h"
#include "include/cmd.h"
#include "include/csdc.h"
#include "libflame/libchunk/chunk_cmd_service.h"

#include "include/spdk_common.h"
#include "memzone/rdma_mz.h"

#include <memory>
#include <string>

#define LINE_LENGTH 128

using namespace std;
using namespace flame;
using namespace flame::cli;

namespace flame {

class CsdCli final : public Cmdline {
public:
    CsdCli() : Cmdline("csd", "Flame Chunk Store Daemon") {}

    // Argument
    Argument<string>    config_path {this, 'c', "config_file", "config file path", "/etc/flame/csd.conf"};
    Argument<string>    clt_name    {this, 'g', CFG_CSD_CLT_NAME, "cluster name", "flame"};
    Argument<string>    csd_name    {this, 'n', CFG_CSD_NAME, "CSD Name which used to registering.", "csd"};
    Argument<string>    admin_addr  {this, 'a', CFG_CSD_ADMIN_ADDR, "CSD admin listen port", "0.0.0.0:7777"};
    Argument<string>    io_addr     {this, 'i', CFG_CSD_IO_ADDR, "CSD IO listen port", "0.0.0.0:9999"};
    Argument<string>    mgr_addr    {this, 'm', CFG_CSD_MGR_ADDR, "Flame MGR Address", "0.0.0.0:6666"};
    Argument<string>    chunkstore  {this, CFG_CSD_CHUNKSTORE, "ChunkStore url", ""};
    Argument<uint64_t>  heart_beat  {this, CFG_CSD_HEART_BEAT_CYCLE, "heart beat cycle, unit: ms", 3000};
    Argument<string>    log_dir     {this, CFG_CSD_LOG_DIR, "log dir", "/var/log/flame"};
    Argument<string>    rpc_addr     {this, CFG_CSD_RPC_ADDR, "rpc addr", "/var/temp/spdk_csd.sock"};
    Argument<string>    log_level   {this, CFG_CSD_LOG_LEVEL, 
        "log level. {PRINT, TRACE, DEBUG, INFO, WARN, ERROR, WRONG, CRITICAL, DEAD}", "INFO"};

    Argument<string>    reactor_mask {this, 'r', CFG_CSD_REACTOR_MASK, "reactor mask", "0x3f"};
    Argument<string>    nvme_conf   {this, 'f', CFG_CSD_NVME_CONF, "Nvme config file path", "/etc/flame/nvme.conf"};

    // Switch
    Switch  console_log {this, CFG_CSD_CONSOLE_LOG, "print log in console"};
    Switch  force_format{this, "force_format", "force reformat the device"};

    HelpAction help {this};
}; // class CsdCli

struct app_opts {
    char csd_name[LINE_LENGTH];
    char reactor_mask[LINE_LENGTH];
    char nvme_conf[LINE_LENGTH];
    char print_level[LINE_LENGTH];
    char rpc_addr[LINE_LENGTH];
public:
    app_opts() {

    }

    app_opts(CsdCli *csd_cli) {
        strcpy(csd_name, csd_cli->csd_name.get().c_str());
        strcpy(nvme_conf, csd_cli->nvme_conf.get().c_str());
        strcpy(reactor_mask, csd_cli->reactor_mask.get().c_str());
        strcpy(print_level, csd_cli->log_level.get().c_str());
        strcpy(rpc_addr, csd_cli->rpc_addr.get().c_str());
    }

    void convert_to_spdk_app_opts(struct spdk_app_opts *opts) {
        opts->name = csd_name;
        opts->config_file = nvme_conf;
        opts->reactor_mask = reactor_mask;
        opts->rpc_addr = rpc_addr;
        opts->mem_size = 2048;

        if(strcmp(print_level, "TRACE") == 0 || strcmp(print_level, "DEBUG")) {
            opts->print_level = SPDK_LOG_DEBUG;
        } else if(strcmp(print_level, "INFO") == 0) {
            opts->print_level = SPDK_LOG_INFO;
        } else if(strcmp(print_level, "WARN") == 0) {
            opts->print_level = SPDK_LOG_WARN;
        } else if(strcmp(print_level, "ERROR") == 0) {
            opts->print_level = SPDK_LOG_ERROR;
        }
    }   
};

class CsdAdminThread;

/**
 * CSD
 */
class CSD final {
public:
    CSD(CsdContext* cct) : cct_(cct) {}
    ~CSD() { down(); }

    int init(CsdCli* csd_cli);
    int run();
    void down();
    void chunkstore_flush();

    friend class CsdAdminThread;

private:
    shared_ptr<CsdContext> cct_;
    unique_ptr<CsdAdminServer> server_;
    unique_ptr<InternalClientFoctory> internal_client_foctory_;

    int admin_server_stat_ {0};
    unique_ptr<CsdAdminThread> admin_thread_;
    unique_ptr<ClusterAgent> clt_agent_;

    /**
     * 配置项
     */
    string      cfg_clt_name_;
    string      cfg_csd_name_;
    node_addr_t cfg_mgr_addr_;
    node_addr_t cfg_admin_addr_;
    node_addr_t cfg_io_addr_;
    string      cfg_chunkstore_url_;
    uint64_t    cfg_heart_beat_cycle_ms_;
    string      cfg_log_dir_;
    string      cfg_rpc_addr_;
    string      cfg_log_level_;

    int read_config(CsdCli* csd_cli);

    bool init_log(bool with_console);
    bool init_mgr();
    bool init_chunkstore(bool force_format);
    bool init_server();

    bool csd_register();
    bool csd_run_server();
    bool csd_sign_up();

    int csd_wait();
}; // class CSD

class CsdAdminThread : public Thread {
public:
    CsdAdminThread(CSD* csd) : csd_(csd) {}

    virtual void entry() override {
        if (!csd_->server_) {
            csd_->admin_server_stat_ = -1;
            return;
        }
        csd_->cct_->log()->linfo("start service");
        csd_->admin_server_stat_ = csd_->server_->run();
    }

private:
    CSD* csd_;
};

int CSD::init(CsdCli* csd_cli) {
    // 读取配置信息
    int r;
    if ((r = read_config(csd_cli)) != 0) {
        cct_->log()->lerror("read config faild");
        return 1;
    }

    // 初始化log
    if (!init_log(csd_cli->console_log)) {
        cct_->log()->lerror("init log faild");
        return 2;
    }

    // 初始化MGR
    if (!init_mgr()) {
        cct_->log()->lerror("init mgr faild");
        return 3;
    }

    // 初始化ChunkStore
    if (!init_chunkstore(csd_cli->force_format)) {
        cct_->log()->lerror("init chunkstore faild");
        return 4;
    }

    // 初始化CsdServer
    if (!init_server()) {
        cct_->log()->lerror("init server faild");
        return 5;
    }

    return 0;
}

int CSD::run() {
    if (!server_) return -1;

    if (!csd_register()) {
        cct_->log()->lerror("csd register faild");
        return 1;
    }

    if (!csd_run_server()) {
        cct_->log()->lerror("csd run server faild");
        return 2;
    }

    if (!csd_sign_up()) {
        cct_->log()->lerror("csd sign up faild");
        return 3;
    }

    return csd_wait();
}

void CSD::down() {
    if (cct_->cs())
        cct_->cs()->dev_unmount();
}

int CSD::read_config(CsdCli* csd_cli) {
    FlameConfig* config = cct_->config();
    
    /**
     * cfg_clt_name_
     */
    if (csd_cli->clt_name.done() && !csd_cli->clt_name.get().empty()) {
        cfg_clt_name_ = csd_cli->clt_name;
    } else if (config->has_key(CFG_CSD_CLT_NAME)) {
        cfg_clt_name_ = config->get(CFG_CSD_CLT_NAME, "");
    } else {
        cct_->log()->lerror("config[ " CFG_CSD_CLT_NAME " ] not found");
        return 1;
    }

    /**
     * cfg_csd_name_
     */
    if (csd_cli->csd_name.done() && !csd_cli->csd_name.get().empty()) {
        cfg_csd_name_ = csd_cli->csd_name;
    } else if (config->has_key(CFG_CSD_NAME)) {
        cfg_csd_name_ = config->get(CFG_CSD_NAME, "");
    } else {
        cct_->log()->lerror("config[ " CFG_CSD_NAME " ] not found");
        return 2;
    }

    /**
     * cfg_mgr_addr_
     */
    string mgr_addr;
    if (csd_cli->mgr_addr.done() && !csd_cli->mgr_addr.get().empty()) {
        mgr_addr = csd_cli->mgr_addr;
    } else if (config->has_key(CFG_CSD_MGR_ADDR)) {
        mgr_addr = config->get(CFG_CSD_MGR_ADDR, "");
    } else {
        cct_->log()->lerror("config[ " CFG_CSD_MGR_ADDR " ] not found");
        return 3;
    }

    if (!string_parse(cfg_mgr_addr_, mgr_addr)) {
        cct_->log()->lerror("invalid config[ " CFG_CSD_MGR_ADDR " ]");
        return 3;
    }

    /**
     * cfg_admin_addr_
     */
    string admin_addr;
    if (csd_cli->admin_addr.done() && !csd_cli->admin_addr.get().empty()) {
        admin_addr = csd_cli->admin_addr;
    } else if (config->has_key(CFG_CSD_ADMIN_ADDR)) {
        admin_addr = config->get(CFG_CSD_ADMIN_ADDR, "");
    } else {
        cct_->log()->lerror("config[ " CFG_CSD_ADMIN_ADDR " ] not found");
        return 4;
    }

    if (!string_parse(cfg_admin_addr_, admin_addr)) {
        cct_->log()->lerror("invalid config[ " CFG_CSD_ADMIN_ADDR " ]");
        return 4;
    }

    cct_->admin_addr(cfg_admin_addr_);

    /**
     * cfg_io_addr_
     */
    string io_addr;
    if (csd_cli->io_addr.done() && !csd_cli->io_addr.get().empty()) {
        io_addr = csd_cli->io_addr;
    } else if (config->has_key(CFG_CSD_IO_ADDR)) {
        io_addr = config->get(CFG_CSD_IO_ADDR, "");
    } else {
        cct_->log()->lerror("config[ " CFG_CSD_IO_ADDR " ] not found");
        return 5;
    }

    if (!string_parse(cfg_io_addr_, io_addr)) {
        cct_->log()->lerror("invalid config[ " CFG_CSD_IO_ADDR " ]");
        return 5;
    }

    cct_->io_addr(cfg_io_addr_);

    /**
     * cfg_chunkstore_url_
     */
    if (csd_cli->chunkstore.done() && !csd_cli->chunkstore.get().empty()) {
        cfg_chunkstore_url_ = csd_cli->chunkstore;
    } else if (config->has_key(CFG_CSD_CHUNKSTORE)) {
        cfg_chunkstore_url_ = config->get(CFG_CSD_CHUNKSTORE, "");
    } else {
        cct_->log()->lerror("config[ " CFG_CSD_CHUNKSTORE " ] not found");
        return 6;
    }

    /**
     * cfg_heart_beat_cycle_ms_
     */
    if (csd_cli->heart_beat.done() && !csd_cli->heart_beat.get()) {
        cfg_heart_beat_cycle_ms_ = csd_cli->heart_beat;
    } else if (config->has_key(CFG_CSD_HEART_BEAT_CYCLE)) {
        if (!string_parse(cfg_heart_beat_cycle_ms_, config->get(CFG_CSD_HEART_BEAT_CYCLE, ""))) {
            cct_->log()->lerror("invalid config[ " CFG_CSD_HEART_BEAT_CYCLE " ]");
            return 7;
        }
    } else {
        cct_->log()->lerror("config[ " CFG_CSD_HEART_BEAT_CYCLE " ] not found");
        return 7;
    }

    /**
     * cfg_log_dir_
     */
    string log_dir;
    if (csd_cli->log_dir.done() && !csd_cli->log_dir.get().empty()) {
        cfg_log_dir_ = csd_cli->log_dir;
    } else if (config->has_key(CFG_CSD_LOG_DIR)) {
        cfg_log_dir_ = config->get(CFG_CSD_LOG_DIR, "");
    } else {
        cct_->log()->lerror("config[ " CFG_CSD_LOG_DIR " ] not found");
        return 8;
    }

    /**
     * cfg_rpc_addr_
     */
    string rpc_addr;
    if (csd_cli->rpc_addr.done() && !csd_cli->rpc_addr.get().empty()) {
        cfg_rpc_addr_ = csd_cli->rpc_addr;
    } else if (config->has_key(CFG_CSD_RPC_ADDR)) {
        cfg_rpc_addr_ = config->get(CFG_CSD_RPC_ADDR, "");
    } else {
        cct_->log()->lerror("config[ " CFG_CSD_RPC_ADDR " ] not found");
        return 9;
    }

    /**
     * cfg_log_level_
     */
    string log_level;
    if (csd_cli->log_level.done() && !csd_cli->log_level.get().empty()) {
        cfg_log_level_ = csd_cli->log_level;
    } else if (config->has_key(CFG_CSD_LOG_LEVEL)) {
        cfg_log_level_ = config->get(CFG_CSD_LOG_LEVEL, "");
    } else {
        cct_->log()->lerror("config[ " CFG_CSD_LOG_LEVEL " ] not found");
        return 9;
    }

    return 0;
}

bool CSD::init_log(bool with_console) {
    if (!cct_->fct()->init_log(cfg_log_dir_, cfg_log_level_, "csd")) {
        cct_->fct()->log()->lerror("init log faild");
        return false;
    }
    cct_->fct()->log()->set_with_console(with_console);
    return true;
}

bool CSD::init_mgr() {
    internal_client_foctory_.reset(new InternalClientFoctoryImpl(cct_->fct()));

    auto stub = internal_client_foctory_->make_internal_client(convert2string(cfg_mgr_addr_));
    if (stub.get() == nullptr) {
        cct_->log()->lerror("create mgr stub faild");
        return false;
    }

    cct_->mgr_stub(stub);

    return true;
}

bool CSD::init_chunkstore(bool force_format) {
    auto cs = create_chunkstore(cct_->fct(), cfg_chunkstore_url_);
    if (!cs) {
        cct_->log()->lerror("create metastore with url(%s) faild", cfg_chunkstore_url_.c_str());
        return false;
    }

    int r = cs->dev_check();
    switch (r) {
    case ChunkStore::DevStatus::NONE:
        cct_->log()->lerror("device not existed");
        return false;
    case ChunkStore::DevStatus::UNKNOWN:
        cct_->log()->lwarn("unknown device format");
        if (!force_format)
            return false;
        break;
    case ChunkStore::DevStatus::CLT_OUT:
        cct_->log()->lwarn("the divice belong to other cluster");
        if (!force_format)
            return false;
        break;
    case ChunkStore::DevStatus::CLT_IN:
        break;
    }

    if (force_format) {
        r = cs->dev_format();
        if (r != 0) {
            cct_->log()->lerror("format device faild");
            return false;
        }
    }

    if ((r = cs->dev_mount()) != 0) {
        cct_->log()->lerror("mount device faild (%d)", r);
        return false;
    }

    cct_->cs(cs);

    /*libchunk部分*/
    FlameContext* flame_context = FlameContext::get_context();
    CmdServerStubImpl* cmd_sever_stub = new CmdServerStubImpl(flame_context);
    CmdServiceMapper *cmd_service_mapper = CmdServiceMapper::get_cmd_service_mapper();
    cmd_service_mapper->register_service(CMD_CLS_IO_CHK, CMD_CHK_IO_READ, new ReadCmdService(cct_.get()));
    cmd_service_mapper->register_service(CMD_CLS_IO_CHK, CMD_CHK_IO_WRITE, new WriteCmdService(cct_.get()));

    flame_context->log()->ltrace("CmdSeverStub created!");

    return true;
}

bool CSD::init_server() {
    server_.reset(new CsdAdminServer(cct_.get(), convert2string(cfg_admin_addr_)));

    cct_->timer(shared_ptr<TimerWorker>(new TimerWorker()));
    cct_->timer()->run();
    return true;
}

bool CSD::csd_register() {
    cs_info_t info;

    int r;
    if ((r = cct_->cs()->get_info(info)) != RC_SUCCESS) {
        cct_->log()->lerror("get chunkstore info faild");
        return false;
    }

    if (info.id == 0) {
        // 尚未注册，后期还应该检查cluster信息是否正确
        reg_attr_t reg_attr;
        reg_attr.csd_name = cfg_csd_name_;
        reg_attr.size = info.size;
        reg_attr.io_addr = cfg_io_addr_;
        reg_attr.admin_addr = cfg_admin_addr_;
        reg_attr.stat = CSD_STAT_PAUSE; // 注册完后统一状态为PAUSE
        
        reg_res_t  reg_res;
        if ((r = cct_->mgr_stub()->register_csd(reg_res, reg_attr)) != RC_SUCCESS) {
            cct_->log()->lerror("(mgr_stub) register csd faild, code(%d)", r);
            return false;
        }

        info.id = reg_res.csd_id;
        if ((r = cct_->cs()->set_info(info)) != RC_SUCCESS) {
            cct_->log()->lerror("chunkstore set info faild");
            return false;
        }
    } else {
        cct_->log()->linfo("csd (%llu) have been registered", info.id);
    }

    cct_->csd_id(info.id);
    cct_->clt_name(info.cluster_name);
    cct_->csd_name(info.name);
    if (!cct_->csd_stat(CSD_STAT_NONE, CSD_STAT_PAUSE)) {
        cct_->log()->lerror("csd stat error: none to pause");
        return false;
    }

    cct_->log()->ldebug("csd register success: clt_name(%s), csd_id(%llu), csd_name(%s), size(%llu)", 
        info.cluster_name.c_str(), info.id, info.name.c_str(), info.size);

    return true;
}

bool CSD::csd_run_server() {
    admin_thread_.reset(new CsdAdminThread(this));
    admin_thread_->create("csd_admin");
    return true;
}

bool CSD::csd_sign_up() {
    if (!cct_->csd_stat(CSD_STAT_PAUSE, CSD_STAT_ACTIVE)) {
        cct_->log()->lerror("csd stat error: pause to active");
        return false;
    }
    
    cct_->log()->linfo("init cluster agent");
    clt_agent_.reset(new MyClusterAgent(cct_.get(), utime_t::get_by_msec(cfg_heart_beat_cycle_ms_)));
    clt_agent_->init();
    return true;
}

int CSD::csd_wait() {
    admin_thread_->join();
    cct_->log()->lerror("join admin_thread with: %d", admin_server_stat_);
    return 0;
}

void CSD::chunkstore_flush(){
    cct_->cs()->flush();
}

} // namespace flame

static CSD *g_csd;

void pre_exit_csd(int signum){
    g_csd->chunkstore_flush();
    exit(signum); 
}

static void csd_start(void *arg1, void *arg2) {
    CsdContext *cct = static_cast<CsdContext *>(arg1);
    CsdCli *csd_cli = static_cast<CsdCli *>(arg2);

    std::cout << "csd_start...." << std::endl;

    CSD *csd = new CSD(cct);
    assert(csd);

    g_csd = csd;

    if(csd->init(csd_cli) != 0) {
        exit(-1);
    }

    delete csd_cli;

    csd->run();

    return ;
}

int main(int argc, char *argv[]) {
    // 解析Csd的命令行参数，获取配置参数
    CsdCli *csd_cli = new CsdCli();
    int r = csd_cli->parser(argc, argv);
    if(r != CmdRetCode::SUCCESS) {
        csd_cli->print_error();
        return r;
    } else if(csd_cli->help.done()) {
        csd_cli->print_help();
        return 0;
    }

    signal(SIGINT, pre_exit_csd);

    //获取全局上下文
    FlameContext *fct = FlameContext::get_context();
    if(!fct->init_config(csd_cli->config_path)) {
        //clog("init config failed.");
        fct->log()->lerror("init config failed.");
        return -1;
    }

    CsdContext *cct = new CsdContext(fct);

    // 初始化spdk应用程序启动的配置参数
    struct spdk_app_opts opts = {};
    struct app_opts temp_opt(csd_cli);

    int rc = 0;
    spdk_app_opts_init(&opts);

    temp_opt.convert_to_spdk_app_opts(&opts);

    rc = spdk_app_start(&opts, csd_start, cct, csd_cli);
    if(rc) {
        SPDK_NOTICELOG("spdk app start: ERROR!\n");
    } else {
        SPDK_NOTICELOG("SUCCESS.\n");
    }

    spdk_app_fini();

    return 0;
}