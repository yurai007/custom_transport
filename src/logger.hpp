#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <cstdio>
#include <mutex>

namespace framework
{

/* logger is singleton so protection is needed only in log method */
class logger
{
public:

    static logger& get()
    {
		static logger instance(true, false, true, false);
        return instance;
    }

    void log(const char *string, ...);
    void log_in_place(const char *string, ...);

private:

    logger(bool enabled, bool log_to_file, bool log_date, bool log_in_place);

    template<class Msg>
    void log_message(const Msg &msg)
    {
        log("Message: {name = \"%s\", id = %d}", msg.name.c_str(), msg.id);
    }

    ~logger();

    char *put_time_in_buffer();

    const bool on, write_to_file, write_date;
    bool in_place;
    static const int max_log_size = 128;
    char buffer[max_log_size];
    FILE * file_proxy = nullptr;
    std::mutex mutex;
};

}

#define logger_ framework::logger::get()

#endif // LOGGER_HPP
