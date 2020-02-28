// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include "network.hpp"

// TODO - play with SO_PRIORITY

namespace crab {

CRAB_INLINE Address::Address(const std::string &numeric_host, uint16_t port) {
	if (!parse(*this, numeric_host, port))
		throw std::runtime_error("Address failed to parse, numeric_host='" + numeric_host + "'");
}

CRAB_INLINE void TCPSocket::set_handler(Handler &&rwd_handler) { this->rwd_handler.handler = std::move(rwd_handler); }

CRAB_INLINE void RunLoop::push_record(const char *event_type, size_t count) {
	performance.emplace_back(std::chrono::steady_clock::now(), event_type, count);
}

CRAB_INLINE std::vector<PerformanceRecord> RunLoop::pop_records() {
	std::vector<PerformanceRecord> result;
	result.swap(performance);
	return result;
}

CRAB_INLINE void RunLoop::print_records() {
	for (const auto &p : pop_records()) {
		auto mksec = std::chrono::duration_cast<std::chrono::microseconds>(p.tm.time_since_epoch()).count();
		auto sec   = mksec / 1000000;
		std::cout << "* " << sec << "." << mksec % 1000000 << " " << p.event_type << " " << p.count << std::endl;
	}
}

#if CRAB_SOCKET_KEVENT || CRAB_SOCKET_EPOLL || CRAB_SOCKET_WINDOWS

CRAB_INLINE void Callable::add_pending_callable(bool can_read, bool can_write) {
	this->can_read  = this->can_read || can_read;
	this->can_write = this->can_write || can_write;
	RunLoop::current()->links.triggered_callables.push_back(*this);
}

namespace details {

CRAB_INLINE void RunLoopLinks::trigger_idle_handlers() {
	if (!triggered_callables.empty())
		return;
	// Nothing triggered during poll
	for (auto &idle : idle_handlers) {
		triggered_callables.push_back(idle.a_handler);
	}
}

CRAB_INLINE bool RunLoopLinks::process_timer(const std::chrono::steady_clock::time_point &now, int &timeout_ms) {
	if (active_timers.empty())
		return false;
	Timer &timer = active_timers.front();
	if (timer.fire_time <= now) {
		active_timers.pop_front();
		// Timer is not Callable, timers must not be put into triggered_callables en masse
		// then executed, because then the timer.is_set() will be false for all timers except
		// the first one, while handler for that particular timer did not run yet.
		// This would break app logic if timers are used as a logic state holders (which is
		// useful and common), what is worse the probability of bug will be very low, so those
		// problems would be very hard to debug.
		timer.a_handler();
		return true;
	}
	// We do not want to overlap duration_cast
	const auto now_plus_max_sleep = now + std::chrono::milliseconds(RunLoop::MAX_SLEEP_MS);
	if (timer.fire_time >= now_plus_max_sleep)
		return false;
	timeout_ms =
	    1 + static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(timer.fire_time - now).count());
	// crude way of rounding up, we do not want to wake loop up BEFORE fire_time.
	// Moreover, 0 means "poll" and we do not want to poll for 1 msec waiting for timer
	return false;
}

CRAB_INLINE void RunLoopLinks::call_watcher(Watcher *watcher) {
	std::unique_lock<std::mutex> lock(mutex);
	fired_objects.push_back(*watcher);
}

CRAB_INLINE void RunLoopLinks::cancel_called_watcher(Watcher *watcher) {
	std::unique_lock<std::mutex> lock(mutex);
	watcher->fired_objects_node.unlink();
}

CRAB_INLINE void RunLoopLinks::trigger_called_watchers() {
	std::unique_lock<std::mutex> lock(mutex);
	while (!fired_objects.empty()) {
		Watcher &watcher = *fired_objects.begin();
		watcher.fired_objects_node.unlink();
		triggered_callables.push_back(watcher.a_handler);
	}
}
}  // namespace details

CRAB_INLINE void RunLoop::run() {
	links.quit = false;
	auto now   = std::chrono::steady_clock::now();
	while (!links.quit) {
		if (!links.triggered_callables.empty()) {
			Callable &callable = *links.triggered_callables.begin();
			callable.triggered_callables_node.unlink();
			callable.handler();
			continue;
		}
		int timeout_ms = MAX_SLEEP_MS;
		if (links.process_timer(now, timeout_ms))
			continue;
		// Nothing triggered and no timers here
		if (links.idle_handlers.empty()) {
			step(timeout_ms);  // Just waiting
		} else {
			step(0);  // Poll
			links.trigger_idle_handlers();
		}
		now = std::chrono::steady_clock::now();
		// Runloop optimizes # of calls to now() because those can be slow
	}
}

CRAB_INLINE void Timer::once(float after_seconds) {
	cancel();
	const auto now = std::chrono::steady_clock::now();
	const auto ma  = std::chrono::steady_clock::time_point::max();
	// We do not wish to overflow time point. Observation - chrono is a disaster :(
	const auto max_after_seconds = std::chrono::duration_cast<std::chrono::seconds>(ma - now).count();
	if (after_seconds < 0) {
		fire_time = now;
	} else {
		if (after_seconds >= max_after_seconds)
			fire_time = std::chrono::steady_clock::time_point::max();
		else
			fire_time = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			                      std::chrono::duration<float>(after_seconds));
	}
	RunLoop::current()->links.active_timers.insert(*this);
}

CRAB_INLINE bool Timer::is_set() const { return heap_index.in_heap(); }

CRAB_INLINE void Timer::cancel() { RunLoop::current()->links.active_timers.erase(*this); }

CRAB_INLINE Idle::Idle(Handler &&cb) : a_handler(std::move(cb)) {
	RunLoop::current()->links.idle_handlers.push_back(*this);
}

CRAB_INLINE void Idle::set_active(bool a) {
	if (a) {
		RunLoop::current()->links.idle_handlers.push_back(*this);
	} else {
		a_handler.cancel_callable();
		idle_node.unlink();
	}
}

CRAB_INLINE Watcher::Watcher(Handler &&a_handler) : loop(RunLoop::current()), a_handler(std::move(a_handler)) {}

CRAB_INLINE void Watcher::call() {
	loop->links.call_watcher(this);
	loop->wakeup();
}

CRAB_INLINE void Watcher::cancel() {
	a_handler.cancel_callable();
	loop->links.cancel_called_watcher(this);
}

namespace details {

class DNSWorker {
public:
	static DNSWorker &get_instance() {
		// C++11 local static variables are thread-safe
		// and also will be destroyed on main() exit
		static DNSWorker storage;
		return storage;
	}

private:
	DNSWorker() = default;
	~DNSWorker() {
		{
			std::unique_lock<std::mutex> lock(dns_mutex);
			quit = true;
			cond.notify_all();
		}
		if (dns_thread.joinable())
			dns_thread.join();
	}

	friend class ::crab::DNSResolver;

	std::mutex dns_mutex;
	bool quit = false;
	IntrusiveList<DNSResolver, &DNSResolver::work_queue_node> work_queue;
	DNSResolver *executing_request = nullptr;
	std::condition_variable cond;

	std::thread dns_thread{&DNSWorker::worker_fun, this};
	void worker_fun() {
		while (true) {
			std::string host_name;
			uint16_t port = 0;
			bool ipv4     = false;
			bool ipv6     = false;
			{
				std::unique_lock<std::mutex> lock(dns_mutex);
				if (quit)
					return;
				if (work_queue.empty()) {
					cond.wait(lock);
					continue;
				}
				executing_request = &*work_queue.begin();
				executing_request->work_queue_node.unlink();
				host_name = std::move(executing_request->host_name);
				port      = executing_request->port;
				ipv4      = executing_request->ipv4;
				ipv6      = executing_request->ipv6;
			}
			// resolve
			auto names = DNSResolver::sync_resolve(host_name, port, ipv4, ipv6);
			std::unique_lock<std::mutex> lock(dns_mutex);
			if (!executing_request)
				continue;
			executing_request->names = std::move(names);
			executing_request->ab.call();
			executing_request = nullptr;
		}
	}
};

}  // namespace details

CRAB_INLINE DNSResolver::DNSResolver(DNS_handler &&handler)
    : ab(std::bind(&DNSResolver::on_handler, this)), dns_handler(std::move(handler)) {}

CRAB_INLINE void DNSResolver::on_handler() {
	resolving = false;
	dns_handler(names);
}

CRAB_INLINE void DNSResolver::resolve(const std::string &host_name, uint16_t port, bool ipv4, bool ipv6) {
	cancel();
	auto &w         = details::DNSWorker::get_instance();
	resolving       = true;
	this->host_name = host_name;
	this->port      = port;
	this->ipv4      = ipv4;
	this->ipv6      = ipv6;
	std::unique_lock<std::mutex> lock(w.dns_mutex);
	w.work_queue.push_back(*this);
	w.cond.notify_one();
}

CRAB_INLINE void DNSResolver::cancel() {
	if (!resolving)
		return;
	auto &w = details::DNSWorker::get_instance();
	std::unique_lock<std::mutex> lock(w.dns_mutex);
	if (w.executing_request == this)
		w.executing_request = nullptr;
	work_queue_node.unlink();
	ab.cancel();
	resolving = false;
}

CRAB_INLINE Address DNSResolver::sync_resolve_single(const std::string &host_name, uint16_t port) {
	auto arr = sync_resolve(host_name, port, true, false);
	if (!arr.empty())
		return arr.front();
	arr = sync_resolve(host_name, port, false, true);
	if (!arr.empty())
		return arr.front();
	throw std::runtime_error("Failed to resolve host '" + host_name + "'");
}

#endif

}  // namespace crab
