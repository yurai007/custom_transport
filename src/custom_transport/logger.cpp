#include <chrono>
#include <cassert>
#include <cstdarg>
#include <thread>
#include <ctime>

#include "logger.hpp"

namespace framework
{

logger::logger(bool enabled, bool log_to_file, bool log_date, bool log_in_place)
	: on(enabled), write_to_file(log_to_file), write_date(log_date), in_place(log_in_place)
{
	if (on && write_to_file)
	{
		file_proxy = fopen ("log.txt", "w");
		opened = true;
	}
}

// TO DO: lock may be much relaxed probably (maybe even to put_time_in_buffer)
void logger::log(const char *string, ...)
{
	std::lock_guard<std::mutex> lock(mutex);

	if (!on)
		return;
	char *current_pos = buffer;

	if (write_date && !in_place)
		current_pos = put_time_in_buffer();

	va_list args;
	va_start (args, string);
	int return_code = vsnprintf (current_pos, max_line_size, string, args);
	assert(return_code >= 0);
	va_end (args);

	if (write_to_file)
	{
		fputs(buffer, file_proxy);
			if (!in_place)
				fputs("\n", file_proxy);
		fflush(file_proxy);
	}
	else
	{
		if (!in_place)
			printf("%s\n", buffer);
		else
			printf("%s", buffer);
	}
}

 void logger::log_in_place(const char *string, ...)
 {
	 std::lock_guard<std::mutex> lock(mutex);

	 if (!on)
		 return;

	 char *current_pos = buffer;
	 in_place = true;

	 if (write_date && !in_place)
		 current_pos = put_time_in_buffer();
	 va_list args;
	 va_start (args, string);
	 vsnprintf (current_pos, max_line_size, string, args);
	 va_end (args);

	 if (write_to_file)
	 {
		 fputs(buffer, file_proxy);
			 if (!in_place)
				 fputs("\n", file_proxy);
		 // ???
		 fflush(file_proxy);
	 }
	 else
	 {
		 if (!in_place)
			 printf("%s\n", buffer);
		 else
			 printf("%s", buffer);
	 }
	 in_place = false;
 }

 void logger::enable(bool enabled)
 {
	 std::lock_guard<std::mutex> lock(mutex);
	 on = enabled;
 }

logger::~logger()
{
	if (opened)
	{
		fclose (file_proxy);
	}
}

char *logger::put_time_in_buffer()
{
	auto current_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

	int current = 0;
	buffer[current++] = '[';

	size_t written_bytes = strftime(&buffer[current], 20, "%H:%M:%S", localtime( &current_time ));
	assert(written_bytes > 0);
	current += written_bytes;

	struct timespec ts;

	int result = clock_gettime( CLOCK_REALTIME, &ts);
	assert(result == 0);

	int ms = ts.tv_nsec / 1000000L;

	written_bytes = sprintf(&buffer[current], "::%d", ms);
	assert(written_bytes > 0);
	current += written_bytes;

	buffer[current++] = ']';
	buffer[current++] = ' ';
	return &buffer[current];
}


}
