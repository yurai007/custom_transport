#ifndef EPOLL_SERVER_HPP
#define EPOLL_SERVER_HPP

#include <memory>

#include "custom_transport.hpp"
#include "byte_buffer.hpp"
#include "message_dispatcher.hpp"
#include "logger.hpp"

namespace my_boost
{
	namespace
	{
		class placeholder
		{
		};

		placeholder _1;
	}

	template<class R, class T, class... Args>
	class binder
	{
		// fn - pointer to method Arg -> R in context T
		typedef R (T::*fn)(Args...);

	public:
		binder(fn method, T context)
			: method_(method),
			  context_(context)
		{
		}

		R operator ()(Args... args)
		{
			return (context_.*method_)(args...);
		}

	private:
		fn method_;
		T context_;
	};

	template<class R, class T, class... Args>
	binder<R, T, Args...> my_bind(R (T::*method)(Args...),
										 const T &context, const placeholder &)
	{
		return binder<R, T, Args...>(method, context);
	}
}

struct private_data
{
	serialization::byte_buffer data_buffer;
	int remaining_bytes {0};
	int current {0};
};

class epoll_server //singleton - global variables
{
public:
    epoll_server(int port);
	void add_dispatcher(std::shared_ptr<networking::message_dispatcher> dispatcher);
    void run();
    //void stop();
	void send_on_current_connection(const serialization::byte_buffer &data);

private:
	void write_handler(int bytes_transferred, connection_data *connection);
	// Not static anymore:)
	void read_handler(int bytes_transferred, connection_data *connection);
	void accept_handler(int error, connection_data *connection,
                   const char *address, const char *port);

	connection_data *current_connection;
	std::shared_ptr<networking::message_dispatcher> dispatcher;
};

#endif // EPOLL_SERVER_HPP
