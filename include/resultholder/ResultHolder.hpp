#ifndef RESULTHOLDER_HPP
#define RESULTHOLDER_HPP

/*
* Created by Andres Pastrana on 2019
* pasr1602@usherbrooke.ca
* rapastranac@gmail.com
*/

#include "VoidIntermediate.hpp"
#include "NonVoidIntermediate.hpp"

namespace GemPBA
{

	template <typename _Ret, typename... Args>
	class ResultHolder : public ResultHolderInt<_Ret, void, Args...>
	{
		friend class DLB_Handler;

	private:
		void **root = nullptr;				// raw pointer
		ResultHolder *parent = nullptr;		// smart pointer
		ResultHolder *itself = nullptr;		// this;		// raw pointer
		std::list<ResultHolder *> children; // smart pointer, it keeps the order in which they were appended

		//void prune()
		//{
		//	root = nullptr;
		//	parent = nullptr;
		//}

		/* this changes the root of every descendant and ascendants nodes, however,
		ascendants should have already been pruned and/or used*/
		//void lowerRoot()
		//{
		//	auto root_cpy = static_cast<ResultHolder *>(*root); // cpy pointer to the current root
		//	if (root_cpy->isVirtual)
		//	{
		//		this->branchHandler.assign_root(this->threadId, this);
		//		parent = nullptr;
		//
		//		// at this point nobody should be pointing to the prior root
		//		// this should be safe since every member (**root) is pointing to
		//		// the container cell instead of a cell within a VirtualRoot
		//		delete root_cpy;
		//	}
		//	else
		//	{
		//		this->branchHandler.assign_root(this->threadId, this);
		//
		//		// **************************************************************
		//		// **************************************************************
		//		// **************************************************************
		//		// I believe this is duplication of something that has already happened
		//		// if a thread reach this point of a holder,
		//		// the holder should have already been assigned a root in branchHandler.roots[threadId]
		//		//then "this->root" is already pointing to branchHandler.roots[threadId]
		//
		//		//this->root = &branchHandler.roots[threadId];
		//		// **************************************************************
		//		// **************************************************************
		//		// **************************************************************
		//
		//		//*(this->root) = this; //this changes the root for every node pointing to it
		//
		//		//this->root = &itself;
		//		parent = nullptr;
		//	}
		//}

	public:
		// default constructor, it has no parent, used for virtual roots
		ResultHolder(DLB_Handler &dlb, int threadId) : ResultHolderInt<_Ret, void, Args...>(dlb), Base<Args...>(dlb)
		{
			this->threadId = threadId;
			this->id = dlb.getUniqueId();
			//this->expectedFut.reset(new std::future<_Ret>);
			this->itself = this;

			this->dlb.assign_root(threadId, this);
			this->root = &dlb.roots[threadId];

			this->isVirtual = true;
		}

		ResultHolder(DLB_Handler &dlb, int threadId, void *parent) : ResultHolderInt<_Ret, void, Args...>(dlb), Base<Args...>(dlb)
		{
			this->threadId = threadId;
			this->id = this->dlb.getUniqueId();
			//this->expectedFut.reset(new std::future<_Ret>);
			itself = this;

			if (parent)
			{
				this->root = &dlb.roots[threadId];
				this->parent = static_cast<ResultHolder *>(parent);
				this->parent->children.push_back(this);
			}
			else
			{
				// if there is no parent, it means the thread just took another subtree
				// therefore, root in handler.roots[threadId] should change since
				// no one else is supposed to be using it

				//branchHandler.roots[threadId] = this;
				this->dlb.assign_root(threadId, this);
				this->root = &dlb.roots[threadId];
				return;
			}
		}

		~ResultHolder()
		{
//#ifdef DEBUG_COMMENTS
//			if (this->isVirtual)
//				fmt::print("Destructor called for virtual root, id : {}, \t threadId :{}, \t depth : {} \n", this->id, this->threadId, this->depth);
////else
////	fmt::print("Destructor called for  id : {} \n", this->id);
//#endif
		}

		ResultHolder(ResultHolder &&src) = delete;
		ResultHolder(ResultHolder &src) = delete;
	};
} // namespace library
#endif
