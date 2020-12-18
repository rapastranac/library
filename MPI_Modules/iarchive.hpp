#ifndef IARCHIVE_HPP
#define IARCHIVE_HPP

#include <charconv>
#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>

//STL containers
#include <array>
#include <deque>
#include <list>
#include <set>
#include <queue>
#include <vector>

/*
* Created by Andres Pastrana on 2020
* andres.pastrana@usherbrooke.ca
* rapastranac@gmail.com
*/

/* 
* Non copyable structure
* it contains serialized data in bytes
|NUM_ARGS  |
|  ARG1    |
|  ARG2    |
|   ...    |
|  ARGN,   |
|ARG1_BYTES|
|ARG1_BYTES|
|   ...    |
|ARGN_BYTES|

*/

namespace archive
{
    class iarchive
    {

    private:
        char *buffer;
        int NUM_ARGS;                       // number of arguments attached to stream
        std::vector<std::pair<int, int>> C; // temporary container to store buffer section while building stream
        int Bytes;                          // number of bytes to be contained in stream
        int arg_No;                         // index in stream
        //int position;

    public:
        iarchive(archive::stream &stream)
        {
            this->NUM_ARGS = 0;
            this->Bytes = 0;
            this->arg_No = 0;
            this->buffer = &stream[0];
        }

        ~iarchive() {}

        template <typename TYPE>
        iarchive &operator>>(TYPE &target)
        {
            if (NUM_ARGS == 0)
            {
                std::memcpy(&NUM_ARGS, &buffer[0], sizeof(int));
                int idx = sizeof(int);

                int arg_indx_begin = (NUM_ARGS + 1) * sizeof(int);

                for (int i = 0; i < NUM_ARGS; i++)
                {
                    int count = sizeof(int);
                    int argBytes;
                    std::memcpy(&argBytes, &buffer[idx], count);
                    idx += count;
                    C.emplace_back(std::make_pair(argBytes, arg_indx_begin));
                    arg_indx_begin += argBytes;
                }
            }
            unserialize(target);

            return *this;
        }

    private:
        void unserialize(int &target)
        {
            int disp_unit = sizeof(int);
            int count = C[arg_No].first;
            int start = C[arg_No].second;
            std::memcpy(&target, &buffer[start], count);
            ++arg_No;
        }

        template <typename TYPE>
        void unserialize(std::vector<TYPE> &target)
        {
            int disp_unit = sizeof(TYPE);
            int count = C[arg_No].first;
            int start = C[arg_No].second;
            target.resize(count / disp_unit);
            std::memcpy(target.data(), &buffer[start], count);
            ++arg_No;
        }

        template <typename TYPE>
        void unserialize(std::set<TYPE> &target)
        {
            int disp_unit = sizeof(TYPE);
            int count = C[arg_No].first;
            int start = C[arg_No].second;
            int SIZE = count / disp_unit;

            for (int i = 0; i < SIZE; i++)
            {
                TYPE item;
                std::memcpy(&item, &buffer[start], disp_unit);
                target.insert(item);

                start += disp_unit;
            }
            ++arg_No;
        }

        template <typename TYPE>
        void unserialize(std::list<TYPE> &target)
        {
            int disp_unit = sizeof(TYPE);
            int count = C[arg_No].first;
            int start = C[arg_No].second;
            int SIZE = count / disp_unit;

            for (int i = 0; i < SIZE; i++)
            {
                TYPE item;
                std::memcpy(&item, &buffer[start], disp_unit);
                target.push_back(item);

                start += disp_unit;
            }
            ++arg_No;
        }
         template <typename TYPE>
        void unserialize(std::queue<TYPE> &target)
        {
            int disp_unit = sizeof(TYPE);
            int count = C[arg_No].first;
            int start = C[arg_No].second;
            int SIZE = count / disp_unit;

            for (int i = 0; i < SIZE; i++)
            {
                TYPE item;
                std::memcpy(&item, &buffer[start], disp_unit);
                target.push(item);

                start += disp_unit;
            }
            ++arg_No;
        }
    };

}; // namespace archive

#endif