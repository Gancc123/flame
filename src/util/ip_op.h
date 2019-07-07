#ifndef FLAME_UTIL_IP_OP_H
#define FLAME_UTIL_IP_OP_H

#include <cstdint>
#include <string>
#include <sstream>
#include <regex>

inline uint32_t string_to_ip(std::string& ip_s){ //例如192.168.3.110=>4*8 bit ip低位为192，192.168.3.110为0x6e03a8c0
    uint32_t ip = 0;
    std::regex pattern("([0-9]{1,3})[.]([0-9]{1,3})[.]([0-9]{1,3})[.]([0-9]{1,3})");
    std::smatch result;
    bool match = regex_search(ip_s, result, pattern);
    if(match)
    {
        for (int i = 1; i < result.size(); i++)
        {
            ip = ip | (atoi(result[i].str().c_str()) << (i * 8 - 8));
        }
    }
    return ip;
}
inline std::string ip32_to_string(uint32_t ip){   
    uint8_t s[4];
    for(int i = 0; i < 4; i++){
        s[i] = ip & 0xff;
        ip >>= 8;
    }
    std::ostringstream os;
    os << (int)s[0] << "." << (int)s[1] << "." << (int)s[2] << "." << (int)s[3];
    return os.str();
}

#endif