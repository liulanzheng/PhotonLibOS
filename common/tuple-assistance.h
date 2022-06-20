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

#pragma once

#include <tuple>

struct tuple_assistance
{
    template<int...>
    struct seq { };

    template<int N, int...S>
    struct gens : gens<N-1, N-1, S...> { };

    template<int...S>
    struct gens<0, S...>
    {
        typedef seq<S...> type;
    };

    template <class F>
    struct function_traits;

    // function pointer
    template <class R, class... Args>
    struct function_traits<R (*)(Args...)>
        : public function_traits<R(Args...)> {};

    template <class R, class... Args>
    struct function_traits<R(Args...)> {
        using return_type = R;

        using arguments = std::tuple<Args...>;
    };

    // member function pointer
    template <class C, class R, class... Args>
    struct function_traits<R (C::*)(Args...)>
        : public function_traits<R(C&, Args...)> {};

    // const member function pointer
    template <class C, class R, class... Args>
    struct function_traits<R (C::*)(Args...) const>
        : public function_traits<R(C&, Args...)> {};

    // member object pointer
    template <class C, class R>
    struct function_traits<R(C::*)> : public function_traits<R(C&)> {};

    template <typename T>
    struct __remove_first_type_in_tuple {};

    template <typename T, typename... Ts>
    struct __remove_first_type_in_tuple<std::tuple<T, Ts...>> {
        typedef std::tuple<Ts...> type;
    };

    // functor
    template <class F>
    struct function_traits {
    private:
        using call_type = function_traits<decltype(&F::operator())>;

    public:
        using return_type = typename call_type::return_type;

        using arguments =
            typename __remove_first_type_in_tuple<typename call_type::arguments>::type;
    };

    template <class F>
    struct function_traits<F&> : public function_traits<F> {};

    template <class F>
    struct function_traits<F&&> : public function_traits<F> {};

    template <class F>
    struct callable {
        using return_type = typename function_traits<F>::return_type;
        using arguments = typename function_traits<F>::arguments;

        template<typename...Ts>
        static decltype(auto) apply(F f, std::tuple<Ts...>& args)
        {
            typedef typename gens<sizeof...(Ts)>::type S;
            return do_apply(S(), f, args);
        }

    protected:
        template<int...S, typename...Ts>
        static decltype(auto) do_apply(seq<S...>, F f, std::tuple<Ts...>& args)
        {
            return f(std::forward<Ts>(std::get<S>(args))...);
        }
    };

    template<typename P, size_t I, typename...Ts>
    struct do_enumerate;

    template<typename P, size_t I, typename...Ts>
    struct do_enumerate<P, I, std::tuple<Ts...>>
    {
        static_assert(0 < I && I < sizeof...(Ts), "");
        static void proc(const P& p, std::tuple<Ts...>& t)
        {
            do_enumerate<P, I - 1, std::tuple<Ts...>>::proc(p, t);
            p.proc(std::get<I>(t));
        }
    };

    template<typename P, typename...Ts>
    struct do_enumerate<P, 0, std::tuple<Ts...>>
    {
        static void proc(const P& p, std::tuple<Ts...>& t)
        {
            p.proc(std::get<0>(t));
        }
    };

    template<typename P, typename...Ts>
    static void enumerate(const P& p, std::tuple<Ts...>& t)
    {
        do_enumerate<P, sizeof...(Ts) - 1, std::tuple<Ts...>>::proc(p, t);
    }
};


