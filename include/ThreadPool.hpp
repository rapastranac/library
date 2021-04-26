//
// Created by andres on 2021-04-06.
//

#ifndef OMP_THREADPOOL_H
#define OMP_THREADPOOL_H

#include <fmt/format.h>

#include "Queue.hpp"

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

class GemPBA::MPI_Scheduler;

namespace ThreadPool
{

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
            this->SIZE = numThreads;

            auto f = [this, numThreads]() {

#pragma omp parallel default(none) num_threads(numThreads) shared(this) // enter parallel region
                {
#pragma omp single
                    {
                        fmt::print("Number of threads spawned : {} \n", numThreads);
                    }
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
            std::unique_lock<std::mutex> lck(this->mtx_wait);
            cv_wait.wait(lck, [this]() {
                bool flag = false;

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

        void setExternNumThreads(std::atomic<int> *external_busy_threads)
        {
            this->external_busy_threads = external_busy_threads;
        }

        void link_mpiScheduler(GemPBA::MPI_Scheduler *mpiScheduler)
        {
            this->mpiScheduler = mpiScheduler;
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

        void add_on_idle_time(std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point end)
        {
            long long temp = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
            idleTime.fetch_add(temp, std::memory_order_relaxed);
        }

        void run(int threadId)
        {
            std::function<void(int)> *_f; // pointer to the function enqueued
            bool isPop = this->q.pop(_f); // dequeuing a function
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

                    std::unique_ptr<std::function<void(int)>> func(_f); // acquire ownership of "_f"
                    (*_f)(threadId);

                    //accumulate_external_busy_threads();

                    isPop = this->q.pop(_f);
                }
                // the queue is empty here, wait for the next command
                begin = std::chrono::steady_clock::now();
                std::unique_lock<std::mutex> lock(this->mtx);
                ++this->nWaiting;

                notify_no_tasks();

                // all threads go into sleep mode when pool is launched
                this->cv.wait(lock, [this, &_f, &isPop]() {
                    isPop = this->q.pop(_f);
                    return isPop || this->isDone;
                });
                end = std::chrono::steady_clock::now();

                add_on_idle_time(begin, end); // this only measures the threads idle time
                --this->nWaiting;

                if (!isPop)
                    return; // if the queue is empty and this->isDone == true or then return
            }
        }

        /*
        void accumulate_external_busy_threads()
        {
            if (external_busy_threads)
            {
#pragma omp critical(sync_external_threads)
                {
                    --(*external_busy_threads);
                }
            }
        } */

        void notify_no_tasks()
        {
#pragma omp critical(only_one)
            {
                // this condition is met only when all threads are sleeping (no tasks)
                if (nWaiting.load() == this->size() && running)
                {
                    this->exitWait = true;
                    this->cv_wait.notify_one();
                    if (mpiScheduler)
                        mpiScheduler->notifyTaskFunnelingExit();
                }
            }
        }

        void init()
        {
            this->SIZE = 0;
            this->nWaiting = 0;
            this->isInterrupted = false;
            this->isDone = false;
            this->idleTime = 0;
            this->external_busy_threads = nullptr;
            this->mpiScheduler = nullptr;
        }

        size_t SIZE;                         // number of threads in the thread pool
        std::unique_ptr<std::thread> thread; // primary thread invoking OMP
        std::atomic<int> nWaiting;           // number of waiting threads
        bool running = false;                // running signal
        bool exitWait = false;               // wakeup signal for the thread invoking wait()

        std::atomic<bool> isDone;                // signalise that job is done
        std::atomic<bool> isInterrupted;         // signalise thread pool interruption
        std::atomic<int> *external_busy_threads; // let any external requester to know the current number of busy threads
        GemPBA::MPI_Scheduler *mpiScheduler;     // exclusive for GemPBA, used to notify that thread pool is idle
        std::atomic<long long> idleTime;         // total idle time that threads have been in sleeping mode

        std::mutex mtx;                  // controls tasks creation and their execution atomically
        std::mutex mtx_wait;             // synchronise with wait()
        std::condition_variable cv;      // used with mtx
        std::condition_variable cv_wait; // used with mtx_wait

        detail::Queue<std::function<void(int)> *> q; // task queue
    };

} // namespace ThreadPool

#endif //OMP_THREADPOOL_H
