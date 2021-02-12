#ifndef MPI_MUTEX_HPP
#define MPI_MUTEX_HPP

#include <mpi.h>

class MPI_Mutex
{
public:
    MPI_Mutex()
    {
    }
    MPI_Mutex(MPI_Comm &world_comm, MPI_Win &window)
    {
        this->world_comm = &world_comm;
        this->window = &window;
    }
    ~MPI_Mutex() {}

    void set(MPI_Comm &world_comm, MPI_Win &window)
    {
        this->world_comm = &world_comm;
        this->window = &window;
    }

    // start a critical section
    void lock(int rank = -1)
    {
        bool buffer = true;
        bool compare = false;
        bool result; // holds original value at target

        if (!world_comm || !window || !mutex)
            throw "communicator and/or window and/or mutex not initiliazed";
        do
        {
            MPI_Win_lock(MPI::LOCK_EXCLUSIVE, 0, 0, *window);
            MPI_Compare_and_swap(&buffer, &compare, &result, MPI::BOOL, 0, 0, *window);
            MPI_Win_flush(0, *window);
            MPI_Win_unlock(0, *window);
        } while (result == 1);
#ifdef DEBUG_COMMENTS
        printf("MPI MUTEX ACQUIRED by rank %d \n", rank);
#endif
    }

    // end critical section
    void unlock(int rank = -1)
    {
        bool buffer = false;
        bool compare = true;
        bool result; // holds original value at target

        if (!world_comm || !window || !mutex)
            throw "communicator and/or window and/or mutex not initiliazed";

        MPI_Win_lock(MPI::LOCK_EXCLUSIVE, 0, 0, *window);
        MPI_Compare_and_swap(&buffer, &compare, &result, MPI::BOOL, 0, 0, *window);
        MPI_Win_flush(0, *window);
        MPI_Win_unlock(0, *window);
#ifdef DEBUG_COMMENTS
        printf("MPI MUTEX RELEASED by rank %d \n", rank);
#endif
    }

    // Upon allocation [size = 1], this value should be initialized to false
    bool *mutex = nullptr; // It is strongly recommended to allocate using MPI_Win_allocate

private:
    MPI_Comm *world_comm = nullptr;
    MPI_Win *window = nullptr;
};

#endif