// Copyright (c) 2007-2023, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <algorithm>
#include <iostream>
#include "network.hpp"

#if CRAB_IMPL_LIBEV

namespace crab {

CRAB_INLINE Timer::Timer(Handler &&cb) : a_handler(std::move(cb)), impl(RunLoop::current()->get_impl()) {
	impl.set<Timer, &Timer::io_cb>(this);
}

CRAB_INLINE Timer::~Timer() { cancel(); }

CRAB_INLINE void Timer::cancel() { impl.stop(); }

CRAB_INLINE bool Timer::is_set() const { return impl.is_active(); }

CRAB_INLINE void Timer::once(double after_seconds) {
	cancel();
	impl.start(after_seconds, 0);
}

CRAB_INLINE void Timer::once(steady_clock::duration delay) {
	double after_seconds = std::chrono::duration_cast<std::chrono::duration<double>>(delay).count();
	once(after_seconds);
}

CRAB_INLINE void Timer::once_at(steady_clock::time_point time_point) { once(time_point - RunLoop::current()->now()); }

CRAB_INLINE Watcher::Watcher(Handler &&a_handler)
    : loop(RunLoop::current()), a_handler(std::move(a_handler)), impl(RunLoop::current()->get_impl()) {
	impl.set<Watcher, &Watcher::io_cb>(this);
	impl.start();
}

CRAB_INLINE Watcher::~Watcher() { cancel(); }

CRAB_INLINE void Watcher::call() { impl.send(); }

CRAB_INLINE void Watcher::cancel() {
	impl.stop();
	impl.start();
}

CRAB_INLINE Idle::Idle(Handler &&cb) : a_handler(std::move(cb)), impl(RunLoop::current()->get_impl()) {
	impl.set<Idle, &Idle::io_cb>(this);
	impl.start();
}

CRAB_INLINE void Idle::set_active(bool a) {
	if (a)
		impl.start();
	else
		impl.stop();
}

CRAB_INLINE bool Idle::is_active() const { return impl.is_active(); }

CRAB_INLINE Signal::Signal(Handler &&cb, const std::vector<int> &) : a_handler(std::move(cb)) {}

CRAB_INLINE Signal::~Signal() {}

CRAB_INLINE bool Signal::running_under_debugger() { return false; }

CRAB_INLINE RunLoop::RunLoop() : impl(new ev::dynamic_loop{}) {
	if (CurrentLoop::instance)
		throw std::runtime_error{"RunLoop::RunLoop Only single RunLoop per thread is allowed"};
	CurrentLoop::instance = this;
}

CRAB_INLINE RunLoop::RunLoop(DefaultLoop) : impl(new ev::default_loop{}) {
	if (CurrentLoop::instance)
		throw std::runtime_error{"RunLoop::RunLoop only single RunLoop per thread is allowed"};
	CurrentLoop::instance = this;
}

CRAB_INLINE RunLoop::~RunLoop() { CurrentLoop::instance = this; }

CRAB_INLINE void RunLoop::run() { impl->run(); }

CRAB_INLINE void RunLoop::cancel() { impl->break_loop(); }

CRAB_INLINE steady_clock::time_point RunLoop::now() {
	return steady_clock::now();  // TODO convert impl.now();
}

}  // namespace crab

#endif
