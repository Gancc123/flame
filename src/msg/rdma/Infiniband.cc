/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-04 16:06:50
 */
#include "Infiniband.h"
#include "msg/internal/errno.h"
#include "msg/msg_def.h"
#include "msg/NetHandler.h"
#include "MemoryManager.h"

#include <cstdlib>
#include <sys/time.h>
#include <sys/resource.h>

namespace flame{
namespace msg{
namespace ib{

static const uint32_t MAX_SHARED_RX_SGE_COUNT = 2;
static const uint32_t TCP_MSG_LEN = 
 sizeof("0000:00000000:00000000:00000000:00:00000000000000000000000000000000");
static const uint32_t CQ_DEPTH = 30000;

Port::Port(MsgContext *c, struct ibv_context* ictxt, uint8_t ipn)
:mct(c), ctxt(ictxt), port_num(ipn), port_attr(new ibv_port_attr), gid_idx(0){

}

Port::~Port(){
    if(port_attr){
        delete port_attr;
        port_attr = nullptr;
    }
}

int Port::reload_info(){
    int r = ibv_query_port(ctxt, port_num, port_attr);
    if(r == -1){
        ML(mct, error, "query port failed {}", cpp_strerror(errno));
        return -1;
    }

    lid = port_attr->lid;

    r = ibv_query_gid(ctxt, port_num, 0, &gid);
    if(r){
        ML(mct, error, "query gid failed {}", cpp_strerror(errno));
        return -1;
    }
    return 0;
}

Port* Port::create(MsgContext *mct, struct ibv_context* ictxt, 
                                                                uint8_t ipn){
    auto p = new Port(mct, ictxt, ipn);
    int r = ibv_query_port(ictxt, p->port_num, p->port_attr);
    if(r == -1){
        ML(mct, error, "query port failed {}", cpp_strerror(errno));
        delete p;
        return nullptr;
    }

    p->lid = p->port_attr->lid;

    r = ibv_query_gid(ictxt, p->port_num, 0, &p->gid);
    if(r){
        ML(mct, error, "query gid failed {}", cpp_strerror(errno));
        delete p;
        return nullptr;
    }

    return p;
}

Device::Device(MsgContext *c, ibv_device* d)
:mct(c), device(d), device_attr(new ibv_device_attr), active_port(nullptr){

}

Device* Device::create(MsgContext *mct, ibv_device *device){
    auto d = new Device(mct, device);
    if(device == nullptr){
        ML(mct, error, "device == nullptr {}", cpp_strerror(errno));
        delete d;
        return nullptr;
    }
    d->name = ibv_get_device_name(device);
    d->ctxt = ibv_open_device(device);
    if(d->ctxt == NULL){
        ML(mct, error, "open rdma device failed. {}", cpp_strerror(errno));
        delete d;
        return nullptr;
    }
    int r = ibv_query_device(d->ctxt, d->device_attr);
    if(r == -1){
        ML(mct, error, "failed to query rdma device. {}", cpp_strerror(errno));
        delete d;
        return nullptr;
    }
    return d;
}

void Device::binding_port(int port_num){
    port_cnt = device_attr->phys_port_cnt;
    for(uint8_t i = 0;i < port_cnt; ++i){
        Port *port = Port::create(mct, ctxt, i+1);
        if(i+1 == port_num && port->get_port_attr()->state == IBV_PORT_ACTIVE){
            active_port = port;
            ML(mct, info, "found active port {}", i+1);
            break;
        } else {
            ML(mct, info, "port {} is not what we want. state: {}", i+1,
                                                port->get_port_attr()->state );
        }
        delete port;
    }
    if( nullptr == active_port){
        ML(mct, error, "port not found");
        assert(active_port);
    }
}

int Device::set_async_fd_nonblock(){
    int rc = NetHandler(mct).set_nonblock(ctxt->async_fd);
    if(rc < 0){
        ML(mct, error, "failed. fd: {}", ctxt->async_fd);
        return -1;
    }
    return 0;
}

QueuePair *QueuePair::get_by_ibv_qp(ibv_qp *qp){
    return (QueuePair *)qp->qp_context;
}

QueuePair::QueuePair(
    MsgContext *c, Infiniband& infiniband, ibv_qp_type type,
    int port, ibv_srq *srq, CompletionQueue* txcq, CompletionQueue* rxcq,
    uint32_t tx_queue_len, uint32_t rx_queue_len, uint32_t q_key)
: mct(c), infiniband(infiniband),
  type(type),
  ctxt(infiniband.get_device()->ctxt),
  ib_physical_port(port),
  pd(infiniband.get_pd()->pd),
  srq(srq),
  qp(NULL),
  txcq(txcq),
  rxcq(rxcq),
  max_send_wr(tx_queue_len),
  max_recv_wr(rx_queue_len),
  q_key(q_key),
  dead_(false) {
    initial_psn = ::lrand48() & 0xffffff;
}

int QueuePair::init(){
    if(type != IBV_QPT_RC){
        ML(mct, error, "invalid queue pair type: {}", type);
        return -1;
    }
    // RC max message size is 1GB
    if(mct->config->rdma_buffer_size > 1024 * 1024 * 1024){ //1GB
        ML(mct, error, "rdma_buffer_size is too big. ({} > 1GB)", 
                                        mct->config->rdma_buffer_size);
        return -1;
    } 

    ibv_qp_init_attr qpia;
    memset(&qpia, 0, sizeof(qpia));
    qpia.qp_context = this;
    qpia.send_cq = txcq->get_cq();
    qpia.recv_cq = rxcq->get_cq();
    if(srq){
        qpia.srq = srq;                  // use the same shared receive queue
    }else{
        qpia.cap.max_recv_wr = max_recv_wr;
        qpia.cap.max_recv_sge = 2;
    }
    
    qpia.cap.max_send_wr  = max_send_wr; // max outstanding send requests
    qpia.cap.max_send_sge = 2;           // max send scatter-gather elements
    // max bytes of immediate data on send q
    qpia.cap.max_inline_data = mct->config->rdma_max_inline_data;  
    // RC, UC, UD, or XRC(only RC supported now!!)
    qpia.qp_type = type;                 
    qpia.sq_sig_all = 0;                 // only generate CQEs on requested WQEs

    qp = ibv_create_qp(pd, &qpia);
    if(qp == nullptr){
        ML(mct, error, "failed to create queue pair {}", cpp_strerror(errno));
        if(errno == ENOMEM){
            ML(mct, error, "try reducing rdma_recv_queue_len,"
                "rdma_buffer_num or rdma_buffer_size.");
        }
        return -1;
    }

    ML(mct, info, "successfully create queue pair: qp={:p}", (void *)qp);

    //move from RESET to INIT state
    ibv_qp_attr qpa;
    memset(&qpa, 0, sizeof(qpa));
    qpa.qp_state   = IBV_QPS_INIT;
    qpa.pkey_index = 0;
    qpa.port_num   = (uint8_t)(ib_physical_port);
    qpa.qp_access_flags = IBV_ACCESS_REMOTE_READ 
                            | IBV_ACCESS_REMOTE_WRITE 
                            | IBV_ACCESS_LOCAL_WRITE;
    qpa.qkey       = q_key;

    int mask = IBV_QP_STATE | IBV_QP_PORT;
    switch (type) {
    case IBV_QPT_RC:
        mask |= IBV_QP_ACCESS_FLAGS;
        mask |= IBV_QP_PKEY_INDEX;
        break;
    case IBV_QPT_UD:
        mask |= IBV_QP_QKEY;
        mask |= IBV_QP_PKEY_INDEX;
        break;
    case IBV_QPT_RAW_PACKET:
        break;
    default:
        return -1;
    }

    int ret = ibv_modify_qp(qp, &qpa, mask);
    if(ret){
        ibv_destroy_qp(qp);
        ML(mct, error, "failed to transition to INIT state: {}", 
                                                cpp_strerror(errno));
        return -1;
    }

    ML(mct, info, "successfully change queue pair to INIT: qp={:p}", (void*)qp);
    return 0;
}

QueuePair::~QueuePair(){
    if(qp){
        ML(mct, info, "destory qp={:p}", (void *)qp);
        int r = ibv_destroy_qp(qp);
        assert(!r);
    }
}

/**
 * Change RC QueuePair into the ERROR state. This is necessary modify
 * the Queue Pair into the Error state and poll all of the relevant
 * Work Completions prior to destroying a Queue Pair.
 * Since destroying a Queue Pair does not guarantee that its Work
 * Completions are removed from the CQ upon destruction. Even if the
 * Work Completions are already in the CQ, it might not be possible to
 * retrieve them. If the Queue Pair is associated with an SRQ, it is
 * recommended wait for the affiliated event IBV_EVENT_QP_LAST_WQE_REACHED
 *
 * \return
 *      -errno if the QueuePair can't switch to ERROR
 *      0 for success.
 */
int QueuePair::to_dead(){
    if(dead_) 
        return 0;
    ibv_qp_attr qpa;
    memset(&qpa, 0, sizeof(qpa));
    qpa.qp_state = IBV_QPS_ERR;

    int mask = IBV_QP_STATE;
    int ret = ibv_modify_qp(qp, &qpa, mask);
    if (ret) {
        ML(mct, error, "failed to transition to ERROR state: {}", 
                                                        cpp_strerror(errno));
        return -errno;
    }
    dead_ = true;
    return ret;
}

int QueuePair::get_remote_qp_number(uint32_t *rqp) const{
    ibv_qp_attr qpa;
    ibv_qp_init_attr qpia;

    int r = ibv_query_qp(qp, &qpa, IBV_QP_DEST_QPN, &qpia);
    if(r){
        ML(mct, error, "failed to query qp: {}", cpp_strerror(errno));
        return -1;
    }

    if(rqp)
        *rqp = qpa.dest_qp_num;
    return 0;
}

/**
 * Get the remote infiniband address for this QueuePair, as set in #plumb().
 * LIDs are "local IDs" in infiniband terminology. They are short, locally
 * routable addresses.
 */
int QueuePair::get_remote_lid(uint16_t *lid) const{
    ibv_qp_attr qpa;
    ibv_qp_init_attr qpia;

    int r = ibv_query_qp(qp, &qpa, IBV_QP_AV, &qpia);
    if(r){
        ML(mct, error, "failed to query qp: {}", cpp_strerror(errno));
        return -1;
    }

    if(lid)
        *lid = qpa.ah_attr.dlid;
    return 0;
}

/**
 * Get the state of a QueuePair.
 */
int QueuePair::get_state() const{
    ibv_qp_attr qpa;
    ibv_qp_init_attr qpia;

    int r = ibv_query_qp(qp, &qpa, IBV_QP_STATE, &qpia);
    if (r) {
        ML(mct, error, "failed to get state: {}", cpp_strerror(errno));
        return -1;
    }
    return qpa.qp_state;
}

/**
 * Return true if the queue pair is in an error state, false otherwise.
 */
bool QueuePair::is_error() const{
    ibv_qp_attr qpa;
    ibv_qp_init_attr qpia;

    int r = ibv_query_qp(qp, &qpa, -1, &qpia);
    if (r) {
        ML(mct, error, "failed to get state: {}", cpp_strerror(errno));
        return true;
    }
    return qpa.cur_qp_state == IBV_QPS_ERR;
}

CompletionChannel::CompletionChannel(MsgContext *c, Infiniband &ib)
: mct(c), infiniband(ib), channel(NULL), cq(NULL), cq_events_that_need_ack(0) { 

}

CompletionChannel::~CompletionChannel(){
    if(channel){
        int r = ibv_destroy_comp_channel(channel);
        if (r < 0)
            ML(mct, error, "failed to destroy cc: {}", cpp_strerror(errno));
        assert(r == 0);
    }
}

int CompletionChannel::init(){
    ML(mct, info, "started.");
    channel = ibv_create_comp_channel(infiniband.get_device()->ctxt);
    if(!channel){
        ML(mct, error, "failed to create receive completion channel: {}", 
                                                        cpp_strerror(errno));
        return -1;
    }
    int rc = NetHandler(mct).set_nonblock(channel->fd);
    if(rc < 0){
        ibv_destroy_comp_channel(channel);
        return -1;
    }
    return 0;
}

void CompletionChannel::ack_events(){
    ibv_ack_cq_events(cq, cq_events_that_need_ack);
    cq_events_that_need_ack = 0;
}

bool CompletionChannel::get_cq_event(){
    ibv_cq *cq = NULL;
    void *ev_ctx;
    if (ibv_get_cq_event(channel, &cq, &ev_ctx)) {
        if (errno != EAGAIN && errno != EINTR)
            ML(mct, error, "failed to retrieve CQ event: {}", 
                                                        cpp_strerror(errno));
        return false;
    }

    /**
     * accumulate number of cq events that need to
     * be acked, and periodically ack them
     */
    if (++cq_events_that_need_ack == MAX_ACK_EVENT) {
        ML(mct, info, "ack aq events.");
        ibv_ack_cq_events(cq, MAX_ACK_EVENT);
        cq_events_that_need_ack = 0;
    }

    return true;
}

CompletionQueue::~CompletionQueue(){
    if(cq){
        int r = ibv_destroy_cq(cq);
        if (r != 0)
            ML(mct, error, "failed to destroy cq: {}", cpp_strerror(errno));
        assert(r == 0);
    }
}

int CompletionQueue::init(){
    ibv_comp_channel *ibv_cc = nullptr;
    if(channel){
        ibv_cc = channel->get_channel();
    }
    cq = ibv_create_cq(infiniband.get_device()->ctxt, queue_depth, this, 
                                                                ibv_cc, 0);
    if (!cq) {
        ML(mct, error, "failed to create receive completion queue: {}", 
                                                        cpp_strerror(errno));
        return -1;
    }

    if(channel){
        if (ibv_req_notify_cq(cq, 0)) {
            ML(mct, error, "ibv_req_notify_cq failed: {}", cpp_strerror(errno));
            ibv_destroy_cq(cq);
            cq = nullptr;
            return -1;
        }
        
        channel->bind_cq(cq);
    }
    ML(mct, info, "successfully create cq={:p}", (void *)cq);
    return 0;
}

int CompletionQueue::rearm_notify(bool solicite_only){
    ML(mct, info, "");
    if(!channel) return 0;
    int r = ibv_req_notify_cq(cq, solicite_only?1:0);
    if (r < 0)
        ML(mct, error, "failed to notify cq: {}", cpp_strerror(errno));
    return r;
}

int CompletionQueue::poll_cq(int num_entries, ibv_wc *ret_wc_array) {
    int r = ibv_poll_cq(cq, num_entries, ret_wc_array);
    if (r < 0) {
        ML(mct, error, "poll_completion_queue occur met error: {}", 
                                                        cpp_strerror(errno));
        return -1;
    }
    return r;
}

ProtectionDomain::ProtectionDomain(MsgContext *c, ibv_pd* ipd)
:mct(c), pd(ipd){

}

ProtectionDomain::~ProtectionDomain(){
    if(pd){
        ibv_dealloc_pd(pd);
    }
}

ProtectionDomain *ProtectionDomain::create(MsgContext *mct, Device *device){
    auto pd = ibv_alloc_pd(device->ctxt);
    if(pd == nullptr){
        ML(mct, error, "failed to allocate infiniband protection domain: {}",
                                                        cpp_strerror(errno));
        return nullptr;
    }
    auto pd_ins = new ProtectionDomain(mct, pd);
    return pd_ins;
}

static std::atomic<bool> init_prereq = {false};

bool Infiniband::verify_prereq(MsgContext *mct) {
    init_prereq = true;
    //On RDMA MUST be called before fork
    int rc = ibv_fork_init();
    if (rc) {
        ML(mct, error, "failed to call ibv_for_init()."
                " On RDMA must be called before fork. Application aborts.");
        init_prereq = false;
        return false;
    }
    ML(mct, info, "rdma_enable_hugepage value is: {}", 
                                    mct->config->rdma_enable_hugepage);
    if(mct->config->rdma_enable_hugepage){
        rc =  setenv("RDMAV_HUGEPAGES_SAFE","1",1);
        ML(mct, info, "RDMAV_HUGEPAGES_SAFE is set as: {}", 
                                            getenv("RDMAV_HUGEPAGES_SAFE"));
        if(rc){
            ML(mct, error, "failed to export RDMA_HUGEPAGES_SAFE. "
                "On RDMA must be exported before using huge pages. "
                "Application aborts.");
            init_prereq = false;
            return false;
        }
    }
    //Check ulimit
    struct rlimit limit;
    getrlimit(RLIMIT_MEMLOCK, &limit);
    if (limit.rlim_cur != RLIM_INFINITY || limit.rlim_max != RLIM_INFINITY) {
        ML(mct, error, "!!! WARNING !!! For RDMA to work properly user memlock" 
            " (ulimit -l) must be big enough to allow large amount of "
            "registered memory. We recommend setting this parameter to "
            "infinity");
    }
    return true;
}

Infiniband::Infiniband(MsgContext *c)
:mct(c), m_lock(), max_sge(MAX_SHARED_RX_SGE_COUNT),
device_name(c->config->rdma_device_name),
port_num(c->config->rdma_port_num){

}

bool Infiniband::init(){
    bool ready = false;
    if(!init_prereq){
        ready = verify_prereq(mct);
    }
    if(!ready){
        ML(mct, error, "failed!");
        return false;
    }

    MutexLocker l(m_lock);

    if (initialized)
        return true;

    device_list = DeviceList::create(mct);
    if(!device_list) return false;
    initialized = true;
    NetHandler net_handler(mct);

    device = device_list->get_device(device_name.c_str());
    assert(device);
    device->binding_port(port_num);
    ib_physical_port = device->active_port->get_port_num();
    pd = ProtectionDomain::create(mct, device);
    if(!pd) return false;
    int _r = net_handler.set_nonblock(device->ctxt->async_fd);
    assert(_r == 0);

    enable_srq = mct->config->rdma_enable_srq;
    if(enable_srq){
        rx_queue_len = device->device_attr->max_srq_wr;
        ML(mct, info, "device max_srq_wr: {}", rx_queue_len);
    }else{
        rx_queue_len = device->device_attr->max_qp_wr;
        ML(mct, info, "device max_qp_wr: {}", rx_queue_len);
    }


    if(mct->config->rdma_recv_queue_len <= 0){
        ML(mct, warn, "rdma_recv_queue_len must > 0! now set {}", rx_queue_len);
    }else if (rx_queue_len > mct->config->rdma_recv_queue_len) {
        rx_queue_len = mct->config->rdma_recv_queue_len;
        ML(mct, info, "receive queue length: {} .", rx_queue_len);
    } else {
        ML(mct, info,"requested receive queue length {} is too big. Setting {}",
                        mct->config->rdma_recv_queue_len, rx_queue_len);
    }

    tx_queue_len = device->device_attr->max_qp_wr;
    if(mct->config->rdma_send_queue_len <= 0){
        ML(mct, warn, "rdma_send_queue_len must > 0! now set {}", tx_queue_len);
    }else if (tx_queue_len > mct->config->rdma_send_queue_len) {
        tx_queue_len = mct->config->rdma_send_queue_len;
        ML(mct, info, "send queue length: {} .", tx_queue_len);
    } else {
        ML(mct, info, "requested send queue length {} is too big. Setting {}",
                        mct->config->rdma_send_queue_len, tx_queue_len);
    }

    auto dev_attr = device->device_attr;
    //mct->config->rdma_buffer_num no use now.
    ML(mct, info, "device max_mr_size: {}, page_size_cap: {}", 
                    dev_attr->max_mr_size, dev_attr->page_size_cap);
    ML(mct, info, "device max_qp: {}, max_qp_wr:{}", 
                    dev_attr->max_qp, dev_attr->max_qp_wr);
    ML(mct, info, "device max_cq: {}, max_cqe: {}, max_mr: {}", 
                    dev_attr->max_cq, dev_attr->max_cqe, dev_attr->max_mr);
    ML(mct, info, "device max_srq: {}, max_srq_wr: {}", 
                    dev_attr->max_srq, dev_attr->max_srq_wr);
    ML(mct, info, "max inline data: {}", mct->config->rdma_max_inline_data);
    
    return true;
}

Infiniband::~Infiniband(){
    if (!initialized)
        return;
    delete pd;
    delete device_list;
}

/**
 * Create a shared receive queue. This basically wraps the verbs call. 
 *
 * \param[in] max_wr
 *      The max number of outstanding work requests in the SRQ.
 * \param[in] max_sge
 *      The max number of scatter elements per WR.
 * \return
 *      A valid ibv_srq pointer, or NULL on error.
 */
ibv_srq* Infiniband::create_shared_receive_queue(uint32_t max_wr){
  ibv_srq_init_attr sia;
  memset(&sia, 0, sizeof(sia));
  sia.srq_context = device->ctxt;
  sia.attr.max_wr = max_wr;
  sia.attr.max_sge = max_sge;
  auto new_srq = ibv_create_srq(pd->pd, &sia);
  if(!new_srq){
      ML(mct, error, "failed.");
  }
  return new_srq;
}

/**
 * Create a new QueuePair. This factory should be used in preference to
 * the QueuePair constructor directly, since this lets derivatives of
 * Infiniband, e.g. MockInfiniband (if it existed),
 * return mocked out QueuePair derivatives.
 *
 * \return
 *      QueuePair on success or NULL if init fails
 * See QueuePair::QueuePair for parameter documentation.
 */
QueuePair* Infiniband::create_queue_pair(MsgContext *mct, 
                                            CompletionQueue *tx, 
                                            CompletionQueue* rx, 
                                            ibv_srq *srq,
                                            ibv_qp_type type){
    QueuePair *qp = new QueuePair(mct, *this, type, ib_physical_port, 
                                    srq, tx, rx, tx_queue_len, rx_queue_len);
    if (qp->init()) {
        delete qp;
        return nullptr;
    }
    return qp;
}

CompletionChannel *Infiniband::create_comp_channel(MsgContext *c){
    auto cc = new CompletionChannel(c, *this);
    if(cc->init()){
        delete cc;
        return nullptr;
    }
    return cc;
}

CompletionQueue *Infiniband::create_comp_queue(MsgContext *mct, 
                                                    CompletionChannel *cc){
    CompletionQueue *cq = new CompletionQueue(mct, *this, CQ_DEPTH, cc);
    if(cq->init()){
        delete cq;
        return nullptr;
    }
    return cq;
}

int Infiniband::encode_msg(MsgContext *mct, IBSYNMsg& im, MsgBuffer &buffer){
    if(buffer.length() < TCP_MSG_LEN){
        return -1;
    }
    char gid[33];
    gid_to_wire_gid(&(im.gid), gid);
    sprintf(buffer.data(), "%04x:%08x:%08x:%08x:%02x:%s", 
                            im.lid, im.qpn, im.psn, im.peer_qpn, im.sl, gid);
    ML(mct, info, "{}, {}, {}, {}, {}, {}", 
                            im.lid, im.qpn, im.psn, im.peer_qpn, im.sl, gid);
    buffer.set_offset(TCP_MSG_LEN);
    return TCP_MSG_LEN;
}
int Infiniband::decode_msg(MsgContext *mct, IBSYNMsg& im, MsgBuffer &buffer){
    if(buffer.offset() < TCP_MSG_LEN){
        return -1;
    }
    char gid[33];
    sscanf(buffer.data(), "%hu:%x:%x:%x:%hhx:%s", &(im.lid), &(im.qpn), 
                                    &(im.psn), &(im.peer_qpn), &(im.sl), gid);
    wire_gid_to_gid(gid, &(im.gid));
    ML(mct, info, "{}, {}, {}, {}, {}, {}", 
                        im.lid, im.qpn, im.psn, im.peer_qpn, im.sl, gid);
    return TCP_MSG_LEN;
}
int Infiniband::get_ib_syn_msg_len(){
    return TCP_MSG_LEN;
}

void Infiniband::wire_gid_to_gid(const char *wgid, union ibv_gid *gid){
    char tmp[9];
    uint32_t v32;
    int i;

    for (tmp[8] = 0, i = 0; i < 4; ++i) {
        memcpy(tmp, wgid + i * 8, 8);
        sscanf(tmp, "%x", &v32);
        *(uint32_t *)(&gid->raw[i * 4]) = ntohl(v32);
    }
}

void Infiniband::gid_to_wire_gid(const union ibv_gid *gid, char wgid[]){
    for (int i = 0; i < 4; ++i)
        sprintf(&wgid[i * 8], "%08x", htonl(*(uint32_t *)(gid->raw + i * 4)));
}


/**
 * Given a string representation of the `status' field from Verbs
 * struct `ibv_wc'.
 *
 * \param[in] status
 *      The integer status obtained in ibv_wc.status.
 * \return
 *      A string corresponding to the given status.
 */
const char* Infiniband::wc_status_to_string(int status){
    static const char *lookup[] = {
        "SUCCESS",
        "LOC_LEN_ERR",
        "LOC_QP_OP_ERR",
        "LOC_EEC_OP_ERR",
        "LOC_PROT_ERR",
        "WR_FLUSH_ERR",
        "MW_BIND_ERR",
        "BAD_RESP_ERR",
        "LOC_ACCESS_ERR",
        "REM_INV_REQ_ERR",
        "REM_ACCESS_ERR",
        "REM_OP_ERR",
        "RETRY_EXC_ERR",
        "RNR_RETRY_EXC_ERR",
        "LOC_RDD_VIOL_ERR",
        "REM_INV_RD_REQ_ERR",
        "REM_ABORT_ERR",
        "INV_EECN_ERR",
        "INV_EEC_STATE_ERR",
        "FATAL_ERR",
        "RESP_TIMEOUT_ERR",
        "GENERAL_ERR"
    };

    if (status < IBV_WC_SUCCESS || status > IBV_WC_GENERAL_ERR)
        return "<status out of range!>";
    return lookup[status];
}

const char* Infiniband::qp_state_string(int status) {
    switch(status) {
        case IBV_QPS_RESET : return "IBV_QPS_RESET";
        case IBV_QPS_INIT  : return "IBV_QPS_INIT";
        case IBV_QPS_RTR   : return "IBV_QPS_RTR";
        case IBV_QPS_RTS   : return "IBV_QPS_RTS";
        case IBV_QPS_SQD   : return "IBV_QPS_SQD";
        case IBV_QPS_SQE   : return "IBV_QPS_SQE";
        case IBV_QPS_ERR   : return "IBV_QPS_ERR";
        default: return "<qp_state out of range>";
    }
}

const char* Infiniband::wc_opcode_string(int oc){
    switch(oc){
        case IBV_WC_SEND       : return "IBV_WC_SEND";
        case IBV_WC_RDMA_WRITE : return "IBV_WC_RDMA_WRITE";
        case IBV_WC_RDMA_READ  : return "IBV_WC_RDMA_READ";
        case IBV_WC_COMP_SWAP  : return "IBV_WC_COMP_SWAP";
        case IBV_WC_FETCH_ADD  : return "IBV_WC_FETCH_ADD";
        case IBV_WC_BIND_MW    : return "IBV_WC_BIND_MW";
        case IBV_WC_LOCAL_INV  : return "IBV_WC_LOCAL_INV";
        // case IBV_WC_TSO        : return "IBV_WC_TSO";

        case IBV_WC_RECV       : return "IBV_WC_RECV";
        case IBV_WC_RECV_RDMA_WITH_IMM : return "IBV_WC_RECV_RDMA_WITH_IMM";

        // case IBV_WC_TM_ADD     : return "IBV_WC_TM_ADD";
        // case IBV_WC_TM_DEL     : return "IBV_WC_TM_DEL";
        // case IBV_WC_TM_SYNC    : return "IBV_WC_TM_SYNC";
        // case IBV_WC_TM_RECV    : return "IBV_WC_TM_RECV";
        // case IBV_WC_TM_NO_TAG  : return "IBV_WC_TM_NO_TAG";
        default: return "<wc_opcode out of range>";
    }
}

const char* Infiniband::wr_opcode_string(int oc){
    static const char* lookup[] =  {
        "IBV_WR_RDMA_WRITE",
        "IBV_WR_RDMA_WRITE_WITH_IMM",
        "IBV_WR_SEND",
        "IBV_WR_SEND_WITH_IMM",
        "IBV_WR_RDMA_READ",
        "IBV_WR_ATOMIC_CMP_AND_SWP",
        "IBV_WR_ATOMIC_FETCH_AND_ADD",
        "IBV_WR_LOCAL_INV",
        "IBV_WR_BIND_MW",
        "IBV_WR_SEND_WITH_INV",
        // "IBV_WR_TSO",
    };
    if(oc < IBV_WR_RDMA_WRITE || oc > IBV_WR_SEND_WITH_INV){
        return "<wr_opcode out of range>";
    }
    return lookup[oc];
}

enum ibv_mtu Infiniband::ibv_mtu_enum(int mtu){
    switch(mtu){
    case 256:
        return IBV_MTU_256;
    case 512:
        return IBV_MTU_512;
    case 1024:
        return IBV_MTU_1024;
    case 2048:
        return IBV_MTU_2048;
    case 4096:
        return IBV_MTU_4096;
    default:
        return IBV_MTU_1024;
    }
}

} //namespace ib
} //namespace msg
} //namespace flame