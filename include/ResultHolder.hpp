#ifndef RESULTHOLDER_HPP
#define RESULTHOLDER_HPP

/*
* Created by Andres Pastrana on 2019
* pasr1602@usherbrooke.ca
* rapastranac@gmail.com
*/

#include <list>

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

		std::unique_ptr<std::future<_Ret>> expectedFut; // Unique_ptr check it out
		std::any expected;								// expected value

		std::tuple<Args...> tup;
		std::function<bool()> boundCond; // Condition prior to run the branch
		bool isBoundCond = false;		 // is there a condition before running this branch?
		bool isPushed = false;			 // It was performed by another thread
		bool isForwarded = false;		 // It was performed sequentially

		size_t id;
		size_t threadId = -1;

		void **root = nullptr;				// raw pointer
		ResultHolder *parent = nullptr;		// smart pointer
		ResultHolder *itself = nullptr;		// this;		// raw pointer
		std::list<ResultHolder *> children; // smart pointer, it keeps the order in which they were appended

		int depth;
		bool isVirtual = false;

		//for future<void> when pushing void functions to the pool
		//template <class T,
		//		  typename std::enable_if<std::is_same<T, std::future<void>>::value>::type * = nullptr>
		//void hold_future(T &&expectedFut) {}

		template <class T>
		//		  typename std::enable_if<!std::is_same<T, std::future<void>>::value>::type * = nullptr>
		void hold_future(T &&expectedFut)
		{
			*(this->expectedFut) = std::move(expectedFut);
		}

		//for void functions,
		//void hold_actual_result(std::args_handler::Void expected) {}

		template <class T>
		void hold_actual_result(T &&expected)
		{
			this->expected = std::move(expected);
		}

		//void unlink_parents()
		//{
		//	this->root = &itself;
		//	this->children.clear();
		//}
		/* this applies when this is the first instance of a new subtree 
			before pushing it, it is equivalent to creating a new independent
			instance, this does not affect any of the parent nodes*/
		//void prune()
		//{
		//	root_smrt.reset(new std::shared_ptr<ResultHolder>(itself)); // it becomes its own root
		//	parent_smrt = nullptr;										// since it is a root, then parent is not needed
		//
		//	if (!children_smrt.empty())
		//		throw "This should not be happening";
		//	//children_smrt.clear(); // this should be empty before this
		//}

		void prune()
		{
			root = nullptr;
			//root = &itself;
			root = nullptr;
			parent = nullptr;
		}

		/* this changes the root of every descendant and ascendants nodes, however,
		ascendants should have already been pruned and/or used*/
		void lowerRoot()
		{
			//auto root_cpy = *root; // cpy pointer to the current root
			//if (root_cpy->isVirtual)
			//{
			//	*(this->root) = this; //this changes the root for every node pointing to it
			//	//this->root = &itself;
			//	parent = nullptr;
			//
			//	// at this point nobody should be pointing to the prior root
			//	delete root_cpy;
			//}
			//else
			//{

			branchHandler.roots[threadId] = this;		 // this changes the root
			this->root = &branchHandler.roots[threadId]; // this points to that root

			//*(this->root) = this; //this changes the root for every node pointing to it

			//this->root = &itself;
			parent = nullptr;
			//}
		}

	public:
		// default constructor, it has no parent, used for virtual roots
		ResultHolder(library::BranchHandler &handler, int threadId) : branchHandler(handler)
		{
			this->threadId = threadId;
			this->id = branchHandler.getUniqueId();
			this->depth = 0;
			this->expectedFut.reset(new std::future<_Ret>);

			branchHandler.roots[threadId] = this;
			this->root = &branchHandler.roots[threadId];

			this->itself = this;
			//this->root = &itself;
			this->isVirtual = true;
		}

		ResultHolder(library::BranchHandler &handler, int threadId, ResultHolder *parent) : branchHandler(handler)
		{
			this->id = branchHandler.getUniqueId();
			this->isPushed = false;
			this->depth = -1;
			this->expectedFut.reset(new std::future<_Ret>);

			itself = this;

			if (!parent)
			{
				// if there is no parent, it means the thread just took another subtree
				// thefore, root in handler.roots[threadId] should change since
				// no one else is supposed to be using it

				//branchHandler.roots[threadId] = this; //
				//this->root = &branchHandler.roots[threadId];
				return;
			}

			this->root = &branchHandler.roots[threadId];

			this->parent = parent;
			//this->root = parent->root;
			this->parent->children.push_back(this);
		}

		//~ResultHolder()
		//{
		/*To ensure that if this holder dies, then it should dissappear from
				children's parent to avoid exceptions*/
		//if (!children.empty())
		//{
		//	typename std::list<ResultHolder *>::iterator it = children.begin();
		//	while (it != children.end())
		//	{
		//		(*it)->parent = nullptr;
		//		it++;
		//	}
		//}
		//}

		~ResultHolder()
		{

			//if (isVirtual)
			//	printf("Destructor called for virtual root, id : %d, depth : %d \n", id, depth);
			//else
			//	printf("Destructor called for  id : %d \n", id);
			//
		}

		//For multiple recursion algorithms
		//ResultHolder(library::BranchHandler &handler, std::shared_ptr<ResultHolder> parent) : branchHandler(handler)
		//{
		//	this->id = branchHandler.getUniqueId();
		//	this->isPushed = false;
		//	this->depth = -1;
		//	this->expectedFut.reset(new std::future<_Ret>);
		//
		//
		//	if (!parent)
		//	{
		//		this->root = &itself;
		//		return;
		//	}
		//
		//	this->root = &(*parent->root);
		//	this->parent = parent;
		//	//this->parent->children.push_back(this);
		//}

		ResultHolder(ResultHolder &&src) noexcept
		{
			this->depth = src.depth;
			//Unique_ptr check it out
			this->expectedFut = std::move(src.expectedFut); //https://stackoverflow.com/questions/16030081/copy-constructor-for-a-class-with-unique-ptr
			this->isPushed = src.isPushed;
			this->expected = src.expected;
		}

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
				this->expected = expectedFut->get();
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
				//target = std::any_cast<TYPE>(expected);
				//return true;
				return std::any_cast<_Ret>(expected);
			}
			else
				return {}; // returns empty object of type _Ret,

			//return false;
			//return expected;
		}

		bool is_forwarded()
		{
			return isPushed || isForwarded;
		}

		bool is_pushed()
		{
			return isPushed;
		}

		void setForwardStatus(bool val)
		{
			this->isForwarded = val;
		}

		void setPushStatus(bool val)
		{
			this->isPushed = val;
		}

#ifdef MPI_ENABLED

		template <typename TYPE, typename F_deser>
		bool get(TYPE &target, F_deser &&f_deser)
		{
			if (isPushed)
			{
				std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
				expected = expectedFut->get();
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
				printf("rank %d entered get() to retrieve from %d! \n", branchHandler.world_rank, dest_rank);

				MPI_Status status;
				int Bytes;

				MPI_Probe(dest_rank, MPI::ANY_TAG, branchHandler.getCommunicator(), &status); // receives status before receiving the message
				MPI_Get_count(&status, MPI::CHAR, &Bytes);									  // receives total number of datatype elements of the message

				char *in_buffer = new char[Bytes];
				MPI_Recv(in_buffer, Bytes, MPI::CHAR, dest_rank, MPI::ANY_TAG, branchHandler.getCommunicator(), &status);

				std::stringstream ss;
				for (int i = 0; i < Bytes; i++)
					ss << in_buffer[i];

				f_deser(ss, target);
				delete[] in_buffer;

				branchHandler.unlock_mpi(); /* release mpi mutex, thus, other threads are able to push to other nodes*/
				return true;
			}
			/*	This condition is relevant due to some functions might return empty values
				which are not stored in std::any types	*/
			if (expected.has_value())
			{
				target = std::any_cast<TYPE>(expected);
				return true;
			}

			return false;
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
