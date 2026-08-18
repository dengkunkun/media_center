#ifndef DISPATCH_QUEUE_HPP
#define DISPATCH_QUEUE_HPP

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <string>

class DispatchQueue {
public:
	typedef std::function<void(void)> dispatch_operator_t;

public:
	DispatchQueue(std::string name, size_t thread_count = 1);
	~DispatchQueue();


	void dispatch(const dispatch_operator_t& op);
	void dispatch(dispatch_operator_t&& op);

	void remove_pending(void);
	void dispatch_thread_handler(void);

	DispatchQueue(const DispatchQueue& rhs) = delete;
	DispatchQueue& operator=(const DispatchQueue& rhs) = delete;
	DispatchQueue(DispatchQueue&& rhs) = delete;
	DispatchQueue& operator=(DispatchQueue&& rhs) = delete;

private:
	std::string _name;
	std::mutex _mutex;
	std::vector<std::thread> _threads;
	std::queue<dispatch_operator_t> _queue;
	std::condition_variable _cv;
	bool _quit_flag = false;
};

#endif	// DISPATCH_QUEUE_HPP
