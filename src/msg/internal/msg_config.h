#ifndef FLAME_MSG_INTERNAL_CONFIG_H
#define FLAME_MSG_INTERNAL_CONFIG_H

#include "acconfig.h"
#include "util/clog.h"
#include "util/fmt.h"
#include "types.h"
#include "msg/msg_def.h"

#include <string>
#include <vector>
#include <tuple>
#include <cassert>

#define FLAME_MSG_LOG_LEVEL_D         "PRINT"
#define FLAME_MSG_WORKER_TYPE_D       "THREAD"
#define FLAME_MSG_WORKER_NUM_D        "4"
#define FLAME_MSG_WORKER_CPU_MAP_D    ""
#define FLAME_MSG_WORKER_SPDK_EVENT_POLL_PERIOD_D "400" //microsecond
#define FLAME_MSGER_ID_D              ""
#define FLAME_NODE_LISTEN_PORTS_D     ""
#define FLAME_RDMA_ENABLE_D           "false"
#define FLAME_RDMA_CONN_VERSION_D     "1"
#define FLAME_RDMA_DEVICE_NAME_D      ""
#define FLAME_RDMA_PORT_NUM_D         ""
#define FLAME_RDMA_BUFFER_NUM_D       ""
#define FLAME_RDMA_BUFFER_SIZE_D      "4224"
#define FLAME_RDMA_MAX_INLINE_DATA_D  "128"
#define FLAME_RDMA_SEND_QUEUE_LEN_D   "64"
#define FLAME_RDMA_RECV_QUEUE_LEN_D   "1024"
#define FLAME_RDMA_ENABLE_HUGEPAGE_D  "true"
#define FLAME_RDMA_HUGEPAGE_SIZE_D    "2M"
#define FLAME_RDMA_ENABLE_SRQ_D       "true"
#define FLAME_RDMA_CQ_PAIR_NUM_D      "1"
#define FLAME_RDMA_TRAFFIC_CLASS_D    "0"
#define FLAME_RDMA_PATH_MTU_D         "4096"
#define FLAME_RDMA_MEM_MIN_LEVEL_D    "12"
#define FLAME_RDMA_MEM_MAX_LEVEL_D    "29"
#define FLAME_RDMA_POLL_EVENT_D       "true"

#ifdef ON_SW_64
    #define FLAME_RDMA_HUGEPAGE_SIZE_D "8M"
#endif 



namespace flame{

class FlameContext;

namespace msg{

enum class msg_worker_type_t;

class MsgConfig{
    FlameContext *fct;

    void perr_arg(const std::string &v){
        clog(fmt::format("load configure file error : {}", v));
    }

public:
    static MsgConfig *load_config(FlameContext *c){
        auto config = new MsgConfig(c);
        assert(config);
        if(config->load()){
            delete config;
            return nullptr;
        }
        return config;
    }
    static bool get_bool(const std::string &v);

    explicit MsgConfig(FlameContext *c): fct(c) {};

    int load();

    /**
     * Msg module log level
     * @cfg: msg_log_level
     */
    msg_log_level_t msg_log_level;
    int set_msg_log_level(const std::string &v);

    /**
     * Msg module msg worker type
     * @cfg: msg_worker_type
     */
    msg_worker_type_t msg_worker_type;
    int set_msg_worker_type(const std::string &v);

    /**
     * Msg module workers num
     * @cfg: msg_worker_num
     */
    int msg_worker_num;
    int set_msg_worker_num(const std::string &v);

    /**
     * Msg module msg worker cpu map
     * @cfg: msg_worker_cpu_map
     */
    std::vector<int> msg_worker_cpu_map;
    int set_msg_worker_cpu_map(const std::string &v);

    /**
     * Msg module msg worker spdk event poll period
     * Only for SpdkMsgWorker event poll time interval.
     * @cfg: msg_worker_spdk_event_poll_period
     */
    uint64_t msg_worker_spdk_event_poll_period;
    int set_msg_worker_spdk_event_poll_period(const std::string &v);

    /**
     * Msger Id
     * @cfg: msger_id
     */
    msger_id_t msger_id;
    int set_msger_id(const std::string &v);

    /**
     * Node Listen Ports
     * @cfg: node_listen_ports
     */
    std::vector<std::tuple<std::string, std::string, int, int>> 
                                                            node_listen_ports;
    int set_node_listen_ports(const std::string &v);

    /**
     * RDMA enable
     * @cfg: rdma_enable
     */
    bool rdma_enable;
    int set_rdma_enable(const std::string &v);

    /**
     * RDMA Connection version
     * @cfg: rdma_conn_version
     */
    int rdma_conn_version;
    int set_rdma_conn_version(const std::string &v);
    
    /**
     * RDMA device name
     * @cfg: rdma_device_name
     */
    std::string rdma_device_name;
    int set_rdma_device_name(const std::string &v);

    /**
     * RDMA port number
     * @cfg: rdma_port_num
     */
    uint8_t rdma_port_num;
    int set_rdma_port_num(const std::string &v);

    /**
     * RDMA buffer number
     * @cfg: rdma_buffer_num
     * 0 means no limit.
     */
    int rdma_buffer_num;
    int set_rdma_buffer_num(const std::string &v);

    /**
     * RDMA buffer size
     * @cfg: rdma_buffer_size
     */
    uint32_t rdma_buffer_size;
    int set_rdma_buffer_size(const std::string &v);

    /**
     * RDMA max inline data
     * @cfg: rdma_max_inline_data
     * @value: >= 64
     */
    uint32_t rdma_max_inline_data;
    int set_rdma_max_inline_data(const std::string &v);

    /**
     * RDMA send queue len
     * @cfg: rdma_send_queue_len
     */
    int rdma_send_queue_len;
    int set_rdma_send_queue_len(const std::string &v);

    /**
     * RDMA receive queue len
     * @cfg: rdma_recv_queue_len
     */
    int rdma_recv_queue_len;
    int set_rdma_recv_queue_len(const std::string &v);

    /**
     * RDMA enable hugepage
     * @cfg: rdma_enable_hugepage
     */
    int rdma_enable_hugepage;
    int set_rdma_enable_hugepage(const std::string &v);

    /**
     * RDMA hugepage size
     * @cfg: rdma_hugepage_size
     * @value: 2M, 8M, 1G
     */
    uint32_t rdma_hugepage_size;
    int set_rdma_hugepage_size(const std::string &v);

    /**
     * RDMA path mtu
     * @cfg: rdma_path_mtu
     */
    int rdma_path_mtu;
    int set_rdma_path_mtu(const std::string &v);

    /**
     * RDMA enable srq
     * @cfg: rdma_enable_srq
     */
    bool rdma_enable_srq;
    int set_rdma_enable_srq(const std::string &v);

    /**
     * RDMA cq pair number
     * @cfg: rdma_cq_pair_num
     */
    int rdma_cq_pair_num;
    int set_rdma_cq_pair_num(const std::string &v);

    /**
     * RDMA traffic class;
     * @cfg: rdma_traffic_class
     */
    uint8_t rdma_traffic_class;
    int set_rdma_traffic_class(const std::string &v);

    /**
     * RDMA RdmaBuffer min size (2^min_level)
     * @cfg: rdma_mem_min_level
     */
    uint8_t rdma_mem_min_level;
    int set_rdma_mem_min_level(const std::string &v);

    /**
     * RDMA RdmaBuffer max size (2^max_level).
     * One BuddyAllocator will have 2^max_level bytes.
     * @cfg: rdma_mem_max_level
     */
    uint8_t rdma_mem_max_level;
    int set_rdma_mem_max_level(const std::string &v);

    /**
     * RDMA poll event
     * @cfg: rdma_poll_event
     */
    bool rdma_poll_event;
    int set_rdma_poll_event(const std::string &v);

};

} //namespace msg
} //namespace flame

#endif // FLAME_MSG_INTERNAL_CONFIG_H