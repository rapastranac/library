#pragma once

#ifndef UTILS_HPP
#define UTILS_HPP

#include "serialize/oarchive.hpp"

class Utils
{
public:
    template <typename TYPE>
    static void buildBuffer(archive::oarchive &oa, TYPE &&lastElement)
    {
        oa << lastElement;
    }

    template <typename TYPE, typename... Args>
    static void buildBuffer(archive::oarchive &oa, TYPE &&element, Args &&... args)
    {
        oa << element;
        buildBuffer(oa, args...);
    }

    template <typename TYPE>
    static void readBuffer(archive::iarchive &ia, TYPE &&lastElement)
    {
        ia >> lastElement;
    }

    template <typename TYPE, typename... Args>
    static void readBuffer(archive::iarchive &ia, TYPE &&element, Args &&... args)
    {
        ia >> element;
        readBuffer(ia, args...);
    }
};

#endif