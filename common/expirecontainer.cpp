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

#include "expirecontainer.h"

#include <photon/thread/thread.h>

ExpireContainerBase::ExpireContainerBase(uint64_t expiration,
                                         uint64_t recycle_timeout)
    : _expiration(expiration),
      _timer(recycle_timeout, {this, &ExpireContainerBase::expire}, true,
             8UL * 1024 * 1024) {}

bool ExpireContainerBase::insert(Item* item) {
    return _set.emplace(item).second;
}

ExpireContainerBase::Item* ExpireContainerBase::find(const Item& key_item) {
    ItemPtr tp((Item*)&key_item);
    auto it = _set.find(tp);
    (void)tp.release();
    return (it != _set.end()) ? it->get() : nullptr;
}

void ExpireContainerBase::clear() {
    _set.clear();
    _list.node = nullptr;
}

uint64_t ExpireContainerBase::expire() {
    photon::scoped_lock lock(_mtx);
    while (!_list.empty() && _list.front()->_timeout.expire() < photon::now) {
        auto p = _list.pop_front();
        LOG_TEMP("Expire ` ` `", p, p->_timeout.expire(), photon::now);
        ItemPtr ptr(p);
        _set.erase(ptr);
        (void)ptr.release();  // p has been deleted inside erase()
    }
    return 0;
}

bool ExpireContainerBase::keep_alive(const Item& x, bool insert_if_not_exists) {
    DEFER(expire());
    photon::scoped_lock lock(_mtx);
    auto ptr = find(x);
    if (insert_if_not_exists) {
        LOG_TEMP("Inserted")
        ptr = x.construct();
        insert(ptr);
    }
    if (!ptr) return false;
    enqueue(ptr);
    return true;
}

ObjectCacheBase::Item* ObjectCacheBase::ref_acquire(const Base::Item& key_item,
                                                    Delegate<void*> ctor) {
    Base::Item* holder = nullptr;
    Item* item = nullptr;
    do {
        photon::scoped_lock lock(_mtx);
        holder = Base::find(key_item);
        if (!holder) {
            holder = key_item.construct();
            insert(holder);
        }
        _list.pop(holder);
        item = (Item*)holder;
        if (item->_recycle) {
            holder = nullptr;
            item = nullptr;
            blocker.wait(lock);
        } else {
            item->_refcnt++;
        }
    } while (!item);
    {
        photon::scoped_lock lock(item->_mtx);
        if (!item->_obj) {
            item->_obj = ctor();
        }
    }
    if (!item->_obj) {
        ref_release(item, false);
        return nullptr;
    }
    return item;
}

int ObjectCacheBase::ref_release(ObjectCacheBase::Item* item, bool recycle) {
    photon::semaphore sem;
    {
        photon::scoped_lock lock(_mtx);
        if (item->_recycle) recycle = false;
        if (recycle) {
            item->_recycle = &sem;
        }
        item->_refcnt--;
        if (item->_refcnt == 0) {
            if (item->_recycle) {
                item->_recycle->signal(1);
            } else {
                enqueue(item);
            }
        }
    }
    if (recycle) {
        sem.wait(1);
        {
            photon::scoped_lock lock(_mtx);
            assert(item->_refcnt == 0);
            ItemPtr ptr(item);
            _set.erase(ptr);
            (void)ptr.release();
        }
        blocker.notify_all();
    }
    return 0;
}

// the argument `key` plays the roles of (type-erased) key
int ObjectCacheBase::release(const ObjectCacheBase::Base::Item& key_item,
                             bool recycle) {
    auto item = find(key_item);
    if (!item) return -1;
    return ref_release((Item*)item, recycle);
}
