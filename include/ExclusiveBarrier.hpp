#ifndef EXCLUSIVEBARRIER_HPP
#define EXCLUSIVEBARRIER_HPP

//#include "boost/fiber/barrier.hpp"
#include "barrier.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <condition_variable>
#include <thread>

class ExclusiveBarrier
{
public:
	ExclusiveBarrier()
	{
		//bar.reset(new boost::fibers::barrier(2));
		bar.reset(new Barrier(2));
	}

	void wait_donor()
	{
		std::unique_lock<std::mutex> lck(mtx);
		bar->wait();
	}

	void wait_master()
	{
		bar->wait();
	}

private:
	std::mutex mtx;
	//std::unique_ptr< boost::fibers::barrier> bar;
	std::unique_ptr<Barrier> bar;
};

#endif
