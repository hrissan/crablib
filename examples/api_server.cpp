// Copyright (c) 2007-2020, Grigory Buteyko aka Hrissan
// Licensed under the MIT License. See LICENSE for details.

#include <condition_variable>
#include <iostream>
#include <set>

#include <crab/crab.hpp>

const bool debug = false;
enum { HEADER_SIZE = 16 };

class Client;
class ApiWorkers {
public:
	struct OutputQueue;
	struct WorkItem {
		OutputQueue *output_queue = nullptr;
		void *client              = nullptr;  // TODO - fix this crap. We never destroy clients, so pointer is safe
		size_t client_id          = 0;  // But client ids change, so we know the work done is for disconnected one
		crab::Buffer request{0};
		crab::Buffer response{0};
	};
	struct OutputQueue {
		std::mutex worker_responses_mutex;
		std::deque<WorkItem> worker_responses;
		crab::Watcher worker_ready_ab;

		explicit OutputQueue(crab::Handler &&handler) : worker_ready_ab(std::move(handler)) {}
	};
	ApiWorkers() {
		for (size_t i = 0; i != 2; ++i)
			worker_threads.emplace_back(&ApiWorkers::worker_fun, this);
	}
	~ApiWorkers() {
		{
			std::unique_lock<std::mutex> lock(worker_requests_mutex);
			worker_should_quit = true;
			worker_requests_cond.notify_all();
		}
		for (auto &th : worker_threads)
			th.join();
	}

	static void process_work_item(const crab::Buffer &request, crab::Buffer &response) {
		size_t len = request.size();
		response.clear(HEADER_SIZE + len);
		response.write(reinterpret_cast<const uint8_t *>(&len), 4);
		response.did_write(HEADER_SIZE - 4);  // TODO security issue, uninitialized memory
		response.did_write(len);              // TODO security issue, uninitialized memory
	}
	void add_work(WorkItem &&work_item) {
		std::unique_lock<std::mutex> lock(worker_requests_mutex);
		worker_requests.push_back(std::move(work_item));
		worker_requests_cond.notify_one();
	}

private:
	std::mutex worker_requests_mutex;
	bool worker_should_quit = false;
	std::condition_variable worker_requests_cond;
	std::deque<WorkItem> worker_requests;

	std::vector<std::thread> worker_threads;

	void worker_fun() {
		WorkItem work_item;
		while (true) {
			{
				std::unique_lock<std::mutex> lock(worker_requests_mutex);
				if (worker_should_quit)
					return;
				if (worker_requests.empty()) {
					worker_requests_cond.wait(lock);
					continue;
				}
				work_item = std::move(worker_requests.front());
				worker_requests.pop_front();
			}
			process_work_item(work_item.request, work_item.response);
			auto output_queue = work_item.output_queue;
			{
				std::unique_lock<std::mutex> lock(output_queue->worker_responses_mutex);
				output_queue->worker_responses.push_back(std::move(work_item));
			}
			output_queue->worker_ready_ab.call();  // Wake up correct network thread
		}
	}
};

class ApiNetwork {
public:
	explicit ApiNetwork(ApiWorkers &api_workers,
	    const crab::Address &bind_address,
	    const crab::TCPAcceptor::Settings &settings)
	    : api_workers(api_workers)
	    , la_socket(
	          bind_address,
	          [&]() { accept_all(); },
	          settings)
	    , output_queue([&]() { on_worker_ready_ab(); })
	    , stat_timer([&]() { print_stats(); }) {
		print_stats();
	}

private:
	ApiWorkers &api_workers;
	crab::TCPAcceptor la_socket;

	// Client states
	// - waiting for client responses to be sent, so it will continue reading header
	// - waiting for memory for requests to appear, so it will continue reading body

	struct RequestHeader {
		size_t len = 0;
	};
	struct Client {
		crab::IntrusiveNode<Client> disconnected_node;

		size_t client_id = 0;
		crab::TCPSocket socket{crab::empty_handler};
		crab::Buffer read_buffer{4096};
		enum State { READING_HEADER, WAITING_MEMORY_FOR_BODY, READING_BODY } state = READING_HEADER;
		crab::optional<RequestHeader> request_header;  // !empty in WAITING_MEMORY_FOR_BODY state
		crab::Buffer request_body{0};
		std::deque<crab::Buffer> requests;
		std::deque<crab::Buffer> responses;
		size_t requests_in_work = 0;
		crab::IntrusiveNode<Client> request_memory_queue_node;   // Waiting turn to read request body
		crab::IntrusiveNode<Client> read_body_queue_node;        // Waiting turn to read request body
		crab::IntrusiveNode<Client> response_memory_queue_node;  // Waiting for memory to queue work
		size_t total_read    = 0;
		size_t total_written = 0;
	};
	// Reader loop, generates ready requests
	// 1. reading header, !local_limit, !can_read  (socket) -> read_header() -> 1, 2, 3, 4
	// 2. reading header, local_limit, can_read    (local_limit) -> read_header() -> 1, 2, 3, 4
	// 3. waiting request memory, request_memory_limit (request_memory_limit) -> start_reading_body() -> 3, 4
	// 4. reading body (socket) -> read_body() -> 4, 1

	// Processing loop, moves requests into workers
	// 1. requests.empty(), !in response_memory_queue_node
	// 2, !requests.empty(), in response_memory_queue_node

	// Sending loop, receives from workers and sends responses, generates (request_memory_limit), (local_limit)

	enum { SI = sizeof(Client) };
	size_t max_clients                     = 128 * 1024;
	size_t max_pending_requests_per_client = 16;
	size_t max_requests_memory             = 256 * 1024 * 1024;
	size_t max_responses_memory            = 1024 * 1024 * 1024;
	size_t max_request_length              = 1024 * 1024;
	size_t max_response_length             = 1 * 1024 * 1024;
	size_t total_requests_memory           = 0;
	size_t total_response_memory           = 0;
	// TODO we have some fixed overhead per request/response, account for it

	size_t clients_accepted = 0;           // We assign client_id from this counter on client accept
	std::deque<Client> allocated_clients;  // We need container that grows and does not invalidate references
	crab::IntrusiveList<Client, &Client::disconnected_node> disconnected_queue;

	crab::IntrusiveList<Client, &Client::read_body_queue_node> read_body_queue;
	crab::IntrusiveList<Client, &Client::request_memory_queue_node> request_memory_queue;
	crab::IntrusiveList<Client, &Client::response_memory_queue_node> response_memory_queue;
	// clients in fair_queue are considered low-priority and served in on_idle
	// callbacks for such clients are ignored
	// client is put in fair_queue if it has more than 1 request pending (sending batch requests)

	ApiWorkers::OutputQueue output_queue;

	// IntrusiveList is good as a queue due to O(1) removal cost and auto-remove in Node destructor

	crab::Timer stat_timer;
	size_t requests_received = 0;
	size_t responses_sent    = 0;

	//	void on_idle() {
	//		const size_t MAX_COUNTER = 1;
	//		// we will call epoll() once per MAX_COUNTER messages, trading latency for throughput
	//		size_t counter = 0;
	//		while (!read_queue.empty() && counter++ < MAX_COUNTER) {
	//			Client &c = *read_queue.begin();
	//			c.read_queue_node.unlink();
	//			if (process_single_request(c)) {
	//				continue;
	//			}
	//			read_queue.push_back(c);
	//		}
	//		accept_single();
	//	}
	static void busy_sleep_microseconds(int msec) {
		auto start = std::chrono::steady_clock::now();
		while (true) {
			auto now = std::chrono::steady_clock::now();
			if (std::chrono::duration_cast<std::chrono::microseconds>(now - start).count() > msec)
				break;
		}
	}
	bool is_over_local_limit(Client &client) const {
		if (client.responses.size() + client.requests.size() + client.requests_in_work >=
		    max_pending_requests_per_client) {
			// This condition will change after writing some bytes, TODO - retry after writing bytes
			return true;  // Stay in READING_HEADER state, otherwise 4th state will be required
		}
		// TODO - check total responses size for client, if too big also return true
		return false;
	}
	void read_header(Client &client) {
		if (client.state != Client::READING_HEADER)
			return;
		if (is_over_local_limit(client)) {
			if (debug)
				std::cout << "read_header is_over_local_limit" << std::endl;
			return;  // Local limit triggered by local send
		}
		if (client.read_buffer.size() < HEADER_SIZE) {
			client.total_read += client.read_buffer.read_from(client.socket);
			if (client.read_buffer.size() < HEADER_SIZE) {
				return;  // No more requests
			}
		}
		client.request_header = RequestHeader{};
		client.read_buffer.read(reinterpret_cast<uint8_t *>(&client.request_header->len), 4);
		client.read_buffer.did_read(HEADER_SIZE - 4);  // Skip other bytes
		if (client.request_header->len != 4) {         // > max_request_length) {
			if (debug)
				std::cout << "Bad Request Len" << std::endl;
			// TODO - disconnect here
		}
		if (!request_memory_queue.empty() ||
		    total_requests_memory + client.request_header->len > max_requests_memory) {
			if (debug)
				std::cout << "WAITING_MEMORY_FOR_BODY, request_memory_queue.push " << std::endl;
			client.state = Client::WAITING_MEMORY_FOR_BODY;
			request_memory_queue.push_back(client);
			return;
		}
		start_reading_body(client);
	}
	void start_reading_body(Client &client) {
		invariant(client.state != Client::READING_BODY, "");
		if (debug)
			std::cout << "start_reading_body " << std::endl;
		total_requests_memory += client.request_header->len;
		client.request_body.clear(client.request_header->len);
		client.request_header.reset();
		client.request_body.read_from(client.read_buffer);
		client.state = Client::READING_BODY;
		// Here either client.read_buffer is empty, or client.requests.back() is full
		// TODO optimize read_from for the case when client.requests.back() is full

		read_body_queue.push_back(client);
	}
	void read_body(Client &client) {
		invariant(client.state = Client::READING_BODY, "");
		client.total_read += client.request_body.read_from(client.socket);
		if (!client.request_body.full())
			return;
		requests_received += 1;
		client.state = Client::READING_HEADER;
		client.requests.push_back(std::move(client.request_body));
		client.request_body.clear();
		if (!response_memory_queue.empty() || total_response_memory + max_response_length > max_responses_memory) {
			if (debug)
				std::cout << "read_body ready, response_memory_queue.push " << std::endl;
			response_memory_queue.push_back(client);
		} else {
			run_worker(client);
		}
		read_header(client);
	}
	bool run_worker(Client &client) {
		if (debug)
			std::cout << "run_worker " << std::endl;
		if (false) {
			crab::Buffer request = std::move(client.requests.front());
			client.requests.pop_front();
			crab::Buffer response{0};
			ApiWorkers::process_work_item(request, response);

			total_requests_memory -= request.capacity();
			total_response_memory += response.capacity();
			responses_sent += 1;
			client.responses.push_back(std::move(response));
			send_responses(client);
			return true;
		}
		total_response_memory += max_response_length;
		ApiWorkers::WorkItem work_item;
		work_item.output_queue = &output_queue;
		work_item.client_id    = client.client_id;
		work_item.client       = &client;
		work_item.request      = std::move(client.requests.front());
		client.requests.pop_front();
		client.requests_in_work += 1;

		api_workers.add_work(std::move(work_item));
		return true;
	}
	void run_workers_fair() {
		while (!response_memory_queue.empty()) {
			Client &client = response_memory_queue.front();
			invariant(!client.requests.empty(), "");
			if (total_response_memory + max_response_length > max_responses_memory)
				break;  // If next client request would fit, we do not process it due to fairness
			client.response_memory_queue_node.unlink();
			run_worker(client);
			if (!client.requests.empty())
				response_memory_queue.push_back(client);
		}
	}
	std::deque<ApiWorkers::WorkItem> worker_responses_taken;
	void on_worker_ready_ab() {
		{
			// Do not stop workers pushing into worker_responses, do not allocate memory
			std::unique_lock<std::mutex> lock(output_queue.worker_responses_mutex);
			worker_responses_taken.swap(output_queue.worker_responses);  // each iteration 2 containers are swapped
		}
		for (auto &w : worker_responses_taken) {
			total_response_memory -= max_response_length;
			total_requests_memory -= w.request.capacity();
			Client &client = *reinterpret_cast<Client *>(w.client);
			if (client.client_id != w.client_id) {  // Disconnected/reconnected
				continue;
			}
			total_response_memory += w.response.capacity();
			client.requests_in_work -= 1;
			responses_sent += 1;
			//			const bool was_empty = w.client->responses.empty();
			client.responses.push_back(std::move(w.response));
			//			if (was_empty) // TODO - check if this optimization is useful
			send_responses(client);
		}
		worker_responses_taken.clear();
		read_requests_fair();  // if we read header, we could need to read body
	}
	void send_responses(Client &client) {
		//	    if (!client.socket.can_write()) // TODO - check if this optimization is useful
		//	        return;
		while (!client.responses.empty()) {
			client.total_written += client.responses.front().write_to(client.socket);
			if (!client.responses.front().empty())
				break;
			if (debug)
				std::cout << "send_responses sent complete response " << std::endl;
			total_response_memory -= client.responses.front().capacity();
			client.responses.pop_front();
			run_workers_fair();   // because total_response_memory decreased
			read_header(client);  // read header if we were in local limit
		}
	}
	void read_requests_fair() {
		while (!request_memory_queue.empty()) {
			Client &client = request_memory_queue.front();
			invariant(!!client.request_header, "");  // those who request memory always have header
			if (total_requests_memory + client.request_header->len > max_requests_memory)
				break;  // If next client request would fit, we do not process it due to fairness
			client.request_memory_queue_node.unlink();
			start_reading_body(client);  // moves into read_body_queue, which is processed below
		}
		while (!read_body_queue.empty()) {
			Client &client = read_body_queue.front();
			//			invariant(!client.request_header, ""); // those who read body have no header
			client.read_body_queue_node.unlink();
			read_body(client);  // can add client to request_memory_queue or read_body_queue, looping round-robin
		}
	}
	void on_client_handler(Client &client) {
		if (!client.socket.is_open())
			return on_client_disconnected(client);
		send_responses(client);
		read_header(
		    client);  // We read header on every opportunity. sending response could also free client resources
		read_requests_fair();  // sending response could free global resources
	}
	void on_client_disconnected(Client &client) {
		client.request_header.reset();
		total_requests_memory -= client.request_body.capacity();
		client.request_body.clear();
		for (const auto &r : client.requests) {
			total_requests_memory -= r.capacity();
		}
		client.requests.clear();
		client.requests_in_work = 0;
		for (const auto &r : client.responses) {
			total_response_memory -= r.capacity();
		}
		client.responses.clear();
		client.client_id = 0;
		client.socket.close();
		client.read_buffer.clear();
		client.state         = Client::READING_HEADER;
		client.total_read    = 0;
		client.total_written = 0;
		client.request_memory_queue_node.unlink();
		client.read_body_queue_node.unlink();
		client.response_memory_queue_node.unlink();
		disconnected_queue.push_back(client);
		// TODO - cancel request that were not taken by workers?
		//		std::cout << "Fair Client " << clients.back().client_id
		//		          << " disconnected, current number of clients is=" << clients.size() << std::endl;
	}
	void accept_all() {
		//		std::cout << "accept socket event, current number of clients is=" << clients.size() << std::endl;
		while (accept_single()) {
		}
	}
	bool accept_single() {
		if (!la_socket.can_accept())
			return false;
		if (disconnected_queue.empty()) {
			if (allocated_clients.size() >= max_clients)
				return false;
			allocated_clients.emplace_back();
			Client *it = &allocated_clients.back();
			allocated_clients.back().socket.set_handler([this, it]() { on_client_handler(*it); });
			disconnected_queue.push_back(allocated_clients.back());
		}
		Client &client = disconnected_queue.back();
		client.disconnected_node.unlink();
		clients_accepted += 1;
		client.client_id = clients_accepted;
		crab::Address addr;
		client.socket.accept(la_socket, &addr);
		//            std::cout << "Fair Client " << clients.back().client_id
		//                      << " accepted, current number of clients is=" << clients.size()
		//                      << " addr=" << addr.get_address() << ":" << addr.get_port() << std::endl;

		// Before login, clients are assigned low-priority
		// In actual fair server, there would be separate queue for not-yet-logged in clients
		// so that server can select ratio between processing logged-in versus not logged-in clients

		// also actual fair server will ensure that 2 connections from the same login are either not allowed
		// or at least occupy single slot in fair_queue, and have timeouts for connections
		//		read_queue.push_back(client);
		return true;
	}
	void print_stats() {
		stat_timer.once(1);
		std::cout << "requests received/responses sent (during last second)=" << requests_received << "/"
		          << responses_sent << std::endl;
		//		if (!clients.empty()) {
		//			std::cout << "Client.front read=" << clients.front().total_read
		//			          << " written=" << clients.front().total_written << std::endl;
		//		}
		requests_received = 0;
		responses_sent    = 0;
	}
};

class ApiServerApp {
public:
	static crab::TCPAcceptor::Settings setts() {
		crab::TCPAcceptor::Settings result;
		result.reuse_addr = true;
		result.reuse_port = true;
		result.tcp_delay  = false;
		return result;
	}
	explicit ApiServerApp(const crab::Address &bind_address)
	    : stop([&]() { stop_network(); }), network(workers, bind_address, setts()) {
//		for (size_t i = 0; i != 3; ++i)
//			network_threads.emplace_back([this, bind_address]() {
//				ApiNetwork network2(workers, bind_address, setts());
//
//				crab::RunLoop::current()->run();
//			});
		//		timer.once(10);
	}

private:
	void stop_network() {
		std::cout << "Signal Stop Received" << std::endl;
		for (auto &th : network_threads)
			th.cancel();
		crab::RunLoop::current()->cancel();
	}
	crab::Signal stop;  // Must be created before other threads
	ApiWorkers workers;
	ApiNetwork network;
	std::list<crab::Thread> network_threads;
	//	crab::Timer timer;
};

int main(int argc, char *argv[]) {
	std::cout << "crablib version " << crab::version_string() << std::endl;

	std::cout << "This server responds to requests from bunch of api_client via TCP in fair manner -" << std::endl;
	std::cout << "    clients are served in round-robin fashion" << std::endl;
	std::cout << "    there is upper bound for all resources server uses" << std::endl;
	if (argc < 2) {
		std::cout << "Usage: api_server <port>" << std::endl;
		return 0;
	}
	{
		crab::RunLoop runloop;

		ApiServerApp app(crab::Address("0.0.0.0", crab::integer_cast<uint16_t>(argv[1])));

		runloop.run();
	}
	std::cout << "Good Bye" << std::endl;
	return 0;
}
