#pragma once
#ifndef BINARY_H
#define BINARY_H

#include <memory>

class Binary
{
public:
    Binary(/* args */)
    {
        this->SIZE = 0;
    }

    Binary(int SIZE)
    {
        this->SIZE = SIZE;
        buffer.reset(new char[SIZE]);
        //buffer = std::make_unique<char[]>(SIZE);
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
    int SIZE;
    std::unique_ptr<char[]> buffer;
};

#endif