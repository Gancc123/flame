#ifndef FLAME_TESTS_MSG_RDMA_PERF_H
#define FLAME_TESTS_MSG_RDMA_PERF_H

#include "msg/msg_core.h"
#include "util/clog.h"
#include "util/option_parser.h"
#include "util/fmt.h"

#include "get_clock.h"

#include <cstring>
#include <vector>
#include <algorithm>
#include <fstream>
#include <stdexcept>

#define M_ENCODE(bl, data) (bl).append(&(data), sizeof(data))
#define M_DECODE(it, data) (it).copy(&(data), sizeof(data))

namespace flame{
namespace msg{

enum class perf_type_t {
    NONE,
    MEM_PUSH,  // send req + rdma read + send resp
    SEND,      // send req + send resp
    SEND_DATA, // send req with data + send resp
    MEM_FETCH, // send req + rdma write + send resp
    MEM_FETCH_WITH_IMM, //send req + rdma write_with_imm
};

std::string str_from_perf_type(perf_type_t t){
    switch(t){
    case perf_type_t::SEND:
        return "send";
    case perf_type_t::SEND_DATA:
        return "senddata";
    case perf_type_t::MEM_PUSH:
        return "mempush";
    case perf_type_t::MEM_FETCH:
        return "memfetch";
    case perf_type_t::MEM_FETCH_WITH_IMM:
        return "memfetchimm";
    }
    return "unknown";
}

perf_type_t perf_type_from_str(const std::string &s){
    auto lower = str2lower(s);
    if(lower == "mem_push"){
        return perf_type_t::MEM_PUSH;
    }
    if(lower == "send"){
        return perf_type_t::SEND;
    }
    if(lower == "send_data"){
        return perf_type_t::SEND_DATA;
    }
    if(lower == "mem_fetch"){
        return perf_type_t::MEM_FETCH;
    }
    if(lower == "mem_fetch_with_imm"){
        return perf_type_t::MEM_FETCH_WITH_IMM;
    }
    return perf_type_t::NONE;
}

struct perf_config_t{
    bool use_imm_resp;
    bool no_thr_optimize;
    std::string inline_size;
    std::string target_rdma_ip;
    int target_rdma_port;
    perf_type_t perf_type;
    uint64_t size;
    uint32_t num;
    uint8_t depth;
    uint8_t depth_cnt;
    std::vector<ib::RdmaBuffer *> rw_buffers;
    cycles_t *tposted = nullptr;
    std::string result_file;
};

void dump_result(perf_config_t &cfg){
    cfg.depth_cnt--;
    if(cfg.depth_cnt > 0){
        return;
    }
    double cycle_to_unit = get_cpu_mhz(0); // no warn
    std::vector<cycles_t> deltas;
    deltas.resize(cfg.num * cfg.depth, 0);
    for(int iter = 0;iter < cfg.depth;++iter){
        uint32_t base = iter * (cfg.num + 1);
        for(int i = 0;i < cfg.num;++i){
            deltas[i + iter * cfg.num] 
                            = cfg.tposted[i+1 + base] - cfg.tposted[i + base];
        }
    }
    //std::sort(deltas.begin(), deltas.end());
    std::ofstream f;
    f.open(cfg.result_file);
    f << "Cnt: " << cfg.num * cfg.depth <<  '\n';
    double average_sum = 0;
    int cnt = 0;
    for(int i = 0;i < deltas.size();++i){
        double t = deltas[i] / cycle_to_unit;
        f << t << '\n';
        if(i % cfg.num != 0) {
            average_sum += t;
            cnt++;
        }
    }
    f << "Avg: " << (average_sum / cnt) << " us\n";
    std::sort(deltas.begin(), deltas.end());
    //ignore the largest deltas(it's for conn establish.)
    double median = 0;
    size_t dsize = deltas.size() - cfg.depth;
    if(dsize % 2 == 0){
        median = (deltas[dsize / 2 - 1] + deltas[dsize / 2]) 
                    / cycle_to_unit / 2;
    }else{
        median = deltas[dsize / 2] / cycle_to_unit;
    }
    f << "Median: " << median << " us\n";
    f.close();
    clog(fmt::format("avg lat: {} us, median: {} us", 
                        (average_sum / cnt), median));
    clog(fmt::format("dump result to {}", cfg.result_file));
}

void init_resource(perf_config_t &config){
    if(config.num > (1UL << 24)){
        clog(fmt::format("num {} > {}, too big!", config.num, 1UL << 24));
        throw std::invalid_argument("config.num too big!");
    }
    if(config.result_file == "result.txt"){
        config.result_file = fmt::format("result_{}_{}_d{}_{}{}{}.txt",
                                str_from_perf_type(config.perf_type),
                                size_str_from_uint64(config.size),
                                config.depth,
                                config.use_imm_resp?"imm":"noimm",
                                config.inline_size == "0"?"_noinline":"",
                                config.no_thr_optimize?"_notp":"");
    }
    assert(config.num > 0);
    uint32_t arr_len = config.depth * (config.num + 1);
    config.tposted = new cycles_t[arr_len];
    assert(config.tposted);
    std::memset(config.tposted, 0, sizeof(cycles_t)*(arr_len));
    config.depth_cnt = config.depth;
}

void fin_resource(perf_config_t &config){
    delete [] config.tposted;
}

optparse::OptionParser init_parser(){
    using namespace optparse;
    const std::string usage = "usage: %prog [OPTION]... ";
    auto parser = OptionParser()
        .usage(usage);
    
    parser.add_option("-c", "--config").help("set config file");
    parser.add_option("-i", "--index").type("int").set_default(0)
        .help("set node index");
    parser.add_option("-n", "--num").type("long").set_default(1000)
        .help("iter count for perf");
     parser.add_option("--depth").type("int").set_default(1)
        .help("set concurrent operation num");
    parser.add_option("-t", "--type")
        .choices({"mem_push", "send", "send_data", "mem_fetch",
                    "mem_fetch_with_imm"})
        .set_default("send")
        .help("perf type: mem_push, send, send_data, mem_fetch,"
                " mem_fetch_with_imm");
    parser.add_option("--imm_resp")
            .action("store_true")
            .set_default("false")
            .help("use imm data to resp");
    parser.add_option("--inline")
        .set_default("128")
        .help("rdma max inline data size");
    parser.add_option("--log_level").set_default("info");
    parser.add_option("--result_file").set_default("result.txt")
        .help("result file path");

    parser.add_option("-s", "--size").set_default("4M")
        .help("size for mem_push, send_data, mem_fetch, mem_fetch_with_imm ");

    parser.add_option("-a", "--address").set_default("127.0.0.1")
        .help("target ip for rdma");
    parser.add_option("-p", "--port").type("int").set_default(7777)
        .help("target port for rdma");

    parser.add_option("--no_thr_opt")
        .action("store_true")
        .set_default("false")
        .help("don't use thread optimization");

    return parser;
}

struct msg_incre_d : public MsgData{
    uint8_t index;
    uint32_t num;
    virtual int encode(MsgBufferList &bl) override{
        int len = M_ENCODE(bl, index);
        len += M_ENCODE(bl, num);
        return len;
    }
    virtual int decode(MsgBufferList::iterator &it) override{
        int len = M_DECODE(it, index);
        len += M_DECODE(it, num);
        return len;
    }
};

uint32_t imm_data_from_incre_data(uint32_t index, uint32_t num){
    return (index << 24) | (num & 0xffffffUL);
}

void incre_data_from_imm_data(uint32_t imm_data, msg_incre_d &incre_data){
    incre_data.index = imm_data >> 24;
    incre_data.num = imm_data & (0xffffffUL);
}

class RdmaMsger : public MsgerCallback{
    MsgContext *mct;
    perf_config_t *config;
    std::vector<ib::RdmaBuffer *> rw_buffers;
    void on_mem_push_req(Connection *conn, Msg *msg);
    void on_mem_push_resp(Connection *conn, Msg *msg);
    void on_send_req(Connection *conn, Msg *msg);
    void on_send_resp(Connection *conn, Msg *msg);
    void on_mem_fetch_req(Connection *conn, Msg *msg);
    void on_mem_fetch_resp(Connection *conn, Msg *msg);
public:
    explicit RdmaMsger(MsgContext *c, perf_config_t *cfg=nullptr) 
    : mct(c), config(cfg), rw_buffers(cfg->depth, nullptr) {};
    virtual void on_conn_recv(Connection *conn, Msg *msg) override;
    virtual void on_rdma_env_ready() override;
    void clear_rw_buffers(){
        auto allocator = Stack::get_rdma_stack()->get_rdma_allocator();
        allocator->free_buffers(rw_buffers);
    }
};

void RdmaMsger::on_rdma_env_ready(){
    ML(mct, info, "RDMA Env Ready!");
}

void RdmaMsger::on_mem_push_req(Connection *conn, Msg *msg){
    assert(msg->has_rdma());

    msg_rdma_header_d rdma_header(msg->get_rdma_cnt(), msg->with_imm());
    msg_incre_d incre_data;

    auto it = msg->data_iter();
    rdma_header.decode(it);
    incre_data.decode(it);
    
    auto msger_id = conn->get_session()->peer_msger_id;
    ML(mct, trace, "{}=>  {}", msger_id_to_str(msger_id), msg->to_string());

    auto allocator = Stack::get_rdma_stack()->get_rdma_allocator();

    auto lbuf = rw_buffers[incre_data.index];
    if(!lbuf){
        lbuf = allocator->alloc(rdma_header.rdma_bufs[0]->size());
        assert(lbuf);
        ML(mct, info, "alloc buffer {}", lbuf->size());
        rw_buffers[incre_data.index] = lbuf;
    }

    auto rdma_cb = new RdmaRwWork();
    assert(rdma_cb);
    rdma_cb->target_func = 
            [this, allocator, incre_data](RdmaRwWork *w, RdmaConnection *conn){
        auto lbuf = w->lbufs[0];
        ML(mct, info, "rdma read done. buf: {}...{} {}B",
            lbuf->buffer()[0], lbuf->buffer()[lbuf->data_len - 1],
            lbuf->data_len);

        if(config->use_imm_resp){
            auto imm = imm_data_from_incre_data(incre_data.index,
                                                incre_data.num + 1);
            conn->post_imm_data(imm);
        }else{
            auto resp_msg = Msg::alloc_msg(mct, msg_ttype_t::RDMA);
            resp_msg->flag |= FLAME_MSG_FLAG_RESP;

            msg_incre_d new_incre_data;
            new_incre_data.index = incre_data.index;
            new_incre_data.num = incre_data.num + 1;
            resp_msg->append_data(new_incre_data);
            conn->send_msg(resp_msg);

            resp_msg->put();
        }
        
        allocator->free_buffers(w->rbufs);
        delete w;
    };

    rdma_cb->is_write = false;
    rdma_cb->rbufs = rdma_header.rdma_bufs;
    rdma_cb->lbufs.push_back(lbuf);
    rdma_cb->cnt = 1;

    auto rdma_conn = RdmaStack::rdma_conn_cast(conn);
    assert(rdma_conn);
    rdma_conn->post_rdma_rw(rdma_cb);

    return;
}

void RdmaMsger::on_mem_push_resp(Connection *conn, Msg *msg){
    msg_incre_d incre_data;
    if(config->use_imm_resp){
        assert(msg->is_imm_data());
        incre_data_from_imm_data(msg->imm_data, incre_data);
        ML(mct, debug, "recv imm_data: {:x}", msg->imm_data);
    }else{
        auto it = msg->data_iter();
        incre_data.decode(it);
    }

    assert(this->config);
    uint32_t base = incre_data.index * (config->num + 1);
    this->config->tposted[incre_data.num + base] = get_cycles();

    if(incre_data.num >= this->config->num){
        dump_result(*(this->config));
        ML(mct, info, "iter {} times / {}, done.", incre_data.num, 
                                                    incre_data.index);
        return;
    }

    auto buf = this->config->rw_buffers[incre_data.index];
    msg_rdma_header_d rdma_header(1, false);
    rdma_header.rdma_bufs.push_back(buf);
    buf->buffer()[0] = 'A' + (incre_data.num % 26);
    buf->buffer()[buf->size() - 1] = 'Z' - (incre_data.num % 26);

    auto req_msg = Msg::alloc_msg(mct, msg_ttype_t::RDMA);
    req_msg->flag |= FLAME_MSG_FLAG_RDMA;
    req_msg->set_rdma_cnt(1);
    req_msg->append_data(rdma_header);
    req_msg->append_data(incre_data);

    conn->send_msg(req_msg);

    req_msg->put();
}

void RdmaMsger::on_send_req(Connection *conn, Msg *msg){
    msg_incre_d incre_data;

    auto it = msg->data_iter();
    incre_data.decode(it);
    
    auto msger_id = conn->get_session()->peer_msger_id;
    ML(mct, trace, "{}=>  {}", msger_id_to_str(msger_id), msg->to_string());

    if(config->use_imm_resp){
        auto rdma_conn = RdmaStack::rdma_conn_cast(conn);
        assert(rdma_conn);
        auto imm = imm_data_from_incre_data(incre_data.index,
                                                incre_data.num + 1);
        rdma_conn->post_imm_data(imm);
        return;
    }

    auto resp_msg = Msg::alloc_msg(mct, msg_ttype_t::RDMA);
    resp_msg->flag |= FLAME_MSG_FLAG_RESP;

    ++incre_data.num;
    resp_msg->append_data(incre_data);
    conn->send_msg(resp_msg);
    //conn->send_msg(resp_msg, true);
    //conn->post_submit();

    resp_msg->put();
}

void RdmaMsger::on_send_resp(Connection *conn, Msg *msg){
    msg_incre_d incre_data;
    if(config->use_imm_resp){
        assert(msg->is_imm_data());
        incre_data_from_imm_data(msg->imm_data, incre_data);
        ML(mct, debug, "recv imm_data: {:x}", msg->imm_data);
    }else{
        auto it = msg->data_iter();
        incre_data.decode(it);
    }

    assert(this->config);
    uint32_t base = incre_data.index * (config->num + 1);
    this->config->tposted[incre_data.num + base] = get_cycles();

    if(incre_data.num >= this->config->num){
        dump_result(*(this->config));
        ML(mct, info, "iter {} times / {}, done.", incre_data.num, 
                                                    incre_data.index);
        return;
    }

    auto req_msg = Msg::alloc_msg(mct, msg_ttype_t::RDMA);
    req_msg->append_data(incre_data);

    if(config->perf_type == perf_type_t::SEND_DATA){
        MsgBuffer buf(config->size);
        buf.set_offset(config->size);
        buf[0] = 'A' + (incre_data.num % 26);
        buf[buf.offset() - 1] = 'Z' - (incre_data.num % 26);
        req_msg->data_buffer_list().append_nocp(std::move(buf));
    }
    
    conn->send_msg(req_msg);
    //conn->send_msg(req_msg, true);
    //conn->post_submit();

    req_msg->put();
}

void RdmaMsger::on_mem_fetch_req(Connection *conn, Msg *msg){
    assert(msg->has_rdma());

    msg_rdma_header_d rdma_header(msg->get_rdma_cnt(), msg->with_imm());
    msg_incre_d incre_data;

    auto it = msg->data_iter();
    rdma_header.decode(it);
    incre_data.decode(it);
    
    auto msger_id = conn->get_session()->peer_msger_id;
    ML(mct, trace, "{}=>  {}", msger_id_to_str(msger_id), msg->to_string());

    auto allocator = Stack::get_rdma_stack()->get_rdma_allocator();

    auto lbuf = rw_buffers[incre_data.index];
    if(!lbuf){
        lbuf = allocator->alloc(rdma_header.rdma_bufs[0]->data_len);
        assert(lbuf);
        ML(mct, info, "alloc buffer {}", lbuf->size());
        rw_buffers[incre_data.index] = lbuf;
    }
    lbuf->buffer()[0] = 'A' + (incre_data.num % 26);
    lbuf->buffer()[lbuf->size() - 1] = 'Z' - (incre_data.num % 26);
    lbuf->data_len = lbuf->size(); //make sure that data_len is not zero.

    auto rdma_cb = new RdmaRwWork();
    assert(rdma_cb);
    rdma_cb->is_write = true;
    rdma_cb->rbufs = rdma_header.rdma_bufs;
    rdma_cb->lbufs.push_back(lbuf);
    rdma_cb->cnt = 1;

    if(config->perf_type == perf_type_t::MEM_FETCH_WITH_IMM){
        rdma_cb->imm_data = imm_data_from_incre_data(incre_data.index,
                                                     incre_data.num + 1);
        rdma_cb->target_func = 
            [this, allocator](RdmaRwWork *w, RdmaConnection *conn){
            allocator->free_buffers(w->rbufs);
            delete w;
        };
    }else{
        rdma_cb->target_func = 
            [this, allocator, incre_data](RdmaRwWork *w, RdmaConnection *conn){
            auto lbuf = w->lbufs[0];
            auto resp_msg = Msg::alloc_msg(mct, msg_ttype_t::RDMA);
            resp_msg->set_flags(FLAME_MSG_FLAG_RESP);

            msg_incre_d new_incre_data;
            new_incre_data.index = incre_data.index;
            new_incre_data.num = incre_data.num + 1;
            resp_msg->append_data(new_incre_data);
            conn->send_msg(resp_msg);

            resp_msg->put();
            allocator->free_buffers(w->rbufs);
            delete w;
        };
    }

    auto rdma_conn = RdmaStack::rdma_conn_cast(conn);
    assert(rdma_conn);
    rdma_conn->post_rdma_rw(rdma_cb);
}

void RdmaMsger::on_mem_fetch_resp(Connection *conn, Msg *msg){
    msg_incre_d incre_data;
    if(config->perf_type == perf_type_t::MEM_FETCH_WITH_IMM){
        assert(msg->is_imm_data());
        incre_data_from_imm_data(msg->imm_data, incre_data);
    }else{ 
        auto it = msg->data_iter();
        incre_data.decode(it);
    }

    assert(this->config);
    uint32_t base = incre_data.index * (config->num + 1);
    this->config->tposted[incre_data.num + base] = get_cycles();

    auto buf = this->config->rw_buffers[incre_data.index];
    ML(mct, info, "rdma write done. buf: {}...{} {}B",
            buf->buffer()[0], buf->buffer()[buf->data_len - 1],
            buf->data_len);

    if(incre_data.num >= this->config->num){
        dump_result(*(this->config));
        ML(mct, info, "iter {} times / {}, done.", incre_data.num, 
                                                    incre_data.index);
        return;
    }

    //next iter
    msg_rdma_header_d rdma_header(1, false);
    rdma_header.rdma_bufs.push_back(buf);

    auto req_msg = Msg::alloc_msg(mct, msg_ttype_t::RDMA);
    req_msg->flag |= FLAME_MSG_FLAG_RDMA;
    req_msg->set_rdma_cnt(1);
    req_msg->append_data(rdma_header);
    req_msg->append_data(incre_data);

    conn->send_msg(req_msg);

    req_msg->put();
}

void RdmaMsger::on_conn_recv(Connection *conn, Msg *msg){
    //post work to another thread.
    if(config->no_thr_optimize && conn->get_owner()->am_self()){
        assert(mct->manager->get_worker_num() > 1);
        msg->get();
        mct->manager->get_worker(0)->post_work([this, conn, msg](){
            ML(this->mct, trace, "post on_conn_recv");
            this->on_conn_recv(conn, msg);
            msg->put();
        });
        return;
    }
    switch(config->perf_type){
    case perf_type_t::MEM_PUSH:
        if(msg->has_rdma() && msg->is_req()){
            on_mem_push_req(conn, msg);
        }else if(msg->is_resp()){
            on_mem_push_resp(conn, msg);
        }
        break;
    case perf_type_t::SEND:
    case perf_type_t::SEND_DATA:
        if(msg->is_req()){
            on_send_req(conn, msg);
        }else if(msg->is_resp()){
            on_send_resp(conn, msg);
        }
        break;
    case perf_type_t::MEM_FETCH:
    case perf_type_t::MEM_FETCH_WITH_IMM:
        if(msg->is_req()){
            on_mem_fetch_req(conn, msg);
        }else if(msg->is_resp()){
            on_mem_fetch_resp(conn, msg);
        }
        break;
    default:
        break;
    }
    return;
}

} //namespace msg
} //namespace flame

#undef M_ENCODE
#undef M_DECODE

#endif 