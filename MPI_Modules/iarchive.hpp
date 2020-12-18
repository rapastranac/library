#ifndef IARCHIVE_HPP
#define IARCHIVE_HPP

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
    class iarchive
    {

    private:
        archive::stream *stream = nullptr;
        int NUM_ARGS;
        std::vector<std::pair<int, char *>> C;
        int Bytes;
        int idxIA;

    public:
        iarchive(archive::stream &stream)
        {
            this->NUM_ARGS = 0;
            this->Bytes = 0;
            this->stream = &stream;
        }

        ~iarchive() {}

        template <typename... Args>
        void unserialize(char *raw, Args &... args)
        {
            int ARGS_SIZE;
            int disp_unit;
            int count;
            std::memcpy(&ARGS_SIZE, raw, sizeof(int));

            std::cout << "Hello, World!" << std::endl;
        }

        auto get_oarchive()
        {
            createBuffer();
            return std::move(stream);
        }

        template <typename TYPE>
        iarchive &operator<<(TYPE &buffer)
        {
            set(buffer);
            return *this;
        }

        template <typename TYPE>
        iarchive &operator>>(TYPE &buffer)
        {
            get(buffer);
            return buffer;
        }

        void get(int &buffer)
        {
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

        void set(int &buffer)
        {
            ++this->NUM_ARGS;
            C.resize(NUM_ARGS);

            int disp_unit = sizeof(int);
            int count = disp_unit * 1;
            this->Bytes += count;

            C.back().first = count;
            C.back().second = new char[count];
            std::memcpy(C.back().second, &buffer, count);

            //   return *this;
        }
        template <typename TYPE>
        void set(std::vector<TYPE> &buffer)
        {
            ++this->NUM_ARGS;
            C.resize(NUM_ARGS);

            int disp_unit = sizeof(TYPE);
            int count = disp_unit * buffer.size();
            this->Bytes += count;

            C.back().first = count;
            C.back().second = new char[count];

            int i = 0;
            for (auto it : buffer)
            {
                std::memcpy(&C.back().second[i], &it, disp_unit);
            }
            //   return *this;
        }
    };

}; // namespace archive

#endif