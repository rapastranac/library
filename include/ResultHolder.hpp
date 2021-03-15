#ifndef RESULTHOLDER_HPP
#define RESULTHOLDER_HPP

/*
* Created by Andres Pastrana on 2019
* pasr1602@usherbrooke.ca
* rapastranac@gmail.com
*/

#include <fmt/format.h>

#include <any>
#include <list>
#include <future>
#include <functional>
#include <memory>

namespace library
{
	class BranchHandler;

	template <typename _Ret, typename... Args>
	class ResultHolder
	{

		friend class BranchHandler;
		friend class Linker;

	protected:
		BranchHandler &branchHandler;

		std::future<_Ret> expectedFut; // Unique_ptr check it out
		std::any expected;			   // expected value

		std::tuple<Args...> tup;
		std::function<bool()> boundCond; // Condition prior to run the branch
		std::function<bool()> branch_checkIn;
		bool isBoundCond = false; // is there a condition before running this branch?
		bool isPushed = false;	  // It was performed by another thread
		bool isForwarded = false; // It was performed sequentially
		bool isRetrieved = false;
		bool isDiscarded = false;
		size_t fw_count = 0;
		size_t ph_count = 0;

		size_t id;
		int threadId = -1;

		void **root = nullptr;				// raw pointer
		ResultHolder *parent = nullptr;		// smart pointer
		ResultHolder *itself = nullptr;		// this;		// raw pointer
		std::list<ResultHolder *> children; // smart pointer, it keeps the order in which they were appended

		int depth = -1;
		bool isVirtual = false;

		template <class T>
		void hold_future(T &&expectedFut)
		{
			this->expectedFut = std::move(expectedFut);
		}

		//for void functions,
		//void hold_actual_result(std::args_handler::Void expected) {}

		template <class T>
		void hold_actual_result(T &&expected)
		{
			this->expected = std::move(expected);
		}

		void prune()
		{
			root = nullptr;
			parent = nullptr;
		}

		/* this changes the root of every descendant and ascendants nodes, however,
		ascendants should have already been pruned and/or used*/
		void lowerRoot()
		{
			auto root_cpy = static_cast<ResultHolder *>(*root); // cpy pointer to the current root
			if (root_cpy->isVirtual)
			{
				branchHandler.assign_root(threadId, this);
				parent = nullptr;

				// at this point nobody should be pointing to the prior root
				// this should be safe since every member (**root) is pointing to
				// the container cell instead of a cell within a VirtualRoot
				delete root_cpy;
			}
			else
			{
				branchHandler.assign_root(threadId, this);

				// **************************************************************
				// **************************************************************
				// **************************************************************
				// I believe this is duplication of something that has already happened
				// if a thread reach this point of a holder,
				// the holder should have already been assigned a root in branchHandler.roots[threadId]
				//then "this->root" is already pointing to branchHandler.roots[threadId]

				//this->root = &branchHandler.roots[threadId];
				// **************************************************************
				// **************************************************************
				// **************************************************************

				//*(this->root) = this; //this changes the root for every node pointing to it

				//this->root = &itself;
				parent = nullptr;
			}
		}

	public:
		// default constructor, it has no parent, used for virtual roots
		ResultHolder(library::BranchHandler &handler, int threadId) : branchHandler(handler)
		{
			this->threadId = threadId;
			this->id = branchHandler.getUniqueId();
			//this->expectedFut.reset(new std::future<_Ret>);
			this->itself = this;

			branchHandler.assign_root(threadId, this);
			this->root = &branchHandler.roots[threadId];

			this->isVirtual = true;
		}

		ResultHolder(library::BranchHandler &handler, int threadId, void *parent) : branchHandler(handler)
		{
			this->threadId = threadId;
			this->id = branchHandler.getUniqueId();
			//this->expectedFut.reset(new std::future<_Ret>);
			itself = this;

			if (parent)
			{
				this->root = &branchHandler.roots[threadId];
				this->parent = static_cast<ResultHolder *>(parent);
				this->parent->children.push_back(this);
			}
			else
			{
				// if there is no parent, it means the thread just took another subtree
				// therefore, root in handler.roots[threadId] should change since
				// no one else is supposed to be using it

				//branchHandler.roots[threadId] = this; //
				//this->root = &branchHandler.roots[threadId];
				return;
			}
		}

		~ResultHolder()
		{

			if (isVirtual)
				fmt::print("Destructor called for virtual root, id : {}, \t threadId :{}, \t depth : {} \n", id, threadId, depth);
			//else
			//	printf("Destructor called for  id : %d \n", id);
			//
		}
		ResultHolder(ResultHolder &&src) = delete;

		//ResultHolder(ResultHolder &&src) noexcept
		//{
		//	this->depth = src.depth;
		//	//Unique_ptr check it out
		//	this->expectedFut = std::move(src.expectedFut); //https://stackoverflow.com/questions/16030081/copy-constructor-for-a-class-with-unique-ptr
		//	this->isPushed = src.isPushed;
		//	this->expected = src.expected;
		//}

		void addChildren(std::shared_ptr<ResultHolder> &child)
		{
			children.push_back(child);
		}

		template <typename... Rest>
		void addChildren(std::shared_ptr<ResultHolder> &child, Rest &...rest)
		{
			children.push_back(child);
			addChildren(rest...);
		}

		void clearChildren()
		{
			children.clear();
		}

		void setDepth(int depth)
		{
			this->depth = depth;
		}

		size_t getId()
		{
			return id;
		}

		//First argument is the condition bound as a lambda function
		//Then all the arguments needed to perform the condition are passed either by value or reference
		/*If this method is invoked, operation "|| bound.isBound()" must be added to original condition, because
		by the time the thread passes by there, condition might not comply and then prospective result
		could not be fetched*/
		//https://stackoverflow.com/questions/4573952/template-class-c
		template <typename F, typename... Rest>
		void bindCondition(F &&f, Rest &&...rest)
		{
			this->boundCond = std::bind(std::forward<F>(f), std::forward<Rest &&>(rest)...);
			this->isBoundCond = true;
		}

		bool isBound()
		{
			return isBoundCond;
		}

		//14.7.3 Explicit specialization
		//template<typename...Args>
		void holdArgs(Args &...args)
		{
			this->tup = std::make_tuple(std::forward<Args &&>(args)...);
			//std::cout << typeid(tup).name() << "\n";
		}

		std::tuple<Args...> &getArgs()
		{
			return this->tup;
		}

		// if _Ret is void, then this should not be invoked
		_Ret get()
		{
			if (isPushed)
			{
				auto begin = std::chrono::steady_clock::now();
				this->expected = expectedFut.get();
				auto end = std::chrono::steady_clock::now();
				/*If a thread comes in this scope, then it is clear that numThread
					must be decremented in one, also it should be locked before another thread
					changes it, since it is atomic, this operation is already well defined*/

				branchHandler.sumUpIdleTime(begin, end);
				branchHandler.decrementBusyThreads(); // this is reduced from ThreadPool when the callable type is VOID
			}
			/*	This condition is relevant due to some functions might return empty values
				which are not stored in std::any types	*/
			if (this->expected.has_value())
			{
				this->isRetrieved = true;
				return std::any_cast<_Ret>(expected);
			}
			else
			{
				this->isRetrieved = true;
				return {}; // returns empty object of type _Ret,
			}
		}

		bool isFetchable()
		{
#ifdef MPI_ENABLED
			return (isPushed || isForwarded || isMPISent) && !isRetrieved;
#else
			return (isPushed || isForwarded) && !isRetrieved;
#endif
		}

		bool is_forwarded()
		{
			return isPushed || isForwarded;
		}

		bool is_pushed()
		{
			return isPushed;
		}

		bool is_discarded()
		{
			return isDiscarded;
		}

		void setForwardStatus(bool val = true)
		{
			this->isForwarded = val;
			this->fw_count++;
		}

		void setPushStatus(bool val = true)
		{
			this->ph_count++;
			this->isPushed = val;
		}

		void setDiscard(bool val = true)
		{
			this->isDiscarded = val;
		}

		void bind_branch_checkIn(auto &&branch_checkIn)
		{
			this->branch_checkIn = std::move(branch_checkIn);
		}

		/* this should be invoked always before calling a branch, since 
			it invokes user's instructions to prepare data that will be pushed
			If not invoked, input for a specific branch handled by ResultHolder instance
			will be empty.
			
			This method allows to always have input data ready before a branch call, avoiding to
			have data in the stack before it is actually needed.

			Thus, the user can evaluate any condition to check if a branch call is worth it or 
			not, while creating a temporarily a input data set. 
			
			If user's condition is met then this temporarily input is held by the ResultHolder::holdArgs(...)
			and it should return true
			If user's condition is not met, no input is held and it should return false

			if a void function is being used, this should be a problem, since 

			*/
		bool evaluate_branch_checkIn()
		{
			if (isForwarded || isPushed || isDiscarded)
				return false;
			else
				return branch_checkIn();
		}

#ifdef MPI_ENABLED

		template <typename F_deser>
		_Ret get(F_deser &&f_deser)
		{
			if (isPushed)
			{
				std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
				expected = expectedFut.get();
				std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
				/*If a thread comes in this scope, then it is clear that numThread
					must be decremented in one, also it should be locked before another thread
					changes it, since it is atomic, this operation is already well defined*/

				branchHandler.sumUpIdleTime(begin, end);
				branchHandler.decrementBusyThreads(); // this is reduced from ThreadPool when the callable type is VOID
			}
			else if (isMPISent)
			{
				branchHandler.lock_mpi(); /* this blocks any other thread to use an MPI function since MPI_Recv is blocking
												thus, mpi_thread_serialized is guaranteed */
#ifdef DEBUG_COMMENTS
				//printf("rank %d entered get() to retrieve from %d! \n", branchHandler.world_rank, dest_rank);
				fmt::print("rank {} entered get() to retrieve from {}! \n", branchHandler.world_rank, dest_rank);
#endif

				MPI_Status status;
				int Bytes;

				MPI_Probe(dest_rank, MPI::ANY_TAG, branchHandler.getCommunicator(), &status); // receives status before receiving the message
				MPI_Get_count(&status, MPI::CHAR, &Bytes);									  // receives total number of datatype elements of the message

				char *in_buffer = new char[Bytes];
				MPI_Recv(in_buffer, Bytes, MPI::CHAR, dest_rank, MPI::ANY_TAG, branchHandler.getCommunicator(), &status);

#ifdef DEBUG_COMMENTS
				//printf("rank %d received %d Bytes from %d! \n", branchHandler.world_rank, Bytes, dest_rank);
				fmt::print("rank {} received {} Bytes from {}! \n", branchHandler.world_rank, Bytes, dest_rank);
#endif

				std::stringstream ss;
				for (int i = 0; i < Bytes; i++)
					ss << in_buffer[i];

				_Ret temp;
				f_deser(ss, temp);
				delete[] in_buffer;

				this->isRetrieved = true;

				branchHandler.unlock_mpi(); /* release mpi mutex, thus, other threads are able to push to other nodes*/
				return temp;
			}
			/*	This condition is relevant due to some functions might return empty values
				which are not stored in std::any types	*/
			if (this->expected.has_value())
			{
				this->isRetrieved = true;
				return std::any_cast<_Ret>(expected);
			}
			else
			{
				//throw "This should not happen\n";
				this->isRetrieved = true;
				return {}; // returns empty object of type _Ret,
			}
		}

		bool is_MPI_Sent()
		{
			return isMPISent;
		}

		void setMPISent(bool val, int dest_rank)
		{
			this->isMPISent = val;
			this->dest_rank = dest_rank;
		}

	protected:
		// MPI attributes ******
		bool isMPISent = false; // flag to check if was sent via MPI
		int dest_rank = -1;		// rank destination

		// **********************

#endif
	};

} // namespace library
#endif
