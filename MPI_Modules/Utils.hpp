#pragma once

#ifndef UTILS_HPP
#define UTILS_HPP

#include "serialize/oarchive.hpp"

class Utils
{
public:
    template <typename TYPE>
    static void buildBuffer(bool flag, archive::oarchive &oa, TYPE &&lastElement)
    {
        oa << lastElement;
    }

    template <typename TYPE, typename... Args>
    static void buildBuffer(bool flag, archive::oarchive &oa, TYPE &&element, Args &&... args)
    {
        if (!flag)
        {
            //just ignores firs element which is int id
            flag = true;
            return buildBuffer(flag, oa, args...);
        }
        else
        {
            oa << element;
            return buildBuffer(flag, oa, args...);
        }
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
        return readBuffer(ia, args...);
    }

};

#endif