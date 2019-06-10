#ifndef FLAME_MEMORY_LOG_H
#define FLAME_MEMORY_LOG_H

#include "common/log.h"

#ifdef ldead
#undef ldead
#define ldead(fmt, arg...) plog(0, "memory", __FILE__, __LINE__, __func__, (fmt), ##arg)

#undef lcritical
#define lcritical(fmt, arg...) plog(1, "memory", __FILE__, __LINE__, __func__, (fmt), ##arg)

#undef lwrong
#define lwrong(fmt, arg...) plog(2, "memory", __FILE__, __LINE__, __func__, (fmt), ##arg)

#undef lerror
#define lerror(fmt, arg...) plog(3, "memory", __FILE__, __LINE__, __func__, (fmt), ##arg)

#undef lwarn
#define lwarn(fmt, arg...) plog(4, "memory", __FILE__, __LINE__, __func__, (fmt), ##arg)

#undef linfo
#define linfo(fmt, arg...) plog(5, "memory", __FILE__, __LINE__, __func__, (fmt), ##arg)

#undef ldebug
#define ldebug(fmt, arg...) plog(6, "memory", __FILE__, __LINE__, __func__, (fmt), ##arg)

#undef ltrace
#define ltrace(fmt, arg...) plog(7, "memory", __FILE__, __LINE__, __func__, (fmt), ##arg)

#undef lprint
#define lprint(fmt, arg...) plog(8, "memory", __FILE__, __LINE__, __func__, (fmt), ##arg)

#endif

#endif // FLAME_MEMORY_LOG_H