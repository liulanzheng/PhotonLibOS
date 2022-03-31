# Socket API（ease/net/socket.h）

## ISocketStream
在IStream的基础上扩展出了socket独有的recv、send、timeout等操作。
```cpp
namespace Net
{
    class ISocketStream : public ::IStream
    {
    public:
        // recv some bytes from the socket;
        // return actual # of bytes recvd, which may be LESS than `count`;
        // may block once at most, when there's no data yet in the socket;
        virtual ssize_t recv(void *buf, size_t count) = 0;
        virtual ssize_t recv(const struct iovec *iov, int iovcnt) = 0;

        // send some bytes to the socket;
        // return actual # of bytes sent, which may be LESS than `count`;
        // may block once at most, when there's no free space in the socket's internal buffer;
        virtual ssize_t send(const void *buf, size_t count) = 0;
        virtual ssize_t send(const struct iovec *iov, int iovcnt) = 0;

        // get/set timeout, in us, (default +∞)
        virtual uint64_t timeout() = 0;
        virtual void timeout(uint64_t tm) = 0;
    };
}
```

## IPv4地址
```cpp
namespace Net
{
    union IPAddr
    {
        // IP地址存储为32位整数，或4个单字节整数
        uint32_t addr = 0;
        struct { uint8_t a, b, c, d; };

        // 从网络序整数或字符串构造IP地址
        explicit IPAddr(uint32_t nl);
        explicit IPAddr(const char* s)

        // 默认构造和复制构造
        IPAddr() = default;
        IPAddr(const IPAddr& rhs) = default;

        // 与网络序整数之间相互转换
        uint32_t to_nl() const;
        void from_nl(uint32_t nl);
    };

    // EndPoint定义为IP地址+端口
    struct EndPoint
    {
        IPAddr addr;
        uint16_t port;

        // 与经典的struct sockaddr_in之间相互转换
        sockaddr_in to_sockaddr_in() const;
        void from_sockaddr_in(const struct sockaddr_in& addr_in);
    };

    // operators to help logging IP addresses with alog
    LogBuffer& operator << (LogBuffer& log, const IPAddr addr);
    LogBuffer& operator << (LogBuffer& log, const EndPoint ep);
}
```

## TCPSocket
```cpp
namespace Net
{
    class TCPSocket : public ISocketStream
    {
        public:
        // connect to a remote peer
        virtual int connect(const EndPoint& ep) = 0;

        // bind the socket to a local endpoint
        virtual int bind(uint16_t port = 0, IPAddr addr = IPAddr()) = 0;

        // set the socket to be listening
        // `backlog` defines the maximum length of pending connection queue
        virtual int listen(int backlog) = 0;

        // accept an in-coming connection
        virtual TCPSocket* accept(EndPoint* remote_endpoint = nullptr) = 0;

        virtual int getsockname(EndPoint& addr) = 0;
        virtual int getpeername(EndPoint& addr) = 0;

        EndPoint getsockname() { EndPoint ep; getsockname(ep); return ep; }
        EndPoint getpeername() { EndPoint ep; getpeername(ep); return ep; }
    };
}
```

## UNIX Domain Socket
```cpp
namespace Net
{
    class UNIXDomainSocket : public ISocketStream
    {
    public:
        // connect to a server
        virtual int connect(char* path, size_t count = 0) = 0;

        // bind the socket to a local path
        virtual int bind(char* path, size_t count = 0) = 0;

        // set the socket to be listening
        // `backlog` defines the maximum length of pending connection queue
        virtual int listen(int backlog) = 0;

        // accept an in-coming connection
        virtual UNIXDomainSocket* accept() = 0;

        virtual int getsockname(char* path, size_t count) = 0;
        virtual int getpeername(char* path, size_t count) = 0;
    };
}
```

## 创建OS内核Socket
```cpp
namespace Net
{
    extern "C" TCPSocket* new_tcp_socket();
    extern "C" UNIXDomainSocket* new_unix_socket();
}
```



