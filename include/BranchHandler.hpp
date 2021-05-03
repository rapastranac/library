#pragma once
#ifndef BRANCHHANDLER_H
#define BRANCHHANDLER_H

/*
* Created by Andres Pastrana on 2019
* pasr1602@usherbrooke.ca
* rapastranac@gmail.com
*/
#include <fmt/format.h>
#include "args_handler.hpp"
#include "DLB_Handler.hpp"
//#include "pool_include.hpp"
#include "ThreadPool.hpp"

#ifdef MPI_ENABLED
#include <mpi.h>
#include <stdio.h>
#endif

#include <any>
#include <atomic>
#include <bits/stdc++.h>
#include <chrono>
#include <future>
#include <functional>
#include <list>
#include <iostream>
#include <math.h>
#include <mutex>
#include <queue>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace GemPBA
{
	template <typename _Ret, typename... Args>
	class ResultHolder;

	template <typename _Ret, typename... Args>
	class Emulator;

	class MPI_Scheduler;

	class BranchHandler
	{
		template <typename _Ret, typename... Args>
		friend class GemPBA::ResultHolder;

		template <typename _Ret, typename... Args>
		friend class GemPBA::Emulator;

		friend class MPI_Scheduler;

	protected:
		void add_on_idle_time(std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point end)
		{
			double time_tmp = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
			idleTime.fetch_add(time_tmp, std::memory_order_relaxed);
		}

	public:
		double getPoolIdleTime()
		{
			return thread_pool->idle_time() / (double)processor_count;
		}

		int getPoolSize() //ParBranchHandler::getInstance().MaxThreads() = 10;
		{
			return this->thread_pool->size();
		}

		void initThreadPool(int poolSize)
		{
			this->processor_count = poolSize;
			thread_pool = std::make_unique<ThreadPool::Pool>(poolSize);
		}

		//seconds
		double idle_time()
		{
			double nanoseconds = idleTime / ((double)processor_count + 1);
			return nanoseconds * 1.0e-9; // convert to seconds
		}

		void holdSolution(auto &bestLocalSolution)
		{
			this->bestSolution = std::make_any(bestLocalSolution);
		}

		void holdSolution(int refValueLocal, auto &solution, auto &serializer)
		{
			this->bestSolution_serialized.first = refValueLocal;
			this->bestSolution_serialized.second = serializer(solution);
		}
		// get number of successful thread requests
		size_t number_thread_requests()
		{
			return numThreadRequests.load();
		}
#ifdef MPI_ENABLED
		// get number for this rank
		int rank_me()
		{
			return mpiScheduler->getRank();
		}
#endif

		/* for void algorithms, this allows to reuse the pool*/
		void wait()
		{
#ifdef DEBUG_COMMENTS
			fmt::print("Main thread waiting results \n");
#endif
			this->thread_pool->wait();
		}

		bool has_result()
		{
			return bestSolution.has_value();
		}

		bool isDone()
		{
			return thread_pool->hasFinished();
		}

		void clear_result()
		{
			bestSolution.reset();
		}

		template <typename RESULT_TYPE>
		[[nodiscard]] auto fetchSolution() -> RESULT_TYPE
		{ // fetching results caught by the library=

			return std::any_cast<RESULT_TYPE>(bestSolution);
		}

		//TODO still missing some mutexes
		int *refValueTest()
		{
			return &refValueLocal;
		}

		int refValue()
		{
			return refValueLocal;
		}

		//if multi-processing, then every process should call this method
		void setRefValue(int refValue)
		{
#ifdef MPI_ENABLED
			this->refValueGlobal[0] = refValue;
#else
			this->refValueGlobal = new int[1];
			this->refValueGlobal[0] = refValue;
#endif
		}

		/*	return false if there exist already a better value, this better value is copied
			to the second parameter mostUpToDate if provided by reference
			return true if value did not exist*/
		bool updateRefValue(int new_refValue, int *mostUpToDate = nullptr)
		{
			std::scoped_lock<std ::mutex> lck(mtx);

			if (maximisation)
			{
				if (refValueLocal < new_refValue)
				{
					refValueLocal = new_refValue;
					return true;
				}
				else
				{
					if (mostUpToDate)
						*mostUpToDate = refValueLocal;
					return false;
				}
			}
			else // minimisation
			{
				if (refValueLocal > new_refValue)
				{
					refValueLocal = new_refValue;
					return true;
				}
				else
				{
					if (mostUpToDate)
						*mostUpToDate = refValueLocal;
					return false;
				}
			}
		}

	private:
		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		bool try_top_holder(F &&f, Holder &holder)
		{
			if (is_DLB)
			{
				Holder *upperHolder = dlb.checkParent(&holder);
				if (upperHolder)
				{
					if (!upperHolder->evaluate_branch_checkIn()) // checks if it's worth it to push
					{
						upperHolder->setDiscard(); // discard otherwise
						return true;			   // return true because it was still found
					}

					this->numThreadRequests++;
					upperHolder->setPushStatus();

					std::args_handler::unpack_and_push_void(*thread_pool, f, upperHolder->getArgs());
					return true; // top holder found
				}
				dlb.checkRightSiblings(&holder); // this decrements parent's children
			}
			return false; // top holder not found or just DLB disabled
		}

#ifdef MPI_ENABLED
		template <typename Holder>
		bool try_top_holder(auto &getBuffer, Holder &holder)
		{
			Holder *upperHolder = checkParent(&holder); //  if it finds it, then root has been already lowered
			if (upperHolder)
			{
				if (!upperHolder->evaluate_branch_checkIn())
				{
					upperHolder->setDiscard();
					return true; // top holder found but discarded, therefore not sent
				}

				// TODO send message in here

				return true; // top holder found
			}
			return false; // top holder not found
		}

#endif

	public:
		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		bool push_multithreading(F &&f, int id, Holder &holder)
		{
			/* the underlying loop breaks when current holder is treated,
				whether is pushed to the pool or forwarded */
			while (true)
			{
				/* this lock must be adquired before checking the condition,	
					even though busyThreads is atomic*/
				std::unique_lock<std::mutex> lck(mtx, std::defer_lock);

				if (lck.try_lock())
				{
					if (thread_pool->n_idle() > 0)
					{
						if (try_top_holder<_ret>(f, holder))
							continue; // keeps iterating from root to current level

						//after this line, only leftMost holder should be pushed
						this->numThreadRequests++;
						holder.setPushStatus();
						dlb.prune(&holder);
						//lck.unlock(); // [[released at destruction]] WARNING. ATTENTION, CUIDADO, PILAS !!!!

						std::args_handler::unpack_and_push_void(*thread_pool, f, holder.getArgs());
						return true; // pushed to pool
					}
					lck.unlock();
				}

				this->forward<_ret>(f, id, holder);
				return false;
			}
		}

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		bool push_multithreading(F &&f, int id, Holder &holder)
		{
			/*This lock must be performed before checking the condition,
			even though numThread is atomic*/
			std::unique_lock<std::mutex> lck(mtx);
			//if (busyThreads < thread_pool->size())
			if (thread_pool->n_idle() > 0)
			{
				if (is_DLB)
				{
					//bool res = try_top_holder<_ret>(lck, f, holder);
					//if (res)
					//	return false; //if top holder found, then it should return false to keep trying

					dlb.checkRightSiblings(&holder);
				}
				this->numThreadRequests++;
				holder.setPushStatus();

				lck.unlock();
				auto ret = std::args_handler::unpack_and_push_non_void(*thread_pool, f, holder.getArgs());
				holder.hold_future(std::move(ret));
				return true;
			}
			else
			{
				lck.unlock();
				if (is_DLB)
				{
					auto ret = this->forward<_ret>(f, id, holder, true);
					holder.hold_actual_result(ret);
				}
				else
				{
					auto ret = this->forward<_ret>(f, id, holder);
					holder.hold_actual_result(ret);
				}
				return true;
			}
		}

		template <typename _ret, typename F, typename Holder>
		bool try_push_MT(F &&f, int id, Holder &holder)
		{
			return push_multithreading<_ret>(f, id, holder);
		}

		template <typename _ret, typename F, typename Holder, typename Serializer>
		bool try_push_MP(F &&f, int id, Holder &holder, Serializer &&serializer)
		{
			bool _flag = push_multiprocess(id, holder, serializer);

			if (_flag)
				return _flag;
			else
				return try_push_MT<_ret>(f, id, holder);
		}

#ifdef MPI_ENABLED

		bool push_multiprocess(int id, auto &holder, auto &&serializer)
		{
			/* the underlying loop breaks when:
				- successful buffer pushed to main thread
				- unable to push if main thread busy
				- there is not next available process
			*/
			while (true)
			{
				std::unique_lock<std::mutex> lck(mtx_MPI, std::defer_lock);
				if (lck.try_lock()) // if mutex acquired, other threads will jump this section
				{
					//auto getBuffer = [&serializer, &holder]() {
					//	return std::apply(serializer, holder.getArgs());
					//};

					//TODO implement DLB_Handler in here	************************************
					//if (mpiScheduler->acquirePriority())
					//{
					//	if (try_top_holder(getBuffer, holder))
					//		continue; // keeps iterating from root to current level
					//	mpiScheduler->releasePriority();
					//}
					// ****************************************************************************

					if (mpiScheduler->acquirePriority())
					{
						auto getBuffer = [&serializer, &holder]() {
							return std::apply(serializer, holder.getArgs());
						};

						mpiScheduler->push(getBuffer());
						holder.setMPISent();
						dlb.prune(&holder);
						return true;
					}

					/*
					if (mpiScheduler->tryPush(getBuffer))
					{
						holder.setMPISent();
						dlb.prune(&holder);
						return true;
					} */
				}
				return false;
			}
		}

		template <typename _ret, typename F, typename Holder, typename F_SERIAL>
		bool push_multiprocess(F &&f, int id, Holder &holder, F_SERIAL &&f_serial, bool)
		{
			bool _flag = false;
			while (!_flag)
				_flag = push_multiprocess<_ret>(f, id, holder, f_serial);

			return _flag;
		}

		template <typename _ret, typename F, typename Holder, typename F_SERIAL,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		bool push_multiprocess(F &&f, int id, Holder &holder, F_SERIAL &&f_serial)
		{
			int r = try_another_process(holder, f_serial);
			if (r == 0)
				return true;
			if (r == 2)
				return false;

			return push_multithreading<_ret>(f, id, holder);
		}

#endif
		// no DLB_Handler begin **********************************************************************

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		_ret forward(F &&f, int threadId, Holder &holder)
		{
			holder.setForwardStatus();
			return std::args_handler::unpack_and_forward_non_void(f, threadId, holder.getArgs(), &holder);
		}

		// no DLB_Handler ************************************************************************* end

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		_ret forward(F &&f, int threadId, Holder &holder)
		{
#ifdef MPI_ENABLED
			if (holder.is_pushed() || holder.is_MPI_Sent())
				return;
#else
			if (holder.is_pushed())
				return;
#endif
			if (is_DLB)
				dlb.checkLeftSibling(&holder);

			holder.setForwardStatus();
			std::args_handler::unpack_and_forward_void(f, threadId, holder.getArgs(), &holder);
		}

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		_ret forward(F &&f, int threadId, Holder &holder, bool)
		{

			if (holder.is_pushed())
				return holder.get();

			if (is_DLB)
				dlb.checkLeftSibling(&holder);

			return forward<_ret>(f, threadId, holder);
		}

		template <typename _ret, typename F, typename Holder, typename F_DESER,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		_ret forward(F &&f, int threadId, Holder &holder, F_DESER &&f_deser, bool)
		{
			if (holder.is_pushed() || holder.is_MPI_Sent()) //TODO.. this should be considered when using DLB_Handler and pushing to another processsI
				return holder.get(f_deser);					//return {}; // nope, if it was pushed, then result should be retrieved in here

			if (is_DLB)
				dlb.checkLeftSibling(&holder);

			return forward<_ret>(f, threadId, holder);
		}

		/* 	
			types must be passed through the brackets constructBufferDecoder<_Ret, Args...>(..), so it's
			known at compile time.
			
			_Ret: stands for the return type of the main function
			Args...: is the type list of original type of the function, without considering int id, and void* parent
			
			input: this method receives the main algorithm and a deserializer.
			
			return:  a lambda object who is in charge of receiving a raw buffer, this 
			lambda object will deserialize the buffer and create a new Holder containing
			the deserialized arguments. Lambda object will push to thread pool and it
			will return a pointer to the holder
			*/
		template <typename _Ret, typename... Args>
		[[nodiscard]] auto constructBufferDecoder(auto &&callable, auto &&deserializer)
		{
			return [this, callable, deserializer](const char *buffer, const int count) {
				using HolderType = GemPBA::ResultHolder<_Ret, Args...>;
				HolderType *holder = new HolderType(dlb, -1);

				std::stringstream ss;
				for (int i = 0; i < count; i++)
				{
					ss << buffer[i];
				}

				auto _deser = std::bind_front(deserializer, std::ref(ss));
				std::apply(_deser, holder->getArgs());

				try_push_MT<_Ret>(callable, -1, *holder);

				return holder;
				//return nullptr;
			};
		}

		// this returns a lambda function which returns the best results as raw data
		[[nodiscard]] auto constructResultFetcher()
		{
			return [this]() {
				if (bestSolution_serialized.first == -1)
					return std::make_pair(0, static_cast<std::string>("Empty buffer, no result"));
				else
					return bestSolution_serialized;
			};
		}

		[[nodiscard]] auto constructResultFetcher(auto *holder, auto &&deserializer)
		{
			return [this]() {
				if (bestSolution_serialized.first == -1)
					return std::make_pair(0, static_cast<std::string>("Empty buffer, no result"));
				else
					return bestSolution_serialized;
			};
		}

	private:
		void init()
		{
			this->processor_count = std::thread::hardware_concurrency();
			this->idleTime = 0;
			this->numThreadRequests = 0;
			this->bestSolution_serialized.first = -1; // this allows to avoid sending empty buffers
		}

		std::atomic<size_t> numThreadRequests;

		/*This section refers to the strategy wrapping a function
			then pruning data to be use by the wrapped function<<---*/
		std::any bestSolution;
		std::pair<int, std::string> bestSolution_serialized;

		DLB_Handler &dlb = GemPBA::DLB_Handler::getInstance();
#ifdef R_SEARCH
		bool is_DLB = true;
#else
		bool is_DLB = false;
#endif

		/*------------------------------------------------------>>end*/
		/* "processor_count" would allow to set by default the maximum number of threads
			that the machine can handle unless the user invokes setMaxThreads() */

		unsigned int processor_count;
		std::atomic<long long> idleTime;
		std::mutex mtx; //local mutex
		std::condition_variable cv;
		std::unique_ptr<ThreadPool::Pool> thread_pool;

		BranchHandler()
		{
			init();
		}

	public:
		static BranchHandler &getInstance()
		{
			static BranchHandler instance;
			return instance;
		}

		~BranchHandler()
		{
#ifndef MPI_ENABLED
			delete[] refValueGlobal;
#endif
		}
		BranchHandler(const BranchHandler &) = delete;
		BranchHandler(BranchHandler &&) = delete;
		BranchHandler &operator=(const BranchHandler &) = delete;
		BranchHandler &operator=(BranchHandler &&) = delete;

		void passMPIScheduler(MPI_Scheduler *mpiScheduler)
		{
			this->mpiScheduler = mpiScheduler;
			mpiScheduler->linkRefValue(&refValueGlobal);
			refValueGlobal[0] = refValueLocal;
			mpiScheduler->setRefValStrategyLookup(maximisation);
		}

		void setRefValStrategyLookup(std::string keyword)
		{
			// convert string to upper case
			std::for_each(keyword.begin(), keyword.end(), [](char &c) {
				c = std::toupper(c);
			});

			if (keyword == "MINIMISE")
			{
				maximisation = false;
				refValueLocal = INT_MAX;
			}
			else if (keyword != "MAXIMISE")
				throw std::runtime_error("in setRefValStrategyLookup(), keyword : " + keyword + " not recognised\n");

#ifdef MPI_ENABLED
			mpiScheduler->setRefValStrategyLookup(maximisation); // TODO redundant
#endif
		}

		/*----------------Singleton----------------->>end*/
	protected:
		int *refValueGlobal = nullptr; // shared with MPI
		int refValueLocal = INT_MIN;
		bool maximisation = true;

#ifdef MPI_ENABLED

		MPI_Scheduler *mpiScheduler = nullptr;

		std::mutex mtx_MPI;				// mutex to ensure MPI_THREAD_SERIALIZED
		int world_rank = -1;			// get the rank of the process
		int world_size = -1;			// get the number of processes/nodes
		char processor_name[128];		// name of the node
		MPI_Comm *world_Comm = nullptr; // world communicator MPI

		template <typename _ret, typename Holder, typename Serialize,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		void reply(Serialize &&serialize, Holder &holder, int src)
		{
			_ret res; // default construction of return type "_ret"
#ifdef DEBUG_COMMENTS
			fmt::print("rank {} entered reply! \n", world_rank);
#endif
			res = holder.get();

			if (src == 0) // termination, since all recursions return to center node
			{
#ifdef DEBUG_COMMENTS
				fmt::print("Cover size() : {}, sending to center \n", res.coverSize());
#endif

				bestSolution_serialized.first = refValueGlobal[0];
				auto &ss = bestSolution_serialized.second; // it should be empty
				serialize(ss, res);
			}
			else // some other node requested help and it is surely waiting for the return value
			{

#ifdef DEBUG_COMMENTS
				fmt::print("rank {} about to reply to {}! \n", world_rank, src);
#endif
				std::unique_lock<std::mutex> lck(mtx_MPI); //no other thread can retrieve nor send via MPI

				std::stringstream ss;
				serialize(ss, res);
				int count = ss.str().size();

				int err = MPI_Ssend(ss.str().data(), count, MPI_CHAR, src, 0, *world_Comm);
				if (err != MPI_SUCCESS)
					fmt::print("result could not be sent from rank {} to rank {}! \n", world_rank, src);
			}
		}

		template <typename _ret, typename Holder, typename Serialize,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		void reply(Serialize &&, Holder &, int)
		{
			thread_pool->wait();
		}

		MPI_Comm &getCommunicator()
		{
			return *world_Comm;
		}

#endif
	};

} // namespace library

#endif
