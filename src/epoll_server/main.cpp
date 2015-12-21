#include "custom_transport.hpp"
#include "epoll_server.hpp"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/*
   Custom transport as simple library replacement for boost::asio.
   Under the hood simple TCP epoll server + callbacks.
   ref: http://byteandbits.blogspot.com/2013/08/tcp-echo-server-using-epoll-example-for.html

 * Event-driven design. State machine. The only place where we block is epoll_wait.

 * calloc = malloc + memset(0)
 * When I have client and I break server by SIGINT then I can't bind again by server.
   I get bind error: Address already in use. The reason is that socket (even closed on server side)
   is in TIME_WAIT state (for TCP by ~4 minutes). Remedium is using
   SO_REUSEADDR option for the socket by setsockopt (nc do that:)).
   ss -tan | grepc [port] - shows sockets in TIME_WAIT state

 * For TCP non-blocking write/read with EPOLLET (edge-triggered) mode
   1. TCP + EPOLLET guarantes that read always reads all available data so there is no sense to
      call read again in loop until return_code == -1 and errno == EAGAIN
      (We know that second call always return EAGAIN). Ref:
      http://man7.org/linux/man-pages/man7/epoll.7.html
      Without TCP we would be forced to doing that in while(true) because of
      edge-trigged mode (we must read all data,
      because we won't get notification again about same data. Next event will overrite old data) like here:
      https://banu.com/blog/2/how-to-use-epoll-a-complete-example-in-c/
      All I said before would be true but signals...
   2. Fortunately non-blocking call read/write guarantes that no interruption happen and no EINTR will
      be returned. Ref: http://stackoverflow.com/questions/14134440/eintr-and-non-blocking-calls

 * epoll_create argument is ignored since linux 2.6.8 but must be grater than 0

 * Ctrl+C -> SIGINT
 * Send return n bytes <-> n bytes were copyied to TCP/IP stack (a'ka sk_buffer)
   Ref: http://stackoverflow.com/questions/5106674/error-address-already-in-use-while-binding-socket-with-address-but-the-port-num

 * Name resolving in resolve_name_and_bind works only for IPv4 (sin_family = AF_INET) address.
   So using getaddrinfo + iterating over result list would be redundant IMO.

 * code bloat from std::function reason occurs only in debug build with symbols (350kB). For -Ofast without -g
   binary size is only ~30kB so as small as without std::function

 * std::function solves problem with bind to function pointer conversion
*/

//void read_handler(int bytes_transferred, connection_data *connection);

//void write_handler(int bytes_transferred, connection_data *connection)
//{
//    if (bytes_transferred >= 0)
//    {
//        printf("Send all data to client socket = %d: %s", connection->fd, connection->data);
//        async_read(read_handler, connection);
//    }
//}

//void read_handler(int bytes_transferred, connection_data *connection)
//{
//    if(bytes_transferred == 0)
//    {
//        printf("Client closed connection. Detected in read_handler\n");
//    }
//    else
//    {
//        printf("Data from client socket = %d: %s", connection->fd, connection->data);
//        // echo server so we don't touch connection->data and send back in current form
//        async_write(write_handler, connection);
//    }
//}

//void accept_handler(int error, connection_data *connection,
//                    const char *address, const char *port)
//{
//    if (error == 0)
//    {
//        printf("Accepted connection on descriptor %d "
//               "(host=%s, port=%s)\n", connection->fd, address, port);
//        async_read(read_handler, connection);
//    }
//    else
//    {
//        printf("Connection accepting failed\n");
//    }
//}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        printf("Usage: %s [port]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
	epoll_server server(atoi(argv[1]));
	server.run();
    return 0;
}
