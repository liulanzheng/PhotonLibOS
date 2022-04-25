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

#include "estring.h"
#include <algorithm>

size_t estring_view::find_first_of(const charset& set) const
{
    auto it = begin();
    for (;it != end(); ++it)
        if (set.test(*it))
            return it - begin();
    return npos;
}

size_t estring_view::find_first_not_of(const charset& set) const
{
    auto it = begin();
    for (;it != end(); ++it)
        if (!set.test(*it))
            return it - begin();
    return npos;
}

size_t estring_view::find_last_of(const charset& set) const
{
    auto it = rbegin();
    for (;it != rend(); ++it)
        if (set.test(*it))
            return &*it - &*begin();
    return npos;
}

size_t estring_view::find_last_not_of(const charset& set) const
{
    auto it = rbegin();
    for (;it != rend(); ++it)
        if (!set.test(*it))
            return &*it - &*begin();
    return npos;
}

uint64_t estring_view::to_uint64() const
{
    uint64_t ret = 0;
    for (unsigned char c : *this) {
        if (c > '9' || c < '0') return ret;
        ret = ret * 10 + (c - '0');
    }
    return ret;
}

std::string& estring::append(uint64_t x)
{
    auto begin = size();
    do
    {
        *this += '0' + x % 10;
        x /= 10;
    } while(x);
    auto end = size();
    auto ptr = &(*this)[0];
    std::reverse(ptr + begin, ptr + end);
    return *this;
}
