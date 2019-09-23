<!--
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:17:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-04 15:17:04
 -->
Flame 源码目录说明
-----------------
组件大多采用接口和实现分离的方式，`接口`即组件所需要提供的功能，`实现`则指采用某种方式实现功能的方案（理解就好，非常拗口）

> 后面带 `/` 表示目录，否则为文件

## 主目录
- `chunkstore/` : CSD ChunkStore的接口与实现
    - `filestore/` : 基于本地文件系统实现的ChunkStore
    - `nvmestore/` : 基于SPDK NVMe用户态驱动实现的ChunkStore
    - `simstore/` : 模拟实现的ChunkStore，不实际存储数据但会存储元数据，在unmount后元数据信息持久到文件
    - `chunkstore.h` : ChunkStore接口
    - `cs.h` : chunkstore组件
- `cluster/` : 集群管理组件，包含MGR部分（clt_mgmt）和代理（CSD）部分（clt_agent）。同样包含接口部分和实现部分
    - `clt_my/` : 基于周期轮询实现的集群管理
    - `clt_agent.h` : 代理端接口
    - `clt_mgmt.h` : 管理段接口
- `cml/` : Flame的命令行部分，生成一个名为`flame`的二进制程序
    - `flame.cc` 
- `common/` : 通用组件，包括线程相关组件
    - `thread/` : 线程相关封装
    - `cmdline.h` & `cmdline.cc` : 命令行组件，应用于MGR、GW和CSD程序
    - `config.h` & `config.cc` : flame配置文件解析
    - `context.h` & `context.cc` : flame上下文
    - `log.h` & `log.cc` : Log
- `csd/` : CSD进程
    - `csd.cc` : main()
- `gw/` : Gateway进程
    - `gw.cc` : main()
- `include/` : 一些通用头文件
    - `libflame/` : libflame接口头
- `layout/` : MGR的数据布局策略组件
    - `calculator.h` & `calculator.cc` : CSD和Chunk权值计算器
    - `layout.h` : 数据布局策略接口
- `libflame/` : libflame的实现
- `memzone/` : 内存管理组件（针对CSD和GW，暂未采用）
- `metastore/` : MGR MetaStore接口和实现，元数据持久化层
    - `sqlms/` : 基于SQL实现的MetaStore，通过`orm/`抽象数据结构
    - `metastore.h` : MetaStore接口
    - `ms.h` & `ms.cc` : MetaStore组件
- `mgr/` : MGR进程
    - `chkm/` : Chunk管理组件
    - `csdm/` : CSD管理组件
    - `volm/` : Volume管理组件
    - `mgr.cc` : main()
- `mk/`：通用Makefile文件（仅针对Makefile编译方案）
    - `env.mk` : base
    - `googletest.mk`
    - `grpc.mk`
    - `mysql.mk`
    - `objs.mk` : 包含各个模块的引用
    - `spdk.mk`
- `msg/`: 网络消息通信模块
    - `dispatcher/` : 消息模块上层接口(尚未详细测试)
    - `event/` : 对epoll的封装
    - `internal/` : 内部使用的头文件
    - `rdma/` : rdma实现
    - `socket/` : socket实现(TCP)
    - `Connection.h` & `Connection.cc` : Connection抽象类
    - `ListenPort.h` : ListenPort抽象类
    - `msg_common.h` : 内部公用头文件
    - `msg_context.h` & `msg_context.cc` : 消息模块上下文，管理整个消息模块的生命周期和相关资源
    - `msg_core.h` : 调用消息模块时，只需include此文件即可
    - `msg_data.h` : 内部使用的消息数据类
    - `msg_def.h` : 内部宏定义
    - `msg_types.h` : 内部接口定义
    - `Msg.h` & `Msg.cc` : Msg类
    - `MsgManager.h` & `MsgManager.cc` : 管理连接，监听端口，会话和工作线程
    - `MsgWorker.h` & `MsgWorker.cc` : MsgWorker抽象类和ThrMsgWorker子类(pthread实现)
    - `NetHandler.h` & `NetHandler.cc` : socket工具类
    - `Session.h` & `Session.cc` : Session类
    - `Stack.h` & `Stack.cc` : TcpStack和RdmaStack的基类
- `orm/` : MGR ORM，为MetaStore的SQL实现提供数据结构抽象，支持不同的SQL数据库系统
    - `examples/` : 用例
    - `my_impl/` : 适配MySQL的实现
    - `cols.h` : SQL字段类型
    - `driver.h` : SQL数据库系统抽象接口（`my_impl`继承此接口）
    - `engine.h` & `engine.cc` : 对外提供的SQL引擎，包括一些基础功能，如多连接复用
    - `handle.h` : SQL语句句柄
    - `opts.h` : SQL语句组装-操作符封装
    - `orm.h` : 总引用头文件
    - `stmt.h` & `stmt.cc` : SQL语句组装-主体
    - `table.h` : SQL数据表封装，基于SQL引擎
    - `value.h` : SQL语句组装-数据字面量封装
- `proto/` : gRPC生成文件所在的目录
    - gRPC `.proto` 文件位于 `src` 目录同级的目录 `proto` 中
- `service/` : gRPC服务实现，包括服务端和客户端
    - 服务接口文件位于`include/`
    - `csds_*` : csd控制平面服务
    - `flame_*` : mgr对外控制平面
    - `internal_*` : mgr对内控制平面
- `spolicy/` : 存储策略组件，存储策略在MGR、CSD和libflame三端都有涉及
    - `spolicy.h` & `spolicy.cc` : 存储策略接口
- `util/` : 项目无关的工具集
- `work/` : 工作线程

## 子模块
- `googletest/` : GTest
- `spdk/` : spdk