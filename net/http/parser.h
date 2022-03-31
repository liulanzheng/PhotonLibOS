#pragma once
#include "common/estring.h"

namespace Net {
namespace HTTP {

class Parser
{
public:
    Parser(std::string_view headers)
    {
        _begin = _ptr = headers.data();
        _end = _begin + headers.size();
    }
    void skip_string(std::string_view sv)
    {
        if (estring_view(_ptr, _end - _ptr).starts_with(sv)) _ptr += sv.length();
    }
    void skip_chars(char c)
    {
        if (*_ptr == c) _ptr++;
    }
    void skip_until_string(const std::string& s)
    {
        auto esv = estring_view(_ptr, _end - _ptr);
        auto pos = esv.find(s);
        if (pos == esv.npos) {
            _ptr = _end;
            return;
        }
        _ptr += pos;
    }
    uint64_t extract_integer()
    {
        auto esv = estring_view(_ptr, _end - _ptr);
        auto ret = esv.to_uint64();
        auto pos = esv.find_first_not_of(charset("0123456789"));
        if (pos == esv.npos) _ptr = _end; else _ptr += pos;
        return ret;
    }
    rstring_view16 extract_until_char(char c)
    {
        auto esv = estring_view(_ptr, _end - _ptr);
        auto pos = esv.find_first_of(c);
        auto ptr = _ptr;
        if (pos == esv.npos) _ptr = _end; else _ptr += pos;
        return {(uint16_t)(ptr - _begin), (uint16_t)(_ptr - ptr)};
    }
    bool is_done() { return _ptr == _end; }
    char operator[](size_t i) const
    {
        return _ptr[i];
    }

protected:
    const char* _ptr;
    const char* _begin;
    const char* _end;
};

}
}