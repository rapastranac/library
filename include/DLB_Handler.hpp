#ifndef DLB_HANDLER_HPP
#define DLB_HANDLER_HPP

#include <map>
#include <mutex>

namespace GemPBA
{
    template <typename _Ret, typename... Args>
    class ResultHolder;
    // Dynamic Load Balancing
    class DLB_Handler
    {
        template <typename _Ret, typename... Args>
        friend class ResultHolder;

    private:
        std::map<int, void *> roots; // every thread will be solving a sub tree, this point to their roots
        std::mutex mtx;

        size_t idCounter = 0;

        DLB_Handler() {}

    public:
        static DLB_Handler &getInstance()
        {
            static DLB_Handler instance;
            return instance;
        }

        size_t getUniqueId()
        {
            std::unique_lock<std::mutex> lck(mtx);
            ++idCounter;
            return idCounter;
        }

        // thread safe: root creation or root switching
        void assign_root(int threadId, void *root)
        {
            std::unique_lock<std::mutex> lck(mtx);
            roots[threadId] = root;
        }

        template <typename Holder>
        Holder *checkParent(Holder *holder)
        {

            Holder *leftMost = nullptr; // this is the branch that led us to the root
            Holder *root = nullptr;     // local pointer to root, to avoid "*" use

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
                //if (holder->isBound())
                //{
                //	bool isWorthPushing = holder->boundCond();
                //	if (isWorthPushing)
                //		return holder;
                //	else
                //		return nullptr;
                //}
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
                auto secondHolder = *second;                        // catches the pointer of the node	<-------------------------
                root->children.erase(second);                       // removes second node from the root's children

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

                root->children.pop_front();             // deletes leftMost from root's children
                Holder *right = root->children.front(); // The one to be pushed
                root->children.clear();                 // ..

                //right->prune();                         // just in case, right branch is not being sent anyway, only its data
                this->prune(right);

                this->lowerRoot(*leftMost);
                //leftMost->lowerRoot(); // it sets leftMost as the new root

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
                    {                       /*
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
                            holder->parent->children.pop_front();        // removes pb from the parent's children
                            leftMost = holder->parent->children.front(); // it gets the second element from the parent's children
                        }
                        // after this line,this should be true leftMost == holder

                        // There might be more than one remaining sibling
                        if (holder->parent->children.size() > 1)
                            return; // root does not change

                        /* if holder is the only remaining child from parent then this means
						that it will have to become a new root*/

                        //holder->lowerRoot();
                        this->lowerRoot(*holder);

                        //leftMost_cpy->prune();
                        this->prune(leftMost_cpy);
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
            /* this method is invoked when DLB_Handler is enable and the method checkParent() was not able to find
		a top branch to push, because it means the next right sibling will become a root(for binary recursion)
		or just the leftMost will be unlisted from the parent's children. This method is invoked if and only if 
		an available worker is available*/
            auto *_parent = holder->parent;
            if (_parent)                          // it should always comply, virtual parent is being created
            {                                     // it also confirms that holder is not a parent (applies for DLB_Handler)
                if (_parent->children.size() > 2) // this is for more than two recursions per scope
                {
                    //TODO ..
                }
                else if (_parent->children.size() == 2) // this verifies that  it's binary and the rightMost will become a new root
                {
                    _parent->children.pop_front();
                    auto right = _parent->children.front();
                    _parent->children.pop_front();
                    //right->lowerRoot();
                    this->lowerRoot(*right);
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

        template <typename Holder, typename... Args>
        void linkParent(int threadId, void *parent, Holder &child, Args &...args)
        {

            if (!parent) // this should only happen when parent was nullptr at children's construction time
            {
                Holder *virtualRoot = new Holder(*this, threadId);
                virtualRoot->setDepth(child.depth);

                child.parent = static_cast<Holder *>(roots[threadId]);
                {
                    std::unique_lock<std::mutex> lck(mtx);
                    child.root = &roots[threadId];
                }
                virtualRoot->children.push_back(&child);
                helper(virtualRoot, args...);
            }
        }

        template <typename Holder>
        void prune(Holder *holder)
        {
            //holder.prune();
            holder->root = nullptr;
            holder->parent = nullptr;
        }
        template <typename Holder>
        void lowerRoot(Holder &holder)
        {
            auto root_cpy = static_cast<Holder *>(*holder.root); // cpy pointer to the current root
            if (root_cpy->isVirtual)
            {
                this->assign_root(holder.threadId, &holder);
                holder.parent = nullptr;

                // at this point nobody should be pointing to the prior root
                // this should be safe since every member (**root) is pointing to
                // the container cell instead of a cell within a VirtualRoot
                delete root_cpy;
            }
            else
            {
                this->assign_root(holder.threadId, &holder);

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
                holder.parent = nullptr;
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
                //_root->lowerRoot();
                this->lowerRoot(*_root);
            }
        }

        ~DLB_Handler() = default;
        DLB_Handler(const DLB_Handler &) = delete;
        DLB_Handler(DLB_Handler &&) = delete;
        DLB_Handler &operator=(const DLB_Handler &) = delete;
        DLB_Handler &operator=(DLB_Handler &&) = delete;
    };
}
#endif