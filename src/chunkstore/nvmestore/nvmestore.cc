#include <memory>
#include <sstream>
#include <fstream>
#include <cctype>
#include <exception>
#include <vector>
#include <regex>

#include "chunkstore/nvmestore/nvmestore.h"
#include "util/utime.h"
#include "chunkstore/log_cs.h"

using namespace flame;

int NvmeStore::get_info(cs_info_t& info) const {
    info.id             = this->store_id;
    info.cluster_name   = this->cluster_name;
    info.name           = this->name;
    info.size           = this->data_cluster_count * this->cluster_size;
    info.used           = this->used_size;
    info.ftime          = this->format_time;
    info.chk_num        = this->total_chunks;
    
    return 0;
}

int NvmeStore::set_info(const cs_info_t& info) {
    this->store_id      = info.id;
    this->cluster_name  = info.cluster_name;
    this->name          = info.name;
    this->used_size     = used_size;
    this->format_time   = info.ftime;
    this->total_chunks  = info.chk_num;
    this->data_cluster_count = info.size / this->cluster_size;

    return 0;
}

std::string NvmeStore::get_device_name() const {
    return dev_name;
}

std::string NvmeStore::get_driver_name() const {
    return "NvmeStore";
}

std::string NvmeStore::get_config_info() const {
    std::ostringstream oss;
    oss << get_driver_name() << "://" << get_device_name() << ":" << ((data_cluster_count * cluster_size) >> 30) << "G";
    return oss.str();
}

std::string NvmeStore::get_runtime_info() const {
    return "NvmeStore: Runtime Information";
}

int NvmeStore::get_io_mode() const {
    return ChunkStore::IOMode::ASYNC;
}

struct spdk_bdev *NvmeStore::get_bdev() {
    return bdev;
}

struct spdk_bs_dev *NvmeStore::get_bs_dev() {
    return bs_dev;
}

struct spdk_blob_store *NvmeStore::get_blobstore() {
    return blobstore;
}

FlameContext *NvmeStore::get_fct() {
    return fct_;
}

NvmeChunkMap *NvmeStore::get_chunk_map() {
    return chunk_map;
}

ChunkBlobMap *NvmeStore::get_chunk_blob_map() {
    return chunk_blob_map;
}

/*
struct spdk_io_channel *NvmeStore::get_io_channel(size_t index) {
    if(index >= channles.size()) {
        fct->log()->warn("index beyond array bound. return last channel.");
        return channels[channles.size() - 1];
    }

    return channles[index];
}
*/

IOChannels *NvmeStore::get_read_channels() {
    return read_channels;
}

IOChannels *NvmeStore::get_write_channels() {
    return write_channels;
}

struct spdk_io_channel *NvmeStore::get_meta_channel() {
    return meta_channel;
}

uint64_t NvmeStore::get_page_size() {
    return page_size;
}


uint64_t NvmeStore::get_cluster_size() {
    return cluster_size;
}

uint64_t NvmeStore::get_total_chunks() {
    return total_chunks;
}

uint32_t NvmeStore::get_io_core(IOChannelType type, size_t index) {
    switch(type) {
        case READ_CHANNEL:
            return config_file.get_read_core(index);
        case WRITE_CHANNEL:
            return config_file.get_write_core(index);
        default:
            break;
    }
    
    return UINT32_MAX;
}

struct spdk_io_channel *NvmeStore::get_io_channel(IOChannelType type, size_t index) {
    switch(type) {
        case READ_CHANNEL:
            return read_channels->get_io_channel_by_index(index);
        case WRITE_CHANNEL:
            return write_channels->get_io_channel_by_index(index);
        default:
            break;
    }

    return nullptr;
}

uint32_t NvmeStore::get_admin_core() {
    return config_file.admin_core;
}

uint32_t NvmeStore::get_meta_core() {
    return config_file.meta_core;
}

uint32_t NvmeStore::get_core_count() const {
    return config_file.get_core_count();
}

uint32_t NvmeStore::get_core_count(IOChannelType type) {
    switch(type) {
        case READ_CHANNEL:
            return config_file.read_cores.size();
            //return read_channels->channels.size();
        case WRITE_CHANNEL:
            return config_file.write_cores.size();
            //return write_channels->channels.size();
        default:
             break;
    }

    return 0;
}

bool NvmeStore::chunk_exist(uint64_t chk_id) {
    return chunk_blob_map->chunk_exist(chk_id);
}

bool NvmeStore::is_mounted() {
    return state == NVMESTORE_STATE_MOUNTED;
}

bool NvmeStore::is_dirty() {
    return dirty;
}

bool NvmeStore::is_support_mem_persist() const {
    return false;
}

/*
 * create_nvmestore: 该函数解析url，并获取初始化一个nvmestore的参数,并创建NvmeStore的指针
*/
NvmeStore* NvmeStore::create_nvmestore(FlameContext* fct, const std::string& url) {
    std::string pstr = "^(\\w+)://(\\w+):(.+)$";
    std::regex pattern(pstr);
    std::smatch result;

    if(!regex_match(url, result, pattern)) {
        fct->log()->lerror("format error for url: %s", url.c_str());
        return nullptr;
    }

    std::cout << "driver: " << result[1] << std::endl;
    std::cout << "device_name: " << result[2] << std::endl;
    std::cout << "info_file_name: " << result[3] << std::endl;

    std::string driver = result[1];
    if(driver != "NvmeStore" && driver != "nvmestore") {
        fct->log()->lerror("unknown driver: %s", driver.c_str());
        return nullptr;
    }
    std::string device_name = result[2];
    std::string info_file_path = result[3];

    return new NvmeStore(fct, device_name, info_file_path);
}

NvmeStore *NvmeStore::create_nvmestore(FlameContext *fct, 
                                const std::string& device_name, 
                                const std::string& config) {

    return new NvmeStore(fct, device_name, config);
}

/*
 * dev_check: dev_check函数用于自检设备
*/
int NvmeStore::dev_check() {
    //首先判断设备名称是否有效，是否是一个有效的块设备
    if(!(bdev = spdk_bdev_get_by_name(dev_name.c_str()))) {
        fct_->log()->lerror("could not find a bdev by dev_name: %s", dev_name.c_str());
        return ChunkStore::DevStatus::NONE;
    }

    //其次判断配置文件是否有效
    if(config_file.is_valid()) {
        if(config_file.load() != 0) {
            fct_->log()->lerror("load config file failed.");
            return ChunkStore::DevStatus::NONE;
        }

        if(!config_file.is_valid_mask()) {
            fct_->log()->lerror("invalid core mask.");
            return ChunkStore::DevStatus::NONE;
        }

        config_file.print_conf();       

    } else {
        fct_->log()->lerror("invalid config file path: %s", config_file.file_path.c_str());
        return ChunkStore::DevStatus::NONE;
    }

    return ChunkStore::DevStatus::CLT_IN;
}

void NvmeStore::bs_init_cb(void *cb_arg, struct spdk_blob_store *bs, int bserrno) {
    struct format_arg *farg = (struct format_arg *)cb_arg;
    NvmeStore *nvmestore = farg->nvmestore;

    if(bserrno) {
        nvmestore->fct_->log()->lerror("init blobstore failed: %d", bserrno);
        nvmestore->state = NVMESTORE_STATE_DOWN;
        *(farg->ret) = NVMESTORE_FORMAT_ERR;
        nvmestore->signal_format_completed();
        return ;
    }

    nvmestore->blobstore    = bs;
    nvmestore->page_size    = spdk_bs_get_page_size(bs);
    nvmestore->cluster_size = spdk_bs_get_cluster_size(bs);

    nvmestore->meta_channel = spdk_bs_alloc_io_channel(bs);
    if(nvmestore->meta_channel == nullptr) {
        nvmestore->fct_->log()->lerror("alloc meta_channel failed: %d", -ENOMEM);
        nvmestore->state = NVMESTORE_STATE_DOWN;
        *(farg->ret) = NVMESTORE_FORMAT_ERR;
        nvmestore->signal_format_completed();

        return ;
    }

    nvmestore->signal_format_completed();
}

void NvmeStore::format_start(void *arg1, void *arg2) {
    struct format_arg *farg = (struct format_arg *)arg1;
    NvmeStore *nvmestore = farg->nvmestore;

    nvmestore->state = NVMESTORE_STATE_FORMATING;

    if(!nvmestore->config_file.is_default_config()) {
        struct spdk_bs_opts opts = {};
        spdk_bs_opts_init(&opts);

        std::cout << "....\n";
        opts.cluster_sz      = nvmestore->config_file.cluster_sz;
        opts.num_md_pages    = nvmestore->config_file.num_md_pages;
        opts.max_md_ops      = nvmestore->config_file.max_md_ops;
        opts.max_channel_ops = nvmestore->config_file.max_channel_ops;

        spdk_bs_init(nvmestore->bs_dev, &opts, bs_init_cb, farg);
    } else {
        std::cout << "xxx\n";
        spdk_bs_init(nvmestore->bs_dev, NULL, bs_init_cb, farg);
    }
}

/*
int NvmeStore::default_init_core_info() {
    uint32_t last_core = spdk_env_get_last_core();
    uint32_t first_core = spdk_env_get_first_core();
    uint32_t core_count = spdk_env_get_core_count();

    //至少需要两个线程，一个admin线程，一个meta线程
    if(core_count < 2 || last_core == first_core) {
        fct->log()->error("core count less 2, need 2 core at least.");
        return -1;
    }

    //首先决定出当前线程对于nvmestore来说是admin线程
    admin_core = spdk_env_get_current_core();

    //其次决定哪个core作为nvmestore的meta线程，meta线程只需要一个即可
    if(admin_core == last_core) {
        meta_core = first_core;
    } else {
        meta_core = meta_core = spdk_env_get_next_core(admin_core);
    }

    //获取IO线程的core，如果是单线程的话，则与meta线程共用，推荐使用多线程
    if(core_count == 2) {
        io_cores.push_back(meta_core);
    } else {
        io_cores.push_back(spdk_env_get_next_core(meta_core) % UINT32_MAX);
        for(size_t i = 1; i < core_count - 2; i++) {
            io_cores.push_back(spdk_env_get_next_core(io_cores[i - 1]) % UINT32_MAX);
        }
    }

    return 0;
}
*/

/*
 * dev_format: dev_format函数用于格式化dev设备
 * 格式化之后的设备就可以被挂载了，但是格式化操作会删除当前设备上已有的数据
*/
int NvmeStore::dev_format() {
    int ret = 0;
    this->state = NVMESTORE_STATE_FORMATING;
    
    /*注释：将core_info信息添加移到了IOchannels和NvmeConf结构体了，所以不需要在此额外初始化了
        if(default_init_core_info() != 0) {
            fct->log()->error("init core info failed.");
            this->state = NVMESTORE_STATE_DOWN;
            return NVMESTORE_FORMAT_ERR;
        }
    */

    bdev = spdk_bdev_get_by_name(dev_name.c_str());
    if(bdev == NULL) {
        fct_->log()->lerror("counld not find a bdev");
        this->state = NVMESTORE_STATE_DOWN;
        return NVMESTORE_FORMAT_ERR;
    }

    bs_dev = spdk_bdev_create_bs_dev(bdev, NULL, NULL);
    if(bs_dev == NULL) {
        fct_->log()->lerror("could not create blob bdev!!");
        this->state = NVMESTORE_STATE_DOWN;
        return NVMESTORE_FORMAT_ERR;
    }

    struct format_arg *arg = new format_arg(this, &ret);
    struct spdk_event *event = spdk_event_allocate(get_meta_core(), format_start, arg, NULL);
    spdk_event_call(event);

    this->wait_format_completed();
    
    if(ret != NVMESTORE_OP_SUCCESS || state == NVMESTORE_STATE_DOWN)
        goto free_arg;

    //这里创建用于存储chunk和blob映射关系的default blob，通过chunk_blob_map->create_blob()创建
    chunk_blob_map = new ChunkBlobMap(this);
    if(chunk_blob_map == nullptr) {
        fct_->log()->lerror("allocate NvmeChunkMap failed.");
        goto free_arg;
    }

    struct spdk_blob_opts opts;
    spdk_blob_opts_init(&opts);
    if(config_file.default_config) {
        opts.num_clusters = DEFAULT_MAP_BLOB_SIZE;
        opts.thin_provision = false;
    } else {
        opts.num_clusters = config_file.num_clusters;
        opts.thin_provision = false;
    }

    if((ret = chunk_blob_map->create_blob(opts)) != 0) {
        fct_->log()->lerror("create chunk_blob_map's blob_id failed: %d", ret);
        goto free_map;
    }

    //将创建的blob添加到config file中
    std::cout << "chunk_map_blob_id: " << chunk_blob_map->get_chunk_map_blob_id() << std::endl;
    config_file.set_blob_id(chunk_blob_map->get_chunk_map_blob_id());

    if(chunk_blob_map) {
        delete chunk_blob_map;
        chunk_blob_map = nullptr;
    }

    //持久化
    config_file.store();
    std::cout << "config_file store success..\n";

free_map:
    if(chunk_blob_map)
        delete chunk_blob_map;

free_arg:
    if(arg)
        delete arg;

    return ret;
}

/*
    void NvmeStore::alloc_io_channel(void *arg1, void *arg2) {
    struct alloc_ch_arg *ch_arg = (struct alloc_ch_arg *)arg1;
    NvmeStore *nvmestore = ch_arg->nvmestore;
    size_t index = ch_arg->index;

    uint32_t curr_core = spdk_env_get_current_core();
    nvmestore->channles[index] = spdk_bs_alloc_io_channel(nvmestore->blobstore);
    if(nvmestore->channels[index] == NULL) 
        fct_->log()->error("alloc channels failed on core %ld", curr_core);

    if(ch_arg) 
        delete ch_arg;

    nvmestore->signal_alloc_ch_completed();
}
*/

void NvmeStore::bs_load_cb(void *cb_arg, struct spdk_blob_store *bs, int bserrno) {
    struct mount_arg *marg = (struct mount_arg *)cb_arg;
    NvmeStore *nvmestore = marg->nvmestore;

    if(bserrno) {
        nvmestore->state = NVMESTORE_STATE_DOWN;
        *(marg->ret) = NVMESTORE_LOAD_BS_ERR;
        nvmestore->signal_mount_completed();
        return ;     
    }

    nvmestore->blobstore    = bs;
    nvmestore->page_size    = spdk_bs_get_page_size(bs);
    // nvmestore->unit_size    = spdk_bs_get_io_unit_size(bs);
    nvmestore->cluster_size = spdk_bs_get_cluster_size(bs);
    nvmestore->data_cluster_count = spdk_bs_free_cluster_count(bs);
/*
    //需要在不同的线程中分配io_channel,异步操作,需要同步等待
    if(nvmestore->get_meta_core() == nvmestore->get_io_core(0)) {
        //首先判断meta_core是不是等于io_core[0]，如果等于，则表示meta_core也是io_core
        //则在本线程中分配io_channel;
        struct spdk_io_channel *channel = spdk_bs_alloc_io_channel(bs);
        if(channel == nullptr) {
            fct->log()->error("alloc io channel failed.");
            nvmestore->state = NVMESTORE_STATE_DOWN;
            *(marg->ret) = NVMESTORE_ALLOC_CHANNEL_ERR;
            spdk_bs_unload(nvmestore->get_blobstore(), store_unload_cb, marg);
            return ;
        }

        struct nvmestore_io_channel *io_channel = new nvmestore_io_channel(&read_channels, curr_core);
        io_channel.set_channel(channel);

        nvmestore.read_channels.add_channel(io_channel);
        nvmestore.write_channels.add_channel(io_channel);
    } else if(nvmestore->get_io_core_count() == 1) {
        //其次判断如果是只有一个io线程，但是该io线程与meta线程是分离的，则需要在指定的线程创建io_channel。
        struct nvmestore_io_channel *io_channel = new nvmestore_io_channel(&read_channels, nvmestore->get_io_core(READ_CHANNEL, 0));
        read_channels.alloc_channels(nvmestore);
        write_channels.alloc_channels(nvmestore);

    } else{
        channels.resize(io_cores.size());
        for(int i = 0; i < io_cores.size(); i++) {
            struct alloc_ch_arg *ch_arg = (struct alloc_ch_arg *)malloc(sizeof(struct alloc_ch_arg));
            ch_arg->nvmestore = nvmestore;
            ch_arg->index = i;

            struct spdk_event *event = spdk_event_allocate(nvmestore->io_cores[i], alloc_io_channel, alloc_ch_arg, NULL);
            spdk_event_call(event);
        }
        nvmestore->wait_alloc_channel_compeleted();
    }
*/
    //改变nvmestore的状态
    nvmestore->state = NVMESTORE_STATE_MOUNTED;
    nvmestore->format_time = (uint64_t)time(NULL);
    nvmestore->signal_mount_completed();
    return ;

    //spdk_bs_open_blob(nvmestore->blobstore, nvmeconf->blob_id, open_map_blob_cb, marg);  
}

void NvmeStore::mount_start(void *arg1, void *arg2) {
    struct mount_arg *marg = (struct mount_arg *)arg1;

    NvmeStore *nvmestore = marg->nvmestore;

    nvmestore->state = NVMESTORE_STATE_MOUNTING;

    if(nvmestore->config_file.default_config) {
        spdk_bs_load(nvmestore->bs_dev, NULL, bs_load_cb, marg);
    } else {
        struct spdk_bs_opts opts = {};
        spdk_bs_opts_init(&opts);

        opts.cluster_sz      = nvmestore->config_file.cluster_sz;
        opts.num_md_pages    = nvmestore->config_file.num_md_pages;
        opts.max_md_ops      = nvmestore->config_file.max_md_ops;
        opts.max_channel_ops = nvmestore->config_file.max_channel_ops;

        spdk_bs_load(nvmestore->bs_dev, &opts, bs_load_cb, marg);
    }
}

/*
 * dev_mount: dev_mount函数用于挂载nvmestore
 * 首先dev本身一定要是具有格式化，如果没有被格式化，直接进行挂载的话，找不到nvmestore，就会报错
*/
int NvmeStore::dev_mount() {
    int ret = NVMESTORE_OP_SUCCESS;
    this->state = NVMESTORE_STATE_MOUNTING;

    bdev = spdk_bdev_get_by_name(dev_name.c_str());
    if(bdev == NULL) {
        this->state = NVMESTORE_STATE_DOWN;
        fct_->log()->lerror("counld not find a bdev\n");

        return NVMESTORE_MOUNT_ERR;
    }

    bs_dev = spdk_bdev_create_bs_dev(bdev, NULL, NULL);
    if(bs_dev == NULL) {
        this->state = NVMESTORE_STATE_DOWN;
        fct_->log()->lerror("could not create blob bdev!!");
        return NVMESTORE_MOUNT_ERR;
    }

    struct mount_arg *arg = new mount_arg(this, &ret);
    struct spdk_event *event = spdk_event_allocate(this->get_meta_core(), mount_start, arg, NULL);
    spdk_event_call(event);

    this->wait_mount_completed();

    std::cout << "load blobstore completed..." << std::endl;
    if(ret != NVMESTORE_OP_SUCCESS) {
        fct_->log()->lerror("nvmestore mount failed.");
        return NVMESTORE_MOUNT_ERR;
    }

    //uint32_t curr_core = spdk_env_get_current_core();
    //创建read_channels
    read_channels = new IOChannels(READ_CHANNEL, config_file.get_read_core_count());
    if(read_channels == nullptr) {
        this->state = NVMESTORE_STATE_DOWN;
        fct_->log()->lerror("allocate read io channles failed.");
        return NVMESTORE_MOUNT_ERR;
    }

    //创建write_channels;
    write_channels = new IOChannels(WRITE_CHANNEL, config_file.get_write_core_count());
    if(write_channels == nullptr) {
        this->state = NVMESTORE_STATE_DOWN;
        fct_->log()->lerror("allocate write io channels filed.");
        goto delete_read_channels;
    }

    //创建chunk_map
    this->chunk_map = new NvmeChunkMap(this);
    if(this->chunk_map == nullptr) {
        fct_->log()->lerror("allocate chunk_map failed.");
        goto delete_write_channels;
    }

    //创建chunk_blob_map，并加载相应的数据
    this->chunk_blob_map = new ChunkBlobMap(this, config_file.get_blob_id());
    if(this->chunk_blob_map == nullptr) {
        fct_->log()->lerror("allocate chunk_blob_map failed.");
        goto delete_chunk_map;
    }

    std::cout << "create data struct completed." << std::endl;

    //为read channels分配创建IO通道
    read_channels->alloc_channels(this);
    std::cout << "read channel nums: " << read_channels->channel_nums << std::endl;
    for(size_t i = 0; i < read_channels->channels.size(); i++) {
        std::cout << "\tcore: " << read_channels->channels[i]->lcore << std::endl;
        if(read_channels->channels[i]->io_channel)
            std::cout << "\tchannel alloced." << std::endl;
    }
    //为write channels分配创建IO通道
    write_channels->alloc_channels(this);
        std::cout << "read channel nums: " << write_channels->channel_nums << std::endl;
    for(size_t i = 0; i < write_channels->channels.size(); i++) {
        std::cout << "\tcore: " << write_channels->channels[i]->lcore << std::endl;
        if(write_channels->channels[i]->io_channel)
            std::cout << "\tchannel alloced." << std::endl;
    }


    std::cout << "alloc channels compeleted.." << std::endl;

    //加载chunk_blob_map数据到内存中来
    if(this->chunk_blob_map->load() != 0) {
        fct_->log()->lerror("load chunk_blob_map failed.");
        goto delete_chunk_blob_map;
    }

    std::cout << "load chunk blob map completed.." << std::endl;
    return NVMESTORE_OP_SUCCESS;

delete_chunk_blob_map:
    if(this->chunk_blob_map)
        delete this->chunk_blob_map;

delete_chunk_map:
    if(this->chunk_map)
        delete this->chunk_map;

delete_write_channels:
    if(this->write_channels)
        delete this->write_channels;

delete_read_channels:
    if(this->read_channels)
        delete this->read_channels;

    return NVMESTORE_MOUNT_ERR;
}

void NvmeStore::store_unload_cb(void *cb_arg, int bserrno) {
    NvmeStoreOpArg *oparg = (NvmeStoreOpArg *)cb_arg;
    NvmeStore *nvmestore = oparg->nvmestore;

    nvmestore->get_fct()->log()->lerror("unload cb");
    int opcode = oparg->opcode;
    switch(opcode) {
        case UNMOUNT:
            if(bserrno) {
                nvmestore->fct_->log()->lerror("unmount failed: unload blobstore failed: %d", bserrno);
                *(oparg->ret) = NVMESTORE_UNMOUNT_ERR;
                nvmestore->signal_unmount_completed();
                return ;
            }

            std::cout << "unload blobstore completed.." << std::endl;
            nvmestore->blobstore = nullptr;
            nvmestore->config_file.store();
            nvmestore->state = NVMESTORE_STATE_DOWN;
            nvmestore->signal_unmount_completed();
            std::cout << "config_file store completed.." << std::endl;
            break;
        case MOUNT:
            if(bserrno)
                nvmestore->fct_->log()->lerror("mount failed: unload blobstore failed: %d", bserrno);

            nvmestore->signal_mount_completed();
            break;
    }

    return ;
}

void NvmeStore::unmount_start(void *arg1, void *arg2) {
    struct umount_arg *uarg = (struct umount_arg *)arg1;

    NvmeStore *nvmestore = uarg->nvmestore;
    nvmestore->get_fct()->log()->linfo("current core: %ld", spdk_env_get_current_core());

    spdk_bs_unload(nvmestore->blobstore, store_unload_cb, uarg);
}

/*
 * dev_mount: dev_unmount函数主要用于卸载nvmestore
 * nvmestore被unmount之后，就不能在对其进行任何操作，包括chunk的读写等
*/
int NvmeStore::dev_unmount() {
    int ret = NVMESTORE_OP_SUCCESS;
    this->state = NVMESTORE_STATE_UNMOUNTING;

    fct_->log()->lerror("unmount start...");
    std::cout << "chunk_map.size = " << chunk_map->get_active_nums() << std::endl;
    std::cout << "---------\n";

    if(chunk_map->get_active_nums() > 0) {
        fct_->log()->lerror("unmount failed: nvmestore is using.");
        return NVMESTORE_CHUNK_USING;
    }

    if(chunk_blob_map->is_dirty()) {
        std::cout << "persist chunk_blob_map start..\n";
        if(chunk_blob_map->store() != 0) {
            fct_->log()->lerror("chunk_blob_map failed.");
            ret = NVMESTORE_STORE_MAPBLOB_ERR;
            //signal_unmount_completed();

            return NVMESTORE_UNMOUNT_ERR;
        }

        std::cout << "persist chunk_blob_map completed.." << std::endl;

        if(chunk_blob_map->close_blob() != 0) {
            fct_->log()->lerror("chunk_blob_map close failed.");
            ret = NVMESTORE_CLOSE_MAPBLOB_ERR;
            //signal_unmount_completed();

            return NVMESTORE_UNMOUNT_ERR;
        }

        std::cout << "close chunk_blob_map completed.." << std::endl;

        if(chunk_blob_map) {
            //销毁chunk_blob_map中的信息
            chunk_blob_map->cleanup();
            delete chunk_blob_map;
        }

        std::cout << "delete chunk_blob_map_ptr completed.." << std::endl;
    }

    //销毁chunk_map结构
    if(chunk_map) {
        chunk_map->cleanup();
        delete chunk_map;
    }

    std::cout << "cleanup chunk_map completed..." << std::endl;

    //销毁io_channels;
    if(read_channels) {
        read_channels->free_channels();
        delete read_channels;
    }

    if(write_channels) {
        write_channels->free_channels();
        delete write_channels;
    }

    std::cout << "unmount_core: " << spdk_env_get_current_core() << std::endl;

    struct umount_arg *uarg = new umount_arg(this, &ret);
    struct spdk_event *event = spdk_event_allocate(this->get_meta_core(), unmount_start, uarg, NULL);
    spdk_event_call(event);
    std::cout << "wait_unmount_completed.." << std::endl;

    wait_unmount_completed();
    return ret;
}
/*
 * chunk_create: chunk_create函数主要根据给定的chunk_id和初始化选项创建一个chunk
 * chk_id: chk_id表示要创建的chunk的id
 * opts: opts表示的是用于初始化chunk的选项值
 * note: chunk_create创建一个chunk，包括创建chunk的本质blob，并持久化存储一些初始化元数据信息
*/
int NvmeStore::chunk_create(uint64_t chk_id, const chunk_create_opts_t& opts) {
    int ret;
    if(chunk_exist(chk_id)) {
        fct_->log()->lerror("chunk <%" PRIx64 "> has already existed.", chk_id);
        return NVMESTORE_CHUNK_EXISTED;
    }

#ifdef DEBUG
    opts.print(); 
#endif

    //opts.print();

    NvmeChunk *chunk = new NvmeChunk(fct_, this, chk_id, opts);
    if(chunk == nullptr) {
        fct_->log()->lerror("new chunk failed: %s", strerror(errno));
        return NVMESTORE_CHUNK_CREATE_ERR;
    }

    //在构造函数中对该chunk直接进行赋值，不需要额外的赋值操作
    //chunk->set_chunk_info(opts);

    struct spdk_blob_opts blob_opts;
    blob_opts.num_clusters = opts.size / cluster_size;
    blob_opts.thin_provision = (opts.is_prealloc() ? false : true);

    //使用chunk来控制所有同步操作
    ret = chunk->create(opts, true, nullptr);
    if(ret != NVMECHUNK_OP_SUCCESS) {
        fct_->log()->lerror("create chunk failed.");
        return ret;
    }

    chunk_blob_map->insert_entry(chk_id, chunk->get_blob_id());
    total_chunks++;

    if(chunk)
        delete chunk;

    dirty = true;

    return ret;
}

void NvmeStore::delete_blob_cb(void *cb_arg, int bserrno) {
    struct remove_arg *rarg = (struct remove_arg *)cb_arg;
    NvmeStore *nvmestore = rarg->nvmestore;

    if(bserrno) {
        nvmestore->fct_->log()->lerror("delete blob faild.");
        *(rarg->ret) = NVMESTORE_CHUNK_DELETE_ERR;
        nvmestore->signal_remove_completed();
        return ;
    }

    nvmestore->signal_remove_completed();
}

void NvmeStore::remove_chunk_start(void *arg1, void *arg2) {
    struct remove_arg *rarg = (struct remove_arg *)arg1;
    NvmeStore *nvmestore = rarg->nvmestore;
    spdk_blob_id blobid = rarg->blobid;
    
    spdk_bs_delete_blob(nvmestore->blobstore, blobid, delete_blob_cb, rarg);
}

/*
 * chunk_remove: chunk_remove函数用于删除一个chunk
 * chk_id: 表示要删除的chunk_id
 * note： 该函数用于删除一个chunk，但是如果该chunk正在被open或者using，则不能不删除
*/
int NvmeStore::chunk_remove(uint64_t chk_id) {
    int ret = NVMESTORE_OP_SUCCESS;

    if(!chunk_exist(chk_id)) {
        fct_->log()->lerror("chunk <%" PRIx64 "> is not existed.", chk_id);
        return NVMESTORE_CHUNK_NOT_EXISTED;
    }

    NvmeChunk *chunk = chunk_map->get_chunk(chk_id);
    if(chunk != nullptr) {
        fct_->log()->lerror("chunk <%" PRIx64 "> remove failed: chunk is opening.", chk_id);
        return NVMESTORE_CHUNK_USING;
    }

    spdk_blob_id blobid = chunk_blob_map->get_blob_id(chk_id);

    struct remove_arg *rarg = new remove_arg(this, blobid, &ret);
    struct spdk_event *event = spdk_event_allocate(this->get_meta_core(), remove_chunk_start, rarg, NULL);
    spdk_event_call(event);
    this->wait_remove_completed();

    this->chunk_blob_map->remove_entry(chk_id);
    total_chunks--;

    if(rarg)
        delete rarg;

    dirty = true;
    
    return ret;
}

/*
 * chunk_open: chunk_open函数用于open一个chunk
 * chk_id：表示要open的chunk的chunk_id
 * note: 如果该chunk已经被打开了，则使得open_ref加1.
*/
std::shared_ptr<Chunk> NvmeStore::chunk_open(uint64_t chk_id) {
    //int ret = NVMESTORE_OP_SUCCESS;
    if(!chunk_exist(chk_id)) {
        fct_->log()->lerror("chunk <%" PRIx64 "> is not existed.", chk_id);
        return nullptr;
    }

    NvmeChunk *chunk = chunk_map->get_chunk(chk_id);

    if(chunk != nullptr) {
        //chunk_ptr = std::make_shared<Chunk>(raw_chunk);
        return std::shared_ptr<Chunk>(chunk);
    } else {
        chunk = new NvmeChunk(fct_, this, chk_id);
        if(chunk == nullptr) {
            fct_->log()->lerror("new NvmeChunk failed.");
            return nullptr;
        }

        std::cout << "blobid = " << chunk_blob_map->get_blob_id(chk_id) << std::endl;
        
        if(chunk->open(chunk_blob_map->get_blob_id(chk_id), true, nullptr) != NVMECHUNK_OP_SUCCESS) {
            delete chunk;
            chunk = nullptr;
        }
    }

    return std::shared_ptr<Chunk>(chunk);
}

/*
 * chunk_close: chunk_close函数用于关闭一个chunk
 * chunk: 表示被open的chunk指针
 * note: 如果该chunk被多次open，则将open_ref减1
*/
int NvmeStore::chunk_close(Chunk *chunk) {
    if(chunk == nullptr) {
        fct_->log()->lerror("invalid chunk ptr.");
        return NVMESTORE_CHUNK_CLOSE_ERR;
    }

    fct_->log()->linfo("chunk close start");
    
    NvmeChunk *chunk_ptr = dynamic_cast<NvmeChunk *>(chunk);
    if(chunk_ptr->get_open_ref() > 1) {
        chunk_ptr->open_ref_sub(1);
        return NVMESTORE_OP_SUCCESS;
    } else {
        if(chunk_ptr->close(chunk_ptr->get_blob(), true, nullptr) != NVMECHUNK_OP_SUCCESS) {
            fct_->log()->lerror("chunk close failed.");
            return NVMESTORE_CHUNK_CLOSE_ERR;
        }

        if(chunk_ptr)
            delete chunk_ptr;
    }

    return NVMESTORE_OP_SUCCESS;
}

void NvmeStore::print_store() {
    std::cout << "-----NvmeStore Public Info----------------\n";
    std::cout << "|store_id: "      << store_id     << "\n";
    std::cout << "|cluster_name: "  << cluster_name << "\n";
    std::cout << "|name: "          << name         << "\n";
    std::cout << "|page_szie: "     << page_size    << "\n";
    std::cout << "|chunk_pages: "   << chunk_pages  << "\n";
    std::cout << "|total_chunks: "  << total_chunks << "\n";
    // std::cout << "|unit_size: "     << unit_size    << "\n";
    std::cout << "|used_size: "     << used_size    << "\n";
    //std::cout << "|format_time: "   << asctime(localtime(format_time)) << "\n";
    std::cout << "|format_time: "   << format_time  << "\n";
    std::cout << "-------------------------------------------\n"; 
}