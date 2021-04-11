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
//#include "pool_include.hpp"
#include "ThreadPool.hpp"

#include "../MPI_Modules/Utils.hpp"
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

namespace library
{
	template <typename _Ret, typename... Args>
	class ResultHolder;

	class Scheduler;

	class BranchHandler
	{
		template <typename _Ret, typename... Args>
		friend class library::ResultHolder;


	private:
	
		std::atomic<size_t> idCounter;
		std::atomic<size_t> requests;

		std::map<int, void *> roots; // every thread will be solving a sub tree, this point to their roots


		int bestValLocal; 
#ifdef DLB
		bool is_DLB = true;
#else
		bool is_DLB = false;
#endif


		unsigned int processor_count;
		std::atomic<long long> idleTime;
		std::atomic<int> busyThreads;
		std::mutex mtx; //local mutex

		std::condition_variable cv;
		ThreadPool::Pool thread_pool;





		Scheduler scheduler;


		BranchHandler() : scheduler(*this)
		{
			
		}
		
		
		
		


		

	public:
		long dummyvar = 0;
		static BranchHandler &getInstance()
		{
			static BranchHandler instance;
			return instance;
		}

	
		~BranchHandler()
		{

		}
		BranchHandler(const BranchHandler &) = delete;
		BranchHandler(BranchHandler &&) = delete;
		BranchHandler &operator=(const BranchHandler &) = delete;
		BranchHandler &operator=(BranchHandler &&) = delete;
		
		
		
		
		
		
		void init()
		{
		

		
						
			
			

			/*int namelen;
			MPI_Get_processor_name(processor_name, &namelen);
			fmt::print("Process {} of {} is on {}\n", world_rank, world_size, processor_name);
			*/
			

	
	
	
		
			this->processor_count = std::thread::hardware_concurrency();
			this->busyThreads = 0;
			this->idleTime = 0;
			this->idCounter = 0;
			this->requests = 0;

			//this->roots.resize(processor_count, nullptr);

			this->bestValLocal = 999999; //TODO ML : NOT CLEAN
			
			scheduler.initMPI();
		}
		
		
		
		
		
		
		Scheduler& getScheduler()
		{
			return scheduler;
		}
		
		
	
	
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


		double getPoolIdleTime()
		{
			return thread_pool.getIdleTime() / (double)processor_count;
		}

		int getMaxThreads() 
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
			//this->roots.resize(poolSize, nullptr);
		}

		//seconds
		double getIdleTime()
		{
			double nanoseconds = idleTime / ((double)processor_count + 1);
			return nanoseconds * 1.0e-9; // convert to seconds
		}

		size_t getUniqueId()
		{
			std::unique_lock<std::mutex> lck(mtx);
			++idCounter;
			lck.unlock();
			return idCounter;
		}

		void setBestVal(int newval)
		{
			
				
		
			if (newval < bestValLocal)
				cout<<"WR="<<scheduler.getWorldRank()<<" setbestval newval="<<newval<<" bestValLocal="<<bestValLocal<<" centerval="<<scheduler.getCenterBestVal()<<endl;
						
			
//ML : this could be avoided if we had a non-mpi scheduler class
#ifdef MPI_ENABLED
			std::unique_lock<std::mutex> lck(mtx);	
			if (newval < scheduler.getCenterBestVal())
			{
				scheduler.setCenterBestVal(newval);
			}
			lck.unlock();
#endif

			if (newval >= bestValLocal)
				return;
			
			
				
			std::unique_lock<std::mutex> lck2(mtx);
			if (newval < bestValLocal)
			{
				bestValLocal = newval;
			}
			lck2.unlock();
 
		
		}
		
		
		
		int getBestVal()
		{
			//should be thread safe, but in the worst case we return a slightly outdated value
			//std::unique_lock<std::mutex> lck(mtx);
			//int bval = min(bestValLocal, scheduler.getCenterBestVal());
			//lck.unlock();
			//return bval;
			
			int bval = bestValLocal;
			
			if (mtx.try_lock())
			{
				bestValLocal = min(bval, scheduler.getCenterBestVal());
				mtx.unlock();
			}
			
			return bestValLocal;
		}
		
		
		
		

	private:
		template <typename Holder>
		void helper(Holder *parent, Holder &child)
		{
			child.parent = parent->itself;
			child.root = parent->root;
			parent->children.push_back(&child);
		}

		template <typename Holder, typename... Args>
		void helper(Holder *parent, Holder &child, Args &...args)
		{
			child.parent = parent->itself;
			child.root = parent->root;
			parent->children.push_back(&child);
		}

	public:
		template <typename Holder, typename... Args>
		void linkParent(int threadId, void *parent, Holder &child, Args &...args)
		{
			if (is_DLB)
			{
				if (!parent) // this should only happen when parent was nullptr at children's construction time
				{
					Holder *virtualRoot = new Holder(*this, threadId);
					virtualRoot->setDepth(child.depth);

					child.parent = static_cast<Holder *>(roots[threadId]);

#pragma omp critical(sync_roots_access)
					{
						child.root = &roots[threadId];
					}

					virtualRoot->children.push_back(&child);
					helper(virtualRoot, args...);
				}
			}
		}

	


		size_t getNumberRequests()
		{
			return requests.load();
		}



		/* for void algorithms, this allows to reuse the pool*/
		void wait()
		{
#ifdef DEBUG_COMMENTS
			fmt::print("Main thread waiting results \n");
#endif
			this->thread_pool.wait();
		}

		
		
		
		
		int getWorldRank()
		{
			return scheduler.getWorldRank();
		}
		
		
		
		template <typename Function, typename Serializer, typename Deserializer, typename Holder>
		void startMPINode(Holder &initHolder, Function &&function, Serializer &&serializer, Deserializer &&deserializer)
		{
			int world_rank = scheduler.getWorldRank();
			if (world_rank == 0)
			{
				scheduler.runCenter();
			}
			else
			{
				bool runHolder = false;
				if (scheduler.getWorldRank() == 1)
					runHolder = true;
				scheduler.runNode(function, serializer, deserializer, initHolder, runHolder);
			}
		}
		
		
		
		
		void finalize()
		{
			scheduler.finalize();
		
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

		template <typename Holder>
		Holder *checkParent(Holder *holder)
		{

			Holder *leftMost = nullptr; // this is the branch that led us to the root
			Holder *root = nullptr;		// local pointer to root, to avoid "*" use

			if (holder->parent) // this confirms there might be a root
			{
				if (holder->parent != *holder->root) // this confirms, the root isn't the parent
				{
					/* this condition complies if a branch has already
					 been pushed, to ensure pushing leftMost first */
					root = static_cast<Holder *>(*holder->root); //no need to iterate
					//int tmp = root->children.size(); // this probable fix the following

					// the following is not true, it could be also the right branch
					// Unless root is guaranteed to have at least 2 children,
					// TODO ... verify

					leftMost = root->children.front(); //TODO ... check if branch has been pushed or forwarded
				}
				else
					return nullptr; // parent == root
			}
			else
				return nullptr; // there is no parent

#ifdef DEBUG_COMMENTS
			fmt::print("rank {}, likely to get an upperHolder \n", world_rank);
#endif
			int N_children = root->children.size();

#ifdef DEBUG_COMMENTS
			fmt::print("rank {}, root->children.size() = {} \n", world_rank, N_children);
#endif

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

			/* there migh be a chance that a good solution has been found in which a top branch is wortless to
				be pushed, then this branch is ignored if the bound condition is met*/
			auto worthPushing = [](Holder *holder) -> Holder * {
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
				auto secondHolder = *second;						// catches the pointer of the node	<-------------------------
				root->children.erase(second);						// removes second node from the root's children

				return worthPushing(secondHolder);
			}
			else if (root->children.size() == 2)
			{
#ifdef DEBUG_COMMENTS
				fmt::print("rank {}, about to choose an upperHolder \n", world_rank);
#endif
				/*	this scope is meant to push right branch which was put in waiting line
					because there was no available thread to push leftMost branch, then leftMost
					will be the new root since after this scope right branch will have been
					already pushed*/

				root->children.pop_front();				// deletes leftMost from root's children
				Holder *right = root->children.front(); // The one to be pushed
				root->children.clear();					// ..
				right->prune();							// just in case, right branch is not being sent anyway, only its data
				leftMost->lowerRoot();					// it sets leftMost as the new root

				rootCorrecting(leftMost); // if leftMost has no pending branch, then root will be assigned to the next
										  // descendant with at least two children (which is at least a pending branch),
										  // or the lowest branch which is th one giving priority to root's children

				return worthPushing(right);
			}
			else
			{
				fmt::print("fw_count : {} \n ph_count : {}\n isVirtual :{} \n isDiscarded : {} \n",
						   root->fw_count,
						   root->ph_count,
						   root->isVirtual,
						   root->isDiscarded);
				fmt::print("4 Testing, it's not supposed to happen, checkParent() \n");
				//auto s =std::source_location::current();
				//fmt::print("[{}]{}:({},{})\n", s.file_name(), s.function_name(), s.line(), s.column());
				throw "4 Testing, it's not supposed to happen, checkParent()";
				return nullptr;
			}
		}

		// controls the root when sequential calls
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
				parent is also the root, then leftMost child of the root should be
				deleted of the list since it is already solved. Thus, pushing cb twice
				is avoided because checkParent() pushes the second element of the children
			*/

			if (holder->parent) //this confirms the holder is not a root
			{
				if (holder->parent == *holder->root) //this confirms that it's the first level of the root
				{
					Holder *leftMost = holder->parent->children.front();
					if (leftMost != holder) //This confirms pb has already been solved
					{						/*
						 root == parent
						  /  |  \   \  \
						 /   |   \	 \	 \
						/    |    \	  \	   \
					  pb     cb    w1  w2 ... wk
					  		 **
					   next conditional should always comply, there should not be required
						* to use a loop, then this While is entitled to just a single loop. 4 testing!!
						*/
						auto leftMost_cpy = leftMost;
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

						holder->lowerRoot();
						leftMost_cpy->prune();
						//holder->parent = nullptr;
						//holder->prune(); //not even required, nullptr is sent
					}
				}
				else if (holder->parent != *holder->root) //any other level,
				{
					/*
						 root != parent
						   /|  \   \  \
						  / |   \	 \	 \
					solved  *    w1  w2 . wk
					       /|
					solved	*
						   /|
					solved	* parent
						   / \
				 solved(pb)  cb


					this is relevant, because eventhough the root still has some waiting nodes
					the thread in charge of the tree might be deep down solving everything sequentially.
					Every time a leftMost branch is solved sequentially, this one should be removed from 
					the list to avoid failure attempts of solving a branch that has already been solved.

					If a thread attempts to solve an already solved branch, this will throw an error
					because the node won't have information anymore since it has already been passed
					*/

					Holder *leftMost = holder->parent->children.front();
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

		// controls the root when succesfull parallel calls ( if not upperHolder available)
		template <typename Holder>
		void checkRightSiblings(Holder *holder)
		{
			/* this method is invoked when DLB is enable and the method checkParent() was not able to find
		a top branch to push, because it means the next right sibling will become a root(for binary recursion)
		or just the leftMost will be unlisted from the parent's children. This method is invoked if and only if 
		an available worker is available*/
			auto *_parent = holder->parent;
			if (_parent)						  // it should always comply, virtual parent is being created
			{									  // it also confirms that holder is not a parent (applies for DLB)
				if (_parent->children.size() > 2) // this is for more than two recursions per scope
				{
					//TODO ..
				}
				else if (_parent->children.size() == 2) // this verifies that  it's binary and the rightMost will become a new root
				{
					_parent->children.pop_front();
					auto right = _parent->children.front();
					_parent->children.pop_front();
					right->lowerRoot();
				}
				else
				{
					std::cout << "4 Testing, it's not supposed to happen, checkRightSiblings()" << std::endl;
					//auto s =std::source_location::current();
					//fmt::print("[{}]{}:({},{})\n", s.file_name(), s.function_name(), s.line(), s.column());
					throw "4 Testing, it's not supposed to happen, checkRightSiblings()";
				}
			}
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
		void rootCorrecting(Holder *root)
		{
			Holder *_root = root;

			while (_root->children.size() == 1) // lowering the root
			{
				_root = _root->children.front();
				_root->parent->children.pop_front();
				_root->lowerRoot();
			}
		}

		template <typename F, typename Holder>
		bool assignTopHolderToThread(std::unique_lock<std::mutex> &lck, F &&f, Holder &holder)
		{
			Holder *upperHolder = checkParent(&holder);
			if (upperHolder)
			{
				if (!upperHolder->evaluate_branch_checkIn())
				{
					upperHolder->setDiscard();
					return true;
				}
				else
				{

					this->requests++;
					this->busyThreads++;
					upperHolder->setPushStatus();
					lck.unlock();

					std::args_handler::unpack_and_push_void(thread_pool, f, upperHolder->getArgs());
					return true; // top holder found
				}
			}
			//checkRightSiblings(&holder); // this decrements parent's children
			return false; // top holder not found
		}






		/*
		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		bool assignTopHolderToThread(std::unique_lock<std::mutex> &lck, F &&f, Holder &holder)
		{
			Holder *upperHolder = checkParent(&holder);
			if (upperHolder)
			{
				if (!upperHolder->evaluate_branch_checkIn())
				{
					upperHolder->setDiscard();
					return true;
				}

				this->requests++;
				this->busyThreads++;
				upperHolder->setPushStatus();
				lck.unlock();
				auto ret = std::args_handler::unpack_and_push_non_void(thread_pool, f, holder.getArgs());
				upperHolder->hold_future(std::move(ret));
				return true; // top holder found
			}
			//checkRightSiblings(&holder); // this decrements parent's children
			return false; // top holder not found
		}*/





	public:
	
	
		//added the nomutex suffix to ensure that user knows this is not thread safe
		bool hasBusyThreads_nomutex()
		{
			return (busyThreads > 0);
		}
	
	
	
		bool hasBusyThreads()
		{
			std::unique_lock<std::mutex> lck(mtx);
			bool ret = (busyThreads > 0);
			lck.unlock();
			return ret;
		}
	
	
	
		template <typename F, typename Holder>
		bool push_multithreading(F &&f, int id, Holder &holder)
		{
			/*This lock must be acquired before checking the condition,	
			even though busyThreads is atomic*/
			//std::unique_lock<std::mutex> lck(mtx);
			
			if (busyThreads < thread_pool.size())	//don't even try to lock if this doesn't pass, then make sure we lock
			{	

				//if (mtx.try_lock())
				std::unique_lock<std::mutex> lck(mtx);
				{

					if (busyThreads < thread_pool.size())
					{

						/*if (is_DLB)
						{
							bool res = assignTopHolderToThread(lck, f, holder);
							if (res)
								return false; // if top holder found, then it should
											  // return false to keep trying another top holder

							checkRightSiblings(&holder); // this decrements parent's children
						}*/

						//after this line, only leftMost holder should be pushed
						this->requests++;
						this->busyThreads++;
						holder.setPushStatus();
						holder.prune();

						//mtx.unlock();
						lck.unlock();

						std::args_handler::unpack_and_push_void(thread_pool, f, holder.getArgs());
						return true;
					}
					lck.unlock();
					//mtx.unlock();
				}
			}

			
			this->forward(f, id, holder);
			
			return true;

		}




		/*
		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		bool push_multithreading(F &&f, int id, Holder &holder)
		{
			std::unique_lock<std::mutex> lck(mtx);
			if (busyThreads < thread_pool.size())
			{
				if (is_DLB)
				{
					bool res = assignTopHolderToThread<_ret>(lck, f, holder);
					if (res)
						return false; //if top holder found, then it should return false to keep trying

					checkRightSiblings(&holder);
				}
				this->requests++;
				busyThreads++;
				holder.setPushStatus();

				lck.unlock();
				auto ret = std::args_handler::unpack_and_push_non_void(thread_pool, f, holder.getArgs());
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
		*/


		template <typename F, typename Holder>
		bool push_multithreading(F &&f, int id, Holder &holder, bool trackStack)
		{
			bool flag = false;
			/* false means that current holder was not able to be pushed 
				because a top holder was pushed instead, this false allows
				to keep trying to find a top holder in the case of an
				available thread*/
			while (!flag)
				flag = push_multithreading(f, id, holder);

			return flag; // this is for user's tracking pursposes if applicable
		}

#ifdef MPI_ENABLED

		/*
 		return 0, for normal success with an available process
 		return 1, for failure with no available process
 		return 2, for DLB succes with an available process*/
		template <typename Holder, typename F_SERIAL>
		int try_another_process(Holder &holder, F_SERIAL &&serializer)
		{
			
			//if (mtx.try_lock())
			std::unique_lock<std::mutex> lck(mtx);
			{

				bool res = scheduler.sendHolderToNode(holder, serializer);
				
				//mtx.unlock();
				lck.unlock();
				if (res)
				{
					return 0;
				}
				
				
				
			}
			
			return 1;
		}
		
		
		
		/*vector<void*> tasks;
		
		template <typename Holder, typename... Args>
		void addTask(Args& ...args)
		{
			Holder* h = new Holder();
			h->holdArgs(args...);
			tasks.push_back((void*)h);
		}



		void* popHolder()
		{
			std::unique_lock<std::mutex> lck(mtx);
			
			if (tasks.empty())
			{
				lck.unlock();
				return nullptr;
			}
			else
			{
				void* h = tasks.back();
				tasks.pop_back();
				lck.unlock();
				return h;
			}
			
			
		}*/


		void printDebugInfo()
		{
			scheduler.printDebugInfo();
		}



		template <typename F, typename Holder, typename F_SERIAL>
		bool push_multiprocess(F &&f, int id, Holder &holder, F_SERIAL &&f_serial)
		{
			
			int r = try_another_process(holder, f_serial);
			if (r == 0)
				return true; // top holder pushed to another rank
			if (r == 2)
				return false; // current holder pushed to another rank


			return push_multithreading(f, id, holder); //no rank available
			
		}

		template <typename F, typename Holder, typename F_SERIAL>
		bool push_multiprocess(F &&f, int id, Holder &holder, F_SERIAL &&f_serial, bool)
		{
			bool flag = false;
			while (!flag)
				flag = push_multiprocess(f, id, holder, f_serial);

			return flag;
		}




		//NON VOID VERSION
		/*template <typename _ret, typename F, typename Holder, typename F_SERIAL,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		bool push_multiprocess(F &&f, int id, Holder &holder, F_SERIAL &&f_serial)
		{
			int r = try_another_process(holder, f_serial);
			if (r == 0)
				return true;
			if (r == 2)
				return false;

			return push_multithreading<_ret>(f, id, holder);
		}*/


#endif
		// no DLB begin **********************************************************************
		template <typename F, typename Holder>
		void forward(F &&f, int threadId, Holder &holder)
		{
			if (is_DLB)
			{

				if (holder.is_pushed())
				{
					throw "Holder is already pushed.  Someone tried to push it again.";
					return;
				}

				checkLeftSibling(&holder);
			
			}
			//if (threadId != 0)	
			//cout<<"TID="<<threadId<<endl;
			holder.setForwardStatus();
			holder.threadId = threadId;
			std::args_handler::unpack_and_forward_void(f, threadId, holder.getArgs(), &holder);

		}

		/*template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		_ret forward(F &&f, int threadId, Holder &holder)
		{
			holder.setForwardStatus();
			holder.threadId = threadId;
			return std::args_handler::unpack_and_forward_non_void(f, threadId, holder.getArgs(), &holder);
		}*/

		// no DLB ************************************************************************* end







		/*
		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		_ret forward(F &&f, int threadId, Holder &holder, bool)
		{

			if (holder.is_pushed())
				return holder.get();

			if (is_DLB)
				checkLeftSibling(&holder);

			return forward<_ret>(f, threadId, holder);
		}

		template <typename _ret, typename F, typename Holder, typename F_DESER,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		_ret forward(F &&f, int threadId, Holder &holder, F_DESER &&f_deser, bool)
		{
			if (holder.is_pushed() || holder.is_MPI_Sent()) //TODO.. this should be considered when using DLB and pushing to another processsI
				return holder.get(f_deser);					//return {}; // nope, if it was pushed, then result should be retrieved in here

			if (is_DLB)
				checkLeftSibling(&holder);

			return forward<_ret>(f, threadId, holder);
		}
		*/

		/*This syncronizes available threads in branchHandler with
			busyThreads in pool, this is relevant when using void functions
			because there is no need to call getResults(),
			nevertheless this should be avoided when pushing void functions*/
		void functionIsVoid()
		{
			this->thread_pool.setExternNumThreads(&this->busyThreads);
		}

	private:
		// thread safe: root creation or root switching
		void assign_root(int threadId, void *root)
		{
			//std::unique_lock<std::mutex> lck(mtx);
#pragma omp critical(sync_roots_access)
			{
				roots[threadId] = root;
			}
		}

		

	
	};


} // namespace library

#endif
