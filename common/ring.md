<!--$$PHOTON_UNPUBLISHED_FILE$$-->
# 通用的环形缓冲和环形队列类（ease/ring.h）

photon线程安全：是

## 环形队列（模板）
```cpp
template<typename T>
class RingQueue
{
public:
    RingQueue(uint32_t capacity_2expn); // `capacity` 将被向上取整到 2^n
    T& operator [] (uint64_t i);        // 获取队列中第i个元素的引用
    T& front()                          // 获取队列中第一个元素的引用
    T& back()                           // 获取队列中最后一个元素的引用
    uint32_t capacity();                // ring的总容量
    uint32_t size();                    // ring的当前大小
    bool empty();                       // ring是否为空
    bool full();                        // ring是否已满

    template<typename U>
    void push_back(U&& x);              // 向队列尾部插入元素x，类型U必须可默认转换为T
    void pop_front();                   // 弹出并丢弃队列头元素
    void pop_front(T& lhs);             // 弹出并保存队列头元素为lhs
    void clear();                       // 清空队列
};
```

## 环形缓冲
RingBuffer是一个char类型的RingQueue，同时添加了适用于字节流的读写函数。
```cpp
class RingBuffer : public RingQueue<char>
{
public:
    using RingQueue::RingQueue;
    ssize_t read(void *buf, size_t count);
    ssize_t write(const void *buf, size_t count);
    ssize_t readv(const struct iovec *iov, int iovcnt);
    ssize_t writev(const struct iovec *iov, int iovcnt);
};
```
