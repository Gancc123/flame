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

#include "common/context.h"
#include "util/spdk_common.h"
#include "memzone/rdma_mz.h"

#include <memory>
#include <string>

#define LINE_LENGTH 128

using namespace std;
using namespace flame;
using namespace flame::cli;

namespace flame {

class LibchunkCli final : public Cmdline {
public:
    LibchunkCli() : Cmdline("csd", "Flame Chunk Store Daemon") {}

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
    Argument<string>    log_level   {this, CFG_CSD_LOG_LEVEL, 
        "log level. {PRINT, TRACE, DEBUG, INFO, WARN, ERROR, WRONG, CRITICAL, DEAD}", "INFO"};

    Argument<string>    reactor_mask {this, 'r', CFG_CSD_REACTOR_MASK, "reactor mask", "0x3f"};
    Argument<string>    nvme_conf   {this, 'f', CFG_CSD_NVME_CONF, "Nvme config file path", "/etc/flame/nvme.conf"};

    // Switch
    Switch  console_log {this, CFG_CSD_CONSOLE_LOG, "print log in console"};
    Switch  force_format{this, "force_format", "force reformat the device"};

    HelpAction help {this};
}; // class LibchunkCli

struct app_opts {
    char csd_name[LINE_LENGTH];
    char reactor_mask[LINE_LENGTH];
    char nvme_conf[LINE_LENGTH];
    char print_level[LINE_LENGTH];
public:
    app_opts() {

    }

    app_opts(LibchunkCli *csd_cli) {
        strcpy(csd_name, csd_cli->csd_name.get().c_str());
        strcpy(nvme_conf, csd_cli->nvme_conf.get().c_str());
        strcpy(reactor_mask, csd_cli->reactor_mask.get().c_str());
        strcpy(print_level, csd_cli->log_level.get().c_str());
    }

    void convert_to_spdk_app_opts(struct spdk_app_opts *opts) {
        opts->name = csd_name;
        opts->config_file = nvme_conf;
        opts->reactor_mask = reactor_mask;

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

/**
 * Libchunk_S
 */
class Libchunk_S final {
public:
    Libchunk_S(CsdContext* cct) : cct_(cct) {}
    ~Libchunk_S() { down(); }

    int init(LibchunkCli* csd_cli);
    int run();
    void down();
    void chunkstore_flush();

private:
    shared_ptr<CsdContext> cct_;
    unique_ptr<CsdAdminServer> server_;
    unique_ptr<InternalClientFoctory> internal_client_foctory_;

    int admin_server_stat_ {0};


    /**
     * 配置项
     */
    string      cfg_csd_name_;
    node_addr_t cfg_admin_addr_;
    node_addr_t cfg_io_addr_;
    string      cfg_chunkstore_url_;
    string      cfg_log_dir_;
    string      cfg_log_level_;

    int read_config(LibchunkCli* csd_cli);
    bool init_log(bool with_console);
    bool init_chunkstore(bool force_format);


}; // class Libchunk_S


int Libchunk_S::init(LibchunkCli* csd_cli) {
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

    // 初始化ChunkStore
    if (!init_chunkstore(csd_cli->force_format)) {
        cct_->log()->lerror("init chunkstore faild");
        return 3;
    }

    return 0;
}

int Libchunk_S::run() {
    getchar();
    return 1;
}

void Libchunk_S::down() {
    if (cct_->cs())
        cct_->cs()->dev_unmount();
}

int Libchunk_S::read_config(LibchunkCli* csd_cli) {
    FlameConfig* config = cct_->config();

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

bool Libchunk_S::init_log(bool with_console) {
    if (!cct_->fct()->init_log(cfg_log_dir_, cfg_log_level_, "csd")) {
        cct_->fct()->log()->lerror("init log faild");
        return false;
    }
    cct_->fct()->log()->set_with_console(with_console);
    return true;
}


bool Libchunk_S::init_chunkstore(bool force_format) {
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
    chunk_create_opts_t opts;
    opts.set_prealloc(true);
   
    cs->chunk_create(123, opts);

    flame_context->log()->ltrace("CmdSeverStub created!");

    return true;
}

void Libchunk_S::chunkstore_flush(){
    cct_->cs()->flush();
}

} // namespace flame

static Libchunk_S *g_csd;

void pre_exit_csd(int signum){
    g_csd->chunkstore_flush();
    exit(signum); 
}

static void csd_start(void *arg1, void *arg2) {
    CsdContext *cct = static_cast<CsdContext *>(arg1);
    LibchunkCli *csd_cli = static_cast<LibchunkCli *>(arg2);

    std::cout << "csd_start...." << std::endl;

    Libchunk_S *csd = new Libchunk_S(cct);
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
    LibchunkCli *csd_cli = new LibchunkCli();
    int r = csd_cli->parser(argc, argv);
    if(r != CmdRetCode::SUCCESS) {
        csd_cli->print_error();
        return r;
    } else if(csd_cli->help.done()) {
        csd_cli->print_help();
        return 0;
    }

    // signal(SIGINT, pre_exit_csd);

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