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


	protected:

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
				BranchHandler::getInstance().assign_root(threadId, this);
				parent = nullptr;

				// at this point nobody should be pointing to the prior root
				// this should be safe since every member (**root) is pointing to
				// the container cell instead of a cell within a VirtualRoot
				delete root_cpy;
			}
			else
			{
				BranchHandler::getInstance().assign_root(threadId, this);

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
	
		ResultHolder() : ResultHolder(-1)
		{
		
		}
		// default constructor, it has no parent, used for virtual roots
		ResultHolder(int threadId)
		{
			this->threadId = threadId;
			//this->id = BranchHandler::getInstance().getUniqueId();
			//this->expectedFut.reset(new std::future<_Ret>);
			this->itself = this;

			BranchHandler::getInstance().assign_root(threadId, this);
			this->root = &BranchHandler::getInstance().roots[threadId];

			this->isVirtual = true;
		}

		ResultHolder(int threadId, void *parent)
		{
			this->threadId = threadId;
			//this->id = BranchHandler::getInstance().getUniqueId();
			//this->expectedFut.reset(new std::future<_Ret>);
			itself = this;

			if (parent)
			{
				this->root = &BranchHandler::getInstance().roots[threadId];
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

			//if (isVirtual)
			//	fmt::print("Destructor called for virtual root, id : {}, \t threadId :{}, \t depth : {} \n", id, threadId, depth);
			//else
			//	printf("Destructor called for  id : %d \n", id);
			//
		}
		//ResultHolder(ResultHolder &&src) = delete;
		//ResultHolder(ResultHolder &src) = delete;

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

		auto getThreadId()
		{
			return threadId;
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
		void holdArgs(Args& ...args)
		{
			this->tup = std::make_tuple(std::forward<Args &&>(args)...);
			//std::cout << typeid(tup).name() << "\n";
		}

		std::tuple<Args...> &getArgs()
		{
			return tup;
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

				BranchHandler::getInstance().sumUpIdleTime(begin, end);
				//BranchHandler::getInstance().decrementBusyThreads(); // this is reduced from ThreadPool when the callable type is VOID
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

		template <typename F>
		void bind_branch_checkIn(F &&branch_checkIn)
		{
			this->branch_checkIn = std::bind(std::forward<F>(branch_checkIn));
			size_t size = sizeof(this->branch_checkIn);
			return;
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


	};

} // namespace library
#endif
