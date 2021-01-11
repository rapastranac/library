#pragma once

#ifndef UTILS_HPP
#define UTILS_HPP

#include "serialize/oarchive.hpp"

class Utils
{
public:
    template <typename TYPE>
    static void buildBuffer(serializer::oarchive &oa, TYPE &&lastElement)
    {
        oa << lastElement;
    }

    template <typename TYPE, typename... Args>
    static void buildBuffer(serializer::oarchive &oa, TYPE &&element, Args &&... args)
    {
        oa << element;
        return buildBuffer(oa, args...);
    }

    template <typename TYPE>
    static void readBuffer(serializer::iarchive &ia, TYPE &&lastElement)
    {
        ia >> lastElement;
    }

    template <typename TYPE, typename... Args>
    static void readBuffer(serializer::iarchive &ia, TYPE &&element, Args &&... args)
    {
        ia >> element;
        readBuffer(ia, args...);
        return;
    }

    /*begin<--- General unpack tuple and passes arguments to callable -----*/
    template <typename Tuple, size_t... I>
    static void unpack_tuple(serializer::oarchive &oa, Tuple &t, std::index_sequence<I...>)
    {
        buildBuffer(oa, std::get<I>(t)...);
    }

    template <typename Tuple>
    static void unpack_tuple(serializer::oarchive &oa, Tuple &t)
    {
        static constexpr auto size = std::tuple_size<Tuple>::value;
        unpack_tuple(oa, t, std::make_index_sequence<size>{});
    }
    /*------- General unpack tuple and passes arguments to callable ----->end*/
};

#endif