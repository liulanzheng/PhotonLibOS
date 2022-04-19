#include "photon/common/alog.h"
#include <gtest/gtest.h>
#include "photon/thread/thread.h"
#include <vector>

class LogOutputTest : public ILogOutput {
public:
    size_t _log_len;
    char _log_buf[4096];
    void write(int, const char* begin, const char* end) override
    {
        _log_len = end - begin;
        EXPECT_TRUE(_log_len < sizeof(_log_buf));
        memcpy(_log_buf, begin, _log_len);
        _log_buf[ --_log_len ] = '\0';
    }
    int get_log_file_fd() override {
        return -1;
    }

    uint64_t get_throttle() override {
        return -1UL;
    }

    uint64_t set_throttle(uint64_t) override {
        return -1UL;
    }
} log_output_test;

auto &_log_buf=log_output_test._log_buf;
auto &_log_len=log_output_test._log_len;

TEST(alog, example) {
    log_output = &log_output_test;
    DEFER(log_output = log_output_stdout);
    LOG_DEBUG(' ');
    auto log_start = _log_buf + _log_len - 1;
    LOG_INFO("WTF"
    " is `"
    " this `", "exactly", "stuff");
    EXPECT_STREQ("WTF\" \" is exactly\" \" this stuff", log_start);

    LOG_INFO("WTF is ` this `", 1, 2);
    EXPECT_STREQ("WTF is 1 this 2", log_start);
    LOG_INFO("WTF is \n means ` may be work `", "??", "!!");
    EXPECT_STREQ("WTF is \\n means ?? may be work !!", log_start);
    LOG_INFO("WTF is \n means ` may be work `", "??", "!!");
    EXPECT_STREQ("WTF is \\n means ?? may be work !!", log_start);
    const char foobar[] = "n";
    LOG_INFO(foobar);
    // #S len == 6 > 2 + sizeof(yeah) 4, will fit the length condition
    // but miss the leading/tailing character condition;
    EXPECT_STREQ("n", log_start);
    LOG_INFO(VALUE(foobar));
    EXPECT_STREQ("[foobar=n]", log_start);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int ret = RUN_ALL_TESTS();
    LOG_ERROR_RETURN(0, ret, VALUE(ret));
}
