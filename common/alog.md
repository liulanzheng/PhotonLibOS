<!--$$PHOTON_UNPUBLISHED_FILE$$-->
# alog：一款易使用、可扩展、高性能的日志库

维护者：鲁七（李慧霸）

历史贡献者（首次贡献时间逆序）：
* 鲁七（李慧霸）

## 易使用
基本日志输出：
```cpp
LOG_DEBUG("Hello, `, this is alog!", "world");  // 字符串格式化
LOG_INFO ("She's a ", 7, "-year-old girl.");    // 顺序打印
auto str = "asdf";
LOG_WARN ("'`' is a string @ ", str, (void*)str); // 格式化 & 打印
auto x = 1234567;                               // 整数格式
LOG_ERROR("0x", HEX(x), " equals ", DEC(x).comma(true).width(10));
LOG_FATAL(VALUE(x));   // 输出键值对“[x=1234567]”
```

附加功能：
```cpp
if (arg < 0)    // 输出ERROR日志，设置errno为EINVAL，并返回-1
    LOG_ERROR_RETURN(EINVAL, -1, "argument MUST be >= 0");

int fd = open(fn);  // 输出ERROR日志，并附带输出errno和
if (fd < 0)         // strerror信息，同时保持errno不变，并返回-1
    LOG_ERRNO_RETURN(0, -1, "failed to open file ", fn);
```

## 前端可扩展
除了c++内置类型以外，alog还允许用户扩展，以支持自定义类型。
假设用户定义了代表错误码的如下类型：
```cpp
struct ERRNO
{
    const int no;
    constexpr ERRNO(int no=0) : no(no) { }
};
```
可以通过如下扩展代码，以使alog支持输出ERRNO类型：
```cpp
inline LogBuffer& operator << (LogBuffer& log, ERRNO e)
{
    auto no = e.no ? e.no : errno;
    return log.printf("errno=", no, '(', strerror(no), ')');
}

```
其中LogBuffer是alog内定义的用于支持实际输出动作的类型。

例2：IP地址、端口。若有如下定义：
```cpp
union IPAddr
{
    uint32_t addr = 0;
    struct { uint8_t a, b, c, d; };
};

struct EndPoint
{
    IPAddr addr;
    uint16_t port;
}
```
则可对alog做如下扩展：
```cpp
inline LogBuffer& operator << (LogBuffer& log, const IPAddr addr)
{
    return log.printf(addr.d, '.', addr.c, '.', addr.b, '.', addr.a);
}

inline LogBuffer& operator << (LogBuffer& log, const EndPoint ep)
{
    return log << ep.addr << ':' << ep.port;
}
```

注意，alog通过前端扩展实现对std::string的支持，并编写在单独的头文件（alog-stdstring.h）
中，使用时需单独引入。
```cpp
inline LogBuffer& operator << (LogBuffer& log, const std::string& s)
{
    return log << ALogString(s.c_str(), s.length());
}
```
用例：
```cpp
#include "alog.h"
#include "alog-stdstring.h"

int main()
{
    LOG_DEBUG(std::string("Hello world!"));
    return 0;
}
```


## 高性能
在连续打印方式下，alog无需解析格式化字符串，直接调用每个参数的输出函数。例如：
```cpp
LOG_DEBUG(1, 2, 3, "hello");
```
特别地，对于字符串常量，alog能够专门识别其类型和长度，并采用memcpy的方式高速
复制到内部缓冲区，比strcpy更为优化。

在字符串格式化模式下，alog能利用c++11的新能力，在**编译时**对格式串进行解析，
省掉运行时解析的开销，生成极为高效的代码。例如对于如下代码：
```cpp
IPAddr addr = ...;
uint16_t port = ...;
LOG_DEBUG("`.`.`.`:`", addr.d, addr.c, addr.b, addr.a, port);
```
由于编译时格式解析和处理，最终产生的代码接近于：
```cpp
IPAddr addr = ...;
uint16_t port = ...;
LOG_DEBUG(addr.d, ".", addr.c, ".", addr.b, ".", addr.a, ":", port);
```

对于如下的测试代码：
```cpp
void log_format()
{
    LOG_DEBUG("aksdjfj `:` ` ` ` ` `", 234, "^%$#@", 341234, "  hahah `:jksld",
        884, HEX(2345678), "::::::::::::::::::::::::::::::::::::::::::::::::::::");
}

void log_print_()
{
    LOG_DEBUG("aksdjfj ", 234, ':', "^%$#@", ' ', 341234, ' ', "  hahah `:jksld", ' ',
        884, ' ', HEX(2345678), ' ', "::::::::::::::::::::::::::::::::::::::::::::::::::::");
}

void log_snprintf()
{
    static char levels[][6] = {"DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"};
    int th = 2423423;
    char buf[1024];
    snprintf(buf, sizeof(buf), "%d/%02d/%02d %02d:%02d:%02d|%s|th=%016X|%s:%d|%s:"
        "aksdjfj %d:%s%d  hahah `:jksld%d%X%s", 9102,03,04, 05,06,78, levels[0], th,
        __FILE__, __LINE__, __func__, 234, "^%$#@", i, 884, 2345678,
        "::::::::::::::::::::::::::::::::::::::::::::::::::::");
}
```
在禁用掉alog的实际输出的情况下，这三个函数都会在内存buffer当中生成一条内容相当的日志记录。
在2015款15寸MBP机器上用clang编译测试上述三个函数，反复执行100万次，alog输出——包括
log_format() 和 log_print_()——耗时分别耗时253ms、247ms；snprintf耗时839ms。

所以**alog的性能比snprintf高三倍以上**。

## 后端可扩展
alog定义了如下的函数指针，用于输出一条日志记录：
```cpp
extern void (*log_output)(const char* begin, const char* end);
```
alog将会在内部buffer（默认4KB大）里生成日志的内容，然后调用log_output(begin, end)进行输出，
其中由[begin, end)确定的半闭半开字节区间表示输出一条日志。

alog已经内置了输出到stdout（默认）、stderr、文件和丢弃等后端，用户也可方便地编写自定义
输出后端。


