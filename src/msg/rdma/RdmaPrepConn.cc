/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-06 16:52:47
 */
#include "RdmaPrepConn.h"
#include "RdmaListenPort.h"
#include "RdmaConnection.h"
#include "RdmaStack.h"
#include "msg/msg_def.h"
#include "msg/NetHandler.h"
#include "msg/Stack.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <cassert>

namespace flame{
namespace msg{

RdmaPrepConn::RdmaPrepConn(MsgContext *mct)
:EventCallBack(mct, FLAME_EVENT_READABLE | FLAME_EVENT_WRITABLE),
 my_msg_buffer(ib::Infiniband::get_ib_syn_msg_len()),
 peer_msg_buffer(ib::Infiniband::get_ib_syn_msg_len()){
     status = PrepStatus::INIT;
}

int RdmaPrepConn::connect(MsgContext *mct, NodeAddr *addr){
    int cfd, r;
    NetHandler net_handler(mct);
    auto family = addr->get_family();
    cfd = net_handler.create_socket(family, true);
    if(cfd < 0){
        throw ErrnoException(-cfd);
    }

    r = net_handler.set_nonblock(cfd);
    if (r < 0) {
        ::close(cfd);
        throw ErrnoException(-r);
    }

    net_handler.set_close_on_exec(cfd);
    r = net_handler.set_socket_options(cfd, false, 0);
    if (r < 0) {
        ::close(cfd);
        throw ErrnoException(-r);
    }

    r = ::connect(cfd, addr->get_sockaddr(), addr->get_sockaddr_len());
    if(r < 0){
        r = errno;
        if(r == EINPROGRESS || r == EALREADY || r== EISCONN || r == ETIMEDOUT){
            ML(mct, debug, "connect to [{} {}] : {}", 
                                        addr->ip_to_string(), addr->get_port(),
                                        cpp_strerror(r));
            return cfd;
        }
        ML(mct, error, "unable to connect to [{} {}] : {}",
                        addr->ip_to_string(), addr->get_port(),
                        cpp_strerror(r));
        ::close(cfd);
        throw ErrnoException(r);
    }
    return cfd;
}

RdmaPrepConn *RdmaPrepConn::create(MsgContext *mct, int cfd){
    auto prep_conn = new RdmaPrepConn(mct);
    prep_conn->fd = cfd;

    auto rdma_worker = 
        Stack::get_rdma_stack()->get_manager()->get_lightest_load_rdma_worker();
    auto real_conn = RdmaConnection::create(mct, rdma_worker);
    if(!real_conn){
        return nullptr;
    }  
    prep_conn->real_conn = real_conn;

    return prep_conn;
}

RdmaPrepConn *RdmaPrepConn::create(MsgContext *mct, NodeAddr *addr, uint8_t sl){
    try{
        int fd = RdmaPrepConn::connect(mct, addr);
        auto conn = new RdmaPrepConn(mct);
        conn->fd = fd;

        auto rdma_worker = Stack::get_rdma_stack()->get_manager()
                                            ->get_lightest_load_rdma_worker();
        auto real_conn = RdmaConnection::create(mct, rdma_worker, sl);
        if(!real_conn){
            conn->put();
            return nullptr;
        }  
        conn->real_conn = real_conn;

        ib::Infiniband &ib = Stack::get_rdma_stack()->get_rdma_manager()->get_ib();
        auto &my_msg = real_conn->get_my_msg();
        my_msg.peer_qpn = 0;

        int _r = ib.encode_msg(mct, my_msg, conn->my_msg_buffer);
        assert(_r == conn->my_msg_buffer.length());

        return conn;
    } catch (ErrnoException &e){
        return nullptr;
    }
    return nullptr;
}

RdmaPrepConn::~RdmaPrepConn(){
    if(lp){
        lp->put();
        lp = nullptr;
    }
    if(real_conn){
        real_conn->put();
        real_conn = nullptr;
    }
}

void RdmaPrepConn::set_rdma_listen_port(RdmaListenPort *lp){
    if(this->lp){
        this->lp->put();
    }
    if(lp){
        lp->get();
    }
    this->lp = lp;
}

int RdmaPrepConn::send_my_msg(){
    int r = 0, write_len = 0, total_bytes = 0;
    if(my_msg_buffer_offset == my_msg_buffer.offset()){
        return 0;
    }
    while(my_msg_buffer_offset < my_msg_buffer.offset()){
        r = ::send(fd, my_msg_buffer.data() + my_msg_buffer_offset, 
                                my_msg_buffer.offset() - my_msg_buffer_offset,
                                MSG_NOSIGNAL);
        write_len = r;
        r = -errno;
        if(write_len >= 0){
            my_msg_buffer_offset += write_len;
            total_bytes += write_len;
        }else if (r == -EINTR) {
            continue;
        } else if (r == -ENOBUFS || r == -EAGAIN || r == -EWOULDBLOCK) {
            break;
        } else if (r == -ECONNRESET) {
            status = PrepStatus::CLOSED;
            ML(mct, error, "it was closed because of rst arrived sd = {}"
                            " errno {} {}", this->fd, r, cpp_strerror(r));
            return -1;
        } else {
            ML(mct, error, "write error? errno {} {}", r, cpp_strerror(r));
            break;
        }
    }
    ML(mct, trace, "RdmaPrepConn {} send {} bytes, msg_buffer {} bytes.",
            fd, total_bytes, my_msg_buffer.length());
    return total_bytes;
}

int RdmaPrepConn::recv_peer_msg(){
    int r = 0, recv_len = 0, total_bytes = 0;
    if(peer_msg_buffer_offset == peer_msg_buffer.length()){
        return 0;
    }
    while(peer_msg_buffer_offset < peer_msg_buffer.length()){
        r = ::recv(fd, peer_msg_buffer.data() + peer_msg_buffer_offset, 
                            peer_msg_buffer.length() - peer_msg_buffer_offset,
                            MSG_NOSIGNAL);
        recv_len = r;
        r = -errno;
        if(recv_len == 0){
            ML(mct, error, "RdmaPrepConn recv 0 bytes, close the conn.");
            this->get();
            this->error_cb();
            this->put();
            break;
        }else if(recv_len > 0){
            peer_msg_buffer_offset += recv_len;
            peer_msg_buffer.set_offset(peer_msg_buffer_offset);
            total_bytes += recv_len;
        }else if (r == -EINTR) {
            continue;
        } else if (r == -ENOBUFS || r == -EAGAIN || r == -EWOULDBLOCK) {
            break;
        } else if (r == -ECONNRESET) {
            status = PrepStatus::CLOSED;
            ML(mct, error, "it was closed because of rst arrived sd = {}"
                            " errno {} {}", this->fd, r, cpp_strerror(r));
            break;
        } else {
            ML(mct, error, "write error? errno {} {}", r, cpp_strerror(r));
            break;
        }
    }
    ML(mct, trace, "RdmaPrepConn {} recv {} bytes, msg_buffer {} bytes",
            fd, total_bytes, peer_msg_buffer.length());
    return total_bytes;

}

void RdmaPrepConn::read_cb(){
    ML(mct, trace, "RdmaPrepConn status:{}", prep_status_str(status));
    ib::Infiniband &ib = Stack::get_rdma_stack()->get_rdma_manager()->get_ib();
    if(!is_server()){
        if(status == PrepStatus::SYNED_MY_MSG){
            recv_peer_msg();
            if(peer_msg_buffer.full()){
                status = PrepStatus::SYNED_PEER_MSG;
                auto &peer_msg = real_conn->get_peer_msg();
                int _r = ib.decode_msg(mct, peer_msg, peer_msg_buffer);
                assert(_r == peer_msg_buffer.length());
                peer_msg_buffer_offset = 0;
                auto &my_msg = real_conn->get_my_msg();
                my_msg.peer_qpn = peer_msg.qpn;
                if(my_msg.qpn != peer_msg.peer_qpn){
                    ML(mct, error, "syn failed: "
                                    "peer_msg.peer_qpn != my_msg.qpn");
                    this->close();
                    return;
                }
                assert(my_msg_buffer_offset == my_msg_buffer.length());
                _r = ib.encode_msg(mct, my_msg, my_msg_buffer);
                assert(_r == my_msg_buffer.length());
                my_msg_buffer_offset = 0;
                
                //activate client rdma connection
                if(real_conn->activate()){
                    ML(mct, error, "RdmaConn activate() failed."
                                    " Close related RdmaPrepConn.");
                    this->close();
                }else{
                    send_my_msg();
                }
            }else{
                return;
            }
        }else{
            return;
        }
    }else{
        if(status == PrepStatus::INIT){
            recv_peer_msg();
            if(peer_msg_buffer.full()){
                status = PrepStatus::SYNED_PEER_MSG;
                auto &peer_msg = real_conn->get_peer_msg();
                int _r = ib.decode_msg(mct, peer_msg, peer_msg_buffer);
                assert(_r == peer_msg_buffer.offset());
                peer_msg_buffer_offset = 0;
                auto &my_msg = real_conn->get_my_msg();
                my_msg.peer_qpn = peer_msg.qpn;
                my_msg.sl = peer_msg.sl;
                _r = ib.encode_msg(mct, my_msg, my_msg_buffer);
                assert(_r == my_msg_buffer.length());
                my_msg_buffer_offset = 0;

                //activate server rdma connection
                if(real_conn->activate()){
                    ML(mct, error, "RdmaConn activate() failed."
                                    " Close related RdmaPrepConn.");
                    this->close();
                }else{
                    send_my_msg();
                }
            }
        }else if(status == PrepStatus::SYNED_MY_MSG){
            recv_peer_msg();
            if(peer_msg_buffer.full()){
                status = PrepStatus::ACKED;
                auto &peer_msg = real_conn->get_peer_msg();
                int _r = ib.decode_msg(mct, peer_msg, peer_msg_buffer);
                assert(_r == peer_msg_buffer.offset());
                peer_msg_buffer_offset = 0;
                auto &my_msg = real_conn->get_my_msg();
                if(peer_msg.peer_qpn != my_msg.qpn){
                    ML(mct, error, "syn failed: "
                                    "peer_msg.peer_qpn != my_msg.qpn");
                    this->close();
                    return;
                }else{
                    ML(mct, trace, 
                                "Prep Server finished. Close the prep conn.");
                    ML(mct, trace, "my_msg: {} {} {} {}",
                        my_msg.lid, my_msg.qpn, my_msg.psn, my_msg.sl);
                    ML(mct, trace, "peer_msg: {} {} {} {}",
                        peer_msg.lid, peer_msg.qpn, peer_msg.psn, peer_msg.sl);
                    //Prep finished. close the prep conn.
                    this->close();
                    ML(mct, trace, "RdmaConn qp state: {}",
                                        ib::Infiniband::qp_state_string(
                                            real_conn->get_qp()->get_state()));
                    return;
                }
            }else{
                return;
            }
        }else{
            return;
        }
    }
    
}

void RdmaPrepConn::write_cb(){
    ML(mct, trace, "RdmaPrepConn status:{}", prep_status_str(status));
    if(!is_server()){
        if(status == PrepStatus::INIT){
            send_my_msg();
            if(my_msg_buffer_offset == my_msg_buffer.offset()){
                status = PrepStatus::SYNED_MY_MSG;
            }else{
                return;
            }
        }else if(status == PrepStatus::SYNED_PEER_MSG){
            send_my_msg();
            if(my_msg_buffer_offset == my_msg_buffer.offset()){
                status = PrepStatus::ACKED;
                ML(mct, trace, "Prep client finished. Close the prep conn.");
                auto &peer_msg = real_conn->get_peer_msg();
                auto &my_msg = real_conn->get_my_msg();
                ML(mct, trace, "my_msg: {} {} {} {}",
                    my_msg.lid, my_msg.qpn, my_msg.psn, my_msg.sl);
                ML(mct, trace, "peer_msg: {} {} {} {}",
                    peer_msg.lid, peer_msg.qpn, peer_msg.psn, peer_msg.sl);
                this->close();
            }else{
                return;
            }
        }else{
            return;
        }
    }else{
        if(status == PrepStatus::SYNED_PEER_MSG){
            send_my_msg();
            if(my_msg_buffer_offset == my_msg_buffer.offset()){
                status = PrepStatus::SYNED_MY_MSG;
            }else{
                return;
            }
        }else{
            return;
        }
    }
}

void RdmaPrepConn::close_rdma_conn_if_need(){
    // close rdma conn when prep conn failed.
    if(!real_conn->is_connected()){
        ML(mct, warn, "close RdmaConn {:p}", (void *)real_conn);
        real_conn->close();
    }
}

void RdmaPrepConn::error_cb(){
    int so_error = 0;
    socklen_t len = sizeof(so_error);
    ::getsockopt(this->fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
    this->close();
    ML(mct, error, "RdmaPrepConn status: {} [fd: {}] closed: {}", 
                        prep_status_str(status),
                        this->fd,
                        cpp_strerror(so_error));
}

void RdmaPrepConn::close(){
    int tmp_fd = fd;
    Stack::get_rdma_stack()->on_rdma_prep_conn_close(this);
    status = PrepStatus::CLOSED;
    close_rdma_conn_if_need();
    get_owner()->del_event(this->fd);
    ::close(tmp_fd);
}


std::string RdmaPrepConn::prep_status_str(PrepStatus s){
    switch(s){
    case PrepStatus::CLOSED:
        return "closed";
    case PrepStatus::INIT:
        return "init";
    case PrepStatus::SYNED_MY_MSG:
        return "syned_my_msg";
    case PrepStatus::SYNED_PEER_MSG:
        return "syned_peer_msg";
    case PrepStatus::ACKED:
        return "acked";
    default:
        return "invalid prep status";
    }
}

} //namespace msg
} //namespace flame