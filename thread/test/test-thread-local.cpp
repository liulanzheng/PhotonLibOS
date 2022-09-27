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

#include <gtest/gtest.h>

#include <string>
#include <photon/thread/thread11.h>
#include <photon/thread/thread-local.h>
#include <photon/common/alog.h>

static int expected_times = 0;

// Finalizer is destructed after Value
struct Finalizer {
    ~Finalizer() {
        if (destruct_times != expected_times) {
            LOG_ERROR("destruct times is not expected");
            abort();
        }
        LOG_INFO("empty value has destructed ` times", destruct_times);
    }
    int destruct_times = 0;
};

struct Value {
    Value() : empty(true) {}
    explicit Value(std::string s) : v(std::move(s)) {}
    ~Value() {
        static Finalizer f;
        if (empty)
            f.destruct_times++;
    }
    std::string v;
    bool empty = false;
};

Value* get_thread_local_value() {
    static ThreadLocal<Value> v1;
    return &v1;
}

Value* get_thread_local_value_with_constructor() {
    static ThreadLocal<Value> v2(Value("value"));
    return &v2;
}

TEST(tls, tls_variable) {
    auto th1 = photon::thread_create11([] {
        auto p = get_thread_local_value();
        p->v = "value";
        expected_times++;
    });
    photon::thread_enable_join(th1);
    photon::thread_join((photon::join_handle*) th1);

    auto th2 = photon::thread_create11([] {
        auto p = get_thread_local_value();
        ASSERT_TRUE(p->v.empty());
        expected_times++;
    });
    photon::thread_enable_join(th2);
    photon::thread_join((photon::join_handle*) th2);

    auto p = get_thread_local_value();
    ASSERT_TRUE(p->v.empty());
    expected_times++;
}

TEST(tls, tls_variable_with_param) {
    auto th1 = photon::thread_create11([] {
        auto p = get_thread_local_value_with_constructor();
        ASSERT_FALSE(p->v.empty());
        p->v = "";
    });
    photon::thread_enable_join(th1);
    photon::thread_join((photon::join_handle*) th1);

    auto p = get_thread_local_value_with_constructor();
    ASSERT_FALSE(p->v.empty());
}

int main(int argc, char** arg) {
    photon::thread_init();
    DEFER(photon::thread_fini());
    ::testing::InitGoogleTest(&argc, arg);
    return RUN_ALL_TESTS();
}