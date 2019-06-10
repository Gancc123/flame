#ifndef FLAME_UTIL_LOG_HELPER_H
#define FLAME_UTIL_LOG_HELPER_H

#include "util/fmt.h"

namespace flame{

enum class util_log_level_t : int8_t{
    dead      = 0,
    critical  = 1,
    wrong     = 2,

    error     = 3,
    warn      = 4,
    info      = 5,

    debug     = 6,
    trace     = 7,
    print     = 8
};

}//namespace flame

#define UTIL_LOG_HELPER_LOG_COMMON(level, s) l##level("%s", s)

#define FL(fct, level, fmt_, ...)\
    do{\
        if((fct) && \
            (int)flame::util_log_level_t::level <= (fct)->log()->get_level())\
            (fct)->log()->UTIL_LOG_HELPER_LOG_COMMON(level, \
                                    fmt::format(fmt_, ## __VA_ARGS__).c_str());\
    }while(0)

#endif//FLAME_UTIL_LOG_HELPER_H