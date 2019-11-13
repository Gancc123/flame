#include "TcpConnection.h"
#include "TcpStack.h"
#include "msg/internal/errno.h"
#include "msg/internal/types.h"
#include "msg/NetHandler.h"
#include "msg/msg_def.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cassert>

#define FLAME_MSG_IOV_MAX (8)

namespace flame{
namespace msg{

void TcpConnection::read_cb(){
    if(!this->is_connected() || !this->get_owner()->am_self()){
        return;
    }
    ConnectionListener *listener = this->get_listener();
    assert(listener != nullptr);
    while(true){
        auto msg = this->recv_msg();
        if(msg){
            if(listener){
                listener->on_conn_recv(this, msg);
            }
            msg->put();
        }else{
            break;
        }
    }
}

void TcpConnection::write_cb(){
    if(!this->is_connected() || !this->get_owner()->am_self()){
        return;
    }
    this->can_write = TcpConnection::WriteStatus::CANWRITE;
    this->send_msg(nullptr);
}

void TcpConnection::error_cb(){
    this->can_write = TcpConnection::WriteStatus::CLOSED;
    int so_error = 0;
    socklen_t len = sizeof(so_error);
    ::getsockopt(this->get_fd(), SOL_SOCKET, SO_ERROR, &so_error, &len);
    if(so_error != EAGAIN){
        ML(mct, info, "connection {}{} closed : {}", 
                                msg_ttype_to_str(get_id().type), get_id().id,
                                cpp_strerror(so_error));
        this->get_listener()->on_conn_error(this);
        return;
    }
}

int TcpConnection::connect(MsgContext *mct, NodeAddr *addr){
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

TcpConnection::TcpConnection(MsgContext *mct)
    :Connection(mct), msg_list_mutex(MUTEX_TYPE_DEFAULT), 
        cur_msg(msg_list.begin()), 
        cur_msg_header_buffer(sizeof(flame_msg_header_t)),
        cur_msg_offset(0),
        cur_recv_msg(nullptr),
        recv_header_buffer(sizeof(flame_msg_header_t)),
        cur_recv_msg_offset(0),
        send_msg_posted(false){
    this->can_write =  TcpConnection::WriteStatus::CANWRITE;
}

TcpConnection::~TcpConnection() {
        //* host_addr, remote_addr 由dispatcher释放
        ::close(this->get_fd());
        cur_msg = msg_list.end();
        while(!msg_list.empty()){
            auto msg = msg_list.front();
            msg->put();
            msg_list.pop_front();
        }
        if(cur_recv_msg){
            cur_recv_msg->put();
            cur_recv_msg = nullptr;
        }
    }

TcpConnection *TcpConnection::create(MsgContext *mct, int fd){
    auto conn = new TcpConnection(mct);
    conn->set_fd(fd);
    conn_id_t conn_id;
    conn_id.type = msg_ttype_t::TCP;
    conn_id.id = (uint32_t)fd;
    conn->set_id(conn_id);
    return conn;
}

TcpConnection *TcpConnection::create(MsgContext *mct, NodeAddr *addr){
    NetHandler net_handler(mct);
    try{
        int fd = TcpConnection::connect(mct, addr);
        auto conn = new TcpConnection(mct);
        conn->set_fd(fd);
        conn_id_t conn_id;
        conn_id.type = msg_ttype_t::TCP;
        conn_id.id = (uint32_t)fd;
        conn->set_id(conn_id);
        return conn;
    } catch (ErrnoException &e){
        return nullptr;
    }
}

void TcpConnection::append_send_msg(Msg *msg){
    msg->get();
    bool need_encode_header = false;
    std::list<Msg *> new_list;
    new_list.push_back(msg);
    msg_list_mutex.lock();
    msg_list.splice(msg_list.end(), new_list);
    if(cur_msg == msg_list.end()){
        cur_msg--;
        need_encode_header = true;
    }
    msg_list_mutex.unlock();
    if(need_encode_header){
        cur_msg_offset = 0;
        (*cur_msg)->encode_header(cur_msg_header_buffer);
        msg_data_iter = (*cur_msg)->data_iter();
    }
}

// return the sent length
// < 0 means error occurred
ssize_t TcpConnection::do_sendmsg(int fd, struct msghdr &msg, 
                                                       unsigned len, bool more){
    size_t sent = 0;
    while (1) {
        sigpipe_stopper stopper();
        ssize_t r;
        r = ::sendmsg(fd, &msg, MSG_NOSIGNAL | (more ? MSG_MORE : 0));
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }

        sent += r;
        if (len == sent) break;

        while (r > 0) {
            if (msg.msg_iov[0].iov_len <= (size_t)r) {
                // drain this whole item
                r -= msg.msg_iov[0].iov_len;
                msg.msg_iov++;
                msg.msg_iovlen--;
            } else {
                msg.msg_iov[0].iov_base = (char *)msg.msg_iov[0].iov_base + r;
                msg.msg_iov[0].iov_len -= r;
                break;
            }
        }
    }
    return (ssize_t)sent;
}

ssize_t TcpConnection::submit_cur_msg(bool more){
    auto msg_p = *cur_msg;
    struct msghdr mh;
    struct iovec iov[FLAME_MSG_IOV_MAX];
    int iov_index = 0;
    uint32_t total_bytes = 0;
    
    if(cur_msg_offset < cur_msg_header_buffer.offset()){
        iov[iov_index].iov_base = cur_msg_header_buffer.data() + cur_msg_offset;
        iov[iov_index].iov_len = cur_msg_header_buffer.offset() 
                                                               - cur_msg_offset;
        ++iov_index;
        total_bytes += cur_msg_header_buffer.offset() - cur_msg_offset;
    }

    if(cur_msg_offset < msg_p->total_bytes()){
        char *buffer;
        int cd_len;
        while(iov_index < FLAME_MSG_IOV_MAX){
            buffer = msg_data_iter.cur_data_buffer(cd_len);
            if(!buffer) break;
            iov[iov_index].iov_base = buffer;
            iov[iov_index].iov_len = cd_len;
            ++iov_index;
            total_bytes += cd_len;
            msg_data_iter.cur_data_buffer_extend(cd_len);
        }
    }


    memset(&mh, 0, sizeof(mh));
    mh.msg_iov = iov;
    mh.msg_iovlen = iov_index;

    ssize_t r = do_sendmsg(this->get_fd(), mh, total_bytes, more);

    if(r < 0) return r;

    if(r < total_bytes){
        // back bytes of msg_data_iter
        msg_data_iter.advance(-(int64_t)(total_bytes - r));
    }
    cur_msg_offset += r;

    return r;
}


ssize_t TcpConnection::send_msg(Msg *msg, bool more){
    ssize_t r, write_len;
    ssize_t total = 0;
    if(msg){
        this->append_send_msg(msg);
    }
    if(!this->get_owner()->am_self()){
        if(!send_msg_posted){
            send_msg_posted = true;
            // avoid conn released before posted work called.
            this->get();
            this->get_owner()->post_work([this](){
                ML(this->mct, trace, "in send_msg()");
                this->send_msg(nullptr);
                this->put();
            });
        }
        return 0;
    }
    send_msg_posted = false;
    if(can_write.load() !=  TcpConnection::WriteStatus::CANWRITE){
        return 0;
    }
    
    while(!cur_msg_is_end()){
        auto msg_p = *cur_msg;

        more = more?true:(msg_p != msg_list.back());
        r = submit_cur_msg(more);

        ML(mct, trace, "{} total: {}B, actually send: {}B", 
                    msg_p->to_string(), msg_p->total_bytes(), cur_msg_offset);


        if(cur_msg_offset >= msg_p->total_bytes()){
            cur_msg++;
            cur_msg_offset = 0;
            if(!cur_msg_is_end()){
                (*cur_msg)->encode_header(cur_msg_header_buffer);
                msg_data_iter = (*cur_msg)->data_iter();
            }
        }

        if(r >= 0){
            total += r;
        } else if (r == -ENOBUFS || r == -EAGAIN || r == -EWOULDBLOCK) {
            can_write =  TcpConnection::WriteStatus::NOWRITE;
            break;
        } else if (r == -ECONNRESET) {
            can_write =  TcpConnection::WriteStatus::CLOSED;
            ML(mct, error, "it was closed because of rst arrived sd =  {}"
                " errno {} {}", this->get_fd(), r, cpp_strerror(r));
            total = -1;
            break;
        } else {
            ML(mct, error, "write error? errno {} {}", r, cpp_strerror(r));
            total = -1;
            break;
        }
    }

    MutexLocker l(msg_list_mutex);
    for(auto it = msg_list.begin();it != cur_msg;){
        (*it)->put();
        it = msg_list.erase(it);
    }
    return total;
}

Msg* TcpConnection::recv_msg() {
    if(!this->get_owner()->am_self()){
        return nullptr;
    }
    ssize_t r, read_len;
    if(!cur_recv_msg){
        cur_recv_msg = Msg::alloc_msg(this->mct);
        cur_recv_msg->ttype = msg_ttype_t::TCP;
        cur_recv_msg_offset = 0;
    }
    while(true){
        size_t recv_data_offset = cur_recv_msg_offset 
                                                - recv_header_buffer.length();
        if(recv_header_buffer.full()){
            if(cur_recv_msg->decode_header(recv_header_buffer) < 0){
                ML(mct, error, "error when recv msg on [{} {}]", 
                                msg_ttype_to_str(get_id().type), get_id().id);
                this->get_listener()->on_conn_error(this);
                return nullptr;
            }
        }
        if(cur_recv_msg_offset < recv_header_buffer.length()){
            r = ::recv(this->get_fd(), 
                        recv_header_buffer.data() + cur_recv_msg_offset, 
                        recv_header_buffer.length() - cur_recv_msg_offset,
                        MSG_DONTWAIT);
        }else if(recv_data_offset < cur_recv_msg->data_len){
            char* buffer;
            int cd_len;
            buffer = cur_recv_msg->cur_data_buffer(cd_len);
            if(buffer){
                r = ::recv(this->get_fd(), buffer, cd_len, MSG_DONTWAIT);
            }
        }else{
            ML(mct, trace, "{} total: {}B, actually recv: {}B",  
                this->cur_recv_msg->to_string(), 
                this->cur_recv_msg->data_len + FLAME_MSG_HEADER_LEN,
                cur_recv_msg_offset);
            cur_recv_msg_offset = 0;
            Msg *ok_msg = this->cur_recv_msg;
            this->cur_recv_msg = nullptr;
            return ok_msg;
        }
        read_len = r;
        r = -errno;
        if(read_len == 0){  // connection closed!
            this->error_cb();
            return nullptr;
        } else if (read_len > 0){
            if(cur_recv_msg_offset >= recv_header_buffer.length()){
                cur_recv_msg->cur_data_buffer_extend(read_len);
            }else{
                recv_header_buffer.set_offset(cur_recv_msg_offset + read_len);
            }
            cur_recv_msg_offset += read_len;
        } else if (r == -EINTR) {
            continue;
        } else if (r == -ENOBUFS || r == -EAGAIN || r == -EWOULDBLOCK) {
            break;
        } else if (r == -ECONNRESET) {
            can_write = TcpConnection::WriteStatus::CLOSED;
            ML(mct, error, "it was closed because of rst arrived sd =  {}"
                " errno {} {}", this->get_fd(), r, cpp_strerror(r));
            break;
        } else {
            ML(mct, error, "write error? errno {} {}", r, cpp_strerror(r));
            break;
        }
    }
    return nullptr;
}

bool TcpConnection::is_connected(){
    return can_write !=  TcpConnection::WriteStatus::CLOSED;
}

void TcpConnection::close(){
    this->get(); // prevent release when this func hasn't return.
    this->can_write = TcpConnection::WriteStatus::CLOSED;
    this->get_listener()->on_conn_error(this);
    this->put();
}

} //namespace msg
} //namespace flame