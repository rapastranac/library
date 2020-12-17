#ifndef POOL_HPP
#define POOL_HPP

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

/*
*Created by Andres Pastrana on 2020-05
*pasr1602@usherbrooke.ca
*rapastranac@gmail.com
*/

namespace POOL
{

	namespace detail
	{
		template <class T>
		class Queue
		{
		public:
			bool push(T const& value)
			{
				std::unique_lock<std::mutex> lock(this->mtx);
				this->q.push(value);
				return true;
			}
			// deletes the retrieved element, do not use for non integral types
			bool pop(T& v)
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

	protected:
	public:
		Pool() {}
		Pool(int numThreads) {}
		virtual ~Pool() {}

		// number of idle threads
		int n_idle() { return this->nWaiting; }
		std::thread& get_thread(int i) { return *this->threads[i]; }

		/*common members*/
		void linkAnotherPool(POOL::Pool* linkedPool)
		{
			this->linkedPool = linkedPool;
		}
		int size() { return static_cast<int>(this->threads.size()); }

		/*Kills a specific thread*/
		void killThread(int id)
		{
			*(this->flags[id]) = true;

			if (this->threads[id]->joinable())
			{
				this->threads[id]->join();
				threads.erase(id);
				flags.erase(id);
			}

			//TODO check the possible reason for the following mutex
			{
				std::unique_lock<std::mutex> lock(this->mtx);
				this->cv.notify_all(); // interrupt all waiting threads
			}
		}

		// change the number of threads in the pool
		// should be called from one thread, otherwise be careful to not interleave, also with this->interrupt()
		// numThreads must be >= 0
		void setSize(int numThreads)
		{
			if (!this->isInterrupted && !this->isDone)
			{
				int oldnumThreads = static_cast<int>(this->threads.size());
				if (oldnumThreads <= numThreads)
				{
					for (int i = oldnumThreads; i < numThreads; ++i)
					{
						this->flags[i] = std::make_shared<std::atomic<bool>>(false);
						this->run(i);
						printf("Pool size increased by one, thread : %d \n", i);
					}
				}
				else
				{ // the number of threads is decreased
					for (int i = oldnumThreads - 1; i >= numThreads; --i)
					{
						*this->flags[i] = true; // this thread will finish
						this->threads[i]->detach();
					}
					{
						// interrupt the detached threads that were waiting
						std::unique_lock<std::mutex> lock(this->mtx);
						this->cv.notify_all();
					}

					for (int i = oldnumThreads - 1; i >= numThreads; --i)
					{
						this->threads.erase(threads.rbegin()->first);
						this->flags.erase(flags.rbegin()->first);
					}
				}
			}
		}

		/* wait for all computing threads to finish and interrupt all threads
		 may be called asynchronously to not pause the calling thread while waiting
		 if isWait == true, all the functions in the queue are run,
		otherwise the queue is cleared without running the functions */
		//virtual void interrupt(bool isWait = false) = 0;

		void signal_interruption()
		{

			for (int i = 0, n = this->size(); i < n; ++i)
			{
				*this->flags[i] = true; // command the threads to interrupt
			}
			this->clear_queue(); // empty the queue
			{
				std::unique_lock<std::mutex> lock(this->mtx);
				this->cv.notify_all(); // interrupt all waiting threads
			}
		}

		void interrupt(bool isWait = false)
		{
			if (!isWait)
			{
				if (this->isInterrupted)
					return;
				this->isInterrupted = true;
				for (int i = 0, n = this->size(); i < n; ++i)
				{
					*this->flags[i] = true; // command the threads to interrupt
				}
				this->clear_queue(); // empty the queue
			}
			else
			{
				if (this->isDone || this->isInterrupted)
					return;
				this->isDone = true; // give the waiting threads a command to finish
			}
			{
				std::unique_lock<std::mutex> lock(this->mtx);
				this->cv.notify_all(); // interrupt all waiting threads
			}

			/*it kills threads in the order that they have finished */

			int _id;
			bool isPop = this->kill_q.pop(_id);
			while (true)
			{
				while (isPop)
				{
					if (threads[_id]->joinable())
					{
						//std::thread::id actualId = this->threads[_id]->get_id();
						threads[_id]->join();
						threads.erase(_id);
						flags.erase(_id);
						printf("Thread exited : %d \n", _id);
					}

					isPop = this->kill_q.pop(_id);
				}

				if (threads.empty())
					break;

				std::unique_lock<std::mutex> lck(this->mtx2);
				cv2.wait(lck, [this, &isPop, &_id]() {
					isPop = this->kill_q.pop(_id);
					return isPop;
					});
			}
			/*	if there were no threads in the pool but some functors in the queue,
				the functors are not deleted by the threads	therefore delete them here	*/
			this->clear_queue();
		}

		/*	when pushing recursive functions that do not require to wait for merging
			or comparing results, then main thread will wait here until it gets the
			signal that threadPool has gone totally idle, which means that
			the job has finished	*/
		virtual void wait() = 0;

		virtual void clear_queue() = 0;

		void setExternNumThreads(std::atomic<int>* externNumThreads)
		{
			this->externNumThreads = externNumThreads;
		}
		/* If this method invoked, thread will return only when pool has no more tasks to execute,
			this would apply before pushing the first task and right after finishing the last task */

			//template <typename F>
			//void wait_if_idle(F *f)
			//{
			//	std::unique_lock<std::mutex> lck(mtx2);
			//	cv3.wait(lck, [this, &f]() {
			//		if (this->size() == this->n_idle())
			//		{
			//			if (f)
			//			{
			//				(*f)(); //communicates to center node that this node just became available
			//			}
	//
			//			return true;
			//		}
			//		return false;
			//	});
			//	x0r = !x0r;
			//}

		bool isAwake()
		{
			if (awake)
			{
				awake = false;
				return true;
			}
			else
			{
				return awake;
			}
		}

		double fetchIdleTime() {
			return ((double)idleTime.load() * 1.0e-9);
		}

	protected:

		void sumUpIdleTime(std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point end) {
			long long temp = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
			idleTime.fetch_add(temp, std::memory_order_relaxed);
		}


		Pool(const Pool&) = delete;
		Pool(Pool&&) = delete;
		Pool& operator=(const Pool&) = delete;
		Pool& operator=(Pool&&) = delete;

		virtual void run(int threadId) = 0;

		virtual void init() = 0;

		std::map<int, std::unique_ptr<std::thread>> threads;
		std::map<int, std::shared_ptr<std::atomic<bool>>> flags;
		/*It stores internal threadId so it is possible to ascertain
			if a running thread belongs to this pool*/
		std::atomic<int> nWaiting; // how many threads are waiting
		bool x0r = true;
		bool awake = false;

		/*It stores threadIds in the order that they finis*/
		detail::Queue<int> kill_q;

		std::atomic<bool> isDone;
		std::atomic<bool> isInterrupted;
		std::atomic<int>* externNumThreads;
		std::atomic<long long> idleTime;

		std::mutex mtx;
		std::mutex mtx2;
		std::condition_variable cv;
		std::condition_variable cv2;

		/*mtx2 and cv2 let main thread (or any other) know
		that a new thread (that has received the signal to finish)
		has been enqueued to kill_q */

		POOL::Pool* linkedPool; //This allows to modify another linked pool
	};

} // namespace POOL

#endif // !POOL_H
