#ifndef CUSTOM_TRANSPORT_HPP
#define CUSTOM_TRANSPORT_HPP

#include <sys/epoll.h>
#include <functional>

#define MAXCONN 200
#define MAXEVENTS 128
#define MAXLEN 512

struct connection_data
{
    int fd;
    uint32_t event;
    char data[MAXLEN];
	int length, from;
};

typedef std::function<void(int error, connection_data *,
								const char *address, const char *port)> t_accept_handler;
typedef std::function<void(int bytes_transferred, connection_data *)> t_read_handler;
typedef std::function<void(int bytes_transferred, connection_data *)> t_write_handler;

extern t_accept_handler global_accept_handler;
extern t_read_handler global_read_handler;
extern t_write_handler global_write_handler;
extern int server_fd, epoll_fd;
extern epoll_event *events;

extern void init(int port);
extern void run();
extern void async_accept( t_accept_handler accept_handler );
extern void async_read(t_read_handler read_handler, connection_data *connection);
extern void async_write(t_write_handler write_handler, connection_data *connection);


#endif // CUSTOM_TRANSPORT_HPP
