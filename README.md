# EASE介绍

EASE: **E**fficient & **A**gile **S**torage **E**ngine

EASE将众多通用存储功能模块和底层系统服务整合在一起，
并结合了可组合的系统架构，使得存储系统和应用的开发
得到显著简化（逻辑复杂度降低，并且代码减少），并且
系统稳定性得到显著提高（千行代码bug率降低）。

## 通用存储模块
* 各种存储系统接口的适配器：本地文件系统等、P2P文件访问、盘古1&2、HDFS；
* [LSMT](fs/lsmt/lsmt.md)：log-structured merge tree，变append-only为random write；
* ZFile：随机读取压缩文件（自有格式），支持lz4、mini-lzo、zstd等算法；
* httpfile：以文件接口形式通过HTTP协议访问远程文件（基于libcurl）；
* tarfs：将tar文件视为文件系统；
* overlayfs：文件系统叠加，类似于linux内核的overlayfs；
* FUSE适配模块；

## 通用存储模块（开发中）
* 缓存模块：内存缓存（page cache）、持久化缓存等；
* remotefs：访问远程存储模块，跨网络或跨进程；
* refreshfs：可在底层fs更新配置后，一键刷新所有已打开的文件，使新配置立即生效；
* 流控；
* ...

## 存储辅助功能模块
* 同步I/O、异步I/O通用适配模块；
* 随机错误注入模块；
* 目录子树模块；
* 文件I/O对齐矫正模块；
* 文件stripe模块（类似于RAID-0）；
* 文件线性拼接模块；
* fd分配器；
* ...

## 存储支持模块
* 区间切割模块[range_split](fs/range-split.md)：支持固定间隔、可变间隔；
* 路径解析与处理；
* 目录树的内存表示与操作（查询、修改）；
* 高性能索引 for 可变长度extents；
* 多线程[ring buffer、ring queue](ring.md)；
* ...

## 底层系统服务
* 任务管理：[用户态线程photon](photon/photon.md)、[异步回调](callback.md)；
* 网络：抽象化[socket](net/socket.md)、[内核socket封装](photon/syncio.md)；
* 本地存储引擎：psync、[libaio、posix-aio](photon/syncio.md)；
* [资源池](identity-pool.md)、线程池、[日志](alog.md)、[iovector](iovector.md)、...

## 底层系统服务（计划整合）
* 网络：tap网卡、DPDK、RDMA、用户态TCP；
* 存储引擎：SPDK；

## 可组合的系统架构
我们在给集团Sigma容器系统做的快速启动image服务最初不支持压缩，结构如下：

> 虚拟块设备vrbd --> LSMT --> P2PFile --> (parents...) --> Root

后来添加了可选的**ZFile**压缩支持，调整如下：

> 虚拟块设备vrbd --> LSMT --> [ZFile] --> P2PFile --> (parents...) --> Root

整体结构完全没有受到影响，只需要根据情况插入解压缩模块ZFile，其他无关模块
不知道压缩的存在。



