#ifndef FLAME_INCLUDE_META_H
#define FLAME_INCLUDE_META_H

#include <cstdint>
#include <string>

namespace flame {

/**
 * Cluster MetaData
 */
struct cluster_meta_t {
    std::string name    {};
    uint32_t    mgrs    {0};
    uint32_t    csds    {0};
    uint64_t    size    {0};
    uint64_t    alloced {0};
    uint64_t    used    {0};
    uint64_t    ctime   {0};
}; // struct cluster_meta_t

/**
 * Volume Group MetaData
 */
struct volume_group_meta_t {
    uint64_t    vg_id   {0};
    std::string name    {};
    uint64_t    ctime   {0};    // (us)
    uint32_t    volumes {0};
    uint64_t    size    {0};    // total visible size
    uint64_t    alloced {0};    // total logical size
    uint64_t    used    {0};    // total physical size
}; // struct group_mate_t

/**
 * Volume MetaData
 */
struct volume_meta_t {
    uint64_t    vol_id  {0};
    uint64_t    vg_id   {0};
    std::string name    {};
    uint64_t    ctime   {0};    // (us)
    uint64_t    chk_sz  {0};    // chunk size
    uint64_t    size    {0};
    uint64_t    alloced {0};
    uint64_t    used    {0};
    uint32_t    stat    {0};
    uint32_t    flags   {0};
    uint32_t    spolicy {0};    // store policy
    uint32_t    chunks  {0};    // chunk number
}; // struct volume_meta_t

enum VolumeFlags {
    VOL_FLG_PREALLOC = 0x1
};

enum VolumeStat {
    VOL_STAT_CREATING = 0,
    VOL_STAT_CREATED = 1,
    VOL_STAT_DELETING = 2
};

/**
 * chunk_id_t
 * |------------ chunk group id ------------|--------------|
 * |------- volume id -------|---- index ---|--- sub id ---|
 * | 63                   20 | 19         4 | 3          0 |
 */
struct chunk_id_t {
    uint64_t val;

    chunk_id_t() : val(0) {}
    chunk_id_t(uint64_t v) : val(v) {}
    chunk_id_t(uint64_t vol_id, uint64_t index, uint64_t sub_id) {
        val = (vol_id << 20) | ((index & 0xFFFFULL) << 4) | (sub_id & 0xFULL);
    }

    uint64_t get_cg_id() const { return val >> 4; }
    void set_cg_id(uint64_t v) { val = (v << 4) | (val & 0xFULL); }

    uint64_t get_vol_id() const { return val >> 20; }
    void set_vol_id(uint64_t v) { val = (v << 20) | (val & 0xFFFFFULL); }

    uint16_t get_index() const { return (val >> 4) & 0xFFFFULL; }
    void set_index(uint64_t v) { val = (val & ~0xFFFFFULL) | ((v & 0xFFFFULL) << 4) | (val & 0xFULL); }

    uint16_t get_sub_id() const { return val & 0xFULL; }
    void set_sub_id(uint64_t v) { val = (val & ~0xFULL) | (v & 0xFULL); }

    operator uint64_t () const { return val; }
    chunk_id_t& operator = (uint64_t v) { val = v; return *this; }
}; // struct chunk_id_t

/**
 * Chunk MetaData
 */
struct chunk_meta_t {
    uint64_t    chk_id  {0};
    uint64_t    vol_id  {0};
    uint32_t    index   {0};    // Thera is a same index in a chunk group
    uint32_t    stat    {0};    // chunk status
    uint32_t    spolicy {0};
    uint32_t    flags   {0};
    uint64_t    ctime   {0};
    uint64_t    primary {0};    // this chunk is the primary chunk when primary == chk_id
    uint64_t    size    {0};
    uint64_t    csd_id  {0};
    uint64_t    csd_mtime {0};  // csd map time  
    uint64_t    dst_id  {0};
    uint64_t    dst_ctime {0};  // dst start time
}; // struct chunk_meta_t

enum ChunkFlags {
    CHK_FLG_PREALLOC = 0x1
};

struct health_count_t {
    uint64_t    used    {0};
    uint64_t    alloced {0};
    uint64_t    wr_cnt  {0};
    uint64_t    rd_cnt  {0};
};

struct health_period_t {
    uint64_t    ctime   {0};
    uint64_t    wr_cnt  {0};    // unit: 4KB page
    uint64_t    rd_cnt  {0};    // unit: 4KB page
    uint64_t    lat     {0};    // latency (ns)
    uint64_t    alloc   {0};    
};

struct health_weight_t {
    double      w_load  {0};    // 负载权值
    double      w_wear  {0};    // 磨损权值
    double      w_total {0};    // 总权值
};

/**
 * Chunk Health MetaData
 */
struct chunk_health_meta_t {
    uint64_t            chk_id      {0};
    uint32_t            stat        {0};
    uint64_t            size        {0};
    uint64_t            csd_used    {0};
    uint64_t            dst_used    {0};
    health_count_t      grand;      // 总健康信息
    health_period_t     period;     // 上一个周期的健康信息
    health_weight_t     weight;     // 健康权值
}; // struct chunk_health_meta_t

enum ChunkStat {
    CHK_STAT_CREATING = 0,
    CHK_STAT_CREATED = 1,
    CHK_STAT_MOVING = 2,
    CHK_STAT_DELETING = 3,
    CHK_STAT_DELETED = 99
};

/**
 * Node Addr 32bit ip + 16bit port
 */
struct node_addr_t {
    uint64_t val;

    node_addr_t() : val(0) {}
    node_addr_t(uint64_t v) : val(v) {}
    node_addr_t(uint32_t ip, uint16_t port) {
        val = ((uint64_t)ip << 16) | (uint64_t)port;
    }
    node_addr_t(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3, uint16_t port) {
        set_ip(ip0, ip1, ip2, ip3);
        set_port(port);
    } 

    uint32_t get_ip() const { return val >> 16; }
    void set_ip(uint32_t v) { val = ((uint64_t)v << 16) | (val & 0xFFFFULL); }
    void set_ip(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3) {
        uint32_t v = (uint32_t)ip0 | ((uint32_t)ip1 << 8) | ((uint32_t)ip2 << 16) | ((uint32_t)ip3 << 24);
        set_ip(v);
    }

    uint16_t get_port() const { return val & 0xFFFFULL; }
    void set_port(uint16_t v) { val  = (val & ~0XFFFFULL) | (uint64_t)v; }

    operator uint64_t () const { return val; }
    node_addr_t& operator = (uint64_t v) { val = v; return *this; }
};

/**
 * CSD MetaData
 */
struct csd_meta_t {
    uint64_t    csd_id  {0};
    std::string name    {0};
    uint64_t    size    {0};
    uint64_t    ctime   {0};    // (us)
    uint64_t    io_addr {0};
    uint64_t    admin_addr {0}; 
    uint32_t    stat    {0};
    uint64_t    latime   {0};    // last active time
}; // struct csd_meta_t

enum CsdStat {
    CSD_STAT_NONE   = 0,    // 不存在
    CSD_STAT_DOWN   = 1,    // 下线
    CSD_STAT_PAUSE  = 2,    // 在线，但不能提供服务
    CSD_STAT_ACTIVE = 3     // 正常提供服务
};
enum CsdObjStat {
        CSD_OBJ_STAT_NEW  = 0,  // 新创建的状态，尚未保存
        CSD_OBJ_STAT_LOAD = 1,  // 处于加载状态
        CSD_OBJ_STAT_SVAE = 2,  // 已保存状态
        CSD_OBJ_STAT_DIRT = 3,  // 数据脏，等待保存
        CSD_OBJ_STAT_TRIM = 4   // 等待删除
};

/**
 * CSD Health MetaData
 */
struct csd_health_meta_t {
    uint64_t            csd_id      {0};
    uint32_t            stat        {0};
    uint64_t            size        {0};
    health_count_t      grand;
    health_period_t     period;
    health_weight_t     weight;
}; // struct csd_health_meta_t

/**
 * Gateway MetaData
 */
struct gateway_meta_t {
    uint64_t    gw_id   {0};
    uint64_t    admin_addr  {0};
    uint64_t    ltime   {0};    // connect time (us)
    uint64_t    atime   {0};    // active time (us)
}; // struct gateway_meta_t

/***************************************************
 * Attributes
 ***************************************************/

/**
 * volume attr
 */
struct vol_attr_t {
    uint64_t chk_sz;
    uint64_t size;
    uint32_t flags  {0};
    uint32_t spolicy;
};

/**
 * chunk attr
 */
struct chk_attr_t {
    uint32_t spolicy    {0};
    uint32_t flags      {0};
    uint64_t size       {0};
};

/**
 * chunk health attr
 */
struct chk_hlt_attr_t {
    uint64_t        chk_id      {0};
    uint64_t        size        {0};
    uint32_t        stat        {0};
    uint64_t        used        {0};
    uint64_t        csd_used    {0};
    uint64_t        dst_used    {0};
    health_period_t period;
};

/**
 * chunk map
 */
struct chk_map_t {
    uint64_t    chk_id;
    uint32_t    stat;
    uint64_t    csd_id;
    uint64_t    dst_id;
};

/**
 * csd register attribute
 */
struct csd_reg_attr_t {
    std::string csd_name    {};
    uint64_t    size        {0};
    uint64_t    io_addr     {0};
    uint64_t    admin_addr  {0};
    uint64_t    stat        {0};
};

/**
 * csd sign up attribute
 */
struct csd_sgup_attr_t {
    uint64_t    csd_id;
    uint32_t    stat;
    uint64_t    io_addr;
    uint64_t    admin_addr;
};

/**
 * csd health sub
 */
struct csd_hlt_sub_t {
    uint64_t        csd_id;
    uint64_t        size;
    uint64_t        alloced;
    uint64_t        used;
    health_period_t period;
};

/**
 * csd addr
 */
struct csd_addr_t {
    uint64_t    csd_id      {0};
    uint64_t    admin_addr  {0}; 
    uint64_t    io_addr     {0};
    uint32_t    stat        {0};
};

} // namespace flame

#endif // FLAME_INCLUDE_META_H