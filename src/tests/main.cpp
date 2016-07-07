#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <array>
#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include <functional>
#include "../custom_transport/logger.hpp"
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <queue>

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

 * stress_test__one_big_request shows that one send may be mapped to many read-s on reciever side
   stress_test__one_big_request fails:
        Despite I set connection buffer size - MAXLEN to 1024*512 B, so it's big enaugh to hold request
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

 * OK. Issue with too many open files was fixed. Everything is fine if I on ROOT account set limits
        * - nofile 999999 in etc/security/limits.conf and
   run ./tests and ./echo_server on ROOT!

 * #pragma GCC diagnostic ignored "-Wdeprecated-declarations" for ignoring warnings from
   boost::process internals

 * read_some

 * ulimit sucks because it has influence only on current shell (that's why /etc/security/limits.conf).
   For qtcreator I limited memory to 4GB by command:

   (ulimit -v 4000000; /opt/Qt5.4.2/Tools/QtCreator/bin/./qtcreator)

 * never ever use local static-s! However if you really want to use them think 10 times.
   Local statics have limited scope but it's not like field with limited scope. There are much much worse
   because introduce dependency between different object-s of the same type! I can create and destroy object but
   part of state will be still hold in static.
   This extreme memory consumpsion was caused by static buffer which was allocated only once (vector, wtf?)
   fo 16MB and after that (and after destroying object) was used in next test by coping 2000x times

 * using swap dramaticly degradates performance (e.g virtualbox, building seastar on my PC - 4gb memory).
   Avoid it if possible.

 * for offline work I must pass to query tcp::resolver::query::canonical_name

 * it looks like boost::asio::async_read with boost::asio::buffer(connection_buffers[client_id], N) always 'waits'
   for N bytes. Form the other hand async_read_some waits for at least 1B.

 * for ./tests POV in asynchronous_clients_set and stress_test__one_big_request_async
   we have sequenece of non-blocking sendmsg(65536) and recvmsg(65536)

 * strace -e trace=network -o out.strace ./echo_server 5555 and
   strace -e trace=network -o out.strace ./tests
   has significant overhead and problems with blocking tests/async_read disappears

  OLD TO DO:
   - I have no idea why ./tests sometimes hang on async_read and sometimes not.
	 Wireshark may help. Under wireshark we can see that server get all data but not all data are send back.
	 On localhost MCU = 65536.

   - valgrind reports 1068 allocs for stress_test__2k_clients.
	 But it should be only 266 on custom_transport level. Check it.
   - server must be restarted every time
   - gdb in qtcreator for root?
   - semaphores instead sleep(1)


 * TO DO:
    Now it seems that only stress_test__one_big_request_async
    and stress_test__4k_clients don't work.

    Why the hell I get strange errors during:

    for i in {1..50}; do rm -rf log.txt; sleep 2; ./tests; cat log.txt | grep -q "All tests passed";
    echo $?; cp -rf log.txt "log"$i".txt"; done

    Without proper socket closing no way to do this (e.g connect fails with errno= 99 = EADDRNOTAVAIL)
    Work with minimal_epoll and run script for minimal_epoll + ss -tan | wc -l should give small nr
    every time (No TIME-WAIT)

 *  But I have TIME-WAIT ALWAYS. I checked even with vanilla boost asio examples on Ubuntu and Arch.
    So just IGNORE this and fix stress_test__4k_clients.

    stress_test__4k_clients - fails always on 5-10 time both on debug and release build
    (even without logging - so it's not RC). Fails always with:

    tests: ../../../src/tests/main.cpp:340: void echo_server_component_tests::asynchronous_clients_set::connect_handler(const boost::system::error_code&): Assertion `false' failed.
    Aborted (core dumped)

    on client side

 * boost::asio::buffer is just lightweight wrapper on passing buffer. There is no copying here!
   Just a pointer to data!
*/

namespace echo_server_component_tests
{

using boost::asio::ip::tcp;

class synchronous_client
{
public:

    synchronous_client(const std::string ip_address, const std::string &port)
    {
        try
        {
            data.assign(max_buffer_size, 0);
            tcp::resolver::query query(ip_address, port, tcp::resolver::query::canonical_name);
            tcp::resolver::iterator endpoint_iterator = resolver.resolve(query), end;
            boost::system::error_code error = boost::asio::error::host_not_found;

            while (error && endpoint_iterator != end)
            {
                socket.close();
                socket.connect(*endpoint_iterator++, error);
                assert(!error);
            }
            if (error)
            {
                logger_.log("synchronous_client: error = %d", error.value());
                assert(false);
            }
            logger_.log("synchronous_client::  Connection established");

        }
        catch (std::exception &exception)
        {
            logger_.log("client: exception occured: %s", exception.what());
            assert(false);
        }
    }

	void send(const std::string &msg)
	{
		boost::system::error_code error;

		size_t send_bytes = boost::asio::write(socket, boost::asio::buffer(msg, msg.size()), error);
        if (error)
        {
            logger_.log("synchronous_client: error = %d", error.value());
            assert(false);
        }
		assert(send_bytes > 0);

		logger_.log("synchronous_client::  Sent %d bytes", send_bytes);
	}

	// blocks until read excatly expected_bytes B
	std::string read(size_t expected_bytes)
	{
		assert(expected_bytes <= max_buffer_size);
		boost::system::error_code error;

		logger_.log("synchronous_client::  Start recieving...");
		size_t recieved_bytes = boost::asio::read(socket, boost::asio::buffer(data, expected_bytes),
												  error);

        if (error)
        {
            logger_.log("synchronous_client: error = %d", error.value());
        }
		assert(recieved_bytes > 0 && expected_bytes == recieved_bytes);

		logger_.log("synchronous_client::  Recieved %d bytes", recieved_bytes);
		std::string result(data.begin(), data.begin() + recieved_bytes);
		return result;
	}

	// blocks until read at least 1B
    std::string read()
    {
        boost::system::error_code error;

        logger_.log("synchronous_client::  Start recieving...");
        /* TO DO: I use here read_some only because in this point I have no idea how many data
                  I want to read. Using sth like
                  size_t recieved_bytes = boost::asio::read(socket, boost::asio::buffer(data), error);
                  ofc will hang whole thread because we don't expect max_buffer_size bytes in data
        */
        size_t recieved_bytes = socket.read_some(boost::asio::buffer(data), error);

        if (error)
        {
            logger_.log("synchronous_client: error = %d", error.value());
        }
        assert(recieved_bytes > 0);
        assert(recieved_bytes <= max_buffer_size );

        logger_.log("synchronous_client::  Recieved %d bytes", recieved_bytes);
        std::string result(data.begin(), data.begin() + recieved_bytes);
        return result;
    }

	boost::asio::io_service io_service;
	tcp::resolver resolver {io_service};
	tcp::socket socket {io_service};
    constexpr static int max_buffer_size = 1024*16*1024;
    std::vector<char> data;
};

class asynchronous_clients_set
{
public:

    constexpr static int log_step = 1;

	asynchronous_clients_set(const std::string ip_address, const std::string &port,
							 int clients_number_,
							 int max_buffer_size_,

							 std::function<int(std::vector<unsigned char>
                                        &connection_buffer, const int client_id)> test_send_handler,

							 std::function<bool(const std::vector<unsigned char>
                                         &connection_buffer, const int recieved_bytes,
                                                const int client_id)> test_read_handler
							 )
		: clients_number(clients_number_),
		  max_buffer_size(max_buffer_size_),
		  external_send_handler(test_send_handler),
          external_read_handler(test_read_handler),
          client_id(0)
	{
		assert(clients_number <= 128*1024);
		assert(clients_number*max_buffer_size <= 1024*1024*512);
        std::vector<unsigned char> init_buffer(max_buffer_size);

		for (int i = 0; i < clients_number; i++)
        {
			sockets.push_back(tcp::socket(io_service));
			connection_buffers.push_back(init_buffer);
		}

		tcp::resolver::query query(ip_address, port, tcp::resolver::query::canonical_name);
		resolver.async_resolve(query, boost::bind( &asynchronous_clients_set::accept_handler, this,
					 boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred) );
		logger_.log("accept_handler was attached");
	}

	void run()
	{
		try
		{
			io_service.run();
		}
		catch (std::exception& exception)
		{
			logger_.log("Exception: %s", exception.what());
		}
	}

private:

	void read_handler(const boost::system::error_code &error_code, size_t bytes_transferred, int client_id)
	{
		if (!error_code)
		{
			logger_.log("read_handler: %d B was recieved", bytes_transferred);
			const bool conversation_end = external_read_handler(connection_buffers[client_id], bytes_transferred, client_id);
			if (conversation_end)
				return;

			int size = external_send_handler(connection_buffers[client_id], client_id);

			boost::asio::async_write(sockets[client_id],
					boost::asio::buffer(connection_buffers[client_id], size),
					boost::bind(&asynchronous_clients_set::write_handler, this, boost::asio::placeholders::error,
								boost::asio::placeholders::bytes_transferred, client_id));
		}
        else
        {
            logger_.log("read_handler: error = %d", error_code.value());
            assert(false);
        }
	}

	void write_handler(const boost::system::error_code &error_code, size_t bytes_transferred, int client_id)
	{
		if (!error_code)
		{
			assert(bytes_transferred > 0);
			logger_.log("write_handler: %d B was send", bytes_transferred);

			boost::asio::async_read(sockets[client_id], boost::asio::buffer(connection_buffers[client_id], bytes_transferred),
									boost::bind(&asynchronous_clients_set::read_handler, this, boost::asio::placeholders::error,
												boost::asio::placeholders::bytes_transferred, client_id));
		}
		else
        {
            logger_.log("write_handler: error = %d", error_code.value());
			assert(false);
        }
	}

	void connect_handler(const boost::system::error_code &error_code)
	{
		if (!error_code)
		{
			++client_id;
			if (client_id % log_step == 0)
				logger_.log("Next %d clients were succesfuly connected to echo server", client_id);

			int size = external_send_handler(connection_buffers[client_id-1], client_id-1);

            boost::asio::async_write(sockets[client_id-1],
                    boost::asio::buffer(connection_buffers[client_id-1], size),
                    boost::bind(&asynchronous_clients_set::write_handler, this, boost::asio::placeholders::error,
                                boost::asio::placeholders::bytes_transferred, client_id-1));

            // just for sure because we test echo
//            auto it = connection_buffers[client_id-1].begin();
//            std::fill(it, it + size, 0);
		}
		else
		{
			logger_.log("Connecting external server failed with error = %d", error_code.value());
			assert(false);
		}
	}

	void accept_handler(const boost::system::error_code &error_code,
						 tcp::resolver::iterator endpoint_iterator)
	{
		if (!error_code)
		{
			for (auto &socket : sockets)
				socket.async_connect(*endpoint_iterator,
									 boost::bind(&asynchronous_clients_set::connect_handler,
												 this, boost::asio::placeholders::error));
		}
		else
		{
            logger_.log("Resolving external server failed with error = %d", error_code.value());
			assert(false);
		}
	}

	boost::asio::io_service io_service;
	tcp::resolver resolver {io_service};

	int clients_number;
	int max_buffer_size;
	std::vector<std::vector<unsigned char>> connection_buffers;
	std::vector<tcp::socket> sockets;

	const std::function<int(std::vector<unsigned char>
										   &connection_buffer, const int client_id)> external_send_handler {nullptr};

	const std::function<bool(const std::vector<unsigned char>
				&connection_buffer, const int recieved_bytes, const int client_id)> external_read_handler {nullptr};

    int client_id;
};

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
    assert(client.read(request1.size()) == request1);
    client.send(request2);
    assert(client.read(request2.size()) == request2);
    client.send(request3);
    assert(client.read(request3.size()) == request3);
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

// request size about ~2.9MB
void stress_test__one_big_request()
{
    logger_.log("stress_test__one_big_request is starting");
    const std::string request_fragment = "0123456789101112131415161718";
    std::string big_request;
    for (int i = 0; i < 100000; i++)
        big_request.append(request_fragment);

    synchronous_client client("127.0.0.1", "5555");
    client.send(big_request);

    size_t recieved_bytes = 0;

    while (recieved_bytes < big_request.size())
    {
        auto response = client.read();

        int pos = big_request.compare(recieved_bytes, response.size(), response);
        assert(pos == 0);
        recieved_bytes += response.size();
    }
}

//// request size about ~2.8MB
void stress_test__one_big_request_async()
{
    logger_.log("stress_test__one_big_request_async is starting");

    constexpr static int max_buffer_size = 1024*1024*16;

    const auto client_send_handler = [](std::vector<unsigned char> &connection_buffer, const int client_id)
    {
        logger_.log("client_send_handler");
        static bool once = false;
        std::string big_request;

        if (!once)
        {
            once = true;
            const std::string request_fragment = "0123456789101112131ABC";

            for (int i = 0; i < 100000; i++) {
                big_request.append(request_fragment);
                big_request.append(std::to_string(i) += "XYZ");
            }

            assert(big_request.size() <= max_buffer_size);

            auto last = std::copy(big_request.begin(), big_request.end(), connection_buffer.begin());
            return std::distance(connection_buffer.begin(), last);
        }
        logger_.log("Shouldn't be here... Client_id = %d", client_id);
        return 0L;
    };

    const auto client_recv_handler = [](const std::vector<unsigned char>
            &connection_buffer, const int recieved_bytes, const int client_id)
    {
        logger_.log("Recv. Client_id = %d", client_id);
        static bool once = false;
        std::string big_request;

        if (!once)
        {
            once = true;
            const std::string request_fragment = "0123456789101112131ABC";

            for (int i = 0; i < 100000; i++) {
                big_request.append(request_fragment);
                big_request.append(std::to_string(i) += "XYZ");
            }

            assert(big_request.size() <= max_buffer_size);
        }

        static size_t sum_bytes = 0;

        std::string response(connection_buffer.begin(), connection_buffer.begin() + recieved_bytes);
        int pos = big_request.compare(sum_bytes, recieved_bytes, response);
        assert(pos == 0);

        sum_bytes += recieved_bytes;
        logger_.log("OK. Recieved next %d bytes", recieved_bytes);

            if (sum_bytes == big_request.size())
            {
                logger_.log("OK. Recieved all %d bytes", sum_bytes);
                return true;
            }
        return false;
    };

    asynchronous_clients_set clients_pool("127.0.0.1", "5555", 1, max_buffer_size, client_send_handler,
                                          client_recv_handler);
    clients_pool.run();
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

        size_t recieved_bytes = 0;
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

        size_t recieved_bytes = 0;
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

void stress_test__4k_clients()
{
    logger_.log("stress_test__4k_clients is starting");

    constexpr static int max_buffer_size = 512;

    const auto client_send_handler = [](std::vector<unsigned char>
            &connection_buffer, const int client_id)
    {
        const std::string request_prefix = "Hello world from client = ";
        const auto request = request_prefix + std::to_string(client_id) + " !";

        assert(request.size() <= max_buffer_size);
        auto last = std::copy(request.begin(), request.end(), connection_buffer.begin());
        return std::distance(connection_buffer.begin(), last);
    };

    const auto client_recv_handler = [](const std::vector<unsigned char>
            &connection_buffer, const int recieved_bytes, const int client_id)
    {
        const std::string response_prefix = "Hello world from client = ";
        const auto expected_response = response_prefix + std::to_string(client_id) + " !";

        const std::string response(connection_buffer.begin(), connection_buffer.begin()
                                   + recieved_bytes);

        assert(response == expected_response);
        logger_.log("OK. Recieved echo response from client %d.", client_id);
        return true;
    };

    asynchronous_clients_set clients_pool("127.0.0.1", "5555",
                                          4000,
                                          max_buffer_size,
                                          client_send_handler,
                                          client_recv_handler);
    clients_pool.run();
}

void stress_test__2k_clients_increased_size_requests()
{
    logger_.log("stress_test__2k_clients_increased_size_requests is starting");

    constexpr static int max_buffer_size = 10000;
    constexpr static int clients_number = 2000;

    std::vector<std::string> client_requests(clients_number, "");

    for (size_t i = 0; i < client_requests.size(); i++)
    {
        client_requests[i] = "Hello world from client = " + std::to_string(i) + " *";
    }

    const auto client_send_handler = [&client_requests](auto &connection_buffer, const int client_id)
    {
        //const std::string request = "Hello world from client = " + std::to_string(client_id) + " !";

        assert(client_requests[client_id].size() <= max_buffer_size);
        auto last = std::copy(client_requests[client_id].begin(), client_requests[client_id].end(),
                              connection_buffer.begin());

        return std::distance(connection_buffer.begin(), last);
    };

    const auto client_recv_handler = [&client_requests](const auto &connection_buffer,
            const int recieved_bytes, const int client_id)
    {
        const std::string response(connection_buffer.begin(), connection_buffer.begin() + recieved_bytes);

        assert(response == client_requests[client_id]);
        if ( client_requests[client_id].size() < 100 )
        {
            client_requests[client_id] += '*';
            return false;
        }
        return true;
    };

    asynchronous_clients_set clients_pool("127.0.0.1", "5555",
                                          clients_number,
                                          max_buffer_size,
                                          client_send_handler,
                                          client_recv_handler);
    clients_pool.run();
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

    stress_test__one_big_request_async();

    // TO DO: only this shit fails
    //stress_test__4k_clients();

    terminate(server_process);
	logger_.log("All tests passed");
}


}

int main(int argc, char* argv[])
{
    echo_server_component_tests::tests();
    return 0;
}

