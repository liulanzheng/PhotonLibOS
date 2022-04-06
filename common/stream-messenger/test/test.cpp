#include "common/stream-messenger/messenger.h"
#include <memory>
#include <algorithm>
#include <string>
#include <gtest/gtest.h>
#include "thread/thread.h"
#include "common/memory-stream/memory-stream.h"
#include "common/utility.h"
#include "common/alog.h"
using namespace std;
using namespace photon;
using namespace StreamMessenger;

char STR[] = "abcdefghijklmnopqrstuvwxyz";

void* echo_server(void* mc_)
{
    auto mc = (IMessageChannel*)mc_;

    IOVector msg;
    size_t blksz = 2;
    while(true)
    {
        auto ret = mc->recv(msg);
        if (ret == 0)
        {
            LOG_DEBUG("zero-lengthed message recvd, quit");
            break;
        }
        if (ret < 0)
        {
            if (errno == ENOBUFS)
            {
                blksz *= 2;
                msg.push_back(blksz);
                LOG_DEBUG("adding ` bytes to the msg iovector", blksz);
                continue;
            }

            LOG_ERROR_RETURN(0, nullptr, "failed to recv msg");
        }

        auto ret2 = mc->send(msg);
        EXPECT_EQ(ret2, ret);
    }
    LOG_DEBUG("exit");
    return nullptr;
}

TEST(StreamMessenger, DISABLED_normalTest)
{
    auto ds = new_duplex_memory_stream(64);
    DEFER(delete ds);

    auto mca = new_messenger(ds->endpoint_a);
    DEFER(delete mca);
    thread_create(&echo_server, mca);

    auto mcb = new_messenger(ds->endpoint_b);
    DEFER(delete mcb);

    auto size = LEN(STR) - 1;
    auto ret = mcb->send(STR, size);
    EXPECT_EQ(ret, size);
    LOG_DEBUG(ret, " bytes sent");
    char buf[size];
    auto ret2 = mcb->recv(buf, sizeof(buf));
    EXPECT_EQ(ret2, sizeof(buf));
    EXPECT_EQ(memcmp(buf, STR, size), 0);
    LOG_DEBUG(ret, " bytes recvd and verified");

    mcb->send(nullptr, 0);
    thread_yield();
    LOG_DEBUG("exit");
}

int main(int argc, char **argv)
{
    log_output_level = ALOG_DEBUG;
    ::testing::InitGoogleTest(&argc, argv);
    photon::init();
    DEFER(photon::fini());
    auto ret = RUN_ALL_TESTS();
    return 0;
}
