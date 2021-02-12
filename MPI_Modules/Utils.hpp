#pragma once

#ifndef UTILS_HPP
#define UTILS_HPP

class Utils
{
public:
    /*begin<--- General unpack tuple and passes arguments to callable -----*/

    template <typename F, typename Tuple, size_t... I>
    static void unpack_tuple(F &&f, std::stringstream &ss, Tuple &t, std::index_sequence<I...>)
    {
        f(ss, std::get<I>(t)...);
    }

    template <typename F, typename Tuple>
    static void unpack_tuple(F &&f, std::stringstream &ss, Tuple &t)
    {
        static constexpr auto size = std::tuple_size<Tuple>::value;
        unpack_tuple(f, ss, t, std::make_index_sequence<size>{});
    }

    /*------- General unpack tuple and passes arguments to callable ----->end*/
};

#endif