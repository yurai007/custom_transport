#ifndef MESSAGE_DISPATCHER_HPP
#define MESSAGE_DISPATCHER_HPP

#include <memory>
#include <cassert>
#include <string>
#include <functional>
#include <vector>
#include <map>

#include "logger.hpp"
#include "byte_buffer.hpp"

namespace networking
{

/*
 * Ok. This is the most crazy implementation I have ever worked with.
       Ref: http://stackoverflow.com/questions/25714390/is-it-possible-to-do-this-lambda-event-manager-in-c
 * 1. boost::any
     - holds any type. Sth like dynamic in C#.
     - It's impossible to implicit cast from boost::any. We must use boost::any_cast
     - It's not implemented by void*! It uses dynamic polymorphism and virtual methods instead.
   2. Observing callstack is good idea to understanding how it works
 */

typedef std::function<bool(serialization::byte_buffer &buffer)> dispatcher_type;

// those traits converts argument sequence e.g. int, std::string, Foo.. to std::tuple<int, std::string, Foo>
template<typename T>
struct function_traits;

template<typename R, typename C, typename Arg>
struct function_traits<R(C::*)(Arg)>
{
    using arg_type = Arg;
};

template<typename R, typename C, typename Arg>
struct function_traits<R(C::*)(Arg) const>
{
    using arg_type = Arg;
};


template<typename T>
struct dispatcher_maker;

template<typename Arg>
struct dispatcher;


template<typename Arg>
struct dispatcher_maker
{
    template<typename F>
    dispatcher_type make(F&& f)
    {
        return dispatcher<Arg>{std::forward<F>(f)};
    }
};

template<typename F>
dispatcher_type make_dispatcher(F&& f) // add dispatcher_type
{
    using f_type = decltype(&F::operator()); // e.g. void (message_dispatcher_test_case()::<lambda(int)>::*)(int) const
    using arg_type = typename function_traits<f_type>::arg_type; // e.g std::tuple<int>

    return dispatcher_maker<arg_type>().make(std::forward<F>(f)); // temporary dispatcher_maker<arg_type>
}

template<typename Arg>
struct dispatcher
{
    template<typename F> dispatcher(F f) : handler(std::move(f)) { }

    bool operator() (serialization::byte_buffer &buffer)
    {
        typedef typename std::remove_reference<Arg>::type Msg;
        int id = buffer.m_byte_buffer[buffer.offset];

        if (id == Msg::message_id())
        {
            Msg msg = {};
            buffer.offset++;
            msg.deserialize_from_buffer(buffer);
            handler(msg);
            return true;
        }
        return false;
    }

private:
    std::function<void(Arg)> handler;
};


/*
 * be aware that there is no implicit conversion so 1.0 is double but 1.0f is float and "dupa"
   is c-string
 * static variables don't have to be captured by lambda!
 */
struct message_dispatcher
{
public:

    template<typename F>
    void add_handler(F&& f)
    {
        callbacks.emplace_back(make_dispatcher(std::forward<F>(f)));
    }

    void dispatch_msg_from_buffer(serialization::byte_buffer &buffer)
    {
        dispatch(buffer); // buffer[0] is data size
    }

    void dispatch(serialization::byte_buffer &buffer)
    {
        for (auto some_dispatcher : callbacks)
        {
            if (call(some_dispatcher, buffer))
                return;
        }
        logger_.log("message dispatcher: there is no handler for this msg");
    }

private:

    template<typename Dispatcher>
    bool call(Dispatcher const& dispatcher, serialization::byte_buffer &buffer)
    {
        return dispatcher(buffer);
    }

private:
    std::vector<dispatcher_type> callbacks;
};

}

#endif // MESSAGE_DISPATCHER_HPP
