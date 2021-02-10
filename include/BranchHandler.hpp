#pragma once
#ifndef BRANCHHANDLER_H
#define BRANCHHANDLER_H

/*
* Created by Andres Pastrana on 2019
* pasr1602@usherbrooke.ca
* rapastranac@gmail.com
*/

#include "args_handler.hpp"
#include "pool_include.hpp"

#include "../MPI_Modules/Utils.hpp"

#include <any>
#include <atomic>
#include <bits/stdc++.h>
#include <chrono>
#include <future>
#include <list>
#include <iostream>
#include <math.h>
#include <mutex>
#include <queue>
#include <sstream>
#include <type_traits>
#include <typeinfo>
#include <utility>

#include <mpi.h>
#include <stdio.h>

namespace library
{
	template <typename _Ret, typename... Args>
	class ResultHolder;

	class Scheduler;

	class BranchHandler
	{
		template <typename _Ret, typename... Args>
		friend class library::ResultHolder;

		friend class Scheduler;

	protected:
		void sumUpIdleTime(std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point end)
		{
			double time_tmp = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
			idleTime.fetch_add(time_tmp, std::memory_order_relaxed);
		}

		void decrementBusyThreads()
		{
			std::unique_lock lck(mtx);
			--this->busyThreads;
		}

		int whichStrategy()
		{
			return appliedStrategy;
		}

		MPI_Comm &getCommunicator()
		{
			return *world_Comm;
		}

	public:
		double getPoolIdleTime()
		{
			return _pool_default->getIdleTime() / (double)processor_count;
		}

		int getMaxThreads() //ParBranchHandler::getInstance().MaxThreads() = 10;
		{
			return this->_pool_default->size();
		}

		void setMaxThreads(int poolSize)
		{
			/*	I have not implemented yet the resize of other pool*/
			//this->init();
			this->processor_count = poolSize;
			this->_pool_default = new ctpl::Pool(poolSize);
			//this->_pool_default->setSize(n);
		}

		//seconds
		double getIdleTime()
		{
			double nanoseconds = idleTime / ((double)processor_count + 1);
			return nanoseconds * 1.0e-9; // convert to seconds
		}

		size_t getUniqueId()
		{
			++idCounter;
			return idCounter;
		}

		template <typename T>
		void catchBestResult(T &&bestR)
		{
			this->bestR = std::move(bestR);
		}

		/* POTENTIALLY TO REPLACE catchBestResult()
		This is thread safe operation, user does not need  any mutex knowledge
		refValueLocal is the value to compare with a global value, usually the result
		Cond is a lambda function where the user writes its own contidion such that this values are updated or not
		result is the most up-to-date result that complies with previous condition

		reference values:
		local : value found at the end of a leaf
		global : best value within the same rank
		global absolute: best value in the whole execution "There might be a delay"
		*/
		template <class C1, typename T, class F_SERIAL>
		bool replaceIf(int refValueLocal, C1 &&Cond, T &&result, F_SERIAL &&f_serial)
		{
			std::unique_lock<std::mutex> lck(mtx_MPI);
			printf("rank %d entered replaceIf(), acquired mutex \n ", world_rank);

			if (Cond(refValueGlobal[0], refValueLocal)) // check global val in this node
			{
				// then, absolute global is checked
				int refValueGlobalAbsolute;
				int origin_count = 1;
				int target_rank = 0;
				MPI_Aint offset = 0;
				MPI_Win &window = *win_refValueGlobal; // change to a reference to the window (&window, or *window)

				//MPI::LOCK_SHARED
				//MPI::LOCK_EXCLUSIVE
				MPI_Win_lock(MPI::LOCK_EXCLUSIVE, target_rank, 0, window); // open epoch

				printf("rank %d opened epoch to send best result \n", world_rank);
				// this blocks access to this window
				// this is the only place where this window is read or modified

				MPI_Get(&refValueGlobalAbsolute, origin_count, MPI::INTEGER, target_rank, offset, 1, MPI::INTEGER, window);
				MPI_Win_flush(target_rank, window); // retrieve refValueGlobalAbsolute which is the most up-to-date value

				printf("rank %d got refValueGlobalAbsolute : %d \n", world_rank, refValueGlobalAbsolute);

				if (Cond(refValueGlobalAbsolute, refValueLocal)) // compare absolute global value against local
				{
					this->refValueGlobal[0] = refValueLocal; // updates global ref value, in this node

					MPI_Accumulate(&refValueLocal, origin_count, MPI::INTEGER, target_rank, offset, 1, MPI::INTEGER, MPI::REPLACE, window);
					MPI_Win_flush(target_rank, window); // after this line, global ref value is updated in center node, but not broadcasted

					printf("rank %d updated refValueGlobalAbsolute to %d || %d \n", world_rank, refValueLocal, refValueGlobal[0]);

					auto ss = f_serial(result); // serialized result

					printf("rank %d, cover size : %d \n", world_rank, result.coverSize());

					int SIZE = ss.str().size();
					printf("rank %d, buffer size to be sent : %d \n", world_rank, SIZE);

					bestRstream.first = refValueLocal;
					bestRstream.second = std::move(ss);

					MPI_Win_unlock(target_rank, window); // after this line, other processes can access the window

					return true;
				}
				MPI_Win_unlock(target_rank, window); // after this line, other processes can access the window
				return false;
			}
			else
				return false;
		}

		template <class C1, typename T>
		bool replaceIf(int refValueLocal, C1 &&Cond, T &&result)
		{
			std::unique_lock<std::mutex> lck(mtx);
			if (Cond(refValueGlobal[0], refValueLocal))
			{
				this->refValueGlobal[0] = refValueLocal;
				this->bestR = result; //it should move, this copy is only for testing
				return true;
			}
			else
				return false;
		}

		//BETA: It restricts the maximum recursion depth
		void setMaxDepth(int max_push_depth)
		{
			this->max_push_depth = max_push_depth;
		}

		/* for void algorithms, whether main thread helps solving some branches 
		or only pushes to solve in parallel. Main thread will sleep in here until
		a result is ready I*/
		void waitResult(bool isVoid = false)
		{
#ifdef DEBUG_COMMENTS
			printf("Main thread waiting results \n");
#endif
			if (isVoid)
				this->_pool_default->wait();

#ifdef DEBUG_COMMENTS
			printf("Main thread interrupting pool \n");
#endif
			this->_pool_default->interrupt(true);
		}

		template <typename RESULT_TYPE>
		auto retrieveResult()
		{ // fetching results caught by the library
			return std::any_cast<RESULT_TYPE>(bestR);
		}

		bool isResultDone(bool isDone = false)
		{
			std::unique_lock<std::mutex> lck(mtx);
			if (isDone)
			{
				std::call_once(isDoneFlag,
							   [this]() {
								   this->isDone = true;
								   this->singal_interruption();
							   });
			}
			if (this->isDone)
				return true;
			return false;
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

		void lock_mpi()
		{
			this->mtx_MPI.lock();
		}
		void unlock_mpi()
		{
			this->mtx_MPI.unlock();
		}

		//TODO still missing some mutexes
		int getRefValue()
		{
			//std::stringstream ss;
			//ss << "refValueGlobal address : " << refValueGlobal << "\n";
			//ss << "About to retrieve refValueGlobal \n";
			//std::cout << ss.str();
			return refValueGlobal[0];
		}

		//if multi-processing, then every process should call this method
		void setRefValue(int refValue)
		{
			if (is_MPI_enable)
			{
				// since all processes pass by here, then MPI_Bcast is effective
				this->refValueGlobal[0] = refValue;
#ifdef DEBUG_COMMENTS
				printf("rank %d, refValueGlobal has been set : %d at %p \n", world_rank, refValueGlobal[0], refValueGlobal);
#endif
				if (world_rank == 0)
				{
					int err = MPI_Bcast(refValueGlobal, 1, MPI::INTEGER, 0, *world_Comm);
					if (err != MPI::SUCCESS)
						printf("rank %d, broadcast unsucessful with err = %d \n", world_rank, err);
#ifdef DEBUG_COMMENTS
					printf("refValueGlobal broadcasted: %d at %p \n", refValueGlobal[0], refValueGlobal);
#endif
				}
				else
				{
					int err = MPI_Bcast(refValueGlobal, 1, MPI::INTEGER, 0, *world_Comm);
					if (err != MPI::SUCCESS)
						printf("rank %d, broadcast unsucessful with err = %d \n", world_rank, err);
#ifdef DEBUG_COMMENTS
					printf("rank %d, refValueGlobal received: %d at %p \n", refValueGlobal[0], refValueGlobal);
#endif
				}

				MPI_Barrier(*world_Comm);
			}
			else
			{
				this->refValueGlobal = new int[1];
				this->refValueGlobal[0] = refValue;
			}
		}

		/*begin<<------casting strategies -------------------------*/
		//https://stackoverflow.com/questions/15326186/how-to-call-child-method-from-a-parent-pointer-in-c
		//https://stackoverflow.com/questions/3747066/c-cannot-convert-from-base-a-to-derived-type-b-via-virtual-base-a
	private:
		ctpl::Pool *ctpl_casted(POOL::Pool *item)
		{
			return dynamic_cast<ctpl::Pool *>(item);
		}
		/*--------------------------------------------------------end*/

		void customPut(const void *origin_addr, int count, MPI_Datatype mpi_type, int target_rank, MPI_Aint offset, MPI_Win &window)
		{
			MPI_Win_lock(MPI::LOCK_EXCLUSIVE, target_rank, 0, window); // opens epoch
			MPI_Put(origin_addr, count, mpi_type, target_rank, offset, 1, mpi_type, window);
			MPI_Win_flush(target_rank, window);
			MPI_Win_unlock(target_rank, window); // closes epoch
		}

		template <typename Holder>
		void exclude(Holder *holder)
		{
			if (holder->parent)
			{
				holder->parent->children.pop_front();
				Holder *newLeftMost = holder->parent->children.front();
				if (holder->parent->children.size() == 1)
				{
					//(*holder->root)->itself = newLeftMost;
					(*holder->root) = &(*newLeftMost);
					(*holder->root)->parent = nullptr;
				}
				else
				{
					//this case should not happen
					int fds = 54345;
				}
			}
		}

		template <typename Holder>
		Holder *checkParent(Holder *holder)
		{

			Holder *leftMost = nullptr; // this is the branch that led us to the root
			Holder *root = nullptr;		// local pointer to root, to avoid "*" use

			if (holder->parent)
			{
				if (holder->parent != *holder->root)
				{
					/* this condition complies if a branch has already
					 been pushed, to ensure pushing leftMost first */
					root = *holder->root;			 //no need to iterate
					int tmp = root->children.size(); // this probable fix the following

					// the following is not true, it could be also the right branch
					// Unless root is guaranteed to have at least 2 children,
					// TODO ... verify

					leftMost = root->children.front(); //TODO ... check if branch has been pushed or forwarded
				}
				else
					return nullptr;
			}
			else
				return nullptr;

			//while (parent->parent) {
			//	leftMost = parent;
			//	parent = parent->parent;
			//}
			//if (leftMost->children.size() == 0)
			//{
			//	/*If this complies, then it means it is the first time
			//	this branch is going to be pushed, this avoids to push
			//	right branches before the leftMost branch, if available threads*/
			//	return nullptr;
			//}
			//		if (child == holder) {	//Why is this check?
			//			holder->parent = nullptr;
			//			return nullptr;
			//		}

			/*Here below, we check is left child was pushed to pool, then the pointer to parent is pruned*/
			//	typename std::list<Holder*>::iterator leftMost = parent->children.begin();	//Where it came from
			//	typename std::list<Holder*>::iterator nextElt = std::next(parent->children.begin(), 1); //this is to access 2nd elt
			/*				 parent
						  /  |  \   \  \
						 /   |   \	 \	 \
						/    |    \	  \	   \
					  p     cb    w1  w2 ... wk

			p	stands for pushed branch
			cb	stands for current branch
			w	stands for waiting branch, or target holder
			Following previous diagram
			if "p" is already pushed, it won't be part of children list of "parent", then list = {cb,w1,w2}
			leftMost = cb
			nextElt = w1

			There will never be fewer than two elements, asuming multiple recursion per scope,
			because as long as it remains two elements, it means than rightMost element will be pushed to pool
			and then leftMost element will no longer need a parent, which is the first condition to explore
			this level of the tree*/

			//TODO following lines applied to multiple recursion

			if (root->children.size() > 2)
			{ //this condition is for multiple recursion

				typename std::list<Holder *>::const_iterator leftMost = root->children.begin();				 //Where it came from
				typename std::list<Holder *>::const_iterator nextElt = std::next(root->children.begin(), 1); //this is to access 2nd elt
				//nextElt is the one to be pushed
				auto *toBePushed = *nextElt;
				root->children.erase(nextElt); /*it erases second element already pointed by nextElt,
														instead of searching it using remove()*/
				toBePushed->parent = nullptr;  /*It won't need a parent since it'll be pushed*/

				if (toBePushed->isBoundCond)
				{
					bool temp = toBePushed->boundCond();
					if (temp)
						return toBePushed;
					else
						return nullptr;
				}
				return toBePushed;
			}
			else if (root->children.size() == 2)
			{
				/*	this scope is meant to push right branch which was put in waiting line
					because there was no available thread to push leftMost branch, then leftMost
					will be the new root since after this scope right branch will have been
					already pushed*/

				//	Holder* leftMost = root->children.front();

				leftMost->parent = nullptr;				// parent no longer needed
				root->children.pop_front();				// deletes leftMost from root's children
				Holder *right = root->children.front(); //The one to be pushed
				root->children.clear();
				right->parent = nullptr; //parent not needed since it'll be pushed
				right->root = nullptr;

				*leftMost->root = &(*leftMost); // leftMost is the new root

				Holder *test = rootCorrecting(leftMost);

				// next condition no really necessary, just for testing
				if (test != leftMost)
				{
					int dfds = 3423;
				}

				//(*leftMost->root)->parent = nullptr;	//root has no parent

				if (right->isBoundCond)
				{
					bool temp = right->boundCond();
					if (temp)
						return right;
					else
						return nullptr;
				}
				return right;
			}
			else
			{
				/*
				* this scenario would happen when a root has pushed all its children but one,
				* also this remaining one has been probably forwarded, also, this remaining holder
				* might have only one children due to the same reason as above. Then, root should
				* be relocated to the lowest spot
				*/
				/*	Holder* uniqueChild = root->children.front();
					if (uniqueChild->isForwarded) {

						if (uniqueChild->children.size() == 2)

							while (uniqueChild->isForwarded)
							{

							}
					}*/

				std::cout << "4 Testing, it's not supposed to happen" << std::endl;
				return nullptr;
			}

			//		child->parent = nullptr;	//parent is pruned because it is no longer needed
			//
			//		if (parent->right->isBoundCond) {
			//			/*Check if it's worth it to push this holder since bound condition might be ...
			//			... crucial to decide whether to explore branch or not, and this bound condition...
			//			...	could be linked to a global variable.
			//			This is important because condition in the target function is not explore after thread passes by there,
			//			and if this scope is used, it means that thread hasn't passed by there yet but it will eventually do */
			//			bool temp = parent->right->boundCond();
			//			if (temp)
			//				return parent->right;
			//			else
			//				return nullptr;
			//		}
			//
			//		return parent->right;		//holder right is returned
			//
			//	return parent;
		}

		template <typename Holder>
		void checkLeftSibling(Holder *holder)
		{

			/* What does it do?. Having the following figure
						  root == parent
						  /  |  \   \  \
						 /   |   \	 \	 \
						/    |    \	  \	   \
					  pb     cb    w1  w2 ... wk
					  △ -->
			pb	stands for previous branch
			cb	stands for current branch
			w_i	stands for waiting branch, or target holder i={1..k}

			if pb is fully solved sequentially or w_i were pushed but there is at least
				one w_i remaining, then thread will return to first level where the
				parent is also the root, then lestMost child of the root should be
				deleted of the list since is already solved. Thus, pushing cb twice
				is avoided because checkParent() pushes the second element of the children
			*/

			if (holder->parent) //this confirms the holder is not a root
			{
				if (holder->parent == *holder->root) //this confirms that it's the first level of the root
				{
					Holder *leftMost = holder->parent->children.front();
					if (leftMost != holder) //This confirms pb has already been solved
					{
						/* next conditional should always comply, there should not be required
						* to use a loop, then this While is entitled to just a single loop. 4 testing!!
						*/
						while (leftMost != holder)
						{
							holder->parent->children.pop_front();
							leftMost = holder->parent->children.front();
						}
						// Then this remainting child will become a new root
						leftMost->parent = nullptr;
						*(leftMost->root) = &(*leftMost);
					}
				}
				else if (holder->parent != *holder->root) //any other level,
				{
					Holder *leftMost = holder->parent->children.front();
					if (leftMost != holder) //This confirms pb has already been solved
					{
						/*this scope only deletes leftMost holder, which is already
						* solved sequentially by here and leaves the parent with at
						* least a child because an uppter holder still has a holder in
						* the waiting list
						*/
						holder->parent->children.pop_front();
					}
				}
			}
		}

		template <typename Holder>
		Holder *rootCorrecting(Holder *root) //->decltype(holder)
		{
			Holder *_root = root;

			while (_root->children.size() == 1) // lowering the root
			{
				_root = _root->children.front();
				_root->parent->children.pop_front();
				_root->parent = nullptr;
				*(_root->root) = &(*_root);
			}

			return _root;
		}

		template <typename F, typename... Rest>
		auto pushSeed(F &&f, Rest &&...rest)
		{
			auto _pool = ctpl_casted(_pool_default);
			this->busyThreads = 1;								// forces just in case
			auto future = std::move(_pool->push(f, rest...));	// future type can handle void returns
			auto lambda = [&future]() { return future.get(); }; // create lambda to pass to args_handler::invoke_void()

			return std::args_handler::invoke_void(lambda); //this guarantees to assign a non-void value
		}

	public:
		template <typename F, typename Holder>
		int push(F &&f, int id, Holder &holder)
		{
			auto _pool = ctpl_casted(_pool_default);
			/*This lock must be performed before checking the condition,
			even though numThread is atomic*/
			std::unique_lock<std::mutex> lck(mtx);
			if (busyThreads < _pool->size())
			{
				busyThreads++;
				holder.setPushStatus(true);

				lck.unlock();
				auto tmp = std::args_handler::unpack_tuple(_pool, f, holder.getArgs());
				holder.hold_future(std::move(tmp));
				return 1;
			}
			else
			{
				lck.unlock();
				auto retVal = this->forward(f, id, holder);
				holder.hold_actual_result(retVal);
				return 0;
			}
		}

		template <typename F, typename Holder, typename F_SERIAL>
		int push(F &&f, int id, Holder &holder, F_SERIAL &&f_serial)
		{
			bool signal{false};
			auto _pool = ctpl_casted(_pool_default);
			/*This lock must be performed before checking the condition,
			even though numThread is atomic*/
			std::unique_lock<std::mutex> lck(mtx);
			if (busyThreads < _pool->size())
			{
				busyThreads++;
				holder.setPushStatus(true);

				lck.unlock();
				auto tmp = std::args_handler::unpack_tuple(_pool, f, holder.getArgs());
				holder.hold_future(std::move(tmp));
				return 1;
			}
			lck.unlock();													// this allows to other threads to keep pushing in the pool
			std::unique_lock<std::mutex> mpi_lck(mtx_MPI, std::defer_lock); // this guarantees mpi_thread_serialized

			if (mpi_lck.try_lock()) // center node is in charge of broadcasting this number
			{
				if (numAvailableNodes[0] > 0)
				{

					printf("%d about to request center node to push\n", world_rank);
					bool buffer = true;
					customPut(&buffer, 1, MPI::BOOL, 0, world_rank, *win_boolean); // send signal to center node
					MPI_Status status;

					printf("process %d requested to push \n", world_rank);
					MPI_Recv(&signal, 1, MPI::BOOL, 0, MPI::ANY_TAG, *world_Comm, &status); //awaits signal if data can be sent

					if (signal)
					{
						int dest = status.MPI_TAG;
						holder.setMPISent(true, dest);
						printf("process %d received ID %d\n", world_rank, dest);

						//serializer::stream os;
						//serializer::oarchive oa(os);
						//Utils::unpack_tuple(oa, holder.getArgs());
						std::stringstream ss = std::args_handler::unpack_tuple(f_serial, holder.getArgs());

						int count = ss.str().size();										// number of Bytes
						int err = MPI_Ssend(&count, 1, MPI::INTEGER, dest, 0, *world_Comm); // send buffer size
						if (err != MPI::SUCCESS)
							printf("count could not be sent from rank %d to center! \n", world_rank);

						err = MPI_Ssend(ss.str().data(), count, MPI::CHARACTER, dest, 0, *world_Comm); // send buffer
						if (err == MPI::SUCCESS)
							printf("buffer sucessfully sent from rank %d to rank %d! \n", world_rank, dest);

						//TODO how to retrieve result from destination rank?

						mpi_lck.unlock();
						printf("process %d forwarded to process %d \n", world_rank, dest);
						return 2;
					}
					else // numAvailableNodes might have changed due to delay
					{
						mpi_lck.unlock();
						printf("process %d push request failed, forwarded!\n", world_rank);
						auto retVal = this->forward(f, id, holder);
						holder.hold_actual_result(retVal);
						return 0;
					}
				}
				mpi_lck.unlock(); // this ensures to unlock it even if (numAvailableNodes == 0)
			}

			//lck.unlock(); // unlocked already before mpi push
			auto retVal = this->forward(f, id, holder);
			holder.hold_actual_result(retVal);
			return 0;
		}

		template <typename F, typename Holder>
		bool push(F &&f, int id, Holder &holder, bool trackStack)
		{
			bool _flag = false;

			ctpl::Pool *_pool = ctpl_casted(_pool_default);
			while (!_flag)
				_flag = push_to_pool_ctpl(_pool, f, id, holder);

			return _flag;
		}

		template <typename F, typename Holder>
		auto forward(F &&f, int threadId, Holder &holder)
		{
			//std::args_handler::unpack_tuple(f, threadId, holder.getArgs());
			return std::args_handler::unpack_tuple(f, threadId, holder.getArgs());
		}

		template <typename F, typename Holder>
		auto forward(F &&f, int threadId, Holder &holder, bool trackStack)
		{
			//if (holder.isPushed) {	//Fix this, it cannot always return Void()
			//	//printf("Pushed already \n");
			//	return std::args_handler::Void();
			//}
			if (is_LB)
			{
				checkLeftSibling(&holder);
			}

			holder.setForwardStatus(true);
			holder.threadId = threadId;
			return std::args_handler::unpack_tuple(&holder, f, threadId, holder.getArgs(), trackStack);
		}

		//Not useful yet
		void singal_interruption()
		{
			this->_pool_default->signal_interruption();
		}

		/*This syncronizes available threads in branchHandler with
			busyThreads in pool, this is relevant when using void functions
			because there is no need to call getResults(),
			nevertheless this should be avoided when pushing void functions*/
		void functionIsVoid()
		{
			this->_pool_default->setExternNumThreads(&this->busyThreads);
		}

	private:
		void init()
		{
			this->processor_count = std::thread::hardware_concurrency();
			this->busyThreads = 0;
			this->idleTime = 0;
			this->isDone = false;
			this->_pool_default = nullptr;
			this->_pool_2 = nullptr;
			this->max_push_depth = -1;
			this->superFlag = false;
			this->idCounter = 0;
		}

		int appliedStrategy = -1;
		//size_t idCounter;
		std::atomic<size_t> idCounter;

		/*This section refers to the strategy wrapping a function
			then pruning data to be use by the wrapped function<<---*/
		bool isDone;
		int max_push_depth;
		std::once_flag isDoneFlag;
		std::any bestR;
		std::pair<int, std::stringstream> bestRstream;
		bool is_LB = false;

		/*Dequeue while using strategy myPool*/
		bool isDequeueEnable = false;
		int targetArg;

		/*------------------------------------------------------>>end*/
		/* "processor_count" would allow to set by default the maximum number of threads
			that the machine can handle unless the user invokes setMaxThreads() */

		unsigned int processor_count;
		std::atomic<long long> idleTime;
		std::atomic<int> busyThreads;
		std::mutex mtx;		//local mutex
		std::mutex mtx_MPI; //local mutex
		bool is_mtx_mpi_acquired = false;
		std::condition_variable cv;
		std::atomic<bool> superFlag;
		POOL::Pool *_pool_default = nullptr;
		POOL::Pool *_pool_2 = nullptr;

		/*begin<<----------------Singleton----------------*/
		static BranchHandler *INSTANCE;
		static std::once_flag initInstanceFlag;

		static void initSingleton()
		{
			INSTANCE = new BranchHandler();
		}

		BranchHandler()
		{
			init();
		}

	public:
		static BranchHandler &getInstance()
		{
			std::call_once(initInstanceFlag, &BranchHandler::initSingleton);
			return *INSTANCE;
		}

	public:
		~BranchHandler() = default;
		BranchHandler(const BranchHandler &) = delete;
		BranchHandler(BranchHandler &&) = delete;
		BranchHandler &operator=(const BranchHandler &) = delete;
		BranchHandler &operator=(BranchHandler &&) = delete;

		/*----------------Singleton----------------->>end*/
	protected:
		/* MPI parameters */
		std::function<std::any(std::any)> _serialize;
		std::function<std::any(std::any)> _deserialize;

		int *refValueGlobal = nullptr;

		bool is_MPI_enable = false;
		int world_rank = -1;	  // get the rank of the process
		int world_size = -1;	  // get the number of processes/nodes
		char processor_name[128]; // name of the node

		MPI_Win *win_boolean = nullptr;
		MPI_Win *win_NumNodes = nullptr;
		MPI_Win *win_AvNodes = nullptr;
		MPI_Win *win_finalFlag = nullptr;
		MPI_Win *win_accumulator = nullptr;
		MPI_Win *win_inbox_bestResult = nullptr;
		MPI_Win *win_refValueGlobal = nullptr;

		MPI_Comm *world_Comm = nullptr;
		MPI_Comm *second_Comm = nullptr;
		MPI_Comm *SendToNodes_Comm = nullptr;
		MPI_Comm *SendToCenter_Comm = nullptr;
		MPI_Comm *NodeToNode_Comm = nullptr;

		int *numAvailableNodes;
		bool request_response = false;

		void linkMPIargs(int world_rank,
						 int world_size,
						 char *processor_name,
						 int *numAvailableNodes,
						 int *refValueGlobal,
						 MPI_Win *win_accumulator,
						 MPI_Win *win_finalFlag,
						 MPI_Win *win_AvNodes,
						 MPI_Win *win_boolean,
						 MPI_Win *win_inbox_bestResult,
						 MPI_Win *win_NumNodes,
						 MPI_Win *win_refValueGlobal,
						 MPI_Comm *world_Comm,
						 MPI_Comm *second_Comm,
						 MPI_Comm *SendToNodes_Comm,
						 MPI_Comm *SendToCenter_Comm,
						 MPI_Comm *NodeToNode_Comm)
		{
			this->world_rank = world_rank;
			this->world_size = world_size;
			this->numAvailableNodes = numAvailableNodes;
			this->refValueGlobal = refValueGlobal;
			strncpy(this->processor_name, processor_name, 128);
			this->win_accumulator = win_accumulator;
			this->win_AvNodes = win_AvNodes;
			this->win_finalFlag = win_finalFlag;
			this->win_boolean = win_boolean;
			this->win_inbox_bestResult = win_inbox_bestResult;
			this->win_NumNodes = win_NumNodes;
			this->win_refValueGlobal = win_refValueGlobal;
			this->world_Comm = world_Comm;
			this->second_Comm = second_Comm;
			this->SendToNodes_Comm = SendToNodes_Comm;
			this->SendToCenter_Comm = SendToCenter_Comm;
			this->NodeToNode_Comm = NodeToNode_Comm;
		}

		/* if method receives data, this node is supposed to be totally idle */
		template <typename Result, typename F, typename Serialize, typename Deserialize, typename Holder>
		void receiveSeed(F &&f, Serialize &&serialize, Deserialize &&deserialize, Holder &holder)
		{
			bool onceFlag = false;
			int count_rcv = 0;

			std::string msg = "avalaibleNodes[" + std::to_string(world_rank) + "]";
			bool flag = true;
			customPut(&flag, 1, MPI::BOOL, 0, world_rank, *win_AvNodes);
			printf("process %d put flag [true] in process 0 \n", world_rank);

			while (true)
			{
				MPI_Status status;
				/* if a thread passes succesfully this method, library gets ready to receive data*/

				printf("Receiver called on process %d, avl processes %d \n", world_rank, numAvailableNodes[0]);

				printf("Receiver on %d ready to receive \n", world_rank);
				int Bytes; // Bytes to be received
				MPI_Recv(&Bytes, 1, MPI::INTEGER, MPI::ANY_SOURCE, MPI::ANY_TAG, *world_Comm, &status);
				count_rcv++;
				int src = status.MPI_SOURCE;
				printf("process %d has rcvd from %d,%d times \n", world_rank, src, count_rcv);
				if (status.MPI_TAG == 3)
				{
					printf("Exit tag received on process %d \n", world_rank); // loop termination
					return;
				}
				printf("Receiver on %d, received %d Bytes \n", world_rank, Bytes);

				char *in_buffer = new char[Bytes];
				MPI_Recv(in_buffer, Bytes, MPI::CHARACTER, MPI::ANY_SOURCE, MPI::ANY_TAG, *world_Comm, &status);

				Holder newHolder(*this); // copy types

				std::stringstream ss;
				for (int i = 0; i < Bytes; i++)
					ss << in_buffer[i];

				Utils::unpack_tuple(deserialize, ss, newHolder.getArgs());

				accumulate(1, 1, MPI::INT, 0, 0, *win_accumulator, "busyNodes++");

				delete[] in_buffer;
				//for (size_t i = 0; i < std::get<0>(newHolder.getArgs()).size(); i++)
				//{
				//	printf("%d ", std::get<0>(newHolder.getArgs())[i]);
				//}

				//TODO Warning, probably call mtx_MPI in here

				if (!onceFlag)
				{
					if (MPI_COMM_NULL != *second_Comm)
						MPI_Barrier(*second_Comm);
					//onceFlag = true; //prevents to synchronise again if process #1 gets free, yet job is not finished
					onceFlag = true;
				}

				//auto retVal = pushSeed(f, -1, args...);
				push(f, 0, newHolder); // first push, node is idle

				//temporary
				//std::vector<size_t> retVal;
				//newHolder.get(retVal);

				reply<Result>(serialize, newHolder, src);

				printf("Passed on process %d \n", world_rank);
				//				_pool.wait();
				//accumulate(1, 0, world_rank, win_AvNodes, "availableNodes++");
				accumulate(-1, 1, MPI::INT, 0, 0, *win_accumulator, "busyNodes--");
			}
		}

		template <typename Result, typename Holder, typename Serialize,
				  std::enable_if_t<!std::is_void_v<Result>, int> = 0>
		void reply(Serialize &&serialize, Holder &holder, int src)
		{
			Result res;
			printf("rank %d entered reply! \n", world_rank);
			holder.get(res);
			printf("rank %d about to reply to %d! \n", world_rank, src);

			if (src == 0) // termination, since all recursions return to center node
			{
				std::unique_lock<std::mutex> lck(mtx_MPI); // in theory other threads should be are idle, TO DO ..
				//this sends a signal so center node turns into receiving mode
				bool buffer = true;
				customPut(&buffer, 1, MPI::BOOL, src, 0, *win_finalFlag);
				printf("rank %d put to finalFlag! \n", world_rank);

				std::stringstream ss = serialize(res);
				int count = ss.str().size();

				int err = MPI_Ssend(&count, 1, MPI::INTEGER, src, 0, *world_Comm);
				if (err != MPI::SUCCESS)
					printf("count could not be sent from rank %d to rank %d! \n", world_rank, src);

				err = MPI_Ssend(ss.str().data(), count, MPI::CHARACTER, src, 0, *world_Comm);
				if (err != MPI::SUCCESS)
					printf("final result could not be sent from rank %d to rank %d! \n", world_rank, src);
			}
			else // some other node requested help and it is surely waiting for the result
			{
				std::unique_lock<std::mutex> lck(mtx_MPI); //no other thread can retrieve nor send via MPI

				std::stringstream ss = serialize(res);
				int count = ss.str().size();

				int err = MPI_Ssend(&count, 1, MPI::INTEGER, src, 0, *world_Comm);
				if (err != MPI::SUCCESS)
					printf("count could not be sent from rank %d to rank %d! \n", world_rank, src);

				err = MPI_Ssend(ss.str().data(), count, MPI::CHARACTER, src, 0, *world_Comm);
				if (err != MPI::SUCCESS)
					printf("result could not be sent from rank %d to rank %d! \n", world_rank, src);
			}
		}

		template <typename Result, typename Holder, typename Serialize,
				  std::enable_if_t<std::is_void_v<Result>, int> = 0>
		void reply(Serialize &&serialize, Holder &holder, int src)
		{
			//this->waitResult(true);
			_pool_default->wait();
			sendBestResultToCenter();
		}

		// this should is supposed to be called only when all tasks are finished
		void sendBestResultToCenter()
		{
			//sending signal to center so this one turn into receiving best result mode
			int signal = true;
			customPut(&signal, 1, MPI::BOOL, 0, world_rank, *win_inbox_bestResult);

			void *buffer = bestRstream.second.str().data();
			int Bytes = bestRstream.second.str().size();
			int refVal = bestRstream.first;

			MPI_Ssend(&Bytes, 1, MPI::INTEGER, 0, 0, *world_Comm);

			MPI_Ssend(buffer, Bytes, MPI::CHARACTER, 0, refVal, *world_Comm);
			printf("rank %d sent best result, Bytes : %d, refVal : %d\n", world_rank, Bytes, refVal);
		}

		void accumulate(int buffer, int origin_count, MPI_Datatype mpi_datatype, int target_rank, MPI_Aint offset, MPI_Win &window, std::string msg)
		{
			printf("%d about to accumulate on %s\n", world_rank, msg.c_str());
			MPI_Win_lock(MPI::LOCK_EXCLUSIVE, target_rank, 0, window);

			MPI_Accumulate(&buffer, origin_count, mpi_datatype, target_rank, offset, 1, mpi_datatype, MPI::SUM, window);

			printf("%d about to unlock RMA on %d \n", world_rank, target_rank);
			MPI_Win_flush(target_rank, window);
			printf("%d between flush and unlock \n", world_rank);
			MPI_Win_unlock(target_rank, window);

			printf("%s, by %d\n", msg.c_str(), world_rank);
		}
	};
	BranchHandler *BranchHandler::INSTANCE = nullptr;
	std::once_flag BranchHandler::initInstanceFlag;

} // namespace library

#endif
