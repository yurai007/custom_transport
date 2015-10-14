#ifndef EPOLL_SERVER_HPP
#define EPOLL_SERVER_HPP

#include "custom_transport.hpp"
#include "byte_buffer.hpp"
#include "message_dispatcher.hpp"
#include "logger.hpp"

//namespace my_boost
//{
//	namespace
//	{
//		class placeholder
//		{
//		};

//		placeholder _1;
//	}

//	template<class R, class T, class Arg>
//	class binder
//	{
//		// fn - pointer to method Arg -> R in context T
//		typedef R (T::*fn)(Arg);

//	public:
//		binder(fn method, T context)
//			: method_(method),
//			  context_(context)
//		{
//		}

//		R operator ()(Arg &arg)
//		{
//			return (context_.*method_)(arg);
//		}

//	private:
//		fn method_;
//		T context_;
//	};

//	template<class R, class T, class Arg>
//	binder<R, T, Arg> my_bind(R (T::*method)(Arg), const T &context, const placeholder &)
//	{
//		return binder<R, T, Arg>(method, context);
//	}
//}

// TO DO: Finish epoll_server. I need byte_buffer (reinterpret_cast from connection->data?), logger and message_dispatcher.
// That should be different, separated from maze project

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
    //void add_dispatcher(std::shared_ptr<message_dispatcher> dispatcher);
    void run();
    //void stop();
    //void remove_connection(std::shared_ptr<connection> connection_);
	void send_on_current_connection(const serialization::byte_buffer &data);
    //void dispatch_msg_from_buffer(serialization::byte_buffer &buffer);

private:
	static void write_handler(int bytes_transferred, connection_data *connection);
	static void read_handler(int bytes_transferred, connection_data *connection);
	static void accept_handler(int error, connection_data *connection,
                   const char *address, const char *port);
	// TO DO: connection_data *current_connection; epoll_server has state now/fields so I need better bind
};

#endif // EPOLL_SERVER_HPP
