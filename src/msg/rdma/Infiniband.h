/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-04 16:06:51
 */
#ifndef FLAME_MSG_RDMA_INFINIBAND_H
#define FLAME_MSG_RDMA_INFINIBAND_H

#include <infiniband/verbs.h>
#include <cassert>

#include "msg/msg_context.h"
#include "common/thread/mutex.h"
#include "msg/internal/msg_buffer.h"
#include "msg/internal/errno.h"
#include "msg/msg_def.h"


namespace flame{
namespace msg{
namespace ib{

class Infiniband;

struct IBSYNMsg {
    uint16_t lid;
    uint32_t qpn;
    uint32_t psn;
    uint32_t peer_qpn;
    uint8_t sl;
    union ibv_gid gid;
} __attribute__((packed));

class Port {
    MsgContext *mct;
    struct ibv_context* ctxt;
    int port_num;
    struct ibv_port_attr* port_attr;
    uint16_t lid;
    int gid_idx = 0;
    union ibv_gid gid;
    Port(MsgContext *mct, struct ibv_context* ictxt, uint8_t ipn);
public: 
    static Port* create(MsgContext *mct, struct ibv_context* ictxt, 
                                                                uint8_t ipn);
    ~Port();
    int reload_info();
    uint16_t get_lid() { return lid; }
    ibv_gid  get_gid() { return gid; }
    int get_port_num() { return port_num; }
    ibv_port_attr* get_port_attr() { return port_attr; }
    int get_gid_idx() { return gid_idx; }
};


class Device {
    MsgContext *mct;
    ibv_device *device;
    const char* name;
    uint8_t  port_cnt = 0;
    Device(MsgContext *c, ibv_device* d);
public:
    static Device* create(MsgContext *c, ibv_device* d);
    ~Device() {
        if (active_port) {
            delete active_port;
            int r = ibv_close_device(ctxt);
            assert(r == 0);
        }
        delete device_attr;
    }
    const char* get_name() { return name;}
    uint16_t get_lid() { return active_port->get_lid(); }
    ibv_gid get_gid() { return active_port->get_gid(); }
    int get_gid_idx() { return active_port->get_gid_idx(); }
    void binding_port(int port_num);
    int set_async_fd_nonblock();
    int get_async_fd() { return ctxt->async_fd; }
    struct ibv_context *ctxt;
    ibv_device_attr *device_attr;
    Port* active_port;
};


class DeviceList {
    MsgContext *mct;
    struct ibv_device ** device_list;
    int num;
    Device** devices;
    DeviceList(MsgContext *c):mct(c) {}
public:
    static DeviceList* create(MsgContext *mct){
        struct ibv_device **device_list;
        int num;
        device_list = ibv_get_device_list(&num);
        if (device_list == NULL || num == 0) {
            ML(mct, error, "failed to get rdma device list. {}", 
                                                        cpp_strerror(errno));
            return nullptr;
        }
        auto dl = new DeviceList(mct);
        dl->device_list = device_list;
        dl->num = num;
        dl->devices = new Device*[num];
        for (int i = 0;i < num; ++i) {
            dl->devices[i] = Device::create(mct, device_list[i]);
        }

        return dl;
    }
    ~DeviceList() {
        for (int i=0; i < num; ++i) {
            delete devices[i];
        }
        delete []devices;
        ibv_free_device_list(device_list);
    }

    Device* get_device(const char* device_name) {
        assert(devices);
        for (int i = 0; i < num; ++i) {
            if (!strlen(device_name) 
                || !strcmp(device_name, devices[i]->get_name())) {
                return devices[i];
            }
        }
        return NULL;
    }
};

class ProtectionDomain {
    MsgContext *mct;
    ProtectionDomain(MsgContext *mct, ibv_pd* ipd);
public:
    static ProtectionDomain *create(MsgContext *mct, Device *device);
    ~ProtectionDomain();

    ibv_pd* const pd;
};

class CompletionChannel {
    static const uint32_t MAX_ACK_EVENT = 5000;
    MsgContext *mct;
    Infiniband& infiniband;
    ibv_comp_channel *channel;
    ibv_cq *cq;
    uint32_t cq_events_that_need_ack;

public:
    CompletionChannel(MsgContext *c, Infiniband &ib);
    ~CompletionChannel();
    int init();
    bool get_cq_event();
    int get_fd() { return channel->fd; }
    ibv_comp_channel* get_channel() { return channel; }
    void bind_cq(ibv_cq *c) { cq = c; }
    void ack_events();
};

// this class encapsulates the creation, use, and destruction of an RC
// completion queue.
//
// You need to call init and it will create a cq and associate to comp channel
class CompletionQueue {
public:
    CompletionQueue(MsgContext *c, Infiniband &ib,
                    const uint32_t qd, CompletionChannel *cc)
      : mct(c), infiniband(ib), channel(cc), cq(NULL), queue_depth(qd) {}
    ~CompletionQueue();
    int init();
    int poll_cq(int num_entries, ibv_wc *ret_wc_array);

    ibv_cq* get_cq() const { return cq; }
    int rearm_notify(bool solicited_only=false);
    CompletionChannel* get_cc() const { return channel; }
private:
    MsgContext *mct;
    Infiniband&  infiniband;     // Infiniband to which this QP belongs
    CompletionChannel *channel;
    ibv_cq *cq;
    uint32_t queue_depth;
};

// this class encapsulates the creation, use, and destruction of an RC
// queue pair.
//
// you need call init and it will create a qp and bring it to the INIT state.
// after obtaining the lid, qpn, and psn of a remote queue pair, one
// must call plumb() to bring the queue pair to the RTS state.
class QueuePair {
public:
    static QueuePair *get_by_ibv_qp(ibv_qp *qp);
    QueuePair(MsgContext *c, Infiniband& infiniband, ibv_qp_type type,
              int ib_physical_port,  ibv_srq *srq,
              CompletionQueue* txcq, CompletionQueue* rxcq,
              uint32_t tx_queue_len, uint32_t max_recv_wr, uint32_t q_key = 0);
    ~QueuePair();

    int init();

    /**
     * Get the initial packet sequence number for this QueuePair.
     * This is randomly generated on creation. It should not be confused
     * with the remote side's PSN, which is set in #plumb(). 
     */
    uint32_t get_initial_psn() const { return initial_psn; };
    /**
     * Get the local queue pair number for this QueuePair.
     * QPNs are analogous to UDP/TCP port numbers.
     */
    uint32_t get_local_qp_number() const { return qp->qp_num; };
    /**
     * Get the remote queue pair number for this QueuePair, as set in #plumb().
     * QPNs are analogous to UDP/TCP port numbers.
     */
    int get_remote_qp_number(uint32_t *rqp) const;
    /**
     * Get the remote infiniband address for this QueuePair, as set in #plumb().
     * LIDs are "local IDs" in infiniband terminology. They are short, locally
     * routable addresses.
     */
    int get_remote_lid(uint16_t *lid) const;
    /**
     * Get the state of a QueuePair.
     */
    int get_state() const;
    /**
     * Return true if the queue pair is in an error state, false otherwise.
     */
    bool is_error() const;
    /**
     * If tx_wr + amt < limit, update tx_wr.
     * Otherwise, if partial is true, add as many as possible, 
     * and reutrn the amt.
     * @param amt   
     *      the amount to be added.
     * @param limit 
     *      tx_wr can't excceed the limit.
     * @param partial 
     *      if true, add as many as possible, otherwise add when don't 
     *      excceed the limit.
     * @return 
     *      return the real amount added.
     */
    uint32_t add_tx_wr_with_limit(uint32_t amt, uint32_t limit, 
                                                            bool partial=false){
        uint32_t wr_inflight = tx_wr_inflight.load(std::memory_order_relaxed);
        uint32_t real_amt = amt;
        do{
            if(wr_inflight + amt > limit){
                if(partial){
                    assert(wr_inflight <= limit);
                    real_amt = limit - wr_inflight;
                }else{
                    return 0;
                }
                
            }
        }while(!tx_wr_inflight.compare_exchange_weak(wr_inflight, 
                                                    wr_inflight + real_amt, 
                                                    std::memory_order_release,
                                                    std::memory_order_relaxed));
        return real_amt;
    }
    void add_tx_wr(uint32_t amt) { tx_wr_inflight += amt; }
    void dec_tx_wr(uint32_t amt) { tx_wr_inflight -= amt; }
    uint32_t get_tx_wr() const { return tx_wr_inflight; }
    ibv_qp* get_qp() const { return qp; }
    CompletionQueue* get_tx_cq() const { return txcq; }
    CompletionQueue* get_rx_cq() const { return rxcq; }
    uint32_t get_max_send_wr() const { return max_send_wr; }
    uint32_t get_max_recv_wr() const { return max_recv_wr; }
    bool has_srq() const { return srq != nullptr; }
    int to_dead();
    bool is_dead() const { return dead_; }

    void * user_ctx = nullptr;

private:
    MsgContext  *mct;
    Infiniband&  infiniband;     // Infiniband to which this QP belongs
    ibv_qp_type  type;           // QP type (IBV_QPT_RC, etc.)
    ibv_context* ctxt;           // device context of the HCA to use
    int ib_physical_port;
    ibv_pd*      pd;             // protection domain
    ibv_srq*     srq;            // shared receive queue
    ibv_qp*      qp;             // infiniband verbs QP handle
    CompletionQueue* txcq;
    CompletionQueue* rxcq;
    uint32_t     initial_psn;    // initial packet sequence number
    uint32_t     max_send_wr;
    uint32_t     max_recv_wr;
    uint32_t     q_key;
    bool dead_;
    std::atomic<uint32_t> tx_wr_inflight = {0}; // counter for inflight Tx WQEs
};

class Infiniband{
    uint32_t tx_queue_len = 0;
    uint32_t rx_queue_len = 0;
    uint32_t max_sge;
    uint8_t  ib_physical_port = 0;
    Device *device = nullptr;
    ProtectionDomain *pd = nullptr;
    DeviceList *device_list = nullptr;
    void wire_gid_to_gid(const char *wgid, union ibv_gid *gid);
    void gid_to_wire_gid(const union ibv_gid *gid, char wgid[]);
    MsgContext *mct;
    Mutex m_lock;
    bool initialized = false;
    const std::string &device_name;
    uint8_t port_num;
    bool enable_srq = false;
public:
    explicit Infiniband(MsgContext *c);
    ~Infiniband();
    bool init();
    static bool verify_prereq(MsgContext *mct);

    QueuePair* create_queue_pair(MsgContext *c, 
                                    CompletionQueue*, 
                                    CompletionQueue*, 
                                    ibv_srq *srq,
                                    ibv_qp_type type);
    ibv_srq* create_shared_receive_queue(uint32_t max_wr);
    CompletionChannel *create_comp_channel(MsgContext *c);
    CompletionQueue *create_comp_queue(MsgContext *c, 
                                                    CompletionChannel *cc=NULL);
    uint8_t get_ib_physical_port() { return ib_physical_port; }
    int encode_msg(MsgContext *mct, IBSYNMsg& msg, MsgBuffer &buffer);
    int decode_msg(MsgContext *mct, IBSYNMsg& msg, MsgBuffer &buffer);
    static int get_ib_syn_msg_len();
    uint16_t get_lid() { return device->get_lid(); }
    ibv_gid get_gid() { return device->get_gid(); }
    Device* get_device() { return device; }
    ProtectionDomain *get_pd() { return pd; }
    int get_async_fd() { return device->ctxt->async_fd; }
    uint32_t get_tx_queue_len() const { return tx_queue_len; }
    uint32_t get_rx_queue_len() const { return rx_queue_len; }
    static const char* wc_status_to_string(int status);
    static const char* qp_state_string(int status);
    static const char* wc_opcode_string(int oc);
    static const char* wr_opcode_string(int oc);
    static enum ibv_mtu ibv_mtu_enum(int mtu);
};

} //namespace ib
} //namespace msg
} //namespace flame


#endif