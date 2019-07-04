#include "mgr/mgr_server.h"

namespace flame {

// bool MgrServer::register_service(MgrService* service) {
//     service_list_.push_back(service)
// }

int MgrServer::run() {
    server_ = builder_.BuildAndStart();
    server_->Wait();
    return 0;
}

void MgrServer::__init() {
    builder_.AddListeningPort(addr_, grpc::InsecureServerCredentials());

    builder_.RegisterService(&flame_service_);
    builder_.RegisterService(&internal_service_);
    builder_.RegisterService(&libflame_service_);
}

} // namespace flame