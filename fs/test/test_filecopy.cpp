#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "fs/filecopy.cpp"
#include "fs//localfs.h"
#include "thread/thread.h"
#include "io/aio-wrapper.h"
#include "io/fd-events.h"

TEST(filecopy, simple_localfile_copy) {
    auto fs = FileSystem::new_localfs_adaptor("/tmp");
    auto f1 = fs->open("test_filecopy_src", O_RDONLY);
    auto f2 = fs->open("test_filecopy_dst", O_RDWR | O_CREAT | O_TRUNC, 0644);
    auto ret = FileSystem::filecopy(f1, f2);
    delete f2;
    delete f1;
    delete fs;
    EXPECT_EQ(500 * 4100, ret);
    ret = system("diff -b /tmp/test_filecopy_src /tmp/test_filecopy_dst");
    EXPECT_EQ(0, WEXITSTATUS(ret));
}

TEST(filecopy, libaio_localfile_copy) {
    auto fs = FileSystem::new_localfs_adaptor("/tmp", FileSystem::ioengine_libaio);
    auto f1 = fs->open("test_filecopy_src", O_RDONLY);
    auto f2 = fs->open("test_filecopy_dst2", O_RDWR | O_CREAT | O_TRUNC, 0644);
    auto ret = FileSystem::filecopy(f1, f2);
    delete f2;
    delete f1;
    delete fs;
    EXPECT_EQ(500 * 4100, ret);
    ret = system("diff -b /tmp/test_filecopy_src /tmp/test_filecopy_dst2");
    EXPECT_EQ(0, WEXITSTATUS(ret));
}

int main(int argc, char **argv) {
    photon::init();
    DEFER(photon::fini());
    photon::fd_events_init();
    DEFER(photon::fd_events_fini());
    photon::libaio_wrapper_init();
    DEFER(photon::libaio_wrapper_fini());

    ::testing::InitGoogleTest(&argc, argv);
    // 500 * 4100, make sure it have no aligned file length
    system("dd if=/dev/urandom of=/tmp/test_filecopy_src bs=500 count=4100");
    int ret = RUN_ALL_TESTS();
    LOG_ERROR_RETURN(0, ret, VALUE(ret));
}
