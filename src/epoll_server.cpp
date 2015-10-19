#include "epoll_server.hpp"
#include "logger.hpp"

void epoll_server::write_handler(int bytes_transferred, connection_data *connection)
{
	if (bytes_transferred >= 0)
	{
		logger_.log("server: connection on socket = %d: sent %d B", connection->fd, bytes_transferred);
		connection->from = 0;
		async_read(my_boost::my_bind(&epoll_server::read_handler, *this, my_boost::_1), connection);
	}
	else
	{
		logger_.log("server: connection on socket = was removed");
	}
}

void epoll_server::read_handler(int bytes_transferred, connection_data *connection)
{
	if(bytes_transferred == 0)
	{
		logger_.log("server: read_handler; connection on socket = was removed");
	}
	else
	{
		current_connection = connection;
		// no problem with strict aliasing because connection->data type is char*
		private_data *data = reinterpret_cast<private_data*>(connection->data);
		unsigned char msg_length = 0;

		if (data->remaining_bytes == 0)
		{
			msg_length = data->data_buffer.m_byte_buffer[0];
			data->remaining_bytes =  msg_length + 1 - bytes_transferred;
			data->current = bytes_transferred;
		}
		else
		{
			data->remaining_bytes -= bytes_transferred;
			data->current += bytes_transferred;
		}

		if (data->remaining_bytes > 0)
		{
			logger_.log("server: connection on socket = %d: recieved %d B and expected %d B. Waiting for next %d B",
										 connection->fd, bytes_transferred,
										 msg_length + 1, data->remaining_bytes);
			// TO DO: I need buffer(&data_buffer.m_byte_buffer[current], remaining_bytes)
			// so I want read to arbitrary choosed point in data and arbitrary number of bytes.
			// But what if I will send on client side 1GB of data. ET must read all data but I only read
			// MAXLEN/arbitrary bytes number. Is it OK? What about lost of data?
			connection->from = data->current;
			async_read(my_boost::my_bind(&epoll_server::read_handler, *this, my_boost::_1), connection);
		}
		else
		{
			assert(dispatcher != nullptr);
			logger_.log("server: connection on socket = %d: recieved %d B and expected %d B. Got full msg",
						connection->fd, bytes_transferred, bytes_transferred);

			data->data_buffer.offset = 1;
			dispatcher->dispatch_msg_from_buffer(data->data_buffer);
		}
	}
}

void epoll_server::accept_handler(int error, connection_data *connection,
                    const char *address, const char *port)
{
    if (error == 0)
    {
		logger_.log("Accepted connection on descriptor %d "
			   "(host=%s, port=%s)", connection->fd, address, port);
		async_read(my_boost::my_bind(&epoll_server::read_handler, *this, my_boost::_1), connection);
    }
    else
    {
		logger_.log("Connection accepting failed");
    }
}

epoll_server::epoll_server(int port)
	: current_connection(nullptr),
	  dispatcher(nullptr)
{
	async_accept(my_boost::my_bind(&epoll_server::accept_handler, *this, my_boost::_1));
	init(port);
}

void epoll_server::add_dispatcher(std::shared_ptr<networking::message_dispatcher> dispatcher)
{
	this->dispatcher = dispatcher;
}

void epoll_server::run()
{
	::run();
}

void epoll_server::send_on_current_connection(const serialization::byte_buffer &data)
{
	assert(current_connection != nullptr);
	private_data *connection = reinterpret_cast<private_data*>(current_connection->data);
	connection->data_buffer = data;

	current_connection->from = 0;
	current_connection->length = connection->data_buffer.offset;
	async_write(my_boost::my_bind(&epoll_server::write_handler, *this, my_boost::_1), current_connection);
}
