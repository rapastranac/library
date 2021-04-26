#ifndef MEDIATOR_HPP
#define MEDIATOR_HPP

#include <functional>
#include <string>

class Mediator
{
private:
    std::function<void(char *, int)> &decoder;
    std::function<std::string()> &fetcher;

public:
    explicit Mediator(auto &decoder, auto &fetcher) : decoder(decoder), fetcher(fetcher)
    {
    }

    void decode(char *buffer, int count)
    {
        decoder(buffer, count);
    }

    std::string get()
    {
        return fetcher();
    }

    bool isDone()
    {
        return false; // bh.isDone();
    }
};

#endif