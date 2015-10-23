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
		logger_.log("Connection established");
	}

	void send(const std::string &msg)
	{
		boost::system::error_code error;

		size_t send_bytes = boost::asio::write(socket, boost::asio::buffer(msg, msg.size()), error);
		assert(send_bytes > 0);
		assert(!error);
		logger_.log("Sent %d bytes", send_bytes);
	}

	std::string read()
	{
		boost::system::error_code error;
		std::array<char, max_buffer_size> data;

		size_t recieved_bytes = socket.read_some(boost::asio::buffer(data), error);
//		TO DO: why does it block?
//		size_t recieved_bytes = boost::asio::read(socket, boost::asio::buffer(data), error);
		assert(recieved_bytes > 0 && recieved_bytes <= max_buffer_size );
		assert(!error);
		logger_.log("Recieved %d bytes", recieved_bytes);
		std::string result(data.begin(), data.begin() + recieved_bytes);
		return result;
	}

	boost::asio::io_service io_service;
	tcp::resolver resolver {io_service};
	tcp::socket socket {io_service};
	constexpr static int max_buffer_size = 128;
};

using namespace boost::process;

void dummy_test1()
{
	// current version - 0.5:
	// ref: http://www.highscore.de/boost/process0.5/boost_process/tutorial.html#boost_process.tutorial.synchronous_i_o

	// TO DO: I need zip from http://stackoverflow.com/questions/1683665/where-is-boost-process

	// TO DO: compilation error for boost::process classes - can't find execute
	// execute(run_exe("../echo_server/echo_server 5555"));

	// start server
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

void tests()
{
	dummy_test1();
	dummy_test2();
	logger_.log("All tests passed");
}

}

int main(int argc, char* argv[])
{
	echo_server_component_tests::tests();
	return 0;
}

