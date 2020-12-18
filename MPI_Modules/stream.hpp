#ifndef STREAM_HPP
#define STREAM_HPP

#include "oarchive.hpp"

#include <memory>

namespace archive
{

    class stream
    {
        friend class oarchive;

    public:
        stream(/* args */)
        {
            this->Bytes = 0;
        }

        stream(int Bytes)
        {
            this->Bytes = Bytes;
            delete[] buffer;
            buffer = new char[Bytes];
        }

        void allocate(int Bytes)
        {
            this->Bytes = Bytes;
            if (buffer)
                delete[] buffer;

            buffer = new char[Bytes];
        }

        auto *operator*()
        {
            return buffer;
        }

        auto &operator[](std::size_t idx)
        {
            return buffer[idx];
            //return buffer.get()[idx];
        }

        stream(const stream &) = delete;
        stream(stream &&rhs)
        {
            this->Bytes = rhs.Bytes;
            rhs.Bytes = 0;
            this->buffer = std::move(rhs.buffer);
        }
        stream &operator=(const stream &rhs)
        {
            this->Bytes = rhs.Bytes;
            std::memcpy(this->buffer, rhs.buffer, Bytes);
        }
        stream &operator=(stream &&rhs)
        {
            this->Bytes = rhs.Bytes;
            rhs.Bytes = 0;
            this->buffer = std::move(rhs.buffer);
            return *this;
        }

        ~stream()
        {
            if (buffer)
                delete[] buffer;
        }

    private:
        int Bytes;
        char *buffer = nullptr;
    };
} // namespace archive

#endif