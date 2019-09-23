/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:17:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-06 15:08:39
 */
#ifndef FLAME_MSG_INTERNAL_MSG_BUFFER_LIST_H
#define FLAME_MSG_INTERNAL_MSG_BUFFER_LIST_H

#include "msg_buffer.h"
#include <cstring>
#include <cassert>
#include <list>
#include <utility>
#include <algorithm>
#include <iterator>

namespace flame{
namespace msg{

class MsgBufferList{
    std::list<MsgBuffer> m_buffer_list;
    uint64_t len;
public:
    explicit MsgBufferList() 
    :len(0){}

    explicit MsgBufferList(MsgBufferList &&o) noexcept
    :m_buffer_list(std::move(o.m_buffer_list)) {
        len = o.len;
        o.len = 0;
    }

    void swap(MsgBufferList &other) noexcept{
        m_buffer_list.swap(other.m_buffer_list);
        std::swap(len, other.len);
    }

    uint64_t splice(MsgBufferList &other){
        this->len += other.len;
        m_buffer_list.splice(m_buffer_list.end(), other.m_buffer_list);
        auto tmp_len = other.len;
        other.len = 0;
        return tmp_len;
    }

    int append_nocp(MsgBuffer &&buf){
        this->len += buf.offset();
        m_buffer_list.push_back(std::move(buf));
        return buf.offset();
    }

    int append(void *b, int l){
        char *buf = (char *)b;
        int total_len = 0, cb_len = 0;
        auto data_buf = cur_data_buffer(cb_len, l - total_len);
        while(total_len < l && data_buf != nullptr){
            if(cb_len > l - total_len){
                cb_len = l - total_len;
            }
            std::memcpy(data_buf, buf + total_len, cb_len);
            total_len += cb_len;
            cur_data_buffer_extend(cb_len);
            if(total_len >= l) break;
            data_buf = cur_data_buffer(cb_len, l - total_len);
        }
        return total_len;
    }

    int append(MsgBuffer &buf){
        return append(buf.data(), buf.offset());
    }

    void clear(){
        m_buffer_list.clear();
        len = 0;
    }

    uint64_t length() const{
        return len;
    }

    int buffer_num() const{
        return m_buffer_list.size();
    }

    std::_List_iterator<MsgBuffer> list_begin(){
        return m_buffer_list.begin();
    }

    std::_List_iterator<MsgBuffer> list_end(){
        return m_buffer_list.end();
    }

    char *cur_data_buffer(int &cb_len, size_t new_buffer_len=1024){
        if(m_buffer_list.empty()){
            if(new_buffer_len <= 0){
                return nullptr;
            }
            m_buffer_list.push_back(MsgBuffer(new_buffer_len));
        }
        MsgBuffer *buf = &(m_buffer_list.back());
        if(buf->offset() == buf->length()){
            if(new_buffer_len <= 0){
                return nullptr;
            }
            m_buffer_list.push_back(MsgBuffer(new_buffer_len));
            buf = &(m_buffer_list.back());
        }
        cb_len = buf->length() - buf->offset();
        return buf->data() + buf->offset();
    }

    int cur_data_buffer_extend(int ex_len){
        if(m_buffer_list.empty()){
            return 0;
        }
        MsgBuffer &buf = m_buffer_list.back();
        int r = buf.advance(ex_len);
        this->len += r;
        return r;
    }

    class iterator : public std::iterator< std::forward_iterator_tag, char>{
        MsgBufferList *m_bl;
        std::_List_iterator<MsgBuffer> m_it;
        size_t m_cur_off;
    public:
        explicit iterator()
        :m_bl(nullptr), m_cur_off(0){

        }
        explicit iterator(MsgBufferList *bl, bool is_end)
        :m_bl(bl){
            m_cur_off = 0;
            if(!is_end){
                m_it = m_bl->m_buffer_list.begin();
            }else{
                m_it = m_bl->m_buffer_list.end();
            }
        }

        void swap(iterator& other) noexcept{
            using std::swap;
            swap(m_bl, other.m_bl);
            swap(m_it, other.m_it);
            swap(m_cur_off, other.m_cur_off);
        }

        int64_t advance(int64_t len){
            int64_t cur_len = 0;
            if(len > 0){
                while(cur_len < len && m_it != m_bl->m_buffer_list.end()){
                    MsgBuffer &cur_buf = *m_it;
                    if(cur_buf.offset() - m_cur_off <= len - cur_len){
                        cur_len += cur_buf.offset() - m_cur_off;
                        ++m_it;
                        m_cur_off = 0;
                    }else{
                        m_cur_off += (len - cur_len);
                        cur_len = len;
                    }
                }
            }else if(len < 0){
                while(cur_len > len && m_it != m_bl->m_buffer_list.begin()){
                    MsgBuffer &cur_buf = *m_it;
                    if(m_cur_off < cur_len - len){
                        cur_len -= (m_cur_off + 1);
                        --m_it;
                        m_cur_off = (*m_it).offset() - 1;
                    }else{
                        m_cur_off -= (cur_len - len);
                        cur_len = len;
                    }
                }
                if(cur_len > len){
                    // now, m_it is at the first buffer.
                    cur_len -= m_cur_off;
                    m_cur_off = 0;
                }
            }
            return cur_len;
        }

        char *cur_data_buffer(int &cd_len){
            if(m_it == m_bl->m_buffer_list.end()){
                return nullptr;
            }
            MsgBuffer &cur_buf = *m_it;
            cd_len = cur_buf.offset() - m_cur_off;
            if(cd_len == 0){
                ++m_it;
                m_cur_off = 0;
                return nullptr;
            }
            return cur_buf.data() + m_cur_off;
        }

        int cur_data_buffer_extend(int ex_len){
            if(m_it == m_bl->m_buffer_list.end() || ex_len <= 0){
                return 0;
            }
            MsgBuffer &cur_buf = *m_it;
            if(ex_len >= cur_buf.offset() - m_cur_off){
                ex_len = cur_buf.offset() - m_cur_off;
                ++m_it;
                m_cur_off = 0;
            }else{
                m_cur_off += ex_len;
            }
            return ex_len;
        }

        iterator& operator++ (){
            if(m_it == m_bl->m_buffer_list.end()) return *this;
            this->advance(1);
            return *this;
        }

        iterator operator++ (int){
            if(m_it == m_bl->m_buffer_list.end()) return *this;
            iterator tmp = *this;
            this->advance(1);
            return tmp;
        }

        bool operator == (const iterator &o) const{
            if(m_it == o.m_it){
                if(m_cur_off == o.m_cur_off
                    || m_it == m_bl->m_buffer_list.end()){
                    return true;
                }
            }
            return false;
        }

        bool operator != (const iterator &o) const{
            return !(*this == o);
        }

        char& operator* () {
            if(m_it == m_bl->m_buffer_list.end()){
                throw "Out of range!";
            } 
            return (*m_it).data()[m_cur_off];
        }

        char& operator-> () {
            if(m_it == m_bl->m_buffer_list.end()){
                throw "Out of range!";
            } 
            return (*m_it).data()[m_cur_off];
        }

        int copy(void *b, int64_t len){
            char *buf = (char *)b;
            int64_t total_len = 0;
            int cb_len = 0;
            auto data_buf = cur_data_buffer(cb_len);
            while(total_len < len && data_buf != nullptr){
                if(cb_len > len - total_len){
                    cb_len = len - total_len;
                }
                std::memcpy(buf + total_len, data_buf, cb_len);
                cur_data_buffer_extend(cb_len);
                total_len += cb_len;

                data_buf = cur_data_buffer(cb_len);
            }
            return total_len;
        }

        int copy(MsgBuffer &buf){
            return copy(buf.data() + buf.offset(),
                        buf.length() - buf.offset());
        }

    };

    iterator begin() noexcept{
        return iterator(this, false);
    }

    iterator end() noexcept{
        return iterator(this, true);
    }
};

} // namespace msg
} // namespace flame

#endif //FLAME_MSG_INTERNAL_MSG_BUFFER_LIST_H