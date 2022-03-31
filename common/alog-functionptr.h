#pragma once
#include <typeinfo>
#include <cxxabi.h>
#include "alog.h"

template<typename T>
inline LogBuffer& __printfp__(LogBuffer& log, T func)
{
    size_t size = 0;
    int status = -4; // some arbitrary value to eliminate the compiler warning
    auto name = abi::__cxa_demangle(typeid(func).name(), nullptr, &size, &status);
    log << "function_pointer<" << ALogString(name, size) << "> at " << (void*&)func;
    free(name);
    return log;
}

template<typename Ret, typename...Args>
inline LogBuffer& operator << (LogBuffer& log, Ret (*func)(Args...))
{
    return __printfp__(log, func);
}

template<typename Ret, typename Clazz, typename...Args>
inline LogBuffer& operator << (LogBuffer& log, Ret (Clazz::*func)(Args...))
{
    return __printfp__(log, func);
}

template<typename Ret, typename Clazz, typename...Args>
inline LogBuffer& operator << (LogBuffer& log, Ret (Clazz::*func)(Args...) const)
{
    return __printfp__(log, func);
}

