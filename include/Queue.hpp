#ifndef QUEUE_HPP
#define QUEUE_HPP

#include <queue>
#include <mutex>
#include <utility>

namespace detail
{
    template <class T>
    class Queue
    {
    public:
        bool push(T const &value)
        {
            std::unique_lock<std::mutex> lock(this->mtx);
            this->q.push(value);
            return true;
        }

        // deletes the retrieved element, do not use for non integral types
        bool pop(T &v)
        {
            std::unique_lock<std::mutex> lock(this->mtx);
            if (this->q.empty())
                return false;
            v = this->q.front();
            this->q.pop();
            return true;
        }

        bool empty()
        {
            std::unique_lock<std::mutex> lock(this->mtx);
            return this->q.empty();
        }

    private:
        std::queue<T> q;
        std::mutex mtx;
    };
} // namespace detail

#endif