#ifndef BINARY_HPP
#define BINARY_HPP

#include <cstddef>
#include <memory>

class Binary
{
public:
    Binary(/* args */)
    {
        this->Bytes = 0;
    }

    Binary(int Bytes)
    {
        this->Bytes = Bytes;
        buffer.reset(new char[Bytes]);
        //buffer = std::make_unique<char[]>(SIZE);
    }

    void allocate(int Bytes)
    {
        this->Bytes = Bytes;
        buffer.reset(new char[Bytes]);
    }

    ~Binary() {}

    char *operator*()
    {
        return &*buffer.get();
    }

    char &operator[](std::size_t idx)
    {
        return buffer[idx];
    }

private:
    int Bytes;
    std::unique_ptr<char[]> buffer;
};

#endif