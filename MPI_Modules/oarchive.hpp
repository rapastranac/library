#ifndef OARCHIVE_HPP
#define OARCHIVE_HPP

#include "stream.hpp"

#include <charconv>
#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

/*
* Created by Andres Pastrana on 2020
* andres.pastrana@usherbrooke.ca
* rapastranac@gmail.com
*/

/* 
* Non copyable structure
* it contains serialized data in bytes
*/

namespace archive
{
    class oarchive
    {

    private:
        archive::stream *stream = nullptr;
        int NUM_ARGS;
        std::vector<std::pair<int, char *>> C;
        int Bytes;
        int idxIA;

    public:
        oarchive(archive::stream &stream)
        {
            this->NUM_ARGS = 0;
            this->Bytes = 0;
            this->stream = &stream;
        }

        ~oarchive()
        {
            for (int i = 0; i < C.size(); i++)
            {
                delete[] C[i].second;
            }
        }

        template <typename TYPE>
        oarchive &operator<<(TYPE &buffer)
        {
            set(buffer);
            finishBuffer();
            return *this;
        }

    private:
        void createBuffer()
        {
            int bytes = (NUM_ARGS + 1) * sizeof(int);
            Bytes += bytes;
            stream->allocate(Bytes);
            int position = 0;
            int count = sizeof(int);
            std::memcpy(&stream[position], &NUM_ARGS, count);
            position += count;

            for (int i = 0; i < C.size(); i++)
            {
                count = sizeof(int);
                std::memcpy(&stream[position], &C[i].first, count);
                position += count;
            }

            for (int i = 0; i < C.size(); i++)
            {
                count = C[i].first;
                std::memcpy(&stream[position], C[i].second, count);
                position += count;
            }
        }

        void finishBuffer()
        {
            int counter = 0;
            int count;
            //archive::stream streamCpy;
            //if (stream)
            //{
            //    if (stream->Bytes != 0)
            //    {
            //        streamCpy = *stream;
            //
            //        return;
            //    }
            //}

            int firstBytes = (NUM_ARGS + 1) * sizeof(int);
            stream->allocate(Bytes + firstBytes);

            std::memcpy(&stream[0], &NUM_ARGS, sizeof(int)); //NUM_ARGS copied into stream
            counter += sizeof(int);

            for (int i = 0; i < NUM_ARGS; i++)
            {
                count = sizeof(int);
                
                std::memcpy(&stream[counter], &C[i].first, count); //Bytes count copied
                counter += sizeof(int);
            }

            for (size_t i = 0; i < NUM_ARGS; i++)
            {
                count = C[i].first;
                std::memcpy(&stream[counter], C[i].second, count);
                counter += C[i].first;
            }
        }
        void set(const int &src)
        {
            ++this->NUM_ARGS;

            int disp_unit = sizeof(int);
            int count = disp_unit * 1;
            this->Bytes += count;

            C.emplace_back(std::make_pair(count, new char[count]));
            std::memcpy(C.back().second, &src, count);
        }
        template <typename TYPE>
        void set(const std::vector<TYPE> &src)
        {
            ++this->NUM_ARGS;

            int disp_unit = sizeof(TYPE);
            int count = disp_unit * src.size();
            this->Bytes += count;

            C.emplace_back(std::make_pair(count, new char[count]));
            std::memcpy(C.back().second, src.data(), count);
        }
    };

}; // namespace archive

#endif