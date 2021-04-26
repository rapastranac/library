#ifndef WRAPPER_HPP
#define WRAPPER_HPP

#include <functional>
#include <string>

template <class BH>
class Mediator
{
private:
    BH &bh;
    std::function<void(char *, int)> &decoder;

public:
    explicit Mediator(BH &bh, auto &decoder) : bh(bh), decoder(decoder)
    {
    }

    void decode(char *buffer, int count)
    {
        decoder(buffer, count);
    }

    std::string get()
    {
        return bh.get();
    }

    bool isDone()
    {
        return bh.isDone();
    }
};

#endif