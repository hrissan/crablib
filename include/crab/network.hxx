// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <condition_variable>
#include <iostream>
#include <thread>
#include "integer_cast.hpp"
#include "network.hpp"

// TODO - play with SO_PRIORITY

namespace crab {

CRAB_INLINE PerformanceStats::PerformanceStats() { performance.reserve(MAX_PERFORMANCE_RECORDS); }

CRAB_INLINE void PerformanceStats::push_record(const char *event_type_literal, int fd, int count) {
	if (performance.size() < MAX_PERFORMANCE_RECORDS)
		performance.emplace_back(steady_clock::now(), event_type_literal, fd, count);
}

CRAB_INLINE void PerformanceStats::print_records(std::ostream &out) {
	for (const auto &p : get_records()) {
		auto mksec = std::chrono::duration_cast<std::chrono::microseconds>(p.tm.time_since_epoch()).count();
		auto sec   = mksec / 1000000;
		std::cout << "* " << sec << "." << mksec % 1000000 << " " << p.event_type << " " << p.count << std::endl;
	}
	clear_records();
}

CRAB_INLINE Address::Address(const std::string &ip, uint16_t port) {
	if (!parse(*this, ip, port))
		if (!parse(*this, ip, port))
			throw std::runtime_error("Address failed to parse, numeric_host='" + ip + "'");
}

CRAB_INLINE Address::Address(const std::string &ip_port) {
	if (!parse(*this, ip_port))
		throw std::runtime_error("Address failed to parse, must be <ip>:<port> numeric_host_port='" + ip_port + "'");
}

CRAB_INLINE bool Address::parse(Address &address, const std::string &ip_port) {
	size_t pos = ip_port.find(':');
	if (pos == std::string::npos)
		return false;
	uint16_t port = integer_cast<uint16_t>(ip_port.substr(pos + 1));
	return parse(address, ip_port.substr(0, pos), port);
}

CRAB_INLINE std::ostream &operator<<(std::ostream &os, const Address &msg) {
	return os << msg.get_address() << ":" << msg.get_port();
}

#if !CRAB_IMPL_LIBEV

CRAB_INLINE Idle::Idle(Handler &&cb) : a_handler(std::move(cb)) { set_active(true); }

CRAB_INLINE void Idle::set_active(bool a) {
	if (a) {
		RunLoop::current()->idle_handlers.push_back(*this);
	} else {
		idle_node.unlink();
	}
}

CRAB_INLINE void RunLoop::cancel() { links.quit = true; }

#endif

#if CRAB_IMPL_KEVENT || CRAB_IMPL_EPOLL || CRAB_IMPL_WINDOWS

CRAB_INLINE void Callable::add_pending_callable(bool can_read, bool can_write) {
	this->can_read  = this->can_read || can_read;
	this->can_write = this->can_write || can_write;
	RunLoop::current()->links.triggered_callables.push_back(*this);
}

namespace details {

CRAB_INLINE bool RunLoopLinks::process_timer(int &timeout_ms) {
	if (active_timers.empty())
		return false;
	while (true) {
		Timer &timer = active_timers.front();
		if (timer.fire_time <= now) {
			active_timers.pop_front();
			if (timer.moved_fire_time > now) {  // Timer was moved far enough without rescheduling
				timer.fire_time = timer.moved_fire_time;
				active_timers.insert(timer);
				continue;
			}
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
		timeout_ms = 1 + static_cast<int>(
		                     std::chrono::duration_cast<std::chrono::milliseconds>(timer.fire_time - now).count());
		// crude way of rounding up, we do not want to wake loop up BEFORE fire_time.
		// Moreover, 0 means "poll" and we do not want to poll for 0.9 msec waiting for timer
		break;
	}
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
	links.now  = steady_clock::now();
	while (!links.quit) {
		if (!links.triggered_callables.empty()) {
			Callable &callable = *links.triggered_callables.begin();
			callable.triggered_callables_node.unlink();
			callable.handler();
			continue;
		}
		int timeout_ms = MAX_SLEEP_MS;
		if (links.process_timer(timeout_ms))
			continue;
		// Nothing triggered and no timers here
		if (idle_handlers.empty()) {
			step(timeout_ms);  // Just waiting
		} else {
			step(0);  // Poll, beware, both lists in a line below could change as a result
			if (links.triggered_callables.empty() && !idle_handlers.empty()) {
				// Nothing triggered during poll, time for idle handlers to run
				Idle &idle = *idle_handlers.begin();
				// Rotate round-robin
				idle.idle_node.unlink();
				idle_handlers.push_back(idle);
				idle.a_handler();
			}
		}
		links.now = steady_clock::now();
		// Runloop optimizes # of calls to now() because those can be slow
	}
}

CRAB_INLINE steady_clock::time_point RunLoop::now() { return links.now; }

CRAB_INLINE void Timer::once(double delay_seconds) {
	const auto now = RunLoop::current()->links.now;
	// We do not wish to overflow time point. Observation - chrono is a disaster, will do manually
	// once(double) is slower anyway, than once(duration), so couple extra checks
	// Normally this code would be enough
	// once_at(now + std::chrono::duration_cast<steady_clock::duration>(
	//               std::chrono::duration<double>(after_seconds)));
	if (delay_seconds <= 0) {
		once_at(now);
		return;
	}
	double fsc    = delay_seconds * steady_clock::time_point::period::den / steady_clock::time_point::period::num;
	const auto ma = steady_clock::time_point::max();
	const auto max_delay_seconds = (ma - now).count();

	if (fsc >= double(max_delay_seconds))
		once_at(ma);
	else
		once_at(now + steady_clock::duration{static_cast<steady_clock::duration::rep>(fsc)});
}

CRAB_INLINE void Timer::once(steady_clock::duration delay) {
	const auto now = RunLoop::current()->links.now;
	// We wish to clamp time point instead of overflow. Observation - chrono is a disaster, will do manually
	if (delay.count() <= 0) {
		once_at(now);
		return;
	}
	const auto ma                = steady_clock::time_point::max();
	const auto max_delay_seconds = (ma - now).count();

	if (delay.count() >= max_delay_seconds)
		once_at(ma);
	else
		once_at(now + delay);
}

CRAB_INLINE void Timer::once_at(steady_clock::time_point time_point) {
	// if you call once() again without calling cancel and fire_time will increase, crab will
	// not cancel timer, but will only set moved_fire_time instead. When timer fires, crab will
	// reschedule it. So, if you set TCP timeout to 1 second on each TCP packet and you get 1000
	// packets per second, crab will reschedule timer only 1 per second
	if (is_set() && time_point >= fire_time) {
		moved_fire_time = time_point;
	} else {
		RunLoop::current()->links.active_timers.erase(*this);
		moved_fire_time = fire_time = time_point;
		RunLoop::current()->links.active_timers.insert(*this);
	}
}

CRAB_INLINE Timer::~Timer() { cancel(); }

CRAB_INLINE bool Timer::is_set() const { return heap_index.in_heap(); }

CRAB_INLINE void Timer::cancel() { RunLoop::current()->links.active_timers.erase(*this); }

CRAB_INLINE Watcher::Watcher(Handler &&a_handler) : loop(RunLoop::current()), a_handler(std::move(a_handler)) {}

CRAB_INLINE Watcher::~Watcher() { cancel(); }

CRAB_INLINE void Watcher::call() {
	loop->links.call_watcher(this);
	loop->wakeup();
}

CRAB_INLINE void Watcher::cancel() {
	a_handler.cancel_callable();
	loop->links.cancel_called_watcher(this);
}

CRAB_INLINE bool TCPSocket::can_write() const { return rwd_handler.can_write; }

CRAB_INLINE bool UDPTransmitter::can_write() const { return w_handler.can_write; }

#endif

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
#if CRAB_IMPL_BOOST
		RunLoop runloop;  // boost sync resolve requires io_service
#endif
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

}  // namespace crab
