/*********************************************************
*
*  Copyright (C) 2014 by Vitaliy Vitsentiy
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
*
*********************************************************/
#pragma once
#ifndef __ctpl_stl_thread_pool_H__
#define __ctpl_stl_thread_pool_H__

// thread pool to run user's functors with signature
//      ret func(int id, other_params)
// where id is the index of the thread that runs the functor
// ret is some return type

#include "pool_include.hpp"

namespace ctpl
{

	template <typename T>
	class Queue : public virtual POOL::detail::Queue<T>
	{
	};

	class Pool : public virtual POOL::Pool
	{
	public:
		Pool() { this->init(); }
		Pool(int numThreads)
		{
			this->init();
			this->setSize(numThreads);
			this->linkedPool = nullptr;
		}
		// the destructor waits for all the functions in the queue to be finished
		~Pool() { this->interrupt(true); }

		// number of idle threads
		int n_idle() { return this->nWaiting; }
		std::thread& get_thread(int i) { return *this->threads[i]; }

		// empty the queue
		void clear_queue()
		{
			std::function<void(int id)>* _f;
			while (this->q.pop(_f))
				delete _f; // empty the queue
		}

		// pops a functional wrapper to the original function
		std::function<void(int)> pop()
		{
			std::function<void(int id)>* _f = nullptr;
			this->q.pop(_f);
			std::unique_ptr<std::function<void(int id)>> func(_f); // at return, delete the function even if an exception occurred
			std::function<void(int)> f;
			if (_f)
				f = *_f;
			return f;
		}

		template <typename F, typename... Args>
		auto push(F&& f, Args &&... args) -> std::future<decltype(f(0, args...))>
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

		void wait()
		{
			/* There might be a lost wake up if main thread does not
				solve at least a branch. To be checked out*/
			std::unique_lock<std::mutex> lck(this->mtx2);
			cv2.wait(lck, [this]() {
				bool flag = false;

				if (nWaiting.load() == size())
					flag = true;
				return flag;
				});
		}

	protected:
		void run(int threadId)
		{
			std::shared_ptr<std::atomic<bool>> flag(this->flags[threadId]); // a copy of the shared ptr to the flag
			auto f = [this, threadId, flag /* a copy of the shared ptr to the flag */]() {
				std::atomic<bool>& _flag = *flag;
				std::function<void(int threadId)>* _f;
				bool isPop = this->q.pop(_f);
				std::chrono::steady_clock::time_point begin;
				std::chrono::steady_clock::time_point end;
				while (true)
				{
					while (isPop)
					{ // if there is anything in the queue
						/* at return, delete the function even if an exception occurred, this
							allows to free memory according to unique pointer rules*/
						std::unique_ptr<std::function<void(int threadId)>> func(_f);
						(*_f)(threadId);

						if (this->externNumThreads)
						{
							std::unique_lock<std::mutex> lock(this->mtx);
							--* this->externNumThreads;
						}

						if (_flag)
						{
							this->kill_q.push(threadId);
							this->cv2.notify_one();
							return; // Thread returns even if the queue is not empty yet
						}
						else
							isPop = this->q.pop(_f);
					}
					// the queue is empty here, wait for the next command
					begin = std::chrono::steady_clock::now();
					std::unique_lock<std::mutex> lock(this->mtx);
					++this->nWaiting;

					if (nWaiting.load() == this->size())
						this->cv2.notify_one();

					this->cv.wait(lock, [this, &_f, &isPop, &_flag]() {
						isPop = this->q.pop(_f);
						return isPop || this->isDone || _flag;
						});
					end = std::chrono::steady_clock::now();
					sumUpIdleTime(begin, end);
					--this->nWaiting;
					if (!isPop)
					{
						this->kill_q.push(threadId); //It enqueues the order in which the threads return
						this->cv2.notify_one();		 //It notifies to the thread that requested interruption, If applicable.

						return; // if the queue is empty and this->isDone == true or *flag then return
					}
				}
			};
			this->threads[threadId].reset(new std::thread(f)); // compiler may not support std::make_unique()
		}

		void init()
		{
			this->nWaiting = 0;
			this->isInterrupted = false;
			this->isDone = false;
			this->linkedPool = nullptr;
			this->idleTime = 0;
		}

		Queue<std::function<void(int id)>*> q;

		std::atomic<int> nWaiting; // how many threads are waiting
	};

} // namespace ctpl

#endif // __ctpl_stl_thread_pool_H__