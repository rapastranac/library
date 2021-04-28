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
			return thread_pool->getIdleTime() / (double)processor_count;
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
		double getIdleTime()
		{
			double nanoseconds = idleTime / ((double)processor_count + 1);
			return nanoseconds * 1.0e-9; // convert to seconds
		}

		void holdSolution(auto &bestR)
		{
			this->bestR = bestR;
		}

		template <typename T>
		void holdSolution(T &solution, auto &serializer)
		{
			this->bestResultBuffer.second = serializer(solution);
		}

		size_t getNumberRequests()
		{
			return requests.load();
		}
#ifdef MPI_ENABLED
		int getRankID()
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
			return bestR.has_value();
		}

		bool isDone()
		{
			return thread_pool->hasFinished();
		}

		void clear_result()
		{
			bestR.reset();
		}

		template <typename RESULT_TYPE>
		[[nodiscard]] auto fetchSolution() -> RESULT_TYPE
		{ // fetching results caught by the library=

			return std::any_cast<RESULT_TYPE>(bestR);
		}

		void lock()
		{
			this->mtx.lock();
		}
		void unlock()
		{
			//https://developercommunity.visualstudio.com/content/problem/459999/incorrect-lock-warnings-by-analyzer-c26110-and-c26.html
#pragma warning(suppress : 26110)
			this->mtx.unlock();
		}

#ifdef MPI_ENABLED

		void lock_mpi()
		{
			this->mtx_MPI.lock();
		}
		void unlock_mpi()
		{
			this->mtx_MPI.unlock();
		}

#endif

		//TODO still missing some mutexes
		int *getRefValueTest()
		{
			return refValueGlobal;
		}

		int getRefValue()
		{
			if (refValueGlobal)
				return refValueGlobal[0];
			else
			{
				fmt::print("Error, refValueGlobal has not been initialized in BranchHandler.hpp \n");
				throw std::runtime_error("Error, refValueGlobal has not been initialized in BranchHandler.hpp\n");
			}
		}

		//if multi-processing, then every process should call this method
		void setRefValue(int refValue)
		{

#ifdef MPI_ENABLED
			mpiScheduler->linkRefValue(&refValueGlobal);
			this->refValueGlobal[0] = refValue;
#else
			this->refValueGlobal = new int[1];
			this->refValueGlobal[0] = refValue;
#endif
		}

		void updateRefValue(int val)
		{
			refValueGlobal[0] = val;
			bestResultBuffer.first = val;
			bestResultBuffer.second = std::to_string(val);
		}

		void updateRefValue(int val, std::string_view keyword)
		{
			mpiScheduler->updateRefValue(val, keyword);
			refValueGlobal[0] = val;
			bestResultBuffer.first = val;
			bestResultBuffer.second = std::to_string(val);
		}

		/*begin<<------casting strategies -------------------------*/
		//https://stackoverflow.com/questions/15326186/how-to-call-child-method-from-a-parent-pointer-in-c
		//https://stackoverflow.com/questions/3747066/c-cannot-convert-from-base-a-to-derived-type-b-via-virtual-base-a
	private:
		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		bool try_top_holder(std::unique_lock<std::mutex> &lck, F &&f, Holder &holder)
		{
			Holder *upperHolder = dlb.checkParent(&holder);
			if (upperHolder)
			{
				if (!upperHolder->evaluate_branch_checkIn())
				{
					upperHolder->setDiscard();
					return true;
				}

				this->requests++;
				upperHolder->setPushStatus();
				lck.unlock();

				std::args_handler::unpack_and_push_void(*thread_pool, f, upperHolder->getArgs());
				return true; // top holder found
			}
			return false; // top holder not found
		}

#ifdef MPI_ENABLED
		template <typename Holder, typename F_serial>
		bool try_top_holder(Holder &holder, F_serial &&f_serial, int dest_rank)
		{
			Holder *upperHolder = checkParent(&holder); //  if it finds it, then root has been already lowered
			if (upperHolder)
			{
				if (!upperHolder->evaluate_branch_checkIn())
				{
					upperHolder->setDiscard();
					return true;
				}

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
			/*This lock must be adquired before checking the condition,	
			even though busyThreads is atomic*/
			std::unique_lock<std::mutex> lck(mtx, std::defer_lock);

			if (lck.try_lock())
			{
				if (thread_pool->n_idle() > 0)
				{
					if (is_DLB)
					{
						bool res = try_top_holder<_ret>(lck, f, holder);
						if (res)
							return false; // if top holder found, then it should
										  // return false to keep trying another top holder

						dlb.checkRightSiblings(&holder); // this decrements parent's children
					}

					//after this line, only leftMost holder should be pushed
					this->requests++;
					holder.setPushStatus();
					//holder.prune();
					dlb.prune(&holder);
					lck.unlock();

					std::args_handler::unpack_and_push_void(*thread_pool, f, holder.getArgs());
					return true;
				}
				lck.unlock();
			}

			if (is_DLB)
				this->forward<_ret>(f, id, holder, true);
			else
				this->forward<_ret>(f, id, holder);

			return true;
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
				this->requests++;
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
		bool push_multithreading(F &&f, int id, Holder &holder, bool trackStack)
		{
			bool _flag = false;
			/* false means that current holder was not able to be pushed 
				because a top holder was pushed instead, this false allows
				to keep trying to find a top holder in the case of an
				available thread*/
			while (!_flag)
				_flag = push_multithreading<_ret>(f, id, holder);

			return _flag; // this is for user's tracking pursposes if applicable
		}

		template <typename _ret, typename F, typename Holder>
		bool try_push_MT(F &&f, int id, Holder &holder)
		{
			bool _flag = false;
			/* false means that current holder was not able to be pushed 
				because a top holder was pushed instead, this false allows
				to keep trying to find a top holder in the case of an
				available thread*/
			while (!_flag)
				_flag = push_multithreading<_ret>(f, id, holder);

			return _flag; // this is for user's tracking pursposes if applicable
		}

		template <typename _ret, typename F, typename Holder, typename Serializer>
		bool try_push_MP(F &&f, int id, Holder &holder, Serializer &&serializer)
		{
			bool _flag = push_multiprocess(id, holder, serializer);

			if (!_flag)
				return try_push_MT<_ret>(f, id, holder);

			return _flag; // this is for user's tracking pursposes if applicable
		}

#ifdef MPI_ENABLED

		bool is_node_available()
		{
			if (nextNode[0] != -1)
			{
				if (nextNode[0] != nextNode[1])
					return true; // center has put a new target
				else
					return false; // no node is available
			}
			return false; // center has not had time to put any target yet
		}

		bool push_multiprocess(int id, auto &holder, auto &&serializer)
		{
			std::unique_lock<std::mutex> lck(mtx_MPI, std::defer_lock);
			if (lck.try_lock()) // if mutex acquired, other threads will avoid this section
			{
				//TODO implement DLB_Handler in here

				auto getBuffer = [&serializer, &holder]() {
					return std::apply(serializer, holder.getArgs());
				};

				if (mpiScheduler->tryPush(getBuffer))
				{
					holder.setMPISent();
					//holder.prune();
					dlb.prune(&holder);
					return true;
				}
			}
			return false;
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

		int getBusyNodes()
		{
			return world_size - numAvailableNodes[0] - 1;
		}

#endif
		// no DLB_Handler begin **********************************************************************
		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		_ret forward(F &&f, int threadId, Holder &holder)
		{
			holder.setForwardStatus();
			//holder.threadId = threadId;
			std::args_handler::unpack_and_forward_void(f, threadId, holder.getArgs(), &holder);
		}

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		_ret forward(F &&f, int threadId, Holder &holder)
		{
			holder.setForwardStatus();
			//holder.threadId = threadId;
			return std::args_handler::unpack_and_forward_non_void(f, threadId, holder.getArgs(), &holder);
		}

		// no DLB_Handler ************************************************************************* end

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		_ret forward(F &&f, int threadId, Holder &holder, bool)
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

			forward<_ret>(f, threadId, holder);
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

		template <typename _Ret, typename... Args>
		auto constructMediators(auto &&callable, auto &&serializer, auto &&deserializer)
		{
			using HolderType = GemPBA::ResultHolder<_Ret, Args...>;

			/*
				They are wrapped with smart pointer to free memory automatically

				a	fetch the result from the holder (if applicable), serialize it and return it as a string buffer
				b	deserialize a buffer and create the first task of the thread after the node receives a task from center
				c	TODO ...
			*/
			std::shared_ptr<::function<std::string()>> a;

			auto b = [this, &a, callable, serializer, deserializer](const char *buffer, const int count) {
				//std::shared_ptr<HolderType> holder = std::make_shared<HolderType>(dlb, -1);
				HolderType *holder = new HolderType(dlb, -1);

				std::stringstream ss;
				for (int i = 0; i < count; i++)
				{
					ss << buffer[i];
				}

				auto _deser = std::bind_front(deserializer, std::ref(ss));
				std::apply(_deser, holder->getArgs());

				try_push_MT<_Ret>(callable, -1, *holder);

				a = std::make_shared<std::function<std::string()>>([holder, serializer]() {
					auto res = holder->get();
					std::stringstream ss;
					serializer(ss, res);
					delete holder;
					return ss.str();
				});
				return 0;
			};

			auto c = [this]() {
				return this->isDone();
			};

			auto d = [this]() {
				if (bestResultBuffer.first == -1)
					return std::make_pair(0, static_cast<std::string>("Empty buffer, no result"));
				else
					return bestResultBuffer;
			};

			return std::make_tuple(std::move(a), std::move(b), std::move(c), std::move(d));
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
				if (bestResultBuffer.first == -1)
					return std::make_pair(0, static_cast<std::string>("Empty buffer, no result"));
				else
					return bestResultBuffer;
			};
		}

		[[nodiscard]] auto constructResultFetcher(auto *holder, auto &&deserializer)
		{
			return [this]() {
				if (bestResultBuffer.first == -1)
					return std::make_pair(0, static_cast<std::string>("Empty buffer, no result"));
				else
					return bestResultBuffer;
			};
		}

	private:
		void init()
		{
			this->processor_count = std::thread::hardware_concurrency();
			this->idleTime = 0;
			this->superFlag = false;
			this->idCounter = 0;
			this->requests = 0;
			this->bestResultBuffer.first = -1; // this allows to avoid sending empty buffers
		}

		std::atomic<size_t> idCounter;
		std::atomic<size_t> requests;

		/*This section refers to the strategy wrapping a function
			then pruning data to be use by the wrapped function<<---*/
		std::any bestR;
		std::pair<int, std::string> bestResultBuffer;

		DLB_Handler &dlb = GemPBA::DLB_Handler::getInstance();
#ifdef DLB
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
		std::atomic<bool> superFlag;
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

		~BranchHandler() = default;
		BranchHandler(const BranchHandler &) = delete;
		BranchHandler(BranchHandler &&) = delete;
		BranchHandler &operator=(const BranchHandler &) = delete;
		BranchHandler &operator=(BranchHandler &&) = delete;

		void link_mpiScheduler(MPI_Scheduler *mpiScheduler)
		{
			this->mpiScheduler = mpiScheduler;
		}

		/*----------------Singleton----------------->>end*/
	protected:
		int *refValueGlobal = nullptr; // shared with MPI

#ifdef MPI_ENABLED

		MPI_Scheduler *mpiScheduler = nullptr;

		std::mutex mtx_MPI;				  // mutex to ensure MPI_THREAD_SERIALIZED
		int world_rank = -1;			  // get the rank of the process
		int world_size = -1;			  // get the number of processes/nodes
		char processor_name[128];		  // name of the node
		int *numAvailableNodes = nullptr; // remote memory synchronised by center node
		int *nextNode = nullptr;		  // size: world_size array nextNode[0]= {new,-1,-1,-1... -1}
		MPI_Comm *world_Comm = nullptr;	  // world communicator MPI

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

				bestResultBuffer.first = refValueGlobal[0];
				auto &ss = bestResultBuffer.second; // it should be empty
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

		void fetch_and_op(const void *origin_addr, void *result_addr,
						  MPI_Datatype datatype, int target_rank, MPI_Aint target_disp,
						  MPI_Op op, MPI_Win &win)
		{
			MPI_Win_lock(MPI_LOCK_EXCLUSIVE, target_rank, MPI_MODE_NOCHECK, win);
			MPI_Fetch_and_op(origin_addr, result_addr, datatype, target_rank, target_disp, op, win);
			MPI_Win_flush(target_rank, win);
			MPI_Win_unlock(target_rank, win);
		}

		MPI_Comm &getCommunicator()
		{
			return *world_Comm;
		}

#endif
	};

} // namespace library

#endif
