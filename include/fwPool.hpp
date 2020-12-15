#ifndef FWPOOL_HPP
#define FWPOOL_HPP

#include "args_handler.hpp"
#include "POOL.hpp"

#include <tuple>

namespace fwPool
{

	template <typename T>
	class Queue : public virtual POOL::detail::Queue<T>
	{
	};

	template <typename _Ret, typename... Args>
	class Pool : public virtual POOL::Pool
	{
	public:
		Pool() { this->init(); }
		Pool(int numThreads)
		{
			this->init();
			this->setSize(numThreads);
			this->linkedPool = nullptr;
		}
		// the destructor waits for all the functions in the queue to be finished
		~Pool() { this->interrupt(true); }

		// get the number of running threads in the pool
		//int size() { return static_cast<int>(this->threads.size()); }

		// empty the queue
		void clear_queue()
		{
			std::function<void(int id)> *_f;
			while (this->q.pop(_f))
				delete _f; // empty the queue
		}

		void returnElement(std::vector<size_t> elt, size_t id)
		{

			holder.insert(std::pair<size_t, std::vector<size_t>>(id, elt));
		}

		//std::vector<size_t> getResult(int id) {
		//
		//	if (holder.contains(id))
		//	{
		//		std::unique_lock<std::mutex> lck(mtx);
		//		std::vector<size_t> tmp = holder[id];
		//		holder.erase(id);
		//		return tmp;
		//	}
		//}

		bool jobDone()
		{
			return isResultReady;
		}

		template <typename F>
		auto push(F &&f, Args &&... args) -> std::future<_Ret>
		{
			using namespace std::placeholders;
			auto pck = std::make_shared<std::packaged_task<_Ret(int)>>(
				std::bind(std::forward<F>(f), _1, std::forward<Args>(args)...));

			auto _fu = new std::function<void(int id)>([pck](int id) {
				(*pck)(id);
			});

			q.push(_fu);

			std::unique_lock<std::mutex> lock(mtx);
			cv.notify_one();

			return pck->get_future();
		}

		/*template <typename F, typename... Rest>
		bool wrap(F &&f, Rest... rest)
		{

			this->_f = std::args_handler::bind_place_holders(f, 0, rest...);

			if (_f)
				return true;

			return false;
		}*/

		void wait()
		{
		}

	protected:
		void run(int threadId)
		{
			// a copy of the shared ptr to the flag
			std::shared_ptr<std::atomic<bool>> flag(this->flags[threadId]);
			std::set<std::thread::id> *_threadsIds = &(this->threadsIds);

			auto f = [this, threadId, flag, _threadsIds]() {
				_threadsIds->insert(std::this_thread::get_id());
				std::atomic<bool> &_flag = *flag;
				std::function<void(int id)> *_fun;
				bool isPop = q.pop(_fun);

				while (true)
				{
					while (isPop)
					{
						std::unique_ptr<std::function<void(int threadId)>> func(_fun);
						(*_fun)(threadId);

						if (_flag)
						{
							this->kill_q.push(threadId);
							this->cv2.notify_one();
							return;
						}
						else
							isPop = q.pop(_fun);
					}

					std::unique_lock<std::mutex> lock(mtx);
					++this->nWaiting;
					this->cv2.notify_one();
					cv.wait(lock, [this, &_fun, &isPop, &_flag]() {
						isPop = q.pop(_fun);
						return isPop || this->isDone || _flag;
					});
					--this->nWaiting;

					if (!awake)
						awake = true;

					if (!isPop)
					{
						this->kill_q.push(threadId);
						this->cv2.notify_one();

						return;
					}
				}
			};

			this->threads[threadId].reset(new std::thread(f));
		}

		void init()
		{
			this->nWaiting = 0;
			this->isInterrupted = false;
			this->isDone = false;
			this->linkedPool = nullptr;
		}

		Queue<std::function<void(int id)> *> q;

		//std::function<_Ret(int, Args...)> _f;

		int id;
		std::vector<size_t> result;
		std::map<size_t, std::vector<size_t>> holder; //holding what??
		bool isResultReady = false;					  //still unused
	};
} // namespace fwPool

#endif