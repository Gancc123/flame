#ifndef FLAME_TESTS_ALLOCATOR_H
#define FLAME_TESTS_ALLOCATOR_H

#include "msg/msg_core.h"

namespace flame{
namespace msg{

class RdmaMsger : public MsgerCallback{
    MsgContext *mct;
public:
    explicit RdmaMsger(MsgContext *c) 
    : mct(c) {};
};

} //namespace msg
} //namespace flame

#endif 