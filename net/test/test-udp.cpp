/*
Copyright 2022 The Photon Authors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <fcntl.h>
#include <gtest/gtest.h>
#include <photon/common/alog.h>
#include <photon/io/fd-events.h>
#include <photon/net/datagram_socket.h>
#include <photon/thread/thread11.h>
#include <sys/stat.h>
#define protected public
#define private public
#include "../kernel_socket.cpp"
#undef protected
#undef private
#include "cert-key.cpp"

using namespace photon;
using namespace net;

constexpr char uds_path[] = "udsudptest.sock";
constexpr size_t uds_len = sizeof(uds_path) - 1;

TEST(UDP, basic) {
    auto s1 = new_udp_socket();
    DEFER(delete s1);
    auto s2 = new_udp_socket();
    DEFER(delete s2);

    s1->bind(EndPoint(IPAddr("127.0.0.1"), 0));
    auto ep = s1->getsockname();
    LOG_INFO("Bind at ", ep);

    constexpr static size_t msgsize=63 *1024; // more data returned failure
    char hugepack[msgsize];
    std::fill(&hugepack[0], &hugepack[sizeof(hugepack)-1], 0xEA);
    s2->connect(ep);
    ASSERT_EQ(msgsize, s2->send(hugepack, sizeof(hugepack)));
    char buf[msgsize];
    ASSERT_EQ(msgsize, s1->recv(buf, sizeof(buf)));

    s2->connect(ep);
    EXPECT_EQ(6, s2->send("Hello", 6));
    EXPECT_EQ(6, s1->recv(buf, 4096));
    EXPECT_STREQ("Hello", buf);

    auto s3 = new_udp_socket();
    DEFER(delete s3);
    EXPECT_EQ(6, s3->sendto("Hello", 6, ep));
    EndPoint from;
    EXPECT_EQ(6, s1->recvfrom(buf, 4096, &from));
    LOG_INFO(VALUE(from));
    EXPECT_STREQ("Hello", buf);
}

TEST(UDP, uds) {
    remove(uds_path);
    auto s1 = new_uds_datagram_socket();
    DEFER(delete s1);
    auto s2 = new_uds_datagram_socket();
    DEFER(delete s2);

    s1->bind(uds_path, uds_len);
    char path[1024] = {};
    socklen_t pathlen = s1->getsockname(path, 1024);
    LOG_INFO("Bind at ", path);

    s2->connect(path, pathlen);
    ASSERT_EQ(6, s2->send("Hello", 6));
    char buf[4096];
    ASSERT_EQ(6, s1->recv(buf, 4096));
    EXPECT_STREQ("Hello", buf);

    auto s3 = new_uds_datagram_socket();
    DEFER(delete s3);
    ASSERT_EQ(6, s3->sendto("Hello", 6, uds_path, uds_len));
    pathlen = 1024;
    memset(path, 0, sizeof(path));
    ASSERT_EQ(6, s1->recvfrom(buf, 4096, path, sizeof(path)));
    LOG_INFO(VALUE(path));
    EXPECT_STREQ("Hello", buf);
}

TEST(UDP, uds_huge_datag) {
    remove(uds_path);
    auto s1 = new_uds_datagram_socket();
    DEFER(delete s1);
    auto s2 = new_uds_datagram_socket();
    DEFER(delete s2);

    s1->bind(uds_path, uds_len);
    char path[1024] = {};
    socklen_t pathlen = s1->getsockname(path, 1024);
    LOG_INFO("Bind at ", path);

    constexpr static size_t msgsize=207 *1024; // more data returned failure
    char hugepack[msgsize];
    std::fill(&hugepack[0], &hugepack[sizeof(hugepack)-1], 0xEA);
    s2->connect(path, pathlen);
    ASSERT_EQ(msgsize, s2->send(hugepack, sizeof(hugepack)));
    char buf[msgsize];
    ASSERT_EQ(msgsize, s1->recv(buf, sizeof(buf)));
    EXPECT_EQ(0, memcmp(hugepack, buf, sizeof(hugepack)));
    ASSERT_EQ(msgsize, s2->sendto(hugepack, sizeof(hugepack), uds_path, uds_len));
    ASSERT_EQ(msgsize, s1->recv(buf, sizeof(buf)));
    EXPECT_EQ(0, memcmp(hugepack, buf, sizeof(hugepack)));
}

int main(int argc, char** arg) {
    photon::vcpu_init();
    DEFER(photon::vcpu_fini());
    photon::fd_events_init();
    DEFER(photon::fd_events_fini());
#ifdef __linux__
    if (net::et_poller_init() < 0) {
        LOG_ERROR("net::et_poller_init failed");
        exit(EAGAIN);
    }
    DEFER(net::et_poller_fini());
#endif
    ::testing::InitGoogleTest(&argc, arg);

    LOG_DEBUG("test result:`", RUN_ALL_TESTS());
}
