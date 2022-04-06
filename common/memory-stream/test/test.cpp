#include "../memory-stream.cpp"
#include <memory>
#include <algorithm>
#include <string>
#include <gtest/gtest.h>
#include "thread/thread.h"
#include "common/utility.h"
#include "common/alog.h"
#include "common/alog-stdstring.h"
using namespace std;
using namespace photon;

char STR[] = "abcdefghijklmnopqrstuvwxyz";

void* stream_writer(void* stream_)
{
    LOG_INFO("enter");
    auto s = (IStream*)stream_;
    for (int i = 0; i < 2; ++i)
    {
        auto count = LEN(STR) - 1;
        auto p = STR;
        while(count > 0)
        {
            size_t len = rand() % 8;
            len = min(len, count);
            LOG_DEBUG("Begin write ", VALUE(len));
            auto ret = s->write(p, len);
            LOG_DEBUG("End write ", VALUE(len));
            EXPECT_EQ(ret, len);
            p += len;
            count -= len;
        }
    }
    LOG_INFO("exit");
    return nullptr;
}

void do_read_append(IStream* s, size_t count, string& str)
{
    char buf[4096];
    count = min(count, LEN(buf) -1);
    LOG_DEBUG("Begin read ", VALUE(count));
    auto ret = s->read(buf, count);
    LOG_DEBUG("End read ", VALUE(count));
    EXPECT_EQ(ret, count);
    buf[count] = 0;
    str += buf;
}

TEST(MemoryStream, normalTest)
{
    unique_ptr<IStream> s( new_simplex_memory_stream(16) );
    thread_create(&stream_writer, s.get());

    string rst;
    do_read_append(s.get(), 13, rst);
    do_read_append(s.get(), 26, rst);
    do_read_append(s.get(), 13, rst);
    string std_rst = STR;
    std_rst += STR;
    EXPECT_EQ(rst, std_rst);
    LOG_INFO("buffer read: '`'", rst);
}

int main(int argc, char **argv)
{
    log_output_level = ALOG_DEBUG;
    ::testing::InitGoogleTest(&argc, argv);
    photon::init();
    DEFER(photon::fini());
    auto ret = RUN_ALL_TESTS();
    return ret;
}
