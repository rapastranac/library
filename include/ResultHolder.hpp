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

	protected:
		BranchHandler &branchHandler; // = library::BranchHandler();

		std::unique_ptr<std::future<_Ret>> expectedFut; //Unique_ptr check it out
		std::any expected;								//expected value

		std::tuple<Args...> tup;
		std::function<bool()> boundCond; //Condition prior to run the branch
		bool isBoundCond = false;		 //is there a condition before running this branch?

		bool isPushed = false;	  //It was performed by another thread
		bool isForwarded = false; //It was performed sequentially
		bool isMPISent = false;

		size_t id;
		size_t threadId = 0;

		ResultHolder **root = nullptr;
		ResultHolder *parent = nullptr;
		ResultHolder *itself = this;
		std::list<ResultHolder *> children; // it keeps the order in which they were appended

		int depth;

		//for future<void> when pushing void functions to the pool
		template <class T,
				  typename std::enable_if<std::is_same<T, std::future<void>>::value>::type * = nullptr>
		void hold_future(T &&expectedFut) {}

		template <class T,
				  typename std::enable_if<!std::is_same<T, std::future<void>>::value>::type * = nullptr>
		void hold_future(T &&expectedFut)
		{
			*(this->expectedFut) = std::move(expectedFut);
		}

		//for void functions,
		void hold_actual_result(std::args_handler::Void expected) {}

		template <class T>
		void hold_actual_result(T &&expected)
		{
			this->expected = std::move(expected);
		}

	public:
		ResultHolder(library::BranchHandler &handler) : branchHandler(handler)
		{
			this->id = branchHandler.getUniqueId();
			this->depth = 0;
			this->expectedFut.reset(new std::future<_Ret>);
		}
		~ResultHolder()
		{
			/*To ensure that if this holder dies, then it should dissappear from
				children's parent to avoid exceptions*/
			if (!children.empty())
			{
				typename std::list<ResultHolder *>::iterator it = children.begin();

				while (it != children.end())
				{
					(*it)->parent = nullptr;
					it++;
				}
			}
		}

		//For multiple recursion algorithms
		ResultHolder(library::BranchHandler &handler, ResultHolder *parent) : branchHandler(handler)
		{
			this->id = branchHandler.getUniqueId();
			this->isPushed = false;
			this->depth = NULL; //TODO... check
			this->expectedFut.reset(new std::future<_Ret>);

			if (!parent)
			{
				this->root = &itself;
				return;
			}
			else
			{
				this->root = &(*parent->root);
			}

			this->parent = parent;
			this->parent->children.push_back(this);
		}

		ResultHolder(ResultHolder &&src) noexcept
		{
			this->depth = src.depth;
			//Unique_ptr check it out
			this->expectedFut = std::move(src.expectedFut); //https://stackoverflow.com/questions/16030081/copy-constructor-for-a-class-with-unique-ptr
			this->isPushed = src.isPushed;
			this->expected = src.expected;
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
		void bindCondition(F &&f, Rest &&... rest)
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
		void holdArgs(Args &... args)
		{
			this->tup = std::make_tuple(std::forward<Args &&>(args)...);
			//std::cout << typeid(tup).name() << "\n";
		}

		std::tuple<Args...> &getArgs()
		{
			return this->tup;
		}

		template <typename TYPE>
		bool get(TYPE &target)
		{
			if (branchHandler.appliedStrategy != 1) //TODO...  check
			{

				if (isPushed)
				{
					std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
					expected = expectedFut->get();
					std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
					/*If a thread comes in this scope, then it is clear that numThread
					must be reduced in one, also it should be locked before another thread
					changes it, since it is atomic this operation is already well defined*/

					branchHandler.sumUpIdleTime(begin, end);
					/*This violates encapsulation? is it a good practice?
					it is possible due to class friendship*/
					--branchHandler.busyThreads; // this is reduced from ThreadPool when the callable type is VOID
				}
				else if (isMPISent)
				{
					MPI_Status status;
					int Bytes;
					MPI_Recv(&Bytes, 1, MPI::INTEGER, MPI::ANY_SOURCE, MPI::ANY_TAG, *branchHandler.second_Comm, &status);
					serializer::stream is;
					serializer::iarchive ia(is);
					is.allocate(Bytes);
					MPI_Recv(&is[0], Bytes, MPI::CHARACTER, MPI::ANY_SOURCE, MPI::ANY_TAG, *branchHandler.second_Comm, &status);
					ia >> target;
				}
				/*	This condition is relevant due to some functions might return empty values
				which are not stored in variables of type std::any	*/
				if (expected.has_value())
				{
					target = std::any_cast<TYPE>(expected);
					return true;
				}
			}

			return false;
		}

		bool is_forwarded()
		{
			return isPushed || isForwarded;
		}

		bool is_pushed()
		{
			return isPushed;
		}

		bool is_MPI_Sent()
		{
			return isMPISent;
		}

		void setForwardStatus(bool val)
		{
			this->isForwarded = val;
		}

		void setPushStatus(bool val)
		{
			this->isPushed = val;
		}

		void setMPISent(bool val)
		{
			this->isMPISent = val;
		}
	};

} // namespace library
#endif
