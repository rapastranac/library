#pragma once
#ifndef SERIALIZE_HPP
#define SERIALIZE_HPP

#include "Binary.hpp"

#include <charconv>
#include <cstddef>
#include <cstring>
#include <vector>
#include <utility>

namespace library
{
    class Serialize
    {

    private:
        Binary rw;

    public:
        Serialize() {}
        ~Serialize() {}

        template <typename... Args>
        void serialize(Args &&... args)
        {
            const int ARGS_SIZE = sizeof...(args);
            std::vector<std::pair<int, char *>> v(ARGS_SIZE + 1);

            v[0].first = sizeof(int);
            v[0].second = new char[sizeof(int)];
            std::memcpy(v[0].second, &ARGS_SIZE, sizeof(int));

            serializeSTL(v, 1, args...);
            int byteSize = (v.size() - 1) * sizeof(int);

            for (int i = 1; i < v.size(); i++)
            {
                byteSize += v[i].first;
            }

            byteSize += sizeof(int);

            //size_t backwards;
            //std::memcpy(&backwards, v[2].second, sizeof(size_t));

            //char raw[byteSize];
            rw.allocate(byteSize);
            buildRaw(*rw, v, ARGS_SIZE);

            std::cout
                << "Hello, World!" << std::endl;
        }

        void unserialize(char *raw)
        {
            int ARGS_SIZE;
            int disp_unit;
            int count;
            std::memcpy(&ARGS_SIZE, raw, sizeof(int));

            std::cout << "Hello, World!" << std::endl;
        }

        void buildRaw(char *raw, std::vector<std::pair<int, char *>> &v, int ARGS_SIZE)
        {
            int position = 0;
            int count = v[0].first;
            std::memcpy(&raw[position], &ARGS_SIZE, count);
            position += count;

            for (int i = 1; i < v.size(); i++)
            {
                count = sizeof(int);
                std::memcpy(&raw[position], &v[i].first, count);
                position += count;
            }

            for (int i = 1; i < v.size(); i++)
            {
                count = v[i].first;
                std::memcpy(&raw[position], v[i].second, count);
                position += count;
            }
        }

        void serializeSTL(std::vector<std::pair<int, char *>> &v, int item, int data)
        {
        }

        template <typename... Rest>
        void serializeSTL(std::vector<std::pair<int, char *>> &v, int item, int data, Rest &&... rest)
        {
            int disp_unit = sizeof(int);
            int count = disp_unit * 1;
            v[item].first = count;
            v[item].second = new char[count];
            std::memcpy(v[item].second, &data, count);

            serializeSTL(v, ++item, rest...);
        }

        template <typename TYPE>
        void serializeSTL(std::vector<std::pair<int, char *>> &v, int item, std::vector<TYPE> data)
        {
            int disp_unit = sizeof(TYPE);
            int count = disp_unit * data.size();
            v[item].first = count;
            v[item].second = new char[count];
            std::memcpy(v[item].second, data.data(), count);

            //TYPE backwards;
            //
            //std::memcpy(&backwards, v[item].second, disp_unit);
            //
            //printf("Hello world\n");
        }

        template <typename TYPE, typename... Rest>
        void serializeSTL(std::vector<std::pair<int, char *>> &v, int item, std::vector<TYPE> data, Rest &&... rest)
        {
        }
    };

}; // namespace library

#endif