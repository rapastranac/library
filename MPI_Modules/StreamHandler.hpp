#ifndef STREAMHANDLER_HPP
#define STREAMHANDLER_HPP

#include <string>
#include <sstream>
#include <functional>
#include <tuple>

//Generic Massive Parallelisation of Branching Algorithm
namespace GemPBA
{

    class StreamHandler
    {
    private:
        std::stringstream ss;

    public:
        StreamHandler() {}
        ~StreamHandler() {}

        void build_buffer(auto &serializer, auto &tuple)
        {
            auto _serializer = std::bind_front(serializer, std::ref(ss));
            std::apply(_serializer, tuple);
        }

        void read_buffer(auto &deserializer, auto &tuple)
        {
            auto _deserializer = std::bind_front(deserializer, std::ref(ss));
            std::apply(_deserializer, tuple);
        }

        StreamHandler &operator()(const char *buffer, const int SIZE) noexcept
        {
            for (int i = 0; i < SIZE; i++)
            {
                ss << buffer[i];
            }
            return *this;
        }

        void clear()
        {
            ss.str(std::string());
            ss.clear();
        }

        bool empty()
        {
            return ss.str().empty();
        }

        int size() const noexcept
        {
            return ss.str().size();
        }

        std::string string_buf()
        {
            return ss.str();
        }
    };
}
#endif