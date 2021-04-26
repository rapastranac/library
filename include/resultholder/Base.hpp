#ifndef BASE_HPP
#define BASE_HPP

#include <mpi.h>
#include <stdio.h>

#include <omp.h>

#include <fmt/format.h>

#include <chrono>
#include <list>
#include <functional>
#include <future>
#include <tuple>
#include <type_traits>

namespace GemPBA
{
    class DLB_Handler;

    template <typename... Args>
    class Base
    {
        friend class DLB_Handler;

    protected:
        DLB_Handler &dlb;

        std::tuple<Args...> tup;
        std::function<bool()> branch_checkIn;
        bool isPushed = false;    // It was performed by another thread
        bool isForwarded = false; // It was performed sequentially
        bool isRetrieved = false;
        bool isDiscarded = false;
        size_t fw_count = 0;
        size_t ph_count = 0;

        long long int id = -1;
        int threadId = -1;

        int depth = -1;
        bool isVirtual = false;

#ifdef MPI_ENABLED
        // MPI attributes ******
        bool isMPISent = false; // flag to check if was sent via MPI
        int dest_rank = -1;     // rank destination

        // **********************
#endif

    public:
        Base(DLB_Handler &dlb) : dlb(dlb)
        {
        }

        void holdArgs(Args &...args)
        {
            this->tup = std::make_tuple(std::forward<Args &&>(args)...);
        }

        std::tuple<Args...> &getArgs()
        {
            return tup;
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

        bool isFetchable()
        {
#ifdef MPI_ENABLED
            //return (isPushed || isForwarded || isMPISent) && !isRetrieved;
            return false;
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
#ifdef MPI_ENABLED
        bool is_MPI_Sent()
        {
            return this->isMPISent;
        }

        void setMPISent(bool val = true)
        {
            this->isMPISent = val;
            //this->dest_rank = dest_rank;
        }

        void setMPISent(bool val, int dest_rank)
        {
            this->isMPISent = val;
            this->dest_rank = dest_rank;
        }
#endif
    };

    template <typename _Ret, typename Enable = void, typename... Args>
    class ResultHolderInt;
}

#endif