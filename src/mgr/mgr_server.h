#ifndef FLAME_SERVICE_MGR_SERVER_H
#define FLAME_SERVICE_MGR_SERVER_H

#include "mgr_context.h"

#include <grpcpp/grpcpp.h>
#include <memory>
#include <list>

#include "service/flame_service.h"
#include "service/internal_service.h"
#include "service/libflame_service.h"

namespace flame {

class MgrServer {
public:
    MgrServer(MgrContext* mct, const std::string& addr) 
    : mct_(mct), addr_(addr), flame_service_(mct), internal_service_(mct), libflame_service_(mct) { 
        __init(); 
    }

    ~MgrServer() {}

    std::string get_addr() const { return addr_; }
    void set_addr(const std::string& addr) { addr_ = addr; }
    
    int run();

private:
    void __init();

    MgrContext* mct_;
    std::string addr_;

    service::FlameServiceImpl flame_service_;
    service::InternalServiceImpl internal_service_;
    service::LibFlameServiceImpl libflame_service_;

    grpc::ServerBuilder builder_;
    std::unique_ptr<grpc::Server> server_;
}; // class MgrServer

} // namespace flame

#endif // FLAME_SERVICE_MGR_SERVER_H