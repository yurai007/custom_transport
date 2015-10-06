#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>

/*
 * Echo server
 * calloc = malloc + memset(0)
 * When I have client and I break server by SIGSTP/SIGKILL then I can't bind again by server.
   I get bind error: Address already in use. The reason is that socket (even closed on server side)
   is in TIME_WAIT state (for TCP by ~4 minutes). Remedium is using SO_REUSEADDR option for the socket by setsockopt
   (nc do that:)).
   ss -tan | grepc [port] - shows sockets in TIME_WAIT state

   Ref: http://stackoverflow.com/questions/5106674/error-address-already-in-use-while-binding-socket-with-address-but-the-port-num
*  edge-triggered (passing EPOLLET to epoll_ctl, without this default behaviour is level_trigged) mode
   so I must in loop read all data

* TODO 0: create git repo, commit working dummy example and introduce changes in direction of many clients occurence
* TODO 1: Do write. I need buffer per client
* TODO 2: According to man (ref: http://man7.org/linux/man-pages/man7/epoll.7.html)
		  for TCP we can perform only one read/write (no loop, no EAGAIN)
*/

#define MAXEVENTS 64
#define MAXCLIENTS 128
#define MAXDATA 512
char buffer[MAXCLIENTS][MAXDATA];
ssize_t count[MAXCLIENTS];

static void check_errors(const char *message, int result)
{
    if (result < 0)
    {
        perror(message);
        exit(-1);
    }
}

static void check_getaddrinfo_errors(int return_code)
{
    if (return_code != 0)
    {
        fprintf (stderr, "getaddrinfo: %s\n", gai_strerror (return_code));
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

static int resolve_name_and_bind (char *port)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int socket_fd;

    memset (&hints, 0, sizeof (struct addrinfo));
    hints.ai_family = AF_UNSPEC;     /* Return IPv4 and IPv6 choices */
    hints.ai_socktype = SOCK_STREAM; /* We want a TCP socket */
    hints.ai_flags = AI_PASSIVE;     /* All interfaces */

    int return_code = getaddrinfo (NULL, port, &hints, &result);
    check_getaddrinfo_errors(return_code);

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        socket_fd = socket (rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (socket_fd == -1)
            continue;

        return_code = bind (socket_fd, rp->ai_addr, rp->ai_addrlen);
        if (return_code == 0)
            break;

        close (socket_fd);
    }

    if (rp == NULL)
    {
        fprintf (stderr, "Could not bind\n");
        return -1;
    }

    freeaddrinfo (result);
    return socket_fd;
}

static void handle_accepting_connection(int sfd, int efd, struct epoll_event *local_event)
{
    /* We have a notification on the listening socket, which
     means one or more incoming connections. */
    while (true)
    {
        struct sockaddr in_addr;
        char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

        socklen_t in_len = sizeof in_addr;
        int infd = accept (sfd, &in_addr, &in_len);
        if (infd == -1)
        {
            if ((errno == EAGAIN) ||
                    (errno == EWOULDBLOCK))
            {
                /* We have processed all incoming
                 connections. */
                break;
            }
            else
            {
                perror ("accept");
                break;
            }
        }

        int error_code = getnameinfo (&in_addr, in_len,
                         hbuf, sizeof hbuf,
                         sbuf, sizeof sbuf,
                         NI_NUMERICHOST | NI_NUMERICSERV);
        if (error_code == 0)
        {
            printf("Accepted connection on descriptor %d "
                   "(host=%s, port=%s)\n", infd, hbuf, sbuf);
        }

        /* Make the incoming socket non-blocking and add it to the
         list of fds to monitor. */
        error_code = make_socket_non_blocking (infd);
        check_errors("make_socket_non_blocking", error_code);

        local_event->data.fd = infd;
        local_event->events = EPOLLIN | EPOLLET;
        error_code = epoll_ctl (efd, EPOLL_CTL_ADD, infd, local_event);
        check_errors("epoll_ctl", error_code);
    }
}

static void handle_reading_data_from(int client_fd)
{
    /* We have data on the fd waiting to be read. Read and
     display it. We must read whatever data is available
     completely, as we are running in edge-triggered mode
     and won't get a notification again for the same
     data. */
    int done = 0;

    while (true)
    {
		char buffer[512];
		ssize_t count = read (client_fd, buffer, sizeof buffer);
        if (count == -1)
        {
            /* If errno == EAGAIN, that means we have read all
             data. So go back to the main loop. */
            if (errno != EAGAIN)
            {
                perror ("read");
                done = 1;
            }
            break;
        }
        else
        if (count == 0)
        {
            /* End of file. The remote has closed the
             connection. */
            done = 1;
            break;
        }
		buffer[count] = 0;
		int s = printf("Data from client socket = %d: %s", client_fd, buffer);
        check_errors("write", s);
    }

    if (done)
    {
        printf ("Closed connection on descriptor %d\n",
                client_fd);

        /* Closing the descriptor will make epoll remove it
         from the set of descriptors which are monitored. */
        close (client_fd);
    }
}

static void handle_writing_data_from(int client_fd)
{
//	/* We have data on the fd waiting to be read. Read and
//	 display it. We must read whatever data is available
//	 completely, as we are running in edge-triggered mode
//	 and won't get a notification again for the same
//	 data. */
//	int done = 0;

//	while (true)
//	{
//		send_count = write (client_fd, buffer, count);
//		if (send_count == -1)
//		{
//			/* If errno == EAGAIN, that means we have read all
//			 data. So go back to the main loop. */
//			if (errno != EAGAIN)
//			{
//				perror ("read");
//				done = 1;
//			}
//			break;
//		}
//		else
//		if (send_count == 0)
//		{
//			/* End of file. The remote has closed the
//			 connection. */
//			done = 1;
//			break;
//		}
//		buffer[send_count] = 0;
//		int s = printf("Send reply to client socket = %d: %s", client_fd, buffer);
//		check_errors("write", s);
//	}

//	if (done)
//	{
//		printf ("Closed connection on descriptor %d\n",
//				client_fd);

//		/* Closing the descriptor will make epoll remove it
//		 from the set of descriptors which are monitored. */
//		close (client_fd);
//	}
}

static void handle_closing(int client_fd)
{
	// An error has occured on this fd, or the socket is not
	// ready for reading (why were we notified then?)
	printf("Client associated with socket %d is gone...\n", client_fd);
	close (client_fd);
}

int main (int argc, char *argv[])
{
    struct epoll_event event;
    struct epoll_event *events;

    if (argc != 2)
    {
        fprintf (stderr, "Usage: %s [port]\n", argv[0]);
        exit (EXIT_FAILURE);
    }

    int server_fd = resolve_name_and_bind (argv[1]);
    check_errors("resolve_name_and_bind", server_fd);

    int return_code = make_socket_non_blocking (server_fd);
    check_errors("make_socket_non_blocking", return_code);

    return_code = listen (server_fd, SOMAXCONN);
    check_errors("listen", return_code);

    int epoll_fd = epoll_create1 (0);
    check_errors("epoll_create1", epoll_fd);

    event.data.fd = server_fd;
	event.events = EPOLLIN | EPOLLET | EPOLLOUT;
    return_code = epoll_ctl (epoll_fd, EPOLL_CTL_ADD, server_fd, &event);
    check_errors("epoll_ctl", return_code);

    /* Buffer where events are returned */
    events = (epoll_event*)calloc (MAXEVENTS, sizeof event);
	printf("Waiting for connections...\n");
	bool sth_to_write = false;

    while (true)
    {
        int n = epoll_wait (epoll_fd, events, MAXEVENTS, -1);
        for (int i = 0; i < n; i++)
        {
            if ((events[i].events & EPOLLERR) ||
                    (events[i].events & EPOLLHUP) ||
					(!(events[i].events & EPOLLIN))
					//|| (!(events[i].events & EPOLLOUT))
					)
            {
				handle_closing(events[i].data.fd);
                continue;
            }
            else
            if (server_fd == events[i].data.fd)
            {
                handle_accepting_connection(server_fd, epoll_fd, &event);
                continue;
            }
            else
			if (events[i].events & EPOLLIN)
            {
                handle_reading_data_from(events[i].data.fd);
				sth_to_write = true;
            }
//			else
//			if ((events[i].events & EPOLLOUT) && sth_to_write)
//			{
//				handle_writing_data_from(events[i].data.fd);
//				sth_to_write = false;
//			}
        }
    }

    free (events);
    close (server_fd);
    return 0;
}
