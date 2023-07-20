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

#include "resettable_ee.h"
#include <photon/common/alog.h>
#include <pthread.h>

namespace photon {

static thread_local intrusive_list<ResettableEventEngine> ree_list;
static thread_local bool registed = false;

void fork_hook_event_engine() {
    if (!registed)
        return;
    LOG_INFO("reset event engine at fork");
    for (auto ree : ree_list) {
        LOG_DEBUG("reset event engine ", VALUE(ree));
        ree->reset();
    }
}

ResettableEventEngine::ResettableEventEngine() {
    if (!registed) {
        pthread_atfork(nullptr, nullptr, &fork_hook_event_engine);
        registed = true;
    }
    LOG_DEBUG("push ", VALUE(this));
    ree_list.push_back(this);
}

ResettableEventEngine::~ResettableEventEngine() {
    LOG_DEBUG("erase ", VALUE(this));
    ree_list.erase(this);
}

} // namespace photon