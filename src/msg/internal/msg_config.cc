#include "msg_config.h"
#include "msg/msg_context.h"
#include "msg/MsgWorker.h"
#include "node_addr.h"
#include "util.h"

#include <cstring>
#include <string>
#include <iostream>
#include <regex>
#include <utility>

namespace flame{
namespace msg{

bool MsgConfig::get_bool(const std::string &v) {
    const char *true_values[] = {"yes", "enable", "on", "true", nullptr};
    auto lv = str2lower(v);
    const char **it = true_values;
    while(*it){
        if(lv.compare(*it) == 0){
            return true;
        }
        ++it;
    }
    return false;
}

int MsgConfig::load(){
    int res = 0;
    auto cfg = fct->config();

    res = set_msg_log_level(cfg->get("msg_log_level", FLAME_MSG_LOG_LEVEL_D));
    if (res) {
        perr_arg("msg_log_level");
        return 1;
    }

    res = set_msger_id(cfg->get("msger_id", FLAME_MSGER_ID_D));
    if (res) {
        perr_arg("msger_id");
        return 1;
    }

    // node_listen_ports
    res = set_node_listen_ports(cfg->get("node_listen_ports", 
                                            FLAME_NODE_LISTEN_PORTS_D));
    if (res) {
        perr_arg("node_listen_ports");
        return 1;
    }

    res = set_msg_worker_type(cfg->get("msg_worker_type", 
                                            FLAME_MSG_WORKER_TYPE_D));
    if (res) {
        perr_arg("msg_worker_type");
        return 1;
    }

    res = set_msg_worker_num(cfg->get("msg_worker_num", 
                                                    FLAME_MSG_WORKER_NUM_D));
    if (res) {
        perr_arg("msg_worker_num");
        return 1;
    }

    res = set_msg_worker_cpu_map(cfg->get("msg_worker_cpu_map", 
                                                FLAME_MSG_WORKER_CPU_MAP_D));
    if (res) {
        perr_arg("msg_worker_cpu_map");
        return 1;
    }

    res = set_msg_worker_spdk_event_poll_period(
                                cfg->get("msg_worker_spdk_event_poll_period",
                                FLAME_MSG_WORKER_SPDK_EVENT_POLL_PERIOD_D));
    if (res) {
        perr_arg("msg_worker_spdk_event_poll_period");
        return 1;
    }

    res = set_rdma_enable(cfg->get("rdma_enable", FLAME_RDMA_ENABLE_D));
    if (res) {
        perr_arg("rdma_enable");
        return 1;
    }
    
    if (rdma_enable) {

        res = set_rdma_device_name(cfg->get("rdma_device_name", 
                                                FLAME_RDMA_DEVICE_NAME_D));
        if (res) {
            perr_arg("rdma_device_name");
            return 1;
        }

        res = set_rdma_port_num(cfg->get("rdma_port_num", 
                                            FLAME_RDMA_PORT_NUM_D));
        if (res) {
            perr_arg("rdma_port_num");
            return 1;
        }

        res = set_rdma_buffer_num(cfg->get("rdma_buffer_num", 
                                            FLAME_RDMA_BUFFER_NUM_D));
        if (res) {
            perr_arg("rdma_buffer_num");
            return 1;
        }

        res = set_rdma_buffer_size(cfg->get("rdma_buffer_size", 
                                            FLAME_RDMA_BUFFER_SIZE_D));
        if (res) {
            perr_arg("rdma_buffer_size");
            return 1;
        }

        res = set_rdma_max_inline_data(cfg->get("rdma_max_inline_data", 
                                                FLAME_RDMA_MAX_INLINE_DATA_D));
        if (res) {
            perr_arg("rdma_max_inline_data");
            return 1;
        }

        res = set_rdma_send_queue_len(cfg->get("rdma_send_queue_len", 
                                                FLAME_RDMA_SEND_QUEUE_LEN_D));
        if (res) {
            perr_arg("rdma_send_queue_len");
            return 1;
        }

        res = set_rdma_recv_queue_len(cfg->get("rdma_recv_queue_len", 
                                                FLAME_RDMA_RECV_QUEUE_LEN_D));
        if (res) {
            perr_arg("rdma_recv_queue_len");
            return 1;
        }

        res = set_rdma_enable_hugepage(cfg->get("rdma_enable_hugepage", 
                                                FLAME_RDMA_ENABLE_HUGEPAGE_D));
        if (res) {
            perr_arg("rdma_enable_hugepage");
            return 1;
        }

        res = set_rdma_hugepage_size(cfg->get("rdma_hugepage_size",
                                                FLAME_RDMA_HUGEPAGE_SIZE_D));
        if (res) {
            perr_arg("rdma_hugepage_size");
            return 1;
        }

        res = set_rdma_path_mtu(cfg->get("rdma_path_mtu", 
                                            FLAME_RDMA_PATH_MTU_D));
        if (res) {
            perr_arg("rdma_path_mtu");
            return 1;
        }

        res = set_rdma_enable_srq(cfg->get("rdma_enable_srq", 
                                            FLAME_RDMA_ENABLE_SRQ_D));
        if (res) {
            perr_arg("rdma_enable_srq");
            return 1;
        }

        res = set_rdma_cq_pair_num(cfg->get("rdma_cq_pair_num", 
                                            FLAME_RDMA_CQ_PAIR_NUM_D));
        if (res) {
            perr_arg("rdma_cq_pair_num");
            return 1;
        }

        res = set_rdma_traffic_class(cfg->get("rdma_traffic_class", 
                                                FLAME_RDMA_TRAFFIC_CLASS_D));
        if (res) {
            perr_arg("rdma_traffic_class");
            return 1;
        }

        res = set_rdma_mem_min_level(cfg->get("rdma_mem_min_level", 
                                                FLAME_RDMA_MEM_MIN_LEVEL_D));
        if (res) {
            perr_arg("rdma_mem_min_level");
            return 1;
        }

        res = set_rdma_mem_max_level(cfg->get("rdma_mem_max_level", 
                                                FLAME_RDMA_MEM_MAX_LEVEL_D));
        if (res) {
            perr_arg("rdma_mem_max_level");
            return 1;
        }

        res = set_rdma_poll_event(cfg->get("rdma_poll_event", 
                                                FLAME_RDMA_POLL_EVENT_D));
        if (res) {
            perr_arg("rdma_poll_event");
            return 1;
        }
    }

    return 0;
}

int MsgConfig::set_msg_log_level(const std::string &v){
    static const char *msg_log_level_strs[] = {
        "dead", "critical", "wrong",
        "error", "warn", "info",
        "debug", "trace", "print"
    };
    auto level_lower = str2lower(v);
    uint8_t end = static_cast<uint8_t>(msg_log_level_t::print);
    for(uint8_t i = 0;i <= end; i++){
        if(level_lower == msg_log_level_strs[i]){
            this->msg_log_level = static_cast<msg_log_level_t>(i);
            return 0;
        }
    }
    return 1;
}

int MsgConfig::set_msg_worker_type(const std::string &v){
    static const char *msg_worker_type_strs[] = {
        "UNKNOWN", "THREAD", "SPDK"
    };
    if(v.empty()){
        return 1;
    }

    msg_worker_type = msg_worker_type_t::UNKNOWN;
    auto type_upper = str2upper(v);
    uint8_t end = 3;
    for(uint8_t i = 1;i < end; i++){
        if(msg_worker_type_strs[i] == type_upper){
            msg_worker_type = static_cast<msg_worker_type_t>(i);
            break;
        }
    }
    if(msg_worker_type == msg_worker_type_t::UNKNOWN){
        return 1;
    }else if(msg_worker_type == msg_worker_type_t::SPDK){
        rdma_poll_event = false;
    }
    return 0;
}

int MsgConfig::set_msg_worker_num(const std::string &v){
    if(v.empty()){
        return 1;
    }

    int worker_num = std::stoi(v, nullptr, 0);

    msg_worker_num = worker_num;

    return 0;
}

int MsgConfig::set_msg_worker_cpu_map(const std::string &v){
    std::regex cpu_id_regex("\\S+");
    auto iter_begin = std::sregex_iterator(v.begin(), v.end(), cpu_id_regex);
    auto iter_end = std::sregex_iterator();

    if(iter_begin == iter_end && !v.empty()){
        return 1;
    }
    msg_worker_cpu_map.clear();
    for(auto i = iter_begin;i != iter_end;++i){
        auto match = *i;
        int cpu_id = std::stoi(match.str(), nullptr, 10);
        msg_worker_cpu_map.push_back(cpu_id);
    }
    
    return 0;
}

int MsgConfig::set_msg_worker_spdk_event_poll_period(const std::string &v){
    if(v.empty()){
        return 1;
    }

    msg_worker_spdk_event_poll_period = std::stoull(v, nullptr, 0);

    return 0;
}

int MsgConfig::set_msger_id(const std::string &v){
    std::regex msger_id_regex("([0-9.]+)/(\\d+)");
    std::smatch m;
    if(!regex_match(v, m, msger_id_regex) && !v.empty()){
        return 1;
    }
    if(m.size() != 3){
        return 1;
    }
    NodeAddr addr(nullptr);
    if(!addr.ip_from_string(m[1].str())){
        return 1;
    }
    int port = std::stoi(m[2].str(), nullptr, 10);
    if(port > 65535 || port < 0){
        return 1;
    }
    this->msger_id.ip = static_cast<unsigned long>(addr.in4_addr().sin_addr.s_addr);
    this->msger_id.port = static_cast<unsigned short>(port);
    return 0;
}

int MsgConfig::set_node_listen_ports(const std::string &v){
    std::regex lp_regex("(TCP|RDMA)@([0-9a-fA-F:.]+)/(\\d+)-(\\d+)", 
                            std::regex_constants::icase);
    auto lp_begin = std::sregex_iterator(v.begin(), v.end(), lp_regex);
    auto lp_end = std::sregex_iterator();
    if(lp_begin == lp_end && !v.empty()){
        return 1;
    }
    NodeAddr addr(nullptr);
    for(auto i = lp_begin;i != lp_end;++i){
        auto match = *i;
        if(match.size() != 5) {
            goto error;
        }
        auto transport = str2upper(match[1].str());
        auto address = match[2].str();
        if(!addr.ip_from_string(address)){
            goto error;
        }
        int port_min = std::stoi(match[3].str(), nullptr, 10);
        int port_max = std::stoi(match[4].str(), nullptr, 10);
        if(port_min > 65535 || port_min < 0 
            || port_max > 65535 || port_max < 0
            || port_max - port_min < 0 ){
            goto error;
        }
        this->node_listen_ports.push_back(
                std::make_tuple(transport, address, port_min, port_max));
    }
    goto ok;
error:
    this->node_listen_ports.clear();
    return 1;
ok:
    return 0;
}

int MsgConfig::set_rdma_enable(const std::string &v){
    rdma_enable = false;
    if(v.empty()){
        return 1;
    }
    rdma_enable = MsgConfig::get_bool(v);
    return 0;
}

int MsgConfig::set_rdma_device_name(const std::string &v){
    if(v.empty()){
        return 1;
    }
    rdma_device_name = v;
    return 0;
}

int MsgConfig::set_rdma_port_num(const std::string &v){
    if(v.empty()){
        return 1;
    }

    int port_num = std::stoi(v, nullptr, 0);

    if(port_num >=0 && port_num <= 65535){
        rdma_port_num = port_num;
        return 0;
    }

    // error
    return 1;
}


int MsgConfig::set_rdma_buffer_num(const std::string &v){
    rdma_buffer_num = 0;
    if(v.empty()){
        return 0;
    }

    int nbuf = std::stoi(v, nullptr, 0);

    if(nbuf >= 0){
        rdma_buffer_num = nbuf;
        return 0;
    }

    // error
    return 1;
}

int MsgConfig::set_rdma_buffer_size(const std::string &v){
    if(v.empty()){
        return 1;
    }
    uint64_t result = size_str_to_uint64(v);
    if(result < (1ULL << 32)){
        rdma_buffer_size = result;
        return 0;
    }
    return 1;
}

int MsgConfig::set_rdma_max_inline_data(const std::string &v){
    if(v.empty()){
        return 1;
    }
    uint64_t result = size_str_to_uint64(v);
    if(result < (1ULL << 32)){
        if(result < 64){
            return 1;
        }
        rdma_max_inline_data = result;
        return 0;
    }
    return 1;
}

int MsgConfig::set_rdma_send_queue_len(const std::string &v){
    if(v.empty()){
        return 1;
    }

    int queue_len = std::stoi(v, nullptr, 0);

    if(queue_len > 0){
        rdma_send_queue_len = queue_len;
        return 0;
    }

    return 1;
}

int MsgConfig::set_rdma_recv_queue_len(const std::string &v){
    if(v.empty()){
        return 1;
    }

    int queue_len = std::stoi(v, nullptr, 0);

    if(queue_len > 0){
        rdma_recv_queue_len = queue_len;
        return 0;
    }

    return 1;
}

int MsgConfig::set_rdma_enable_hugepage(const std::string &v){
    rdma_enable_hugepage = false;
    if(v.empty()){
        return 1;
    }
    rdma_enable_hugepage = MsgConfig::get_bool(v);
    return 0;
}

int MsgConfig::set_rdma_hugepage_size(const std::string &v){
     if(v.empty()){
        return 1;
    }
    static const char *hugepage_size_strs[] = {
        "2M", "8M", "1G"
    };

    auto size_str_upper = str2upper(v);
    for(int i = 0;i < 3;++i){
        if(size_str_upper == hugepage_size_strs[i]){
            rdma_hugepage_size = size_str_to_uint64(v);
            return 0;
        }
    }

    rdma_hugepage_size = 2 * 1024 * 1024;// 2MB
    return 1;
}

int MsgConfig::set_rdma_path_mtu(const std::string &v){
    if(v.empty()){
        return 1;
    }

    int path_mtu = std::stoi(v, nullptr, 0);

    if(path_mtu == 256 || path_mtu == 512 || path_mtu == 1024 
        || path_mtu == 2048 || path_mtu == 4096){
        rdma_path_mtu = path_mtu;
        return 0;
    }

    return 1;
}

int MsgConfig::set_rdma_enable_srq(const std::string &v){
    rdma_enable_srq = false;
    if(v.empty()){
        return 1;
    }
    rdma_enable_srq = MsgConfig::get_bool(v);
    return 0;
}

int MsgConfig::set_rdma_cq_pair_num(const std::string &v){
    rdma_cq_pair_num = 1;
    if(v.empty()){
        return 1;
    }
    int cq_pair_num = std::stoi(v, nullptr, 0);
    if(cq_pair_num >= 1){
        rdma_cq_pair_num = cq_pair_num;
        return 0;
    }
    return 1;
}

int MsgConfig::set_rdma_traffic_class(const std::string &v){
    if(v.empty()){
        return 1;
    }

    int traffic_class = std::stoi(v, nullptr, 0);
    if(traffic_class >= 0 && traffic_class < (1U << 8)){
        rdma_traffic_class = traffic_class;
        return 0;
    }

    return 1;
}

int MsgConfig::set_rdma_mem_min_level(const std::string &v){
    if(v.empty()){
        return 1;
    }

    int min_level = std::stoi(v, nullptr, 0);
    if(min_level >= 0 && min_level < (1U << 8)){
        rdma_mem_min_level = min_level;
        return 0;
    }

    return 1;
}

int MsgConfig::set_rdma_mem_max_level(const std::string &v){
    if(v.empty()){
        return 1;
    }

    int max_level = std::stoi(v, nullptr, 0);
    if(max_level >= 0 && max_level < (1U << 8)){
        rdma_mem_max_level = max_level;
        return 0;
    }

    return 1;
}

int MsgConfig::set_rdma_poll_event(const std::string &v){
    rdma_poll_event = true;
    if(v.empty()){
        return 1;
    }
    rdma_poll_event = MsgConfig::get_bool(v);
    //force to use direct poll
    if(msg_worker_type == msg_worker_type_t::SPDK){
        rdma_poll_event = false;
    }
    return 0;
}

} //namespace msg
} //namespace flame
