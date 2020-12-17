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
#include "ResultHolder.hpp"
#include "../MPI_Modules/Scheduler.hpp"

#include <any>
#include <atomic>
#include <chrono>
#include <cmath>	/* floor, ceil */
#include <stdlib.h> /* srand, rand */
#include <future>
#include <list>
#include <iostream>
#include <math.h>
#include <mutex>
#include <queue>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace library
{
	class BranchHandler
	{
		template <typename _Ret, typename... Args>
		friend class library::ResultHolder;

		friend class Scheduler;

	protected:
		void sumUpIdleTime(std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point end)
		{
			double temp = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
			idleTime.fetch_add(temp, std::memory_order_relaxed);
		}

	public:
		double fetchPoolIdleTime()
		{
			return _pool_default->fetchIdleTime() / (double)processor_count;
		}

		int getMaxThreads() //ParBranchHandler::getInstance().MaxThreads() = 10;
		{
			return this->_pool_default->size();
		}

		void setMaxThreads(int n)
		{
			/*	I have not implemented yet the resize of other pool*/
			//this->init();
			this->_pool_default = new ctpl::Pool(n);
			this->_pool_default->setSize(n);
		}

		//seconds
		double getIdleTime()
		{
			//return idleTime / ((double)numThreadInitial + 1);
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

		//BETA: It restricts the maximum recursion depth
		void setMaxDepth(int max_push_depth)
		{
			this->max_push_depth = max_push_depth;
		}

		/*This tells mainThread to join Threads in Pool but waiting
			until they finish their tasks*/

		template <typename TYPE>
		bool waitResult(TYPE &target, bool isVoid = false)
		{
			printf("Main thread waiting results \n");
			if (isVoid)
				this->_pool_default->wait();

			printf("Main thread interrupting pool \n");
			this->_pool_default->interrupt(true);
			if (this->_pool_2)
				this->_pool_2->interrupt(true);

			// feching results caught by the library
			if (bestR.has_value())
			{
				target = std::any_cast<TYPE>(bestR);
				return true;
			}
			return false;
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

		/*begin<<------casting strategies -------------------------*/
		//https://stackoverflow.com/questions/15326186/how-to-call-child-method-from-a-parent-pointer-in-c
		//https://stackoverflow.com/questions/3747066/c-cannot-convert-from-base-a-to-derived-type-b-via-virtual-base-a
	private:
		//Strategy 0 casting
		ctpl::Pool *ctpl_casted(POOL::Pool *item)
		{
			return dynamic_cast<ctpl::Pool *>(item);
		}
		//Strategy 1 casting
		//fwPool::Pool* wrapper_casted(POOL::Pool* item) {
		//	return dynamic_cast<fwPool::Pool*>(item);
		//}
		/*--------------------------------------------------------end*/

		template <typename F, typename Holder>
		bool push_to_pool_ctpl(ctpl::Pool *_pool, F &&f, int id, Holder &holder)
		{

			/*This lock must be performed before checking the condition,
			even though numThread is atomic*/
			std::unique_lock<std::mutex> lck(mtx);
			if (numThreads < _pool->size())
			{
				if (is_LB)
				{

					Holder *upperHolder = checkParent(&holder);
					if (upperHolder)
					{
						numThreads++;

						if (std::get<1>(upperHolder->tup).fetchCover().size() == 0)
						{
							int gfdg = 34; //just to create a break point
						}

						upperHolder->isPushed = true;

						lck.unlock();
						auto tmp = std::args_handler::unpack_tuple(_pool, f, upperHolder->getArgs(), true);
						upperHolder->hold_future(std::move(tmp));
						return false;
					}

					exclude(&holder);
				}

				numThreads++;
				holder.isPushed = true;

				if (std::get<1>(holder.tup).fetchCover().size() == 0)
				{
					int gfdg = 34; //just to create a break point
				}

				lck.unlock();

				auto tmp = std::args_handler::unpack_tuple(_pool, f, holder.getArgs(), true);
				holder.hold_future(std::move(tmp));
				return true;
			}
			else
			{
				lck.unlock();

				auto val = this->forward(f, id, holder, true);
				holder.hold_actual_result(val);
				return true;
			}
			return false;
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
				//_root = root->children.front();
				//_root->children.pop_front();
				_root = _root->children.front();
				_root->parent->children.pop_front();
				_root->parent = nullptr;
				*(_root->root) = &(*_root);
			}

			return _root;
		}

	public:
		template <typename F, typename Holder>
		bool push(F &&f, int id, Holder &holder)
		{
			auto _pool = ctpl_casted(_pool_default);
			/*This lock must be performed before checking the condition,
			even though numThread is atomic*/
			std::unique_lock<std::mutex> lck(mtx);
			if (numThreads < _pool->size())
			{
				numThreads++;
				holder.isPushed = true;

				lck.unlock();
				auto tmp = std::args_handler::unpack_tuple(_pool, f, holder.getArgs());
				holder.hold_future(std::move(tmp));
				return true;
			}
			else
			{
				lck.unlock();
				auto val = this->forward(f, id, holder);
				holder.hold_actual_result(val);
				return true;
			}
			return false;
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

			holder.isForwarded = true;
			holder.threadId = threadId;
			return std::args_handler::unpack_tuple(&holder, f, threadId, holder.getArgs(), trackStack);
		}

		//Not useful yet
		void singal_interruption()
		{
			this->_pool_default->signal_interruption();
		}

		//These methods set what kind of pool is going to be used
		//This particular method instantiate the pool
		void setStrategy(int strat)
		{
			if (strat == 0)
			{
				_pool_default = new ctpl::Pool();
			}
			else if (strat == 1)
			{
				//	_pool_default = new fwPool::Pool<_Ret, Args...>();
			}
		}

		//This overloaded method instantiate the pool and set its size
		void setStrategy(int strat, int numThreads)
		{
			if (this->_pool_2)
				delete this->_pool_2;
			if (this->_pool_default)
				delete this->_pool_default;

			if (strat == 0)
			{
				this->_pool_default = new ctpl::Pool(numThreads);
			}
			else if (strat == 1)
			{
				//	this->_pool_default = new fwPool::Pool<_Ret, Args...>(numThreads);
			}
			else if (strat == 2)
			{
				//				this->_pool_default = new fwPool::Pool<_Ret, Args...>(numThreads);
				this->_pool_2 = new ctpl::Pool();
				this->_pool_default->linkAnotherPool(this->_pool_2);
			}
			this->numThreadInitial = numThreads;
			this->appliedStrategy = strat;
		}

		/*This syncronizes available threads in branchHandler with
			busyThreads in pool, this is relevant when using void functions
			because there is no need to call getResults(),
			nevertheless this should be avoided when pushing void functions*/
		void functionIsVoid()
		{
			this->_pool_default->setExternNumThreads(&this->numThreads);
		}

	private:
		void init()
		{
			this->processor_count = std::thread::hardware_concurrency();
			this->numThreads = 0;
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
		bool is_LB = false;

		/*Dequeue while using strategy myPool*/
		bool isDequeueEnable = false;
		int targetArg;

		/*------------------------------------------------------>>end*/
		/* "processor_count" would allow to set by default the maximum number of threads
			that the machine can handle unless the user invokes setMaxThreads() */

		unsigned int processor_count;
		std::atomic<long long> idleTime;
		std::atomic<int> numThreads;
		size_t numThreadInitial;
		std::mutex mtx;	 //local mutex
		std::mutex mtx2; //local mutex
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
			/*static bool static_init = [&]() -> bool {
				INSTANCE = new ThisType;
				init();
				return true;
			}();*/

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
		int world_rank = -1;
		int world_size = -1;
		MPI_Win *win_boolean = nullptr;
		MPI_Win *win_data = nullptr;
		MPI_Win *win_NumNodes = nullptr;
		MPI_Win *win_AvNodes = nullptr;
		MPI_Win *win_test = nullptr;
		MPI_Win *win_accumulator = nullptr;
		char processor_name[128];
		MPI_Comm *SendToNodes_Comm = nullptr;
		MPI_Comm *SendToCenter_Comm = nullptr;
		MPI_Comm *NodeToNode_Comm = nullptr;

		int *numAvailableNodes;
		int *sch_inbox_boolean = nullptr;
		int *sch_inbox_data = nullptr;
		bool request_response = false;
		bool data_ready = false;

		void pass_MPI_args(int world_rank,
						   int world_size,
						   char *processor_name,
						   MPI_Win *win_boolean,
						   MPI_Win *win_data,
						   MPI_Win *win_NumNodes,
						   MPI_Win *win_AvNodes,
						   int *sch_inbox_boolean,
						   int *sch_inbox_data,
						   MPI_Comm *SendToNodes_Comm,
						   MPI_Comm *SendToCenter_Comm,
						   MPI_Comm *NodeToNode_Comm,
						   int *numAvailableNodes,
						   MPI_Win *win_test,
						   MPI_Win *win_accumulator)
		{
			this->world_rank = world_rank;
			this->world_size = world_size;
			strncpy(this->processor_name, processor_name, 128);
			this->win_boolean = win_boolean;
			this->win_data = win_data;
			this->win_NumNodes = win_NumNodes;
			this->win_AvNodes = win_AvNodes;
			this->sch_inbox_boolean = sch_inbox_boolean;
			this->sch_inbox_data = sch_inbox_data;
			this->SendToNodes_Comm = SendToNodes_Comm;
			this->SendToCenter_Comm = SendToCenter_Comm;
			this->NodeToNode_Comm = NodeToNode_Comm;
			this->numAvailableNodes = numAvailableNodes;
			this->win_test = win_test;
			this->win_accumulator = win_accumulator;
		}

		/* if method receives data, this node is suposed to be totally idle */
		void seedReceiver()
		{
			int count_rcv = 0;

			std::string msg = "avalaibleNodes[" + std::to_string(world_rank) + "]";
			accumulate(1, 0, world_rank, win_AvNodes, msg);
			printf("process %d put data [%d] in process 0 \n", world_rank, 1);

			while (true) // Find a way to terminate this loop
			{
				MPI_Status status;
				/* if a thread passes succesfully this method, library gets ready to receive data*/

				//comm_idle_node_to_center(); //as long as it starts, it communicates center node it's ready for duty

				printf("Receiver called on process %d, avl processes %d \n", world_rank, numAvailableNodes[0]);

				//getter(); //check on remote memory if data sent succesfully
				/* **************************************************************************
			*	TODO wait until the thread pool run out of task, then send signal back to
			*	center node so it knows for new available node 
			************************************************************************** */

				printf("Receiver on %d ready to receive \n", world_rank);
				int buffer[1]; //dummy arguments, to be improved
				//MPI_Recv(&buffer, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, *SendToNodes_Comm, &status);
				MPI_Recv(&buffer, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
				count_rcv++;
				int src = status.MPI_SOURCE;
				printf("process %d has rcvd from %d \n", world_rank, src);
				printf("process %d has rcvd %d times \n", world_rank, count_rcv);
				if (status.MPI_TAG == 3)
				{
					printf("Exit tag received on process %d \n", world_rank);
					return;
				}
				printf("Receiver on %d, received %d \n", world_rank, buffer[0]);

				accumulate(1, 0, 0, win_accumulator, "busyNodes++");

				//This push should be guaranteed, it is called only once when receiving seed
//				push(-1, buffer[0]);
//				printf("Hello from line 297, busy_threads %d \n", busy_threads.load());

				printf("Passed on process %d \n", world_rank);
//				_pool.wait();
				//accumulate(1, 0, world_rank, win_AvNodes, "availableNodes++");
				accumulate(-1, 0, 0, win_accumulator, "busyNodes--");
			}
		}

		void accumulate(int buffer, int target_rank, MPI_Aint offset, MPI_Win *window, std::string msg)
		{
			printf("%d about to accumulate on %s\n", world_rank, msg.c_str());
			MPI_Win_lock(MPI_LOCK_EXCLUSIVE, target_rank, 0, *window);
			//MPI_Win_lock(MPI_LOCK_SHARED, target_rank, 0, *window);
			//MPI_Win_lock_all(0, *window);

			MPI_Accumulate(&buffer, 1, MPI_INT, target_rank, offset, 1, MPI_INT, MPI_SUM, *window);

			//MPI_Put(&buffer, 1, MPI_INT, target_rank, offset, 1, MPI_INT, *window);

			printf("%d about to unlock RMA on %d \n", world_rank, target_rank);

			MPI_Win_flush(target_rank, *window);
			//MPI_Win_flush_all(*window);
			printf("%d between flush and unlock \n", world_rank);

			MPI_Win_unlock(target_rank, *window);
			//MPI_Win_unlock_all(*window);
			printf("%s, by %d\n", msg.c_str(), world_rank);
		}

	}; 
	BranchHandler *BranchHandler::INSTANCE = nullptr;
	std::once_flag BranchHandler::initInstanceFlag;

} // namespace library

#endif
