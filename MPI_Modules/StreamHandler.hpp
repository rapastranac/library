#ifndef STREAMHANDLER_HPP
#define STREAMHANDLER_HPP

#include <sstream>
#include <functional>
#include <tuple>

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

        void read_buffer(char *buf, auto &deserializer, auto &tuple)
        {
            auto _serializer = std::bind_front(deserializer, std::ref(ss));
            std::apply(_serializer, tuple);
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

        int size() const noexcept
        {
            return ss.str().size();
        }

        char *data() noexcept
        {
            return ss.str().data();
        }
    };
}
#endif