//
// Created by andres on 2021-04-06.
//

#ifndef OMP_THREADPOOL_H
#define OMP_THREADPOOL_H

#include "fmt/format.h"

#include <any>
#include <atomic>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include <map>
#include <set>
#include <omp.h>

/*
*Created by Andres Pastrana on 2020-05
*pasr1602@usherbrooke.ca
*rapastranac@gmail.com
*/

/*
 * This pool has a fixed size during the whole execution, it's non-copyable, non-deletable, non-movable,
 * once it is interrupted, it cannot be interrupted again. To wait the result of the solution, use wait(),
 * this will wait until all tasks have been resolved.
 *
 * This thread pool spawns a single thread manually, and this one creates parallel region using openMP
 * */

namespace ThreadPool
{

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

    class Pool
    {
    public:
        Pool() { this->init(); }

        Pool(int numThreads)
        {
            this->init();
            this->setSize(numThreads);
        }

        ~Pool() { this->interrupt(); }

        // number of idle threads
        int n_idle() { return this->nWaiting; }

        [[nodiscard]] int size() const { return SIZE; }

        // change the number of threads in the pool
        // should be called from one thread, otherwise be careful to not interleave, also with this->interrupt()
        // numThreads must be >= 0
        void setSize(int numThreads)
        {
            fmt::print("Number of threads spawned : {} \n", numThreads);
            this->SIZE = numThreads;

            auto f = [this, numThreads]() {

#pragma omp parallel default(shared) num_threads(numThreads) // enter parallel region
                {
                    int tid = omp_get_thread_num(); // get thread id
                    run(tid);                       // run thread pool
                }                                   // leave parallel region
            };

            thread = std::make_unique<std::thread>(f);
            while (nWaiting.load() != (int)SIZE)
                ; // main thread loops until one thread in thread pool has attained waiting mode
        }

        /*	when pushing recursive functions that do not require to wait for merging
            or comparing results, then main thread will wait here until it gets the
            signal that threadPool has gone totally idle, which means that
            the job has finished	*/
        void wait()
        {
            /* There might be a lost wake up if main thread does not
                solve at least a branch. To be checked out*/
            std::unique_lock<std::mutex> lck(this->mtx2);
            cv2.wait(lck, [this]() {
                bool flag = false;
                //
                ////if (nWaiting.load() == size() && running) {
                //if (nWaiting.load() == size())
                //{
                //    flag = true;   // this allows the waiting thread to exit when pool finishes its tasks
                //    running = false; // this allows to reused the pool after tasks have been finished
                //}

                if (exitWait)
                {
                    flag = true;
                    this->exitWait = false;
                }

                return flag;
            });
#ifdef DEBUG_COMMENTS
            printf("pool has finished its tasks \n");
#endif
        }

        /* If this method invoked, thread will return only when pool has no more tasks to execute,
            this would apply before pushing the first task and right after finishing the last task */

        [[maybe_unused]] bool isAwake()
        {
            if (running)
            {
                running = false;
                return true;
            }
            else
            {
                return running;
            }
        }

        void clear_queue()
        {
            std::function<void(int id)> *_f;
            while (this->q.pop(_f))
                delete _f; // empty the queue
        }

        void setExternNumThreads(std::atomic<int> *externNumThreads)
        {
            this->externNumThreads = externNumThreads;
        }

        [[maybe_unused]] double getIdleTime()
        {
            return ((double)idleTime.load() * 1.0e-9); //seconds
        }

        template <typename F, typename... Args>
        auto push(F &&f, Args &&...args) -> std::future<decltype(f(0, args...))>
        {
            using namespace std::placeholders;
            auto pck = std::make_shared<std::packaged_task<decltype(f(0, args...))(int)>>(
                std::bind(std::forward<F>(f), _1, std::forward<Args>(args)...));

            auto _f = new std::function<void(int id)>([pck](int id) {
                (*pck)(id);
            });

            this->q.push(_f);
            std::unique_lock<std::mutex> lock(this->mtx);
            this->cv.notify_one();
            return pck->get_future();
        }

        Pool(const Pool &) = delete;

        Pool(Pool &&) = delete;

        Pool &operator=(const Pool &) = delete;

        Pool &operator=(Pool &&) = delete;

    protected:
        void interrupt()
        {
            if (thread)
            {

                if (this->isDone || this->isInterrupted)
                    return;

                this->isDone = true; // give the waiting threads a command to finish
                {
                    std::unique_lock<std::mutex> lock(this->mtx);
                    this->cv.notify_all(); // interrupt all waiting threads
                }
                if (thread->joinable())
                    thread->join();

                this->clear_queue();
            }
        }

        void sumUpIdleTime(std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point end)
        {
            long long temp = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
            idleTime.fetch_add(temp, std::memory_order_relaxed);
        }

        void run(int threadId)
        {
            auto f = [this, threadId]() {
                std::function<void(int threadId)> *_f;
                bool isPop = this->q.pop(_f);
                std::chrono::steady_clock::time_point begin;
                std::chrono::steady_clock::time_point end;
                while (true)
                {
                    while (isPop)
                    { // if there is anything in the queue
                        /* at return, delete the function even if an exception occurred, this
                            allows to free memory according to unique pointer rules*/

                        if (!running)
                            running = true; // this helps blocking a main thread that launches the thread pool

                        std::unique_ptr<std::function<void(int threadId)>> func(_f);
                        (*_f)(threadId);

                        if (this->externNumThreads)
                        {
                            std::unique_lock<std::mutex> lock(this->mtx);
                            --(*this->externNumThreads);
                        }

                        isPop = this->q.pop(_f);
                    }
                    // the queue is empty here, wait for the next command
                    begin = std::chrono::steady_clock::now();
                    std::unique_lock<std::mutex> lock(this->mtx);
                    ++this->nWaiting;

                    if (nWaiting.load() == this->size() && running)
                    {
                        this->exitWait = true;
                        this->cv2.notify_one(); // this only happens when pool finishes all its tasks
                    }

                    this->cv.wait(lock,
                                  [this, &_f, &isPop]() { // all threads go into sleep mode when pool is launched
                                      isPop = this->q.pop(_f);
                                      return isPop || this->isDone;
                                  });
                    end = std::chrono::steady_clock::now();

                    sumUpIdleTime(begin, end); // this only measures the threads idle time
                    --this->nWaiting;

                    if (!isPop)
                        return; // if the queue is empty and this->isDone == true or *flag then return
                }
            };
            f();
        }

        void init()
        {
            this->nWaiting = 0;
            this->isInterrupted = false;
            this->isDone = false;
            this->idleTime = 0;
            this->externNumThreads = nullptr;
        }

        size_t SIZE = 0;
        std::unique_ptr<std::thread> thread;
        std::atomic<int> nWaiting; // how many threads are waiting
        bool running = false;
        bool exitWait = false;

        std::atomic<bool> isDone;
        std::atomic<bool> isInterrupted;
        std::atomic<int> *externNumThreads;
        std::atomic<long long> idleTime;

        std::mutex mtx;              // controls tasks creation and their execution atomically
        std::mutex mtx2;             // synchronise with wait()
        std::condition_variable cv;  // used with mtx
        std::condition_variable cv2; // used with mtx2

        detail::Queue<std::function<void(int id)> *> q;
    };

} // namespace ThreadPool

#endif //OMP_THREADPOOL_H
