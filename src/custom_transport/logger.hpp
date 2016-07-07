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
		static logger instance(true, true, true, false);
		return instance;
	}

	void log(const char *string, ...);
	void log_in_place(const char *string, ...);
	void enable(bool enabled);

private:

	logger(bool enabled, bool log_to_file, bool log_date, bool log_in_place);

	template<class Msg>
	void log_message(const Msg &msg)
	{
		log("Message: {name = \"%s\", id = %d}", msg.name.c_str(), msg.id);
	}

	~logger();

	char *put_time_in_buffer();

	bool on;
	const bool write_to_file, write_date;
	bool opened {false};
	bool in_place;
    static const int max_line_size = 600;
	char buffer[max_line_size];
	FILE * file_proxy = nullptr;
	std::mutex mutex;
};

}

#define logger_ framework::logger::get()

#endif // LOGGER_HPP
