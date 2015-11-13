#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include <functional>
#include "../custom_transport/logger.hpp"

/*
 * boost::asio::read/write read/write all data and works synchronously so it's perfect for
   my synchronous_client
 * In synchronous_client::read
   we have socket.read_some(boost::asio::buffer(data), error);
   Here data can't be std::string - so I used std::array.
   Ref: http://stackoverflow.com/questions/4068249/how-to-use-stdstring-with-asiobuffer

 * Boost process is not part of boost:) Boost Process is header only so linker will be happy.
   Library is quite old and has many incompatible versions. I use version 0.5 from process.zip from SO:
   http://stackoverflow.com/questions/1683665/where-is-boost-process

 * stress_test__one_big_request fails: Despite I set connection buffer size - MAXLEN to 1024*512 B, so it's big enaugh to hold request
   echo_server sometimes gets full data (28k) and sometimes not (only 21.8k).
   Logs:

	 synchronous_client::  Sent 28000 bytes
	 synchronous_client::  Recieved 21845 bytes
	 synchronous_client::  Recieved 6155 bytes

   The reason is that TCP models bytes stream, not packet/msg stream so 1 send -> many reads and vice versa.
   .... OK I fixed that, I just call in test many reads and check if content is OK.

 * the problem is that I won't be have never big enaugh buffer on server side so situations that
   1 send map to many recv will happen. Some allocation politics is needed.

 * tests should be performed on remote machine as well

 * logger is disabled on server side (there was problem - 'All tests passed' was placed in the middle
   but not on the end of logs.

 TODO:
   - buffer allocation policy for very many clients. Starting with 512B, next allocations multiplies size by 2.
   - semaphores instead sleep(1)
 */

namespace echo_server_component_tests
{

using boost::asio::ip::tcp;

class synchronous_client
{
public:

	synchronous_client(const std::string ip_address, const std::string &port)
	{
		tcp::resolver::query query(tcp::tcp::v4(), ip_address, port);
		tcp::resolver::iterator endpoint_iterator = resolver.resolve(query), end;
		boost::system::error_code error = boost::asio::error::host_not_found;

		while (error && endpoint_iterator != end)
		{
			socket.close();
			socket.connect(*endpoint_iterator++, error);
		}
		assert(!error);
		logger_.log("synchronous_client::  Connection established");
	}

	void send(const std::string &msg)
	{
		boost::system::error_code error;

		size_t send_bytes = boost::asio::write(socket, boost::asio::buffer(msg, msg.size()), error);
		assert(send_bytes > 0);
		assert(!error);
		logger_.log("synchronous_client::  Sent %d bytes", send_bytes);
	}

	std::string read()
	{
		boost::system::error_code error;
		static std::array<char, max_buffer_size> data;

		size_t recieved_bytes = socket.read_some(boost::asio::buffer(data), error);
//		TO DO: why does it block?
//		size_t recieved_bytes = boost::asio::read(socket, boost::asio::buffer(data), error);
		assert(recieved_bytes > 0 && recieved_bytes <= max_buffer_size );
		assert(!error);
		logger_.log("synchronous_client::  Recieved %d bytes", recieved_bytes);
		std::string result(data.begin(), data.begin() + recieved_bytes);
		return result;
	}

	boost::asio::io_service io_service;
	tcp::resolver resolver {io_service};
	tcp::socket socket {io_service};
	constexpr static int max_buffer_size = 1024*1024*16;
};

//class asynchronous_clients_set
//{
//public:
//	asynchronous_clients_set(const std::string ip_address, const std::string &port,
//							 int connections_number)
//	{
//		assert(connections_number <= 10000);

//		for (int i = 0; i < connections_number; i++)
//				   sockets.push_back(tcp::socket(io_service));

//		tcp::resolver::query query(tcp::tcp::v4(), ip_address, port);
//		resolver.async_resolve(query, boost::bind( &accept_handler,
//					 placeholders::error, placeholders::bytes_transferred) );


//	}

//	void run()
//	{
//		try
//		{
//			io_service.run();
//		}
//		catch (std::exception& exception)
//		{
//			logger_.log("Exception: %s", exception.what());
//		}
//	}

//private:

//	void accept_handler(const boost::system::error_code &error_code,
//						 ip::tcp::resolver::iterator endpoint_iterator)
//	{
//		if (!error_code)
//		{
//			for (int i = 0; i < connections_per_IP; i++)
//			{
//				int socket_index = connections_per_IP * p_interface_id + i;
//				m_sockets[socket_index].async_connect(*endpoint_iterator, connect_handler);
//			}
//		}
//		else
//		{
//			logger_.log("Connection accepting failed");
//		}
//	}


//	boost::asio::io_service io_service;
//	tcp::resolver resolver {io_service};
//	std::vector<tcp::socket> sockets;
//	constexpr static int max_buffer_size = 512;
//};

using namespace boost::process;
using namespace boost::process::initializers;

void dummy_test1()
{
	logger_.log("dummy_test1 is starting");
	const std::string request1 = "Hello!";
	const std::string request2 = "World!";
	const std::string request3 = "Now";

	synchronous_client client("127.0.0.1", "5555");
	client.send(request1);
	assert(client.read() == request1);
	client.send(request2);
	assert(client.read() == request2);
	client.send(request3);
	assert(client.read() == request3);
}

void dummy_test2()
{
	logger_.log("dummy_test2 is starting");
	const std::vector<std::string> request = {"json - ", "is an open standard format",
											   "that uses human-readable text"};

	synchronous_client client1("127.0.0.1", "5555");

	client1.send(request[0]);
	assert(client1.read() == request[0]);

	synchronous_client client2("127.0.0.1", "5555");

	client2.send(request[0]);
	assert(client2.read() == request[0]);

	client1.send(request[1]);
	assert(client1.read() == request[1]);

	client2.send(request[1]);
	assert(client2.read() == request[1]);

	client1.send(request[2]);
	assert(client1.read() == request[2]);

	client2.send(request[2]);
	assert(client2.read() == request[2]);
}

// This test shows that one send may be mapped to many read-s
void stress_test__one_big_request()
{
	logger_.log("stress_test__one_big_request is starting");
	const std::string request_fragment = "0123456789101112131415161718";
	std::string big_request;
	for (int i = 0; i < 100000; i++)
		big_request.append(request_fragment);

	synchronous_client client("127.0.0.1", "5555");
	client.send(big_request);

	int recieved_bytes = 0;

	while (recieved_bytes < big_request.size())
	{
		auto response = client.read();

		int pos = big_request.compare(recieved_bytes, response.size(), response);
		assert(pos == 0);
		recieved_bytes += response.size();
	}
}

void stress_test__many_small_requests()
{
	logger_.log("stress_test__many_small_requests is starting");
	std::string request;
	synchronous_client client("127.0.0.1", "5555");

	for (int i = 0; i < 20000; i++) //TO DO: increase to 100000
	{
		request = std::to_string(i);
		client.send(request);
		assert(client.read() == request);
	}
}

void stress_test__increased_size_requests()
{
	logger_.log("stress_test__increased_size_requests is starting");
	std::string request = "*";
	synchronous_client client("127.0.0.1", "5555");

	for (int i = 0; i < 10000; i++)
	{
		client.send(request);
		assert(client.read() == request);
		request.append("*");
	}

	for (int i = 0; i < 10000; i++)
	{
		client.send(request);
		assert(client.read() == request);
		request.pop_back();
	}
}

void stress_test__increased_size_big_requests()
{
	logger_.log("stress_test__increased_size_big_requests is starting");
	synchronous_client client("127.0.0.1", "5555");

	std::string request;
	for (int i = 0; i < 40000; i++)
		request.append("*");

	for (int i = 40000; i < 41000; i++)
	{
		client.send(request);

		int recieved_bytes = 0;
		while (recieved_bytes < request.size())
		{
			auto response = client.read();

			int pos = request.compare(recieved_bytes, response.size(), response);
			assert(pos == 0);
			recieved_bytes += response.size();
		}

		request.append("*");
	}

	for (int i = 40000; i < 41000; i++)
	{
		client.send(request);

		int recieved_bytes = 0;
		while (recieved_bytes < request.size())
		{
			auto response = client.read();

			int pos = request.compare(recieved_bytes, response.size(), response);
			assert(pos == 0);
			recieved_bytes += response.size();
		}

		request.pop_back();
	}
}

void stress_test__1k_clients()
{
	// TO DO: asynchronous_clients_set is needed
}

void tests()
{
	auto server_process = execute(
				run_exe("../echo_server/echo_server"),
				set_cmd_line("../echo_server/echo_server 5555")
				);
	sleep(1);

	dummy_test1();
	dummy_test2();
	stress_test__one_big_request();
	stress_test__many_small_requests();
	stress_test__increased_size_requests();
	stress_test__increased_size_big_requests();

	terminate(server_process);

	logger_.log("All tests passed");
}

}

int main(int argc, char* argv[])
{
	echo_server_component_tests::tests();
	return 0;
}

