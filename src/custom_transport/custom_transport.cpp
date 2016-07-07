#include "custom_transport.hpp"
#include "logger.hpp"
#include <cstdio>
#include <cstring>
#include <cassert>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <errno.h>
#include <netinet/in.h>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>

#include "memory_pool.hpp"

t_accept_handler global_accept_handler = NULL;
t_read_handler global_read_handler = NULL;
t_write_handler global_write_handler = NULL;

int server_fd = 0, epoll_fd = 0;
epoll_event *events = NULL;
memory_pool *pool = NULL;
size_t connections = 0;
volatile bool interrupted = false;

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

static void reallocate_buffer_exp(buffer *data)
{
	assert(data->size == data->capacity);
	data->capacity = 2 * data->capacity;

//	data->bytes = (char *) realloc(data->bytes, data->capacity);
	data->bytes = (char *) reallocate(pool, data->capacity, data->capacity/2, data->bytes);
	assert(data->bytes != NULL);
	logger_.log("Buffer reallocation. Capacity increased from %d B to %d B", data->size,
				data->capacity);
}

static int allocate_buffer(buffer *data)
{
	data->capacity = STARTLEN;

	data->bytes = (char*) callocate(pool, data->capacity);
	assert(data->bytes != NULL);
	data->start = 0;
	data->size = 0;
	return 0;
}

static void free_buffer(buffer *)
{
}

static connection_data *allocate_connection(int client_fd)
{
    connection_data* connection = (connection_data*) callocate(pool, sizeof(connection_data));
	connection->fd = client_fd;
	allocate_buffer(&connection->data);
	return connection;
}

static void free_connection(connection_data *connection)
{
	close(connection->fd);
	free_buffer(&connection->data);
}

static void modify_epoll_context(int epoll_fd, int operation, int client_fd,
								 uint32_t events, void *data)
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
    const int opt = 1;

    int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    check_errors("socket", server_fd);

    int return_code = setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));
    check_errors("socket", return_code);

    bzero(&server_addr, sizeof(server_addr));

	server_addr.sin_family = AF_INET; // IPv4 only
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    return_code = bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr));
    check_errors("bind", return_code);

    return server_fd;
}

static void handle_reading_data_from_event(connection_data *connection)
{
	buffer *data = &connection->data;
	data->size = 0;

	while (true)
	{
		if (data->size == data->capacity)
			reallocate_buffer_exp(data);

		int n = read(connection->fd, data->bytes + data->size,
					 data->capacity - data->size);

		if (n == -1)
		{
			assert(errno == EAGAIN);
			break;
		}
		else
		if(n == 0)
		{

            logger_.log("Error during reading. Connection was closed on %d", connection->fd);
			free_connection(connection);

			if (global_read_handler != NULL)
				global_read_handler(n, NULL);
			return;
		}
		else
		{
			assert(n > 0);
			data->size += n;
		}
	}

	if (global_read_handler != NULL)
		global_read_handler(data->size, connection);
}

static bool handle_writing_data_to_event(connection_data *connection)
{
	buffer *data = &connection->data;
	assert(data->start <= data->capacity);

	while (true)
	{
		int n = write(connection->fd, data->bytes + data->start, data->size - data->start);
		//logger_.log("%d B was written", n); // <--- this is the greatest WTF I have ever seen :(

		assert( !((n == -1 && errno == EINTR)) );

		if (n != -1)
		{
			assert(n > 0 && (n <= (int)data->size) );
			data->start += n;
			if (data->start == data->size)
				break;
		}
		else
			if (n == -1)
			{
				// TO DO:
				// EAGAIN means that we cannot send more data now (because of e.g full recv buffer on client side)
				// but there are some data to send. So we should break loop but before that
				// remember state and modify epoll context for writing like:
				// modify_epoll_context(epoll_fd, EPOLL_CTL_ADD, connection->fd, EPOLLOUT, connection);

				// Now we have spin loop here but we just want to sleep in epoll_wait.
				if (errno == EAGAIN)
				{
					// we leave data->start as it is
					return false;
				}

				logger_.log("Error during writing. Connection was closed on %d", connection->fd);
				free_connection(connection);

				if (global_write_handler != NULL)
					global_write_handler(n, NULL);

				data->start = 0;
				return true;
			}
	}

	if (data->start != data->size)
	{
		logger_.log("start = %zu, data->size = %zu!", data->start, data->size);
		assert(false);
	}


	if (global_write_handler != NULL)
		global_write_handler(data->size, connection);

	data->start = 0;
	return true;
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

	connections++;

    if (global_accept_handler != NULL)
		global_accept_handler(error_code, connection, client_address, client_port);
}

static void new_handle_accepting_connection(int server_fd, struct epoll_event &client_event,
                                            struct epoll_event &server_event)
{
    sockaddr_in clientaddr;
    socklen_t clientlen = sizeof(clientaddr);

    int client_fd = accept(server_fd, (sockaddr*)&clientaddr, &clientlen);
    assert(client_fd > 0 || (client_fd == -1 && errno == EAGAIN));

    if (client_fd > 0)
    {
        char client_address[NI_MAXHOST], client_port[NI_MAXSERV];
        int error_code = getnameinfo ((sockaddr*)&clientaddr, clientlen,
                                      client_address, sizeof client_address,
                                      client_port, sizeof client_port,
                                      NI_NUMERICHOST | NI_NUMERICSERV);

        make_socket_non_blocking(client_fd);
        connection_data *connection = allocate_connection(client_fd);

        connections++;

//        epoll_event event;
//        event.events = EPOLLIN | EPOLLET;
//        event.data.ptr = connection;

//        int return_code = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);
//        assert(return_code >= 0);

        int return_code = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, server_fd, &server_event);
        assert(return_code >= 0);

        if (global_accept_handler != NULL)
            global_accept_handler(error_code, connection, client_address, client_port);
    }
}

static void interrupt_handler(int , siginfo_t *, void *)
{
	interrupted = true;
}

void init(int port)
{
    pool = ( memory_pool *) malloc(sizeof(memory_pool));
    init_pool(pool);
    logger_.log("Memory pool is ready");

    server_fd = resolve_name_and_bind(port);

    int return_code = listen (server_fd, MAXCONN);
    check_errors("listen", return_code);

	epoll_fd = epoll_create (1);
    check_errors("epoll_create", epoll_fd);

    modify_epoll_context(epoll_fd, EPOLL_CTL_ADD, server_fd, EPOLLIN, &server_fd);
    modify_epoll_context(epoll_fd, EPOLL_CTL_MOD, server_fd, EPOLLIN, &server_fd);
    events = (epoll_event *)calloc(MAXEVENTS, sizeof(epoll_event));

	struct sigaction action;
	memset(&action, 0, sizeof(action));
	sigemptyset(&action.sa_mask);
	action.sa_sigaction = &interrupt_handler;
	action.sa_flags = SA_SIGINFO;

	return_code = sigaction(SIGINT, &action, NULL);
	check_errors("sigaction SIGINT", return_code);

	return_code = sigaction(SIGTERM, &action, NULL);
	check_errors("sigaction SIGTERM", return_code);

    logger_.log("Event loop is ready. Waiting for connections on port = %d...", port);
}

void run()
{
    int client_fd;
    struct epoll_event client_event =
    {
        .events = EPOLLIN | EPOLLET,
        .data = {.ptr = &client_fd}
    };
    struct epoll_event server_event =
    {
        .events = EPOLLIN | EPOLLET,
        .data = {.ptr = &server_fd}
    };

	while(!interrupted)
    {
        int n = epoll_wait(epoll_fd, events, MAXEVENTS, -1);
        assert(n >= 0 || (n == -1 && errno == EINTR));

        if (n == 0)
        {
            logger_.log("Timeout");
            assert(false);
        }

        for(int i = 0; i < n; i++)
        {
            if(EPOLLIN & events[i].events)
            {
                if(events[i].data.ptr == &server_fd)
                {
                    new_handle_accepting_connection(server_fd, client_event, server_event);
                }
                else
                {
                    connection_data* connection = (connection_data*) events[i].data.ptr;

                    connection->event = EPOLLIN;
                    modify_epoll_context(epoll_fd, EPOLL_CTL_DEL, connection->fd, 0, 0);
                    handle_reading_data_from_event(connection);
                }
            }
            else
                if(EPOLLOUT & events[i].events)
                {
                    connection_data* connection = (connection_data*) events[i].data.ptr;

                    connection->event = EPOLLOUT;
                    modify_epoll_context(epoll_fd, EPOLL_CTL_DEL, connection->fd, 0, 0);

                    if (!handle_writing_data_to_event(connection))
                    {
                        modify_epoll_context(epoll_fd, EPOLL_CTL_ADD, connection->fd, EPOLLOUT,
                                             connection);
                    }
                }
                else
                {
                    if(events[i].events & EPOLLHUP || events[i].events & EPOLLERR)
                    {
                        connection_data* connection = (connection_data*) events[i].data.ptr;
                        handle_closing(connection);
                    }
                }
        }
    }

	logger_.log("Accepted %d connections", connections);

	free(events);
	events = NULL;
	logger_.log("Events are destroyed");

	destroy_pool(pool);
	free(pool);
	logger_.log("Memory pool is destroyed");
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
 * Writes all data available in connection buffer (connection->size bytes) to kernel. There is no message concept
   so from sender POV all data may be send in many calls (by async_write) but from reciever POV only one async_read
   may be sufficient (and vice versa). If caller won't move connection->from and connection->size
   next async_write send excatly the same data (but as I noticed behaviour on receiver side may be different).
 */
void async_write( t_write_handler write_handler, connection_data *connection)
{
    assert(connection != NULL && epoll_fd != 0);
	modify_epoll_context(epoll_fd, EPOLL_CTL_ADD, connection->fd, EPOLLOUT, connection);
    global_write_handler = write_handler;
}
