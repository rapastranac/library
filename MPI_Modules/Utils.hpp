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
};

#endif