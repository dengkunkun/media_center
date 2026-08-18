#include "dispatch_queue.hpp"


DispatchQueue::DispatchQueue(std::string name, size_t thread_count)
	:
		_name{std::move(name)},
		_threads(thread_count)
{
	for (size_t i = 0; i < _threads.size(); i++) {
		_threads[i] = std::thread(&DispatchQueue::dispatch_thread_handler, this);
	}
}

DispatchQueue::~DispatchQueue()
{
	std::unique_lock<std::mutex> lock(_mutex);
	_quit_flag = true;
	lock.unlock();
	_cv.notify_all();

	for (size_t i = 0; i < _threads.size(); i++) {
		if (_threads[i].joinable()) {
			_threads[i].join();
		}
	}
}

void DispatchQueue::remove_pending()
{
    std::unique_lock<std::mutex> lock(_mutex);
    _queue = {};
}

void DispatchQueue::dispatch(const dispatch_operator_t& op)
{
    std::unique_lock<std::mutex> lock(_mutex);
    _queue.push(op);

    lock.unlock();
    _cv.notify_one();
}

void DispatchQueue::dispatch(dispatch_operator_t&& op)
{
	std::unique_lock<std::mutex> lock(_mutex);
	_queue.push(std::move(op));

	lock.unlock();
	_cv.notify_one();
}

void DispatchQueue::dispatch_thread_handler(void)
{
	std::unique_lock<std::mutex> lock(_mutex);
	do {
		_cv.wait(lock,
			[this] {
				return (_queue.size() || _quit_flag);
			}
		);

		if (!_quit_flag && _queue.size()) {
			auto op = std::move(_queue.front());
			_queue.pop();

			lock.unlock();

			op();

			lock.lock();
		}
	} while (!_quit_flag);
}
