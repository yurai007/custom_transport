#include "custom_transport.hpp"
#include "logger.hpp"
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <assert.h>

t_accept_handler global_accept_handler = NULL;
t_read_handler global_read_handler = NULL;
t_write_handler global_write_handler = NULL;

int server_fd = 0, epoll_fd = 0;
epoll_event *events = NULL;

static void check_errors(const char *message, int result)
{
    if (result < 0)
    {
        perror(message);
        exit(-1);
    }
}

static int make_socket_non_blocking (int sfd)
{
    int flags = fcntl (sfd, F_GETFL, 0);
    check_errors("fcntl", flags);

    flags |= O_NONBLOCK;
    int s = fcntl (sfd, F_SETFL, flags);
    check_errors("fcntl", s);
    return 0;
}

static void reallocate_buffer_with_increased_size(connection_data *connection)
{
	size_t new_size = 2 * connection->size;
	assert(new_size <= MAXLEN);

	connection->data = (char *) realloc(connection->data, new_size);
	assert(connection->data != NULL);
	connection->size = new_size;
}

static connection_data *allocate_connection(int client_fd)
{
	connection_data* connection = (connection_data*) calloc(1, sizeof(connection_data));
	assert(connection->from == 0);

	connection->size = STARTLEN;
	connection->data = (char*) calloc(1, connection->size);
	connection->fd = client_fd;
	return connection;
}

static void free_connection(connection_data *connection)
{
	close(connection->fd);
	free(connection->data);
	free(connection);
	connection = NULL;
}

static void modify_epoll_context(int epoll_fd, int operation, int client_fd, uint32_t events, void *data)
{
    epoll_event event;
    event.events = events | EPOLLET;
    event.data.ptr = data;

    int return_code = epoll_ctl(epoll_fd, operation, client_fd, &event);
    check_errors("epoll_ctl", return_code);
}

static int resolve_name_and_bind (int port)
{
    sockaddr_in server_addr;

    int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    check_errors("socket", server_fd);

    bzero(&server_addr, sizeof(server_addr));

	server_addr.sin_family = AF_INET; // IPv4 only
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int return_code = bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr));
    check_errors("bind", return_code);

    return server_fd;
}

static void handle_reading_data_from_event(connection_data *connection)
{
	assert((size_t)connection->from <= connection->size);
	int n = read(connection->fd, connection->data + connection->from, connection->size - connection->from);

    if(n == 0)
    {
		free_connection(connection);

        if (global_read_handler != NULL)
			global_read_handler(n, NULL);
    }
    else
    {
        assert(n > 0);
        connection->length = n;

        if (global_read_handler != NULL)
			global_read_handler(n, connection);
    }
}

static void handle_writing_data_to_event(connection_data *connection)
{
	assert(connection->length + connection->from <= (int)connection->size);
	int n = write(connection->fd, connection->data + connection->from, connection->length);

    assert( !((n == -1 && errno == EINTR) || (n < connection->length)) );
    if(n == -1)
    {
		free_connection(connection);

        if (global_write_handler != NULL)
			global_write_handler(n, NULL);
    }
    else
    {
        if (global_write_handler != NULL)
			global_write_handler(n, connection);
    }
}

static void handle_closing(connection_data *connection)
{
	logger_.log("Client associated with socket %d is gone...", connection->fd);
	free_connection(connection);
}

static void handle_server_closing(int server_fd)
{
	logger_.log("Server associated with socket %d is gone...", server_fd);
    close (server_fd);
}

static void handle_accepting_connection(int server_fd)
{
    sockaddr_in clientaddr;
    socklen_t clientlen = sizeof(clientaddr);

    int client_fd = accept(server_fd, (sockaddr*)&clientaddr, &clientlen);
    check_errors("accept", client_fd);

    char client_address[NI_MAXHOST], client_port[NI_MAXSERV];
    int error_code = getnameinfo ((sockaddr*)&clientaddr, clientlen,
                                  client_address, sizeof client_address,
                                  client_port, sizeof client_port,
                                  NI_NUMERICHOST | NI_NUMERICSERV);

    make_socket_non_blocking(client_fd);
	connection_data *connection = allocate_connection(client_fd);

    if (global_accept_handler != NULL)
		global_accept_handler(error_code, connection, client_address, client_port);
}

void init(int port)
{
    server_fd = resolve_name_and_bind(port);

    int return_code = listen (server_fd, MAXCONN);
    check_errors("listen", return_code);

	epoll_fd = epoll_create (1);
    check_errors("epoll_create", epoll_fd);

    modify_epoll_context(epoll_fd, EPOLL_CTL_ADD, server_fd, EPOLLIN, &server_fd);
    events = (epoll_event *)calloc(MAXEVENTS, sizeof(epoll_event));
	logger_.log("Waiting for connections on port = %d...", port);
}

void run()
{
    while(true)
    {
        int n = epoll_wait(epoll_fd, events, MAXEVENTS, -1);
        check_errors("epoll_wait", n);

        for(int i = 0; i < n; i++)
        {
            if(events[i].data.ptr == &server_fd)
            {
                if(events[i].events & EPOLLHUP || events[i].events & EPOLLERR)
                {
                    handle_server_closing(server_fd);
                    break;
                }

				handle_accepting_connection(server_fd);
            }
            else
            {
                if(events[i].events & EPOLLHUP || events[i].events & EPOLLERR)
                {
                    connection_data* connection = (connection_data*) events[i].data.ptr;
					handle_closing(connection);
                }
                else if(EPOLLIN & events[i].events)
                {
                    connection_data* connection = (connection_data*) events[i].data.ptr;

                    connection->event = EPOLLIN;
                    modify_epoll_context(epoll_fd, EPOLL_CTL_DEL, connection->fd, 0, 0);

					handle_reading_data_from_event(connection);
                }
                else if(EPOLLOUT & events[i].events)
                {
                    connection_data* connection = (connection_data*) events[i].data.ptr;

                    connection->event = EPOLLOUT;
                    modify_epoll_context(epoll_fd, EPOLL_CTL_DEL, connection->fd, 0, 0);

					handle_writing_data_to_event(connection);
                }
            }
        }
    }

    free(events);
	events = NULL;
}

void async_accept( t_accept_handler accept_handler )
{
    global_accept_handler = accept_handler;
}

/*
 * Reads all current available data in kernel for connection to connection buffer. There is no message concept
   so from sender POV all data may be send (by async_write) in one call but from reciever POV there may be
   need to perform many async_read (and vice versa). If caller won't copy data from connection or
   won't move connection->from next async_read overwrite previous data in buffer.
 */
void async_read( t_read_handler read_handler, connection_data *connection)
{
    assert(connection != NULL && epoll_fd != 0);
    modify_epoll_context(epoll_fd, EPOLL_CTL_ADD, connection->fd, EPOLLIN, connection);
    global_read_handler = read_handler;
}

/*
 * Writes all data available in connection buffer (connection->length bytes) to kernel. There is no message concept
   so from sender POV all data may be send in many calls (by async_write) but from reciever POV only one async_read
   may be sufficient (and vice versa). If caller won't move connection->from and connection->length
   next async_write send excatly the same data (but as I noticed behaviour on receiver side may be different).
 */
void async_write( t_write_handler write_handler, connection_data *connection)
{
    assert(connection != NULL && epoll_fd != 0);
    modify_epoll_context(epoll_fd, EPOLL_CTL_ADD, connection->fd, EPOLLOUT, connection);
    global_write_handler = write_handler;
}
