#ifndef EPOLL_SERVER_HPP
#define EPOLL_SERVER_HPP

class epoll_server
{
public:
    epoll_server(short port);
    //void add_dispatcher(std::shared_ptr<message_dispatcher> dispatcher);
    void run();
    void stop();
    //void remove_connection(std::shared_ptr<connection> connection_);
    //void send_on_current_connection(const serialization::byte_buffer &data);
    //void dispatch_msg_from_buffer(serialization::byte_buffer &buffer);


    //std::shared_ptr<connection> current_connection {nullptr};
    //io_service m_io_service;

private:
};

#endif // EPOLL_SERVER_HPP
