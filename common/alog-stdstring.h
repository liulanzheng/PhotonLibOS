#pragma once
#include <string>
#include <photon/common/alog.h>
#include <photon/common/string_view.h>

inline LogBuffer& operator << (LogBuffer& log, const std::string& s)
{
    return log << ALogString(s.c_str(), s.length());
}

inline LogBuffer& operator << (LogBuffer& log, const std::string_view& sv)
{
    return log << ALogString(sv.data(), sv.length());
}

class string_key;
inline LogBuffer& operator << (LogBuffer& log, const string_key& sv)
{
    return log << (const std::string_view&)sv;
}

class estring;
inline LogBuffer& operator << (LogBuffer& log, const estring& es)
{
    return log << (const std::string&)es;
}

class estring_view;
inline LogBuffer& operator << (LogBuffer& log, const estring_view& esv)
{
    return log << (const std::string_view&)esv;
}

