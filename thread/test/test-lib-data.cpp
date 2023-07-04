// $$PHOTON_UNPUBLISHED_FILE$$

#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>

bool popen_test(const std::string& cmd, int expect = 0) {
    puts(cmd.c_str());
    auto p = popen(cmd.c_str(), "r");
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), p) != NULL);
    auto r = pclose(p);
    if (WIFEXITED(r)) return WEXITSTATUS(r) == expect;
    puts("Not exit");
    return false;
}

std::string libpath() {
    char self[4096];
    auto len = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (len < 0) return {};
    std::string path(self, len);
    puts(path.c_str());
    char* tp = strdup(path.c_str());
    path = dirname(tp);
    path = path + "/../../";
    puts(path.c_str());
    char* buf = realpath(path.c_str(), nullptr);
    path = buf;
    puts(path.c_str());
    free(buf);
    free(tp);
    return path;
}

TEST(static_lib, photon_thread_alloc) {
    auto p = libpath() + "/libphoton.a";
    EXPECT_TRUE(popen_test("objdump -tr \"" + p + "\" | grep photon_thread_allocE | grep .data"));
    EXPECT_TRUE(popen_test("objdump -tr \"" + p + "\" | grep photon_thread_deallocE | grep .data"));
}

TEST(shared_lib, photon_thread_alloc) {
    auto p = libpath() + "/libphoton.so";
    EXPECT_TRUE(popen_test("objdump -tr \"" + p + "\" | grep photon_thread_allocE | grep .data"));
    EXPECT_TRUE(popen_test("objdump -tr \"" + p + "\" | grep photon_thread_deallocE | grep .data"));
}


int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int ret = RUN_ALL_TESTS();
}