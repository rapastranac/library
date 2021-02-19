﻿#pragma once
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
#include "../MPI_Modules/MPI_Mutex.hpp"

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

	public:
		double getPoolIdleTime()
		{
			return thread_pool.getIdleTime() / (double)processor_count;
		}

		int getMaxThreads() //ParBranchHandler::getInstance().MaxThreads() = 10;
		{
			return this->thread_pool.size();
		}

		void setMaxThreads(int poolSize)
		{
			/*	I have not implemented yet the resize of other pool*/
			//this->init();
			this->processor_count = poolSize;
			//this->_pool = new ctpl::Pool(poolSize);
			this->thread_pool.setSize(poolSize);
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
#ifdef MPI_ENABLED
		/* a reference value is replaced based on the user's conditions. 
			ifCond is used when the user wants to print out results or any other thing thread safely
			ifCond is optional, if not passed, then nullptr should be passed in instead*/
		template <class C1, class C2, typename T, class F_SERIAL>
		bool replaceIf(int refValueLocal, C1 &&Cond, C2 *ifCond, T &&result, F_SERIAL &&f_serial)
		{
			std::unique_lock<std::mutex> lck(mtx_MPI);
#ifdef DEBUG_COMMENTS
			printf("rank %d entered replaceIf(), acquired mutex \n ", world_rank);
#endif

			if (Cond(refValueGlobal[0], refValueLocal)) // check global val in this node
			{
#ifdef DEBUG_COMMENTS
				printf("rank %d, local condition satisfied refValueGlobal : %d vs refValueLocal : %d \n", world_rank, refValueGlobal[0], refValueLocal);
#endif
				// then, absolute global is checked
				int refValueGlobalAbsolute;
				int origin_count = 1;
				int target_rank = 0;
				MPI_Aint offset = 0;
				MPI_Win &window = *win_refValueGlobal; // change to a reference to the window (&window, or *window)

				//mpi_mutex->lock(world_rank); // critical section begins

				MPI_Win_lock(MPI::LOCK_EXCLUSIVE, target_rank, 0, window); // open epoch
#ifdef DEBUG_COMMENTS
				printf("rank %d opened epoch to send best result \n", world_rank);
#endif
				// this blocks access to this window
				// this is the only place where this window is read or modified

				MPI_Get(&refValueGlobalAbsolute, origin_count, MPI::INT, target_rank, offset, 1, MPI::INT, window);
				MPI_Win_flush(target_rank, window); // retrieve refValueGlobalAbsolute which is the most up-to-date value

#ifdef DEBUG_COMMENTS
				printf("rank %d got refValueGlobalAbsolute : %d \n", world_rank, refValueGlobalAbsolute);
#endif

				if (Cond(refValueGlobalAbsolute, refValueLocal)) // compare absolute global value against local
				{
					this->refValueGlobal[0] = refValueLocal; // updates global ref value, in this node

					if (ifCond)
						(*ifCond)();

					MPI_Accumulate(&refValueLocal, origin_count, MPI::INT, target_rank, offset, 1, MPI::INT, MPI::REPLACE, window);
					MPI_Win_flush(target_rank, window); // after this line, global ref value is updated in center node, but not broadcasted

					printf("rank %d updated refValueGlobalAbsolute to %d || %d \n", world_rank, refValueLocal, refValueGlobal[0]);

					auto ss = f_serial(result); // serialized result

					printf("rank %d, cover size : %d \n", world_rank, result.coverSize());
					int sz_before = bestRstream.second.str().size(); //testing only

					int SIZE = ss.str().size();
					printf("rank %d, buffer size to be sent : %d \n", world_rank, SIZE);

					bestRstream.first = refValueLocal;
					bestRstream.second = std::move(ss);

					MPI_Win_unlock(target_rank, window); // after this line, other processes can access the window

					//mpi_mutex->unlock(world_rank); // critical section ends
					return true;
				}
				MPI_Win_unlock(target_rank, window); // after this line, other processes can access the window
				//mpi_mutex->unlock(world_rank);		 // critical section ends
				return false;
			}
			else
				return false;
		}

#endif

		/* a reference value is replaced based on the user's conditions. 
			ifCond is used when the user wants to print out results or any other thing thread safely
			ifCond is optional, if not passed, then nullptr should be passed in instead*/
		template <class C1, class C2, typename T>
		bool replaceIf(int refValueLocal, C1 &&Cond, C2 *ifCond, T &&result)
		{
			std::unique_lock<std::mutex> lck(mtx);
			if (Cond(refValueGlobal[0], refValueLocal))
			{
				this->refValueGlobal[0] = refValueLocal;
				if (refValueLocal == 0)
				{
					int fgd = 3423; // testing, debugging
				}

				if (ifCond)
					(*ifCond)();

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

		/* for void algorithms, this also calls the destructor of the pool*/
		void wait_and_finish()
		{

#ifdef DEBUG_COMMENTS
			printf("Main thread interrupting pool \n");
#endif
			this->thread_pool.interrupt(true);
		}

		/* for void algorithms, this allows to reuse the pool*/
		void wait()
		{
#ifdef DEBUG_COMMENTS
			printf("Main thread waiting results \n");
#endif
			this->thread_pool.wait();
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

#ifdef MPI_ENABLED

			// since all processes pass by here, then MPI_Bcast is effective
			this->refValueGlobal[0] = refValue;
#ifdef DEBUG_COMMENTS
			printf("rank %d, refValueGlobal has been set : %d at %p \n", world_rank, refValueGlobal[0], refValueGlobal);
#endif
			if (world_rank == 0)
			{
				int err = MPI_Bcast(refValueGlobal, 1, MPI::INT, 0, *world_Comm);
				if (err != MPI::SUCCESS)
					printf("rank %d, broadcast unsucessful with err = %d \n", world_rank, err);
#ifdef DEBUG_COMMENTS
				printf("refValueGlobal broadcasted: %d at %p \n", refValueGlobal[0], refValueGlobal);
#endif
			}
			else
			{
				int err = MPI_Bcast(refValueGlobal, 1, MPI::INT, 0, *world_Comm);
				if (err != MPI::SUCCESS)
					printf("rank %d, broadcast unsucessful with err = %d \n", world_rank, err);
#ifdef DEBUG_COMMENTS
				printf("rank %d, refValueGlobal received: %d at %p \n", refValueGlobal[0], refValueGlobal);
#endif
			}

			MPI_Barrier(*world_Comm);

#else

			this->refValueGlobal = new int[1];
			this->refValueGlobal[0] = refValue;
#endif
		}

		/*begin<<------casting strategies -------------------------*/
		//https://stackoverflow.com/questions/15326186/how-to-call-child-method-from-a-parent-pointer-in-c
		//https://stackoverflow.com/questions/3747066/c-cannot-convert-from-base-a-to-derived-type-b-via-virtual-base-a
	private:
		//ctpl::Pool *ctpl_casted(POOL::Pool *item)
		//{
		//	return dynamic_cast<ctpl::Pool *>(item);
		//}
		/*--------------------------------------------------------end*/

		void put_mpi(const void *origin_addr, int count, MPI_Datatype mpi_type, int target_rank, MPI_Aint offset, MPI_Win &window)
		{
			MPI_Win_lock(MPI::LOCK_EXCLUSIVE, target_rank, 0, window); // opens epoch
			MPI_Put(origin_addr, count, mpi_type, target_rank, offset, 1, mpi_type, window);
			MPI_Win_flush(target_rank, window);
			MPI_Win_unlock(target_rank, window); // closes epoch
		}

		//not sure what it does, it works fine with/without it
		template <typename Holder>
		void exclude(Holder *holder)
		{
			if (holder->parent)
			{
				holder->parent->children.pop_front();
				Holder *newLeftMost = holder->parent->children.front();
				if (holder->parent->children.size() == 1)
				{
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

				std::cout << "4 Testing, it's not supposed to happen" << std::endl;
				return nullptr;
			}
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
		void checkLeftSibling_smrt(Holder *holder)
		{
			/* What does it do?. Having the following ilustration
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
				deleted of the list since it is already solved. Thus, pushing cb twice
				is avoided because checkParent() pushes the second element of the children
			*/

			if (holder->parent) //this confirms the holder is not a root
			{
				if (holder->parent.get() == *(holder->root)) //this confirms that it's the first level of the root
				{
					Holder *leftMost = holder->parent->children.front();
					if (leftMost != holder) //This confirms pb has already been solved
					{
						/* next conditional should always comply, there should not be required
						* to use a loop, then this While is entitled to just a single loop. 4 testing!!
						*/
						while (leftMost != holder)
						{
							holder->parent->children.pop_front();		 // removes pb from the parent's children
							leftMost = holder->parent->children.front(); // it gets the second element from the parent's children
						}
						// after this line,this should be true leftMost == holder

						// There might be more than one remaining sibling
						if (holder->parent->children.size() > 1)
							return; // root does not change

						/* if holder is the only remaining child from parent then this means
						that it will have to become a new root*/
						holder->parent = nullptr;
						holder->prune();
					}
				}
				else if (holder->parent.get() != *holder->root) //any other level,
				{
					/* this is relevant, because eventhough the root still has some waiting nodes
					the thread in charge of the tree might be deep down solving everything sequentially.
					Every time a leftMost branch is solved sequentially, this one should be removed from 
					the list to avoid failure attempts of solving a branch that has already been solved.

					If a thread attempts to solve an already solved branch, this will throw an error
					because the node won't have information anymore since it has already been passed
					*/

					auto leftMost = holder->parent->children.front();
					if (leftMost != holder) //This confirms pb has already been solved
					{
						/*this scope only deletes leftMost holder, which is already
						* solved sequentially by here and leaves the parent with at
						* least a child because the root still has at least a holder in
						* the waiting list
						*/
						holder->parent->children.pop_front();
					}
				}
			}
		}

		template <typename Holder>
		Holder *rootCorrecting(Holder *root)
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

		/* this is useful because at level zero of a root, there might be multiple
		waiting nodes, though the leftMost branch (at zero level) might be at one of 
		the very right sub branches deep down, which means that there is a line of
		 multiple nodes with a single child.
		 A node with a single child means that it has already been solved and 
		 also its siblings, because children are unlinked from their parent node
		 when these ones are pushed or fully solved (returned) 
		 							
							root == parent
						  /  |  \   \  \
						 /   |   \	 \	 \
						/    |    \	  \	   \
				leftMost     w1    w2  w3 ... wk
						\
						 *
						  \
						   *
						   	\
						current_level
		
		if there are available threads, and all waiting nodes at level zero are pushed,
		then root should lower down where it finds a node with at least two children or
		the deepest node
		 */
		template <typename Holder>
		void rootCorrecting_smrt(Holder *root) //<-------------------------  raw pointers
		{
			Holder *_root = root;

			while (_root->children.size() == 1) // lowering the root
			{
				_root = _root->children.front();
				_root->parent->children.pop_front();
				_root->lowerRoot();
				//_root->parent = nullptr;
				//*(_root->root) = &(*_root);
			}
			//return _root;		//<-------------------------
		}

		//template <typename F, typename... Rest>
		//auto pushSeed(F &&f, Rest &&...rest)
		//{
		//	//auto _pool = ctpl_casted(_pool);
		//	this->busyThreads = 1;								// forces just in case
		//	auto future = std::move(thread_pool.push(f, rest...));	// future type can handle void returns
		//	auto lambda = [&future]() { return future.get(); }; // create lambda to pass to args_handler::invoke_void()
		//	return std::args_handler::invoke_void(lambda); //this guarantees to assign a non-void value
		//}

		template <typename Holder>
		Holder checkParent_smrt(Holder &holder_smrt)
		{
			using RH = decltype(holder_smrt.get()); //raw pointer type to ResultHolder
			RH holder = holder_smrt.get();
			RH leftMost; // this is the branch that led us to the root
			RH root;	 // local pointer to root, to avoid "*" use

			if (holder->parent) //  this confirms holder is not a root
			{
				if (holder->parent.get() != *holder->root)
				{
					/* this confirms, it's not the first level, because it is meant
					to assist top sub trees of a branch that has started to run
					sequentially */
					root = *holder->root; //no need to iterate	<------------------
					//root.reset(*holder->root);

					int tmp = root->children.size(); // this probable fix the following

					// the following is not true, it could be also the right branch
					// Unless root is guaranteed to have at least 2 children,
					// TODO ... verify

					//leftMost.reset(root->children.front());
					leftMost = root->children.front(); //TODO ... check if branch has been pushed or forwarded <------------------
				}
				else
					return nullptr;
			}
			else
				return nullptr;

			// if the above condition is succesfully met, then this method should never return a nullptr

			/*Here below, we check is left child was pushed to pool, then the pointer to parent is pruned
							 parent
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

			auto worthPushing = [](Holder &holder) -> Holder {
				if (holder->isBound())
				{
					bool isWorthPushing = holder->boundCond();
					if (isWorthPushing)
						return holder;
					else
						return nullptr;
				}
				return holder;
			};

			if (root->children.size() > 2)
			{
				/* this condition is for multiple recursion, the diference with the one below is that
				the root does not move after returning one of the waiting nodes,
				say we have the following root's childen

				children =	{	cb	w1	w2	... wk}

				the goal is to push w1, which is the inmmediate right node */

				auto second = std::next(root->children.begin(), 1); // this is to access the 2nd element
				//auto secondHolder = *second;						// catches the pointer of the node	<-------------------------
				root->children.erase(second); // removes second node from the root's children

				Holder secondHolder(*second);

				return worthPushing(secondHolder);
			}
			else if (root->children.size() == 2)
			{
				/*	this scope is meant to push right branch which was put in waiting line
					because there was no available thread to push leftMost branch, then leftMost
					will be the new root since after this scope right branch will have already been
					pushed*/

				root->children.pop_front(); // deletes leftMost from root's children
				//Holder *right = root->children.front(); // the one to be pushed		<-------------------------
				Holder right(root->children.front());

				root->children.clear(); // just in case
				//right->parent = nullptr;					 //parent not needed since it'll be pushed
				//right->root = nullptr;
				right->prune(); //it does the same than previous two lines

				leftMost->lowerRoot(); // leftMost is the new root IMPORTANT!!!!! this sets the new root
				//Holder *test = rootCorrecting_smrt(leftMost); //IMPORTANT, this method receives a root	<-------------------------

				rootCorrecting_smrt(leftMost); //it passes the raw pointers

				return worthPushing(right);
			}
			else
			{
				std::cout << "4 Testing, it's not supposed to happen" << std::endl;
				return nullptr;
			}
		}

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		bool push_dummy(F &&f, int id, Holder &holder_smrt)
		{
			//using RESULT_HOLDER = decltype(holder_smrt.get()); //actual ResultHolder type (pointer)
			//RESULT_HOLDER holder = holder_smrt.get();
			/*This lock must be adquired before checking the condition,	
			even though busyThreads is atomic*/
			std::unique_lock<std::mutex> lck(mtx);

			if (busyThreads < thread_pool.size())
			{
				if (is_DLB)
				{
					Holder upperHolder = checkParent_smrt(holder_smrt); //this is creating a new share_ptr which is not the same as the existing ones
					if (upperHolder)
					{

						this->busyThreads++;
						upperHolder->setPushStatus(true);
						lck.unlock();
						// **************************************************
						// if holder sent, then its parents should be
						upperHolder->prune();
						// **************************************************
						// since it's void, no need to store a returned/future value
						//std::args_handler::unpack_tuple(thread_pool, f, upperHolder->getArgs(), true);
						//std::args_handler::unpack_tuple(upperHolder, f, 0, upperHolder->getArgs(), true);

						//auto s_ptr = std::make_shared<RESULT_HOLDER>(upperHolder);
						//Holder ptr(upperHolder);
						int count = upperHolder.use_count();

						std::args_handler::unpack_and_send(upperHolder, thread_pool, f, 0, upperHolder->getArgs());
						return false;
					}
					//exclude(&holder);
				}

				this->busyThreads++;
				holder_smrt->setPushStatus(true);
				lck.unlock();

				// **************************************************
				// if holder sent, then its parent and children info should be reseted
				holder_smrt->prune();
				// **************************************************

				//std::args_handler::unpack_tuple(thread_pool, f, holder.getArgs(), true);
				//std::args_handler::unpack_tuple(thread_pool, f, holder.getArgs(), &holder, true);
				std::args_handler::unpack_and_send(holder_smrt, thread_pool, f, 0, holder_smrt->getArgs());
				return true;
			}
			else
			{
				lck.unlock();
				forward_smrt<_ret>(f, id, holder_smrt, true);
				return true;
			}
			return false;
		}

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		bool push_DLB(F &&f, int id, Holder &holder)
		{
			/*This lock must be adquired before checking the condition,	
			even though busyThreads is atomic*/
			std::unique_lock<std::mutex> lck(mtx);

			if (busyThreads < thread_pool.size())
			{
				if (is_DLB)
				{
					Holder *upperHolder = checkParent(&holder);
					//Holder upperHolder_smrt = checkParent_smrt(holder);
					if (upperHolder)
					{
						if (std::get<1>(upperHolder->getArgs()).coverSize() == 0)
						{
							int g = 4534;
						}
						this->busyThreads++;
						upperHolder->setPushStatus(true);
						lck.unlock();
						// **************************************************
						// if holder sent, then its parent and children info should be reseted
						upperHolder->unlink_parents();
						// **************************************************
						// since it's void, no need to store a returned/future value
						//std::args_handler::unpack_tuple(thread_pool, f, upperHolder->getArgs(), true);
						//std::args_handler::unpack_tuple(upperHolder, f, 0, upperHolder->getArgs(), true);
						return false;
					}
					//exclude(&holder);
				}

				//if (std::get<1>(holder.getArgs()).coverSize() == 0)
				//{
				//	int g = 4534;
				//}

				this->busyThreads++;
				holder.setPushStatus(true);
				lck.unlock();

				// **************************************************
				// if holder sent, then its parent and children info should be reseted
				holder.unlink_parents();
				// **************************************************

				std::args_handler::unpack_tuple(thread_pool, f, holder.getArgs(), true);
				//std::args_handler::unpack_tuple(thread_pool, f, holder.getArgs(), &holder, true);
				return true;
			}
			else
			{
				lck.unlock();
				this->forward<_ret>(f, id, holder, true);
				return true;
			}
			return false;
		}

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		bool push_DLB(F &&f, int id, Holder &holder)
		{
			/*This lock must be adquired before checking the condition,	
			even though busyThreads is atomic*/
			std::unique_lock<std::mutex> lck(mtx);

			if (std::get<1>(holder.getArgs()).coverSize() == 0)
			{
				int g = 4534;
			}

			if (busyThreads < thread_pool.size())
			{
				if (is_DLB)
				{
					Holder *upperHolder = checkParent(&holder);
					if (upperHolder)
					{

						if (std::get<1>(upperHolder->getArgs()).coverSize() == 0)
						{
							int g = 4534;
						}

						this->busyThreads++;
						upperHolder->setPushStatus(true);
						lck.unlock();

						auto ret = std::args_handler::unpack_tuple(thread_pool, f, upperHolder->getArgs(), true);
						upperHolder->hold_future(std::move(ret));
						return false;
					}

					//exclude(&holder);
				}

				if (std::get<1>(holder.getArgs()).coverSize() == 0)
				{
					int g = 4534;
				}

				this->busyThreads++;
				holder.setPushStatus(true);
				lck.unlock();

				auto ret = std::args_handler::unpack_tuple(thread_pool, f, holder.getArgs(), true);
				holder.hold_future(std::move(ret));
				return true;
			}
			else
			{
				lck.unlock();
				auto ret = this->forward(f, id, holder, true);
				holder.hold_actual_result(ret);
				return true;
			}
			return false;
		}

	public:
		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		int push(F &&f, int id, Holder &holder)
		{
			/*This lock must be performed before checking the condition,
			even though numThread is atomic*/
			std::unique_lock<std::mutex> lck(mtx);
			if (busyThreads < thread_pool.size())
			{
				busyThreads++;
				holder.setPushStatus(true);

				lck.unlock();
				std::args_handler::unpack_tuple(thread_pool, f, holder.getArgs());
				return 1;
			}
			else
			{
				lck.unlock();
				this->forward<_ret>(f, id, holder);
				return 0;
			}
		}

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		int push(F &&f, int id, Holder &holder)
		{
			/*This lock must be performed before checking the condition,
			even though numThread is atomic*/
			std::unique_lock<std::mutex> lck(mtx);
			if (busyThreads < thread_pool.size())
			{
				busyThreads++;
				holder.setPushStatus(true);

				lck.unlock();
				auto ret = std::args_handler::unpack_tuple(thread_pool, f, holder.getArgs());
				holder.hold_future(std::move(ret));
				return 1;
			}
			else
			{
				lck.unlock();
				auto ret = this->forward<_ret>(f, id, holder);
				holder.hold_actual_result(ret);
				return 0;
			}
		}

		template <typename _ret, typename F, typename Holder>
		bool push(F &&f, int id, Holder &holder, bool trackStack)
		{
			bool _flag = false;
			while (!_flag)
				_flag = push_DLB<_ret>(f, id, holder);

			return _flag;
		}

		template <typename _ret, typename F, typename Holder>
		bool push_test(F &&f, int id, Holder &holder, bool trackStack)
		{
			bool _flag = false;
			while (!_flag)
				_flag = push_dummy<_ret>(f, id, holder);

			return _flag;
		}

#ifdef MPI_ENABLED
		template <typename F, typename Holder, typename F_SERIAL>
		int push(F &&f, int id, Holder &holder, F_SERIAL &&f_serial)
		{
			bool signal{false};
			std::unique_lock<std::mutex> mpi_lck(mtx_MPI, std::defer_lock); // this guarantees mpi_thread_serialized
			if (mpi_lck.try_lock())
			{
				//printf("rank %d entered try_lock \n", world_rank);
				//printf("rank %d, numAvailableNodes : %d \n", world_rank, numAvailableNodes[0]);

				if (numAvailableNodes[0] > 0) // center node is in charge of broadcasting this number
				{
#ifdef DEBUG_COMMENTS
					printf("%d about to request center node to push\n", world_rank);
#endif
					bool buffer = true;
					put_mpi(&buffer, 1, MPI::BOOL, 0, world_rank, *win_boolean); // send signal to center node
					MPI_Status status;
#ifdef DEBUG_COMMENTS
					printf("process %d requested to push \n", world_rank);
#endif
					MPI_Recv(&signal, 1, MPI::BOOL, 0, MPI::ANY_TAG, *world_Comm, &status); //awaits signal if data can be sent

					if (signal)
					{
						int dest = status.MPI_TAG; // this is the available node, sent by center node as a tag
						holder.setMPISent(true, dest);
#ifdef DEBUG_COMMENTS
						printf("process %d received ID %d\n", world_rank, dest);
#endif

						std::stringstream ss = std::args_handler::unpack_tuple(f_serial, holder.getArgs());

						int Bytes = ss.str().size(); // number of Bytes

						int err = MPI_Ssend(ss.str().data(), Bytes, MPI::CHAR, dest, 0, *world_Comm); // send buffer
						if (err == MPI::SUCCESS)
							printf("buffer sucessfully sent from rank %d to rank %d! \n", world_rank, dest);

						//TODO how to retrieve result from destination rank?

						mpi_lck.unlock();
#ifdef DEBUG_COMMENTS
						printf("process %d forwarded to process %d \n", world_rank, dest);
#endif
						return 2;
					}
					/*else // numAvailableNodes might have changed due to delay
					{
						mpi_lck.unlock();
						#ifdef DEBUG_COMMENTS
						printf("process %d push request failed, forwarded!\n", world_rank);
						#endif
						auto retVal = this->forward(f, id, holder);
						holder.hold_actual_result(retVal);
						return 0;
					}*/
				}
				mpi_lck.unlock(); // this ensures to unlock it even if (numAvailableNodes == 0)
			}

			/*This lock must be performed before checking the condition,
			even though numThread is atomic*/
			std::unique_lock<std::mutex> lck(mtx);
			if (busyThreads < thread_pool.size())
			{
				busyThreads++;
				holder.setPushStatus(true);

				lck.unlock();
				auto tmp = std::args_handler::unpack_tuple(thread_pool, f, holder.getArgs());
				holder.hold_future(std::move(tmp));
				return 1;
			}
			lck.unlock(); // this allows to other threads to keep pushing in the pool

			//lck.unlock(); // unlocked already before mpi push
			auto retVal = this->forward(f, id, holder);
			holder.hold_actual_result(retVal);
			return 0;
		}

#endif
		// no DLB begin **********************************************************************
		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		_ret forward(F &&f, int threadId, Holder &holder)
		{
			std::args_handler::unpack_tuple(f, threadId, holder.getArgs());
		}

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		_ret forward(F &&f, int threadId, Holder &holder)
		{
			return std::args_handler::unpack_tuple(f, threadId, holder.getArgs());
		}

		// no DLB ************************************************************************* end

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		_ret forward(F &&f, int threadId, Holder &holder, bool trackStack)
		{
			if (holder.isPushed)
				return;

			if (is_DLB)
				checkLeftSibling(&holder);

			holder.setForwardStatus(true);
			holder.threadId = threadId;
			std::args_handler::unpack_tuple(&holder, f, threadId, holder.getArgs(), trackStack);
		}

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		_ret forward(F &&f, int threadId, Holder &holder, bool trackStack)
		{
			if (holder.isPushed)
			{
				return holder.get();
				return NULL; // nope, if it was pushed, then result should be retrieved in here
			}
			if (is_DLB)
			{
				checkLeftSibling(&holder);
			}

			holder.setForwardStatus(true);
			holder.threadId = threadId;
			return std::args_handler::unpack_tuple(&holder, f, threadId, holder.getArgs(), trackStack);
		}

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		_ret forward_smrt(F &&f, int threadId, Holder &holder, bool trackStack)
		{
			if (holder->is_pushed())
				return;

			if (is_DLB)
			{
				auto ptr = holder.get();
				checkLeftSibling_smrt(ptr);
			}

			holder->setForwardStatus(true);
			holder->threadId = threadId;
			std::args_handler::unpack_and_send(holder, f, threadId, holder->getArgs());
		}

		//Not useful yet
		void singal_interruption()
		{
			this->thread_pool.signal_interruption();
		}

		/*This syncronizes available threads in branchHandler with
			busyThreads in pool, this is relevant when using void functions
			because there is no need to call getResults(),
			nevertheless this should be avoided when pushing void functions*/
		void functionIsVoid()
		{
			this->thread_pool.setExternNumThreads(&this->busyThreads);
		}

	private:
		void init()
		{
			this->processor_count = std::thread::hardware_concurrency();
			this->busyThreads = 0;
			this->idleTime = 0;
			this->isDone = false;
			this->max_push_depth = -1;
			this->superFlag = false;
			this->idCounter = 0;

			this->bestRstream.first = -1; // this allows to avoid sending empty buffers
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
		bool is_DLB = true;

		/*Dequeue while using strategy myPool*/
		bool isDequeueEnable = false;
		int targetArg;

		/*------------------------------------------------------>>end*/
		/* "processor_count" would allow to set by default the maximum number of threads
			that the machine can handle unless the user invokes setMaxThreads() */

		unsigned int processor_count;
		std::atomic<long long> idleTime;
		std::atomic<int> busyThreads;
		std::mutex mtx; //local mutex
		std::condition_variable cv;
		std::atomic<bool> superFlag;
		ctpl::Pool thread_pool;

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
		int *refValueGlobal = nullptr; // shared with MPI

#ifdef MPI_ENABLED
		std::mutex mtx_MPI; //local mutex

		/* MPI parameters */
		MPI_Mutex *mpi_mutex = nullptr;

		std::function<std::any(std::any)> _serialize;
		std::function<std::any(std::any)> _deserialize;

		int world_rank = -1;	  // get the rank of the process
		int world_size = -1;	  // get the number of processes/nodes
		char processor_name[128]; // name of the node

		MPI_Win *win_boolean = nullptr;
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

		int *numAvailableNodes = nullptr;
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
						 MPI_Win *win_refValueGlobal,
						 MPI_Comm *world_Comm,
						 MPI_Comm *second_Comm,
						 MPI_Comm *SendToNodes_Comm,
						 MPI_Comm *SendToCenter_Comm,
						 MPI_Comm *NodeToNode_Comm,
						 MPI_Mutex *mpi_mutex)
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
			this->win_refValueGlobal = win_refValueGlobal;
			this->world_Comm = world_Comm;
			this->second_Comm = second_Comm;
			this->SendToNodes_Comm = SendToNodes_Comm;
			this->SendToCenter_Comm = SendToCenter_Comm;
			this->NodeToNode_Comm = NodeToNode_Comm;

			// mpi mutex *********************
			this->mpi_mutex = mpi_mutex;
			// *******************************
		}

		/* if method receives data, this node is supposed to be totally idle */
		template <typename Result, typename F, typename Serialize, typename Deserialize, typename Holder>
		void receiveSeed(F &&f, Serialize &&serialize, Deserialize &&deserialize, Holder &holder)
		{
			bool onceFlag = false;
			int count_rcv = 0;

			std::string msg = "avalaibleNodes[" + std::to_string(world_rank) + "]";
			bool flag = true;

			while (true)
			{

				put_mpi(&flag, 1, MPI::BOOL, 0, world_rank, *win_AvNodes);
				printf("process %d put flag [true] in process 0 \n", world_rank);

				MPI_Status status;
				/* if a thread passes succesfully this method, library gets ready to receive data*/

				printf("Receiver called on process %d, avl processes %d \n", world_rank, numAvailableNodes[0]);

				printf("Receiver on %d ready to receive \n", world_rank);
				int Bytes; // Bytes to be received

				MPI_Probe(MPI::ANY_SOURCE, MPI::ANY_TAG, *world_Comm, &status); // receives status before receiving the message
				MPI_Get_count(&status, MPI::CHAR, &Bytes);						// receives total number of datatype elements of the message

				char *in_buffer = new char[Bytes];
				MPI_Recv(in_buffer, Bytes, MPI::CHAR, MPI::ANY_SOURCE, MPI::ANY_TAG, *world_Comm, &status);

				count_rcv++;
				int src = status.MPI_SOURCE;
				printf("process %d has rcvd from %d,%d times \n", world_rank, src, count_rcv);
				if (status.MPI_TAG == 3)
				{
					printf("rank %d about to send best result to center \n", world_rank);
					sendBestResultToCenter();
					printf("Exit tag received on process %d \n", world_rank); // loop termination
					break;
				}
				printf("Receiver on %d, received %d Bytes \n", world_rank, Bytes);

				Holder newHolder(*this); // copy types

				std::stringstream ss;
				for (int i = 0; i < Bytes; i++)
					ss << in_buffer[i];

				Utils::unpack_tuple(deserialize, ss, newHolder.getArgs());

				accumulate_mpi(1, 1, MPI::INT, 0, 0, *win_accumulator, "busyNodes++");

				delete[] in_buffer;

				if (!onceFlag)
				{
					if (MPI_COMM_NULL != *second_Comm)
						MPI_Barrier(*second_Comm);
					//onceFlag = true; //prevents to synchronise again if process #1 gets free, yet job is not finished
					onceFlag = true;
				}

				push(f, 0, newHolder); // first push, node is idle

				reply<Result>(serialize, newHolder, src);

				printf("Passed on process %d \n", world_rank);
				accumulate_mpi(-1, 1, MPI::INT, 0, 0, *win_accumulator, "busyNodes--");
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
				put_mpi(&buffer, 1, MPI::BOOL, src, 0, *win_finalFlag);
				printf("rank %d put to finalFlag! \n", world_rank);

				std::stringstream ss = serialize(res);
				int count = ss.str().size();

				int err = MPI_Ssend(ss.str().data(), count, MPI::CHAR, src, 0, *world_Comm);
				if (err != MPI::SUCCESS)
					printf("final result could not be sent from rank %d to rank %d! \n", world_rank, src);
			}
			else // some other node requested help and it is surely waiting for the result
			{
				std::unique_lock<std::mutex> lck(mtx_MPI); //no other thread can retrieve nor send via MPI

				std::stringstream ss = serialize(res);
				int count = ss.str().size();

				int err = MPI_Ssend(ss.str().data(), count, MPI::CHAR, src, 0, *world_Comm);
				if (err != MPI::SUCCESS)
					printf("result could not be sent from rank %d to rank %d! \n", world_rank, src);
			}
		}

		template <typename Result, typename Holder, typename Serialize,
				  std::enable_if_t<std::is_void_v<Result>, int> = 0>
		void reply(Serialize &&serialize, Holder &holder, int src)
		{
			//this->waitResult(true);
			thread_pool.wait();
			//sendBestResultToCenter();
		}

		// this should is supposed to be called only when all tasks are finished
		void sendBestResultToCenter()
		{
			if (bestRstream.first == -1)
			{
				printf("rank %d did not catch a best result \n", world_rank);
				// if a solution was not found, processes will synchronise in here
				MPI_Barrier(*world_Comm); // this guarantees that center nodes gets aware of prior signals
				return;
			}

			//sending signal to center so this one turn into receiving best result mode
			int signal = true;
			put_mpi(&signal, 1, MPI::BOOL, 0, world_rank, *win_inbox_bestResult);

			printf("rank %d put signal in inbox to retrieve a best result \n", world_rank);

			MPI_Barrier(*world_Comm); // this guarantees that center nodes gets aware of prior signals

			//char *buffer = bestRstream.second.str().data(); //This does not work, SEGFAULT
			int Bytes = bestRstream.second.str().size();
			int refVal = bestRstream.first;

			MPI_Ssend(bestRstream.second.str().data(), Bytes, MPI::CHAR, 0, refVal, *world_Comm);
			printf("rank %d sent best result, Bytes : %d, refVal : %d\n", world_rank, Bytes, refVal);

			//reset bestRstream
			bestRstream.first = -1;		// reset condition to avoid sending empty buffers
			bestRstream.second.str(""); // clear stream, eventhough it's not necessary since this value is replaced when a better solution is found
		}

		void accumulate_mpi(int buffer, int origin_count, MPI_Datatype mpi_datatype, int target_rank, MPI_Aint offset, MPI_Win &window, std::string msg)
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

		MPI_Comm &getCommunicator()
		{
			return *world_Comm;
		}

#endif
	};

	BranchHandler *BranchHandler::INSTANCE = nullptr;
	std::once_flag BranchHandler::initInstanceFlag;

} // namespace library

#endif
