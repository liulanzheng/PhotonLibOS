#pragma once

#include <stdint.h>

#include <type_traits>

#include "string_view.h"

namespace ConstString {

template <typename T, T... xs>
struct TList {};
template <size_t... indices>
struct index_sequence {
    template <size_t Z>
    using append = index_sequence<indices..., sizeof...(indices) + Z>;
};

template <size_t N, size_t Z>
struct make_index_sequence {
    using result =
        typename make_index_sequence<N - 1, Z>::result::template append<Z>;
};

template <size_t Z>
struct make_index_sequence<0, Z> {
    using result = index_sequence<>;
};

struct tstring_base {};

template <char... str>
struct TString : tstring_base {
    static constexpr const char chars[sizeof...(str) + 1] = {str..., '\0'};
    static constexpr const size_t len = sizeof...(str);

    using type = TString<str...>;

    template <char ch>
    using prepend = TString<ch, str...>;
    template <char ch>
    using append = TString<str..., ch>;

    template <char... rhs>
    decltype(auto) concat(TString<rhs...>) {
        return TString<str..., rhs...>{};
    }

    template <size_t... indices>
    decltype(auto) _do_substr(index_sequence<indices...>) {
        return TString<chars[indices]...>();
    }

    static constexpr std::string_view view() { return {chars, len}; }
};

template <char... str>
constexpr const char TString<str...>::chars[sizeof...(str) + 1];

template <char... str>
constexpr const size_t TString<str...>::len;

template <typename str, size_t... indices>
constexpr decltype(auto) build_tstring(index_sequence<indices...>) {
    return TString<str().chars[indices]...>();
}

#define TSTRING(x)                                                         \
    [] {                                                                   \
        struct const_str {                                                 \
            const char* chars = x;                                         \
        };                                                                 \
        return ConstString::build_tstring<const_str>(                      \
            ConstString::make_index_sequence<sizeof(x) - 1, 0>::result{}); \
    }()

constexpr TString<> concat_tstring() { return TString<>{}; }

template <typename Arg>
constexpr decltype(auto) concat_tstring(Arg arg) {
    return arg;
}

template <typename LS, typename RS, typename... Args>
constexpr decltype(auto) concat_tstring(LS ls, RS rs, Args... args) {
    return concat_tstring(ls.concat(rs), args...);
}

template <char>
constexpr TString<> join_tstring() {
    return TString<>{};
}

template <char sp, typename Arg>
constexpr decltype(auto) join_tstring(Arg arg) {
    return arg;
}

template <char sp, typename LS, typename RS, typename... Args>
constexpr decltype(auto) join_tstring(LS ls, RS rs, Args... args) {
    using with_tail = typename LS::template append<sp>;
    return join_tstring<sp>(with_tail{}.concat(rs), args...);
}

template <typename T, T... xs>
struct Accumulate {
    template <T x>
    using prepend = Accumulate<T, 0, (xs + x)...>;

    constexpr static T arr[sizeof...(xs)] = {xs...};
};

template <typename T, T... xs>
constexpr T Accumulate<T, xs...>::arr[];

template <typename T, T x, T... xs>
constexpr decltype(auto) accumulate_helper(TList<T, x, xs...>) {
    return typename decltype(
        accumulate_helper(TList<T, xs...>{}))::template prepend<x>{};
}

template <typename T>
constexpr Accumulate<T, 0> accumulate_helper(TList<T>) {
    return {};
}

template <typename... Tss>
struct TStrArray {
    template <typename T>
    using prepend = TStrArray<T, Tss...>;

    constexpr static decltype(auto) whole() {
        return join_tstring<'\0', Tss...>(Tss()...);
    }
    constexpr static std::string_view views[sizeof...(Tss)] = {Tss::view()...};
    constexpr static auto offset =
        decltype(accumulate_helper(TList<int16_t, (Tss::len + 1)...>{})){};
};

template <typename... Tss>

constexpr std::string_view TStrArray<Tss...>::views[];

template <char SP, char IGN, char head, char... tail>
struct Spliter {
    using current = typename std::conditional<
        head == SP, TString<>,
        typename std::conditional<
            head == IGN, typename Spliter<SP, IGN, tail...>::current,
            typename Spliter<SP, IGN, tail...>::current::template prepend<
                head> >::type>::type;
    using next = typename std::conditional<
        head == SP, Spliter<SP, IGN, tail...>,
        typename Spliter<SP, IGN, tail...>::next>::type;
    using arr = typename next::arr::template prepend<current>;
    static constexpr size_t len = next::len + 1;
    static constexpr arr array{};
};

template <char SP, char IGN, char head, char... tail>
constexpr size_t Spliter<SP, IGN, head, tail...>::len;

template <char SP, char IGN>
struct Spliter<SP, IGN, '\0'> {
    using current = TString<>;
    using next = Spliter<SP, IGN, '\0'>;
    using arr = TStrArray<>;
    static constexpr size_t len = 0;
    static constexpr arr array{};
};

template <char SP, char IGN>
constexpr size_t Spliter<SP, IGN, '\0'>::len;

template <char SP, char Skip = '\0', char... chars>
constexpr auto split_helper(TString<chars...>)
    -> Spliter<SP, Skip, chars..., '\0'> {
    return {};
}

template <typename EnumType, typename Split>
struct EnumStr : public Split {
    using Enum = EnumType;
    std::string_view operator[](EnumType x) const {
        return {base() + off((int)x),
                (size_t)(off((int)x + 1) - off((int)x)) - 1};
    }

    constexpr static decltype(auto) whole() { return Split::arr::whole(); }

    constexpr static decltype(auto) arr() { return typename Split::arr{}; }

    static int16_t off(int i) { return Split::arr::offset.arr[i]; }

    constexpr static const char* base() { return Split::arr::whole().chars; }

    constexpr static size_t size() { return Split::len; }
};

#define DEFINE_ENUM_STR(enum_name, str_name, ...)                    \
    enum class enum_name { __VA_ARGS__ };                            \
    static const auto str_name = [] {                                \
        auto x = TSTRING(#__VA_ARGS__);                              \
        using sp = decltype(ConstString::split_helper<',', ' '>(x)); \
        return ConstString::EnumStr<enum_name, sp>();                \
    }()

}  // namespace ConstString