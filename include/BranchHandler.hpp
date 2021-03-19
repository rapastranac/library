﻿#pragma once
#ifndef BRANCHHANDLER_H
#define BRANCHHANDLER_H

/*
* Created by Andres Pastrana on 2019
* pasr1602@usherbrooke.ca
* rapastranac@gmail.com
*/
#include <fmt/format.h>
#include "args_handler.hpp"
#include "pool_include.hpp"

#include "../MPI_Modules/Utils.hpp"
#ifdef MPI_ENABLED
#include "../MPI_Modules/MPI_Mutex.hpp"
#include <mpi.h>
#include <stdio.h>
#endif

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
			return idCounter;
		}

		template <typename T>
		void catchBestResult(T &&bestR)
		{
			this->bestR = std::move(bestR);
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
					child.root = &roots[threadId];

					virtualRoot->children.push_back(&child);
					helper(virtualRoot, args...);
				}
			}
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

		// overload to ignore second condition, nullptr must be passed
		template <typename _ret, class C1, typename T, class F_SERIAL,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		bool replace_refValGlobal_If(int refValueLocal, C1 &&Cond, std::nullptr_t, T &&result, F_SERIAL &&f_serial)
		{
			std::unique_lock<std::mutex> lck(mtx_MPI);
#ifdef DEBUG_COMMENTS
			fmt::print("rank {} entered replace_refValGlobal_If(), acquired mutex \n", world_rank);
#endif

			if (Cond(refValueGlobal[0], refValueLocal)) // check global val in this node
			{
#ifdef DEBUG_COMMENTS
				fmt::print("rank {}, local condition satisfied refValueGlobal : {} vs refValueLocal : {} \n", world_rank, refValueGlobal[0], refValueLocal);
#endif
				// then, absolute global is checked
				int refValueGlobalAbsolute;
				int origin_count = 1;
				int target_rank = 0;
				MPI_Aint offset = 0;
				MPI_Win &window = *win_refValueGlobal; // change to a reference to the window (&window, or *window)

				//mpi_mutex->lock(world_rank); // critical section begins

				MPI_Win_lock(MPI_LOCK_EXCLUSIVE, target_rank, 0, window); // open epoch
#ifdef DEBUG_COMMENTS
				fmt::print("rank {} opened epoch to send best result \n", world_rank);
#endif
				// this blocks access to this window
				// this is the only place where this window is read or modified

				MPI_Get(&refValueGlobalAbsolute, origin_count, MPI_INT, target_rank, offset, 1, MPI_INT, window);
				MPI_Win_flush(target_rank, window); // retrieve refValueGlobalAbsolute which is the most up-to-date value

#ifdef DEBUG_COMMENTS
				fmt::print("rank {} got refValueGlobalAbsolute : {} \n", world_rank, refValueGlobalAbsolute);
#endif

				if (Cond(refValueGlobalAbsolute, refValueLocal)) // compare absolute global value against local
				{
					this->refValueGlobal[0] = refValueLocal; // updates global ref value, in this node

					MPI_Accumulate(&refValueLocal, origin_count, MPI_INT, target_rank, offset, 1, MPI_INT, MPI_REPLACE, window);
					MPI_Win_flush(target_rank, window); // after this line, global ref value is updated in center node, but not broadcasted

					fmt::print("rank {} updated refValueGlobalAbsolute to {} || {} \n", world_rank, refValueLocal, refValueGlobal[0]);

					auto ss = f_serial(result); // serialized result
#ifdef DEBUG_COMMENTS
					fmt::print("rank {}, cover size : {} \n", world_rank, result.coverSize());
#endif
					//int sz_before = bestRstream.second.str().size(); //testing only

					int SIZE = ss.str().size();
					fmt::print("rank {}, buffer size to be sent : {} \n", world_rank, SIZE);

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

		/* a reference value is replaced based on the user's conditions. 
			ifCond is used when the user wants to print out results or any other thing thread safely
			ifCond is optional, if not passed, then nullptr should be passed in instead*/
		template <typename _ret, class C1, class C2, typename T, class F_SERIAL,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		bool replace_refValGlobal_If(int refValueLocal, C1 &&Cond, C2 &&ifCond, T &&result, F_SERIAL &&f_serial)
		{
			std::unique_lock<std::mutex> lck(mtx_MPI);
#ifdef DEBUG_COMMENTS
			fmt::print("rank {} entered replace_refValGlobal_If(), acquired mutex \n", world_rank);
#endif

			if (Cond(refValueGlobal[0], refValueLocal)) // check global val in this node
			{
#ifdef DEBUG_COMMENTS
				fmt::print("rank {}, local condition satisfied refValueGlobal : {} vs refValueLocal : {} \n", world_rank, refValueGlobal[0], refValueLocal);
#endif
				// then, absolute global is checked
				int refValueGlobalAbsolute;
				int origin_count = 1;
				int target_rank = 0;
				MPI_Aint offset = 0;
				MPI_Win &window = *win_refValueGlobal; // change to a reference to the window (&window, or *window)

				//mpi_mutex->lock(world_rank); // critical section begins

				MPI_Win_lock(MPI_LOCK_EXCLUSIVE, target_rank, 0, window); // open epoch
#ifdef DEBUG_COMMENTS
				fmt::print("rank {} opened epoch to send best result \n", world_rank);
#endif
				// this blocks access to this window
				// this is the only place where this window is read or modified

				MPI_Get(&refValueGlobalAbsolute, origin_count, MPI_INT, target_rank, offset, 1, MPI_INT, window);
				MPI_Win_flush(target_rank, window); // retrieve refValueGlobalAbsolute which is the most up-to-date value

#ifdef DEBUG_COMMENTS
				fmt::print("rank {} got refValueGlobalAbsolute : {} \n", world_rank, refValueGlobalAbsolute);
#endif

				if (Cond(refValueGlobalAbsolute, refValueLocal)) // compare absolute global value against local
				{
					this->refValueGlobal[0] = refValueLocal; // updates global ref value, in this node

					ifCond();

					MPI_Accumulate(&refValueLocal, origin_count, MPI_INT, target_rank, offset, 1, MPI_INT, MPI_REPLACE, window);
					MPI_Win_flush(target_rank, window); // after this line, global ref value is updated in center node, but not broadcasted

					fmt::print("rank {} updated refValueGlobalAbsolute to {} || {} \n", world_rank, refValueLocal, refValueGlobal[0]);

					auto ss = f_serial(result); // serialized result
#ifdef DEBUG_COMMENTS
					fmt::print("rank {}, cover size : {} \n", world_rank, result.coverSize());
#endif
					//int sz_before = bestRstream.second.str().size(); //testing only

					int SIZE = ss.str().size();
					fmt::print("rank {}, buffer size to be sent : {} \n", world_rank, SIZE);

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
		// overload to ignore second condition, nullptr must be passed
		template <typename _ret, class C1, typename T, class F_SERIAL,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		bool replace_refValGlobal_If(int refValueLocal, C1 &&Cond, std::nullptr_t, T &&result, F_SERIAL &&f_serial)
		{
			std::unique_lock<std::mutex> lck(mtx_MPI);
#ifdef DEBUG_COMMENTS
			fmt::print("rank {} entered replace_refValGlobal_If(), acquired mutex \n", world_rank);
#endif

			if (Cond(refValueGlobal[0], refValueLocal)) // check global val in this node
			{
#ifdef DEBUG_COMMENTS
				fmt::print("rank {}, local condition satisfied refValueGlobal : {} vs refValueLocal : {} \n", world_rank, refValueGlobal[0], refValueLocal);
#endif
				// then, absolute global is checked
				int refValueGlobalAbsolute;
				int origin_count = 1;
				int target_rank = 0;
				MPI_Aint offset = 0;
				MPI_Win &window = *win_refValueGlobal; // change to a reference to the window (&window, or *window)

				//mpi_mutex->lock(world_rank); // critical section begins

				MPI_Win_lock(MPI_LOCK_EXCLUSIVE, target_rank, 0, window); // open epoch
#ifdef DEBUG_COMMENTS
				fmt::print("rank {} opened epoch to send best result \n", world_rank);
#endif
				// this blocks access to this window
				// this is the only place where this window is read or modified

				MPI_Get(&refValueGlobalAbsolute, origin_count, MPI_INT, target_rank, offset, 1, MPI_INT, window);
				MPI_Win_flush(target_rank, window); // retrieve refValueGlobalAbsolute which is the most up-to-date value

#ifdef DEBUG_COMMENTS
				fmt::print("rank {} got refValueGlobalAbsolute : {} \n", world_rank, refValueGlobalAbsolute);
#endif

				if (Cond(refValueGlobalAbsolute, refValueLocal)) // compare absolute global value against local
				{
					this->refValueGlobal[0] = refValueLocal; // updates global ref value, in this node

					MPI_Accumulate(&refValueLocal, origin_count, MPI_INT, target_rank, offset, 1, MPI_INT, MPI_REPLACE, window);
					MPI_Win_flush(target_rank, window); // after this line, global ref value is updated in center node, but not broadcasted

					fmt::print("rank {} updated refValueGlobalAbsolute to {} || {} \n", world_rank, refValueLocal, refValueGlobal[0]);

					auto ss = f_serial(result); // serialized result

					//fmt::print("rank {}, cover size : {} \n", world_rank, result.coverSize());
					//int sz_before = bestRstream.second.str().size(); //testing only

					int SIZE = ss.str().size();
					fmt::print("rank {}, buffer size to be sent : {} \n", world_rank, SIZE);

					//bestRstream.first = refValueLocal;
					//bestRstream.second = std::move(ss);

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

		template <typename _ret, class C1, class C2, typename T, class F_SERIAL,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		bool replace_refValGlobal_If(int refValueLocal, C1 &&Cond, C2 &&ifCond, T &&result, F_SERIAL &&f_serial)
		{
			std::unique_lock<std::mutex> lck(mtx_MPI);
#ifdef DEBUG_COMMENTS
			fmt::print("rank {} entered replace_refValGlobal_If(), acquired mutex \n", world_rank);
#endif

			if (Cond(refValueGlobal[0], refValueLocal)) // check global val in this node
			{
#ifdef DEBUG_COMMENTS
				fmt::print("rank {}, local condition satisfied refValueGlobal : {} vs refValueLocal : {} \n", world_rank, refValueGlobal[0], refValueLocal);
#endif
				// then, absolute global is checked
				int refValueGlobalAbsolute;
				int origin_count = 1;
				int target_rank = 0;
				MPI_Aint offset = 0;
				MPI_Win &window = *win_refValueGlobal; // change to a reference to the window (&window, or *window)

				//mpi_mutex->lock(world_rank); // critical section begins

				MPI_Win_lock(MPI_LOCK_EXCLUSIVE, target_rank, 0, window); // open epoch
#ifdef DEBUG_COMMENTS
				fmt::print("rank {} opened epoch to send best result \n", world_rank);
#endif
				// this blocks access to this window
				// this is the only place where this window is read or modified

				MPI_Get(&refValueGlobalAbsolute, origin_count, MPI_INT, target_rank, offset, 1, MPI_INT, window);
				MPI_Win_flush(target_rank, window); // retrieve refValueGlobalAbsolute which is the most up-to-date value

#ifdef DEBUG_COMMENTS
				fmt::print("rank {} got refValueGlobalAbsolute : {} \n", world_rank, refValueGlobalAbsolute);
#endif

				if (Cond(refValueGlobalAbsolute, refValueLocal)) // compare absolute global value against local
				{
					this->refValueGlobal[0] = refValueLocal; // updates global ref value, in this node

					ifCond();

					MPI_Accumulate(&refValueLocal, origin_count, MPI_INT, target_rank, offset, 1, MPI_INT, MPI_REPLACE, window);
					MPI_Win_flush(target_rank, window); // after this line, global ref value is updated in center node, but not broadcasted

					fmt::print("rank {} updated refValueGlobalAbsolute to {} || {} \n", world_rank, refValueLocal, refValueGlobal[0]);

					auto ss = f_serial(result); // serialized result

					//fmt::print("rank {}, cover size : {} \n", world_rank, result.coverSize());
					//int sz_before = bestRstream.second.str().size(); //testing only

					int SIZE = ss.str().size();
					fmt::print("rank {}, buffer size to be sent : {} \n", world_rank, SIZE);

					//bestRstream.first = refValueLocal;
					//bestRstream.second = std::move(ss);

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

		// overload to ignore second condition, nullptr must be passed
		template <typename _ret, class C1, typename T,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		bool replace_refValGlobal_If(int refValueLocal, C1 &&Cond, std::nullptr_t, T &&result)
		{
			std::unique_lock<std::mutex> lck(mtx);
			if (Cond(refValueGlobal[0], refValueLocal))
			{
				this->refValueGlobal[0] = refValueLocal;
				this->bestR = result;
				return true;
			}
			else
				return false;
		}

		/* a reference value is replaced based on the user's conditions. 
			ifCond is used when the user wants to print out results or any other thing thread safely
			ifCond is optional, if not passed, then nullptr should be passed in instead*/
		template <typename _ret, class C1, class C2, typename T,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		bool replace_refValGlobal_If(int refValueLocal, C1 &&Cond, C2 &&ifCond, T &&result)
		{
			std::unique_lock<std::mutex> lck(mtx);
			if (Cond(refValueGlobal[0], refValueLocal))
			{
				this->refValueGlobal[0] = refValueLocal;
				if (refValueLocal == 0)
				{
					int fgd = 3423; // testing, debugging
				}
				ifCond();

				this->bestR = result; //it should move, this copy is only for testing
				return true;
			}
			else
				return false;
		}

		// overload to ignore second condition, nullptr must be passed
		template <typename _ret, class C1, typename T,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		bool replace_refValGlobal_If(int refValueLocal, C1 &&Cond, std::nullptr_t, T &&result)
		{
			std::unique_lock<std::mutex> lck(mtx);
			if (Cond(refValueGlobal[0], refValueLocal))
			{
				this->refValueGlobal[0] = refValueLocal;
				return true;
			}
			else
				return false;
		}
		/* a reference value is replaced based on the user's conditions. 
			ifCond is used when the user wants to print out results or any other thing thread safely
			ifCond is optional, if not passed, then nullptr should be passed in instead*/
		template <typename _ret, class C1, class C2, typename T,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		bool replace_refValGlobal_If(int refValueLocal, C1 &&Cond, C2 &&ifCond, T &&result)
		{
			std::unique_lock<std::mutex> lck(mtx);
			if (Cond(refValueGlobal[0], refValueLocal))
			{
				this->refValueGlobal[0] = refValueLocal;
				ifCond();
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

		size_t getNumberRequests()
		{
			return requests.load();
		}

		int getRankID()
		{
#ifdef MPI_ENABLED
			return world_rank;
#else
			return -1;
#endif
		}

		/* for void algorithms, this also calls the destructor of the pool*/
		void wait_and_finish()
		{

#ifdef DEBUG_COMMENTS
			fmt::print("Main thread interrupting pool \n");
#endif
			this->thread_pool.interrupt(true);
		}

		/* for void algorithms, this allows to reuse the pool*/
		void wait()
		{
#ifdef DEBUG_COMMENTS
			fmt::print("Main thread waiting results \n");
#endif
			this->thread_pool.wait();
		}

		bool has_result()
		{
			return bestR.has_value();
		}

		void clear_result()
		{
			bestR.reset();
		}

		template <typename RESULT_TYPE>
		[[nodiscard]] auto retrieveResult() -> RESULT_TYPE
		{ // fetching results caught by the library=

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
		int *getRefValueTest()
		{
			return refValueGlobal;
		}

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
			fmt::print("rank {}, refValueGlobal has been set : {} at {} \n", world_rank, refValueGlobal[0], fmt::ptr(refValueGlobal));
#endif
			if (world_rank == 0)
			{
				int err = MPI_Bcast(refValueGlobal, 1, MPI_INT, 0, *world_Comm);
				if (err != MPI_SUCCESS)
					fmt::print("rank {}, broadcast unsucessful with err = {} \n", world_rank, err);
#ifdef DEBUG_COMMENTS
				fmt::print("refValueGlobal broadcasted: {} at {} \n", refValueGlobal[0], fmt::ptr(refValueGlobal));
#endif
			}
			else
			{
				int err = MPI_Bcast(refValueGlobal, 1, MPI_INT, 0, *world_Comm);
				if (err != MPI_SUCCESS)
					fmt::print("rank {}, broadcast unsucessful with err = {} \n", world_rank, err);
#ifdef DEBUG_COMMENTS
				fmt::print("rank {}, refValueGlobal received: {} at {} \n", world_rank, refValueGlobal[0], fmt::ptr(refValueGlobal));
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
#ifdef MPI_ENABLED
		int put_mpi(const void *origin_addr, int count, MPI_Datatype mpi_type, int target_rank, MPI_Aint offset, MPI_Win &window)
		{
			int result;
			MPI_Win_lock(MPI_LOCK_EXCLUSIVE, target_rank, MPI_MODE_NOCHECK, window); // opens epoch
			MPI_Put(origin_addr, count, mpi_type, target_rank, offset, count, mpi_type, window);
			MPI_Win_flush(target_rank, window);

			// 4 testing, verifies if value successfully put
			MPI_Get(&result, count, mpi_type, target_rank, offset, count, mpi_type, window);
			MPI_Win_flush(target_rank, window);

			MPI_Win_unlock(target_rank, window); // closes epoch
			return result;
		}
#endif
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

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		bool try_top_holder(std::unique_lock<std::mutex> &lck, F &&f, Holder &holder)
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

				std::args_handler::unpack_and_push_void(thread_pool, f, upperHolder->getArgs());
				return true; // top holder found
			}
			//checkRightSiblings(&holder); // this decrements parent's children
			return false; // top holder not found
		}

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		bool try_top_holder(std::unique_lock<std::mutex> &lck, F &&f, Holder &holder)
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
		}

#ifdef MPI_ENABLED
		template <typename Holder, typename F_serial>
		bool try_top_holder(std::unique_lock<std::mutex> &lck, Holder &holder, F_serial &&f_serial, int dest_rank)
		{
			Holder *upperHolder = checkParent(&holder);
			if (upperHolder)
			{
				if (!upperHolder->evaluate_branch_checkIn())
				{
					upperHolder->setDiscard();
					return true;
				}

				upperHolder->setMPISent(true, dest_rank);

				auto _stream = std::apply(f_serial, upperHolder->getArgs());
				int Bytes = _stream.str().size(); // number of Bytes

				int err = MPI_Ssend(_stream.str().data(), Bytes, MPI_CHAR, dest_rank, 0, *world_Comm); // send buffer
				if (err != MPI_SUCCESS)
					fmt::print("buffer failed to sent from rank {} to rank {}! \n", world_rank, dest_rank);

				lck.unlock();
#ifdef DEBUG_COMMENTS
				fmt::print("process {} forwarded top-holder to process {} \n", world_rank, dest_rank);
#endif

				return true; // top holder found
			}
			//checkRightSiblings(&holder); // this decrements parent's children
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
			std::unique_lock<std::mutex> lck(mtx);
			if (busyThreads < thread_pool.size())
			{
				if (is_DLB)
				{
					bool res = try_top_holder<_ret>(lck, f, holder);
					if (res)
						return false; // if top holder found, then it should
									  // return false to keep trying another top holder

					checkRightSiblings(&holder); // this decrements parent's children
				}

				//after this line, only leftMost holder should be pushed
				this->requests++;
				this->busyThreads++;
				holder.setPushStatus();
				holder.prune();
				lck.unlock();

				std::args_handler::unpack_and_push_void(thread_pool, f, holder.getArgs());
				return true;
			}
			else
			{
				lck.unlock();
				if (is_DLB)
					this->forward<_ret>(f, id, holder, true);
				else
					this->forward<_ret>(f, id, holder);

				return true;
			}
		}

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		bool push_multithreading(F &&f, int id, Holder &holder)
		{
			/*This lock must be performed before checking the condition,
			even though numThread is atomic*/
			std::unique_lock<std::mutex> lck(mtx);
			if (busyThreads < thread_pool.size())
			{
				if (is_DLB)
				{
					bool res = try_top_holder<_ret>(lck, f, holder);
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

#ifdef MPI_ENABLED

		/*
 		return 0, for normal success with an available process
 		return 1, for failure with no available process
 		return 2, for DLB succes with an available process*/
		template <typename Holder, typename F_SERIAL>
		int try_another_process(Holder &holder, F_SERIAL &&f_serial)
		{
			int signal{0};
			std::unique_lock<std::mutex> mpi_lck(mtx_MPI, std::defer_lock); // this guarantees mpi_thread_serialized
			if (mpi_lck.try_lock())
			{
				CHECK_MPI_MUTEX(1, holder.getThreadId());
				//fmt::print("rank {}, numAvailableNodes : {} \n", world_rank, numAvailableNodes[0]);

				if (numAvailableNodes[0] > 0) // center node is in charge of broadcasting this number
				{
#ifdef DEBUG_COMMENTS
					fmt::print("{} about to request center node to push\n", world_rank);

					mpi_mutex->lock(0, world_rank);
#endif
					int buffer = 1;
					put_mpi(&buffer, 1, MPI_INT, 0, world_rank, *win_phsRequest); // send signal to center node
					MPI_Status status;
#ifdef DEBUG_COMMENTS
					fmt::print("process {} requested to push \n", world_rank);
#endif
					MPI_Recv(&signal, 1, MPI_INT, 0, MPI_ANY_TAG, *world_Comm, &status); //awaits signal if data can be sent

					if (signal == 1)
					{
						mpi_mutex->unlock(0, world_rank);
#ifdef DEBUG_COMMENTS
						fmt::print("process {} received positive signal from center \n", world_rank);
#endif
						int dest_rank = status.MPI_TAG; // this is the available node, sent by center node as a tag
#ifdef DEBUG_COMMENTS
						fmt::print("process {} received ID {}\n", world_rank, dest_rank);
#endif

						///****************************
						if (is_DLB)
						{
							bool r = try_top_holder(mpi_lck, holder, f_serial, dest_rank);
							if (r)
							{
								CHECK_MPI_MUTEX(-1, holder.getThreadId());
								return 2;
							}

							checkRightSiblings(&holder); // this decrements parent's children
						}
						///****************************

						holder.setMPISent(true, dest_rank);

						auto _stream = std::apply(f_serial, holder.getArgs());
						int Bytes = _stream.str().size(); // number of Bytes

						int err = MPI_Ssend(_stream.str().data(), Bytes, MPI_CHAR, dest_rank, 0, *world_Comm); // send buffer
						if (err != MPI_SUCCESS)
							fmt::print("buffer failed to send from rank {} to rank {}! \n", world_rank, dest_rank);

						CHECK_MPI_MUTEX(-1, holder.getThreadId());
						mpi_lck.unlock();
#ifdef DEBUG_COMMENTS
						fmt::print("process {} forwarded to process {} \n", world_rank, dest_rank);
#endif
						return 0;
					}

					mpi_mutex->unlock(0, world_rank);
				}

				CHECK_MPI_MUTEX(-1, holder.getThreadId());
				mpi_lck.unlock(); // this ensures to unlock it even if (numAvailableNodes == 0)
			}
			return 1; //this return is necessary only due to function extraction
		}

		template <typename _ret, typename F, typename Holder, typename F_SERIAL,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		bool push_multiprocess(F &&f, int id, Holder &holder, F_SERIAL &&f_serial)
		{
			int r = try_another_process(holder, f_serial);
			if (r == 0)
				return true;
			if (r == 2)
				return false;

			return push_multithreading<_ret>(f, id, holder);
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

		template <typename _ret, typename F, typename Holder, typename F_SERIAL>
		bool push_multiprocess(F &&f, int id, Holder &holder, F_SERIAL &&f_serial, bool)
		{
			bool _flag = false;
			while (!_flag)
				_flag = push_multiprocess<_ret>(f, id, holder, f_serial);

			return _flag;
		}
#endif
		// no DLB begin **********************************************************************
		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		_ret forward(F &&f, int threadId, Holder &holder)
		{
			holder.setForwardStatus();
			holder.threadId = threadId;
			std::args_handler::unpack_and_forward_void(f, threadId, holder.getArgs(), &holder);
		}

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		_ret forward(F &&f, int threadId, Holder &holder)
		{
			holder.setForwardStatus();
			holder.threadId = threadId;
			return std::args_handler::unpack_and_forward_non_void(f, threadId, holder.getArgs(), &holder);
		}

		// no DLB ************************************************************************* end

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
				checkLeftSibling(&holder);

			forward<_ret>(f, threadId, holder);
		}

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
		// thread safe: root creation or root switching
		void assign_root(int threadId, void *root)
		{
			std::unique_lock<std::mutex> lck(root_mtx);
			roots[threadId] = root;
		}

		void init()
		{
			this->processor_count = std::thread::hardware_concurrency();
			this->busyThreads = 0;
			this->idleTime = 0;
			this->isDone = false;
			this->max_push_depth = -1;
			this->superFlag = false;
			this->idCounter = 0;
			this->requests = 0;

			//this->roots.resize(processor_count, nullptr);

			this->bestRstream.first = -1; // this allows to avoid sending empty buffers
		}

		int appliedStrategy = -1;
		//size_t idCounter;
		std::atomic<size_t> idCounter;
		std::atomic<size_t> requests;

		std::map<int, void *> roots; // every thread will be solving a sub tree, this point to their roots
		std::mutex root_mtx;

		/*This section refers to the strategy wrapping a function
			then pruning data to be use by the wrapped function<<---*/
		bool isDone;
		int max_push_depth;
		std::once_flag isDoneFlag;
		std::any bestR;
		std::pair<int, std::stringstream> bestRstream;
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
		std::atomic<int> busyThreads;
		std::mutex mtx; //local mutex
		std::condition_variable cv;
		std::atomic<bool> superFlag;
		ctpl::Pool thread_pool;

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
		std::atomic<int> CHECKER{0};

		std::mutex mtx_MPI; //local mutex

		/* MPI parameters */
		MPI_Mutex *mpi_mutex = nullptr;

		int world_rank = -1;	  // get the rank of the process
		int world_size = -1;	  // get the number of processes/nodes
		char processor_name[128]; // name of the node

		MPI_Win *win_phsRequest = nullptr;
		MPI_Win *win_AvNodes = nullptr;
		MPI_Win *win_termination = nullptr;
		MPI_Win *win_accumulator = nullptr;
		MPI_Win *win_resRequest = nullptr;
		MPI_Win *win_NumNodes = nullptr;
		MPI_Win *win_refValueGlobal = nullptr;

		MPI_Comm *world_Comm = nullptr;
		MPI_Comm *second_Comm = nullptr;
		MPI_Comm *NodeToNode_Comm = nullptr;

		int *numAvailableNodes = nullptr;
		bool request_response = false;

		void CHECK_MPI_MUTEX(int sum, int threadId)
		{
			this->CHECKER.fetch_add(sum, std::memory_order_relaxed);
			int tmp = CHECKER.load();
			if (tmp > 1 || tmp < 0)
				fmt::print("rank {}, threadID {}, mutex has failed : CHECKER = {}", world_rank, threadId, tmp);
		}

		void linkMPIargs(int world_rank,
						 int world_size,
						 char *processor_name,
						 int *numAvailableNodes,
						 int *refValueGlobal,
						 MPI_Win *win_accumulator,
						 MPI_Win *win_termination,
						 MPI_Win *win_AvNodes,
						 MPI_Win *win_phsRequest,
						 MPI_Win *win_resRequest,
						 MPI_Win *win_NumNodes,
						 MPI_Win *win_refValueGlobal,
						 MPI_Comm *world_Comm,
						 MPI_Comm *second_Comm,
						 MPI_Mutex *mpi_mutex)
		{
			this->world_rank = world_rank;
			this->world_size = world_size;
			strncpy(this->processor_name, processor_name, 128);
			this->numAvailableNodes = numAvailableNodes;
			this->refValueGlobal = refValueGlobal;
			this->win_accumulator = win_accumulator;
			this->win_termination = win_termination;
			this->win_AvNodes = win_AvNodes;
			this->win_phsRequest = win_phsRequest;
			this->win_resRequest = win_resRequest;
			this->win_NumNodes = win_NumNodes;
			this->win_refValueGlobal = win_refValueGlobal;
			this->world_Comm = world_Comm;
			this->second_Comm = second_Comm;

			// mpi mutex *********************
			this->mpi_mutex = mpi_mutex;
			// *******************************
		}

		/* if method receives data, this node is supposed to be totally idle */
		template <typename _ret, typename F, typename Serialize, typename Deserialize, typename Holder>
		void receiveSeed(F &&f, Serialize &&serialize, Deserialize &&deserialize, Holder &holder)
		{
			bool onceFlag = false;
			int count_rcv = 0;

			std::string msg = "avalaibleNodes[" + std::to_string(world_rank) + "]";
			int flag{1};

			int sum = 1;
			int nodes_at_center;
			fetch_and_op(&sum, &nodes_at_center, MPI_INT, 0, 0, MPI_SUM, *win_NumNodes);

			if (MPI_COMM_NULL != *second_Comm)
				MPI_Barrier(*second_Comm);

			while (true)
			{
				int rflag = put_mpi(&flag, 1, MPI_INT, 0, world_rank, *win_AvNodes);

#ifdef DEBUG_COMMENTS
				fmt::print("process {} put flag [{}] in process 0 \n", world_rank, flag);
#endif

				MPI_Status status;
				/* if a thread passes succesfully this method, library gets ready to receive data*/
#ifdef DEBUG_COMMENTS
				fmt::print("Receiver called on process {}, avl processes {} \n", world_rank, numAvailableNodes[0]);
				fmt::print("Receiver on {} ready to receive \n", world_rank);
#endif
				int Bytes; // Bytes to be received

				MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, *world_Comm, &status); // receives status before receiving the message
				MPI_Get_count(&status, MPI_CHAR, &Bytes);					  // receives total number of datatype elements of the message

				char *in_buffer = new char[Bytes];
				MPI_Recv(in_buffer, Bytes, MPI_CHAR, MPI_ANY_SOURCE, MPI_ANY_TAG, *world_Comm, &status);

				count_rcv++;
				int src = status.MPI_SOURCE;
#ifdef DEBUG_COMMENTS
				fmt::print("process {} has rcvd from {}, {} times \n", world_rank, src, count_rcv);
				fmt::print("Receiver on {}, received {} Bytes from {} \n", world_rank, Bytes, src);
#endif
				if (status.MPI_TAG == 3)
				{
#ifdef DEBUG_COMMENTS
					fmt::print("rank {} about to send best result to center \n", world_rank);
#endif
					sendBestResultToCenter();
					fmt::print("Exit tag received on process {} \n", world_rank); // loop termination
					break;
				}

				Holder newHolder(*this, -1); // copy types

				std::stringstream ss;
				for (int i = 0; i < Bytes; i++)
					ss << in_buffer[i];

				Utils::unpack_tuple(deserialize, ss, newHolder.getArgs());

				accumulate_mpi(1, 1, MPI_INT, 0, 0, *win_accumulator, "busyNodes++");

				delete[] in_buffer;

				if (!onceFlag)
				{
					if (MPI_COMM_NULL != *second_Comm)
						MPI_Barrier(*second_Comm);
					//onceFlag = true; //prevents to synchronise again if process #1 gets free, yet job is not finished
					onceFlag = true;
				}

				push_multithreading<_ret>(f, 0, newHolder); // first push, node is idle

				reply<_ret>(serialize, newHolder, src);
#ifdef DEBUG_COMMENTS
				fmt::print("Passed on process {} \n", world_rank);
#endif
				accumulate_mpi(-1, 1, MPI_INT, 0, 0, *win_accumulator, "busyNodes--");
			}
		}

		template <typename _ret, typename Holder, typename Serialize,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		void reply(Serialize &&serialize, Holder &holder, int src)
		{
			//thread_pool.wait();

			_ret res;
#ifdef DEBUG_COMMENTS
			fmt::print("rank {} entered reply! \n", world_rank);
#endif
			//holder.get(res);
			res = holder.get();

			if (src == 0) // termination, since all recursions return to center node
			{
#ifdef DEBUG_COMMENTS
				fmt::print("Cover size() : {}, sending to center \n", res.coverSize());
#endif

				std::unique_lock<std::mutex> lck(mtx_MPI); // in theory other threads should be are idle, TO DO ..
				//this sends a signal so center node turns into receiving mode
				bool buffer = true;
				put_mpi(&buffer, 1, MPI_CXX_BOOL, src, 0, *win_termination);
#ifdef DEBUG_COMMENTS
				fmt::print("rank {} put to termination! \n", world_rank);
#endif

				std::stringstream ss = serialize(res);
				int count = ss.str().size();

				int err = MPI_Ssend(ss.str().data(), count, MPI_CHAR, src, refValueGlobal[0], *world_Comm);
				if (err != MPI_SUCCESS)
					fmt::print("final result could not be sent from rank {} to rank {}! \n", world_rank, src);
			}
			else // some other node requested help and it is surely waiting for the result
			{

#ifdef DEBUG_COMMENTS
				fmt::print("rank {} about to reply to {}! \n", world_rank, src);
#endif
				std::unique_lock<std::mutex> lck(mtx_MPI); //no other thread can retrieve nor send via MPI

				std::stringstream ss = serialize(res);
				int count = ss.str().size();

				int err = MPI_Ssend(ss.str().data(), count, MPI_CHAR, src, 0, *world_Comm);
				if (err != MPI_SUCCESS)
					fmt::print("result could not be sent from rank {} to rank {}! \n", world_rank, src);
			}
		}

		template <typename _ret, typename Holder, typename Serialize,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
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
#ifdef DEBUG_COMMENTS
				fmt::print("rank {} did not catch a best result \n", world_rank);
#endif
				// if a solution was not found, processes will synchronise in here
				MPI_Barrier(*world_Comm); // this guarantees that center nodes gets aware of prior signals
				return;
			}

			//sending signal to center so this one turn into receiving best result mode
			int signal = true;
			put_mpi(&signal, 1, MPI_INT, 0, world_rank, *win_resRequest);
#ifdef DEBUG_COMMENTS
			fmt::print("rank {} put signal in inbox to retrieve a best result \n", world_rank);
#endif
			MPI_Barrier(*world_Comm); // this guarantees that center nodes gets aware of prior signals

			//char *buffer = bestRstream.second.str().data(); //This does not work, SEGFAULT
			int Bytes = bestRstream.second.str().size();
			int refVal = bestRstream.first;

			MPI_Ssend(bestRstream.second.str().data(), Bytes, MPI_CHAR, 0, refVal, *world_Comm);
			fmt::print("rank {} sent best result, Bytes : {}, refVal : {}\n", world_rank, Bytes, refVal);

			//reset bestRstream
			bestRstream.first = -1;		// reset condition to avoid sending empty buffers
			bestRstream.second.str(""); // clear stream, eventhough it's not necessary since this value is replaced when a better solution is found
		}

		void accumulate_mpi(int buffer, int origin_count, MPI_Datatype mpi_datatype, int target_rank, MPI_Aint offset, MPI_Win &window, std::string msg)
		{
#ifdef DEBUG_COMMENTS
			fmt::print("{} about to accumulate on {}\n", world_rank, msg.c_str());
#endif
			MPI_Win_lock(MPI_LOCK_EXCLUSIVE, target_rank, 0, window);

			MPI_Accumulate(&buffer, origin_count, mpi_datatype, target_rank, offset, 1, mpi_datatype, MPI_SUM, window);
#ifdef DEBUG_COMMENTS
			fmt::print("{} about to unlock RMA on {} \n", world_rank, target_rank);
#endif
			MPI_Win_flush(target_rank, window);
#ifdef DEBUG_COMMENTS
			fmt::print("{} between flush and unlock \n", world_rank);
#endif
			MPI_Win_unlock(target_rank, window);
#ifdef DEBUG_COMMENTS
			fmt::print("{}, by {}\n", msg.c_str(), world_rank);
#endif
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

#ifdef MPI_ENABLED

	int Scheduler::initMPI(int *argc, char *argv[])
	{
		// Initilialise MPI and ask for thread support
		int provided;
		MPI_Init_thread(argc, &argv, MPI_THREAD_SERIALIZED, &provided);
		if (provided < MPI_THREAD_SERIALIZED)
		{
			fmt::print("The threading support level is lesser than that demanded.\n");
			MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
		}
		else
		{
#ifdef DEBUG_COMMENTS
			fmt::print("The threading support level corresponds to that demanded.\n");
#endif
		}

		communicators();

		int namelen;
		MPI_Get_processor_name(processor_name, &namelen);
#ifdef DEBUG_COMMENTS
		fmt::print("Process {} of {} is on {}\n", world_rank, world_size, processor_name);
#endif
		MPI_Barrier(world_Comm);
		//fmt::print("About to create window, {} / {}!! \n", world_rank, world_size);
		MPI_Barrier(world_Comm);
		win_allocate();

		// initiliaze MPI mutex *********************************************************
		mpi_mutex.set(world_Comm, win_mutex);
		// ******************************************************************************

		MPI_Barrier(world_Comm);

		_branchHandler.linkMPIargs(world_rank,
								   world_size,
								   processor_name,
								   numAvailableNodes,
								   refValueGlobal,
								   &win_accumulator,
								   &win_termination,
								   &win_AvNodes,
								   &win_phsRequest,
								   &win_resRequest,
								   &win_NumNodes,
								   &win_refValueGlobal,
								   &world_Comm,
								   &second_Comm,
								   &mpi_mutex);

		return world_rank;
	}

	template <typename _ret, typename F, typename Holder, typename Serialize, typename Deserialize>
	void Scheduler::start(F &&f, Holder &holder, Serialize &&serialize, Deserialize &&deserialize)
	{
#ifdef DEBUG_COMMENTS
		fmt::print("About to start, {} / {}!! \n", world_rank, world_size);
#endif

		if (world_rank == 0)
		{
			start_time = MPI_Wtime();
#ifdef DEBUG_COMMENTS
			fmt::print("scheduler() launched!! \n");
#endif
			this->schedule(holder, serialize);
			end_time = MPI_Wtime();
		}
		else
		{
			_branchHandler.setMaxThreads(threadsPerNode);
			if (std::is_void<_ret>::value)
				_branchHandler.functionIsVoid();

			_branchHandler.receiveSeed<_ret>(f, serialize, deserialize, holder);
		}
#ifdef DEBUG_COMMENTS
		fmt::print("process {} waiting at barrier \n", world_rank);
#endif
		MPI_Barrier(world_Comm);
#ifdef DEBUG_COMMENTS
		fmt::print("process {} passed barrier \n", world_rank);
#endif
	}

#endif

} // namespace library

#endif
