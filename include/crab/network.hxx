// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include "network.hpp"

namespace crab {

CRAB_INLINE void TCPSocket::set_handlers(Handler &&rw_handler, Handler &&d_handler) {
	this->rw_handler = std::move(rw_handler);
	this->d_handler  = std::move(d_handler);
}

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

namespace details {
CRAB_INLINE bool RunLoopLinks::process_timer(const std::chrono::steady_clock::time_point &now, int &timeout_ms) {
#if CRAB_INTRUSIVE_SET
	IntrusiveNode<Timer> *t = &active_timers;
	if (t->is_end())
		return false;
	Timer *timer = t->get_current();
	if (timer->fire_time <= now) {
		timer->active_timers_node.unlink(&Timer::active_timers_node);
		timer->a_handler();
		return true;
	}
#else
	if (active_timers.empty())
		return false;
	Timer *timer = *active_timers.begin();
	if (timer->fire_time <= now) {
		active_timers.erase(active_timers.begin());
		timer->set = false;
		timer->a_handler();
		return true;
	}
#endif
	// We do not want to overlap
	const auto now_plus_max_sleep = now + std::chrono::milliseconds(RunLoop::MAX_SLEEP_MS);
	if (timer->fire_time >= now_plus_max_sleep)
		return false;
	timeout_ms =
	    1 + static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(timer->fire_time - now).count());
	// crude way of rounding up, we do not want to wake loop up BEFORE fire_time.
	// Moreover, 0 means "poll" and we do not want to poll waiting for timer
	return false;
}

CRAB_INLINE void RunLoopLinks::add_triggered_callables(RunLoopCallable *callable) {
	triggered_callables.push_back(*callable);
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
		Watcher *watcher = &*fired_objects.begin();
		watcher->fired_objects_node.unlink();
		add_triggered_callables(watcher);
	}
}
}  // namespace details
CRAB_INLINE void RunLoop::run() {
	links.quit = false;
	auto now   = std::chrono::steady_clock::now();
	while (!links.quit) {
		if (!links.triggered_callables.empty()) {
			RunLoopCallable *callable = &*links.triggered_callables.begin();
			callable->triggered_callables_node.unlink();
			callable->on_runloop_call();
			continue;
		}
		int timeout_ms = MAX_SLEEP_MS;
		if (links.process_timer(now, timeout_ms))
			continue;
		for (auto it = links.idle_handlers.begin(); it != links.idle_handlers.end(); ++it) {
			links.add_triggered_callables(&*it);
			timeout_ms = 0;
		}
		step(timeout_ms);
		now = std::chrono::steady_clock::now();
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
	auto loop = RunLoop::current();

#if CRAB_INTRUSIVE_SET
	// Actual code is for intrusive list and O(N)
	// Works very slow for 100k sockets, each with heartbit timer. TODO - intrusive balanced tree
	auto t = loop->links.active_timers.begin();
	auto e = loop->links.active_timers.end();
	while (t != e && t->fire_time <= fire_time)
		++t;
	loop->links.active_timers.insert(t, *this);
#else
	loop->links.active_timers.insert(this);
	set = true;
#endif
}

CRAB_INLINE bool Timer::is_set() const {
#if CRAB_INTRUSIVE_SET
	return active_timers_node.in_list();
#else
	return set;
#endif
}

CRAB_INLINE void Timer::cancel() {
	cancel_callable();
#if CRAB_INTRUSIVE_SET
	active_timers_node.unlink();
#else
	if (!set)
		return;
	RunLoop::current()->links.active_timers.erase(this);
	set = false;
#endif
}

CRAB_INLINE Idle::Idle(Handler &&cb) : a_handler(cb) { RunLoop::current()->links.idle_handlers.push_back(*this); }

CRAB_INLINE Watcher::Watcher(Handler &&a_handler) : loop(RunLoop::current()), a_handler(std::move(a_handler)) {}

CRAB_INLINE void Watcher::call() {
	loop->links.call_watcher(this);
	loop->wakeup();
}

CRAB_INLINE void Watcher::cancel() {
	cancel_callable();
	loop->links.cancel_called_watcher(this);
}

CRAB_INLINE DNSWorker::DNSWorker() {
	if (StaticWorker::instance)
		throw std::runtime_error("Create single DNSWorker in your main");
	StaticWorker::instance = this;
	dns_thread             = std::thread(&DNSWorker::worker_fun, this);
}

CRAB_INLINE DNSWorker::~DNSWorker() {
	{
		std::unique_lock<std::mutex> lock(dns_mutex);
		quit = true;
		cond.notify_all();
	}
	if (dns_thread.joinable())
		dns_thread.join();
	StaticWorker::instance = nullptr;
}

CRAB_INLINE void DNSWorker::worker_fun() {
	while (true) {
		DNSResolver *req = nullptr;
		std::string full_name;
		bool ipv4 = false;
		bool ipv6 = false;
		{
			std::unique_lock<std::mutex> lock(dns_mutex);
			if (quit)
				return;
			if (work_queue.empty()) {
				cond.wait(lock);
				continue;
			}
			//  std::cout << "Took work" << std::endl;
			req = work_queue.front();
			work_queue.pop_front();
			full_name              = std::move(req->full_name);
			ipv4                   = req->ipv4;
			ipv6                   = req->ipv6;
			req->executing_request = &req;
		}
		// resolve
		// std::this_thread::sleep_for(std::chrono::seconds(1));
		auto names = sync_resolve(full_name, ipv4, ipv6);
		// std::cout << "Resolved 1" << std::endl;
		std::unique_lock<std::mutex> lock(dns_mutex);
		if (!req)
			continue;
		// std::cout << "Resolved 2" << std::endl;
		req->names = std::move(names);
		// std::cout << "Resolved 3" << std::endl;
		req->ab.call();
		// std::cout << "Resolved 4" << std::endl;
	}
}

CRAB_INLINE DNSResolver::DNSResolver(DNS_handler &&handler)
    : ab(std::bind(&DNSResolver::on_handler, this)), dns_handler(std::move(handler)) {}

CRAB_INLINE void DNSResolver::on_handler() {
	resolving = false;
	dns_handler(names);
}

CRAB_INLINE void DNSResolver::resolve(const std::string &full_name, bool ipv4, bool ipv6) {
	cancel();
	DNSWorker *w = DNSWorker::StaticWorker::instance;
	if (!w)
		throw std::runtime_error("Please, create single DNSWorker instance in your main");
	resolving = true;
	std::unique_lock<std::mutex> lock(w->dns_mutex);
	this->full_name = full_name;
	this->ipv4      = ipv4;
	this->ipv6      = ipv6;
	w->work_queue.push_back(this);
	w->cond.notify_one();
}

CRAB_INLINE void DNSResolver::cancel() {
	if (!resolving)
		return;
	DNSWorker *w = DNSWorker::StaticWorker::instance;
	std::unique_lock<std::mutex> lock(w->dns_mutex);
	if (executing_request)
		*executing_request = nullptr;
	for (auto it = w->work_queue.begin(); it != w->work_queue.end();)
		if (*it == this)
			it = w->work_queue.erase(it);
		else
			++it;
	ab.cancel();
}

CRAB_INLINE bdata DNSResolver::parse_ipaddress(const std::string &str) {
	bdata result;
	if (!parse_ipaddress(str, &result))
		throw std::runtime_error("Error parsing IP address '" + str + "'");
	return result;
}

CRAB_INLINE bool DNSResolver::is_multicast(const bdata &data) {
	if (data.size() == 4 && (data[0] & 0xf0) == 0xe0)
		return true;
	if (data.size() == 16 && data[0] == 0xff)
		return true;
	return false;
}

#endif

}  // namespace crab
