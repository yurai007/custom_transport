#ifndef BYTE_BUFFER_HPP
#define BYTE_BUFFER_HPP

#include <cstring>
#include <array>
#include <cassert>
#include <vector>
#include <type_traits>

namespace serialization
{

const static int max_size = 300;

struct byte_buffer
{
    byte_buffer() : offset(0) {}

    template<class T>
    void put_value(typename std::enable_if<std::is_integral<T>::value, T>::type
                   value)
    {
        memcpy(&m_byte_buffer[offset], &value, sizeof(value));
        offset += sizeof(value);
        assert(offset < max_size);
    }

    template<class T>
    void put_vector_value(const std::vector<T> //typename std::enable_if<std::is_integral<T>::value, T>::type
                   &value)
    {
        size_t size = value.size()*sizeof(T);
        put_int(size);
        if (size > 0)
        {
            memcpy(&m_byte_buffer[offset], &value[0], size);
            offset += size;
        }
        assert(offset < max_size);
    }

    void put_string(const std::string &value)
    {
        size_t size = value.size()*sizeof(char);
        put_int(size);
        memcpy(&m_byte_buffer[offset], &value[0], size);
        offset += size;
        assert(offset < max_size);
    }

    template<class T>
    typename std::enable_if<std::is_integral<T>::value, T>::type get_value()
    {
        T value;
        memcpy(&value, &m_byte_buffer[offset], sizeof(value));
        offset += sizeof(value);
        assert(offset < max_size);
        return value;
    }

    template<class T>
    std::vector<T> get_vector_value()
    {
        size_t size = get_int();
        std::vector<T> value(size / sizeof(T), 0);
        if (size > 0)
        {
            memcpy(&value[0], &m_byte_buffer[offset], size);
            offset += size;
        }
        assert(offset < max_size);
        return value;
    }

    void put_char(char value)
    {
        put_value<char>(value);
    }

    void put_bool(bool value)
    {
        put_value<bool>(value);
    }

    void put_int(int value)
    {
        put_value<int>(value);
    }

    void put_long(long value)
    {
        put_value<long>(value);
    }

    void put_double_vector(const std::vector<double> &value)
    {
        put_vector_value<double>(value);
    }

    void put_long_vector(const std::vector<long> &value)
    {
        put_vector_value<long>(value);
    }

    void put_int_vector(const std::vector<int> &value)
    {
        put_vector_value<int>(value);
    }

    char get_char()
    {
        return get_value<char>();
    }

    bool get_bool()
    {
        return get_value<bool>();
    }

    int get_int()
    {
        return get_value<int>();
    }

    long get_long()
    {
        return get_value<long>();
    }

    std::vector<int> get_int_vector()
    {
        return get_vector_value<int>();
    }

    std::vector<double> get_double_vector()
    {
        return get_vector_value<double>();
    }

    std::vector<long> get_long_vector()
    {
        return get_vector_value<long>();
    }

    std::string get_string()
    {
        size_t size = get_int();
        std::string result(&m_byte_buffer[offset], &m_byte_buffer[offset + size]);
        offset += size;
        assert(offset < max_size);
        return result;
    }

    void clear()
    {
        offset = 0;
    }

    void set_offset_on_start()
    {
        offset = 0;
    }

    int get_size()
    {
        return offset;
    }

    std::array<unsigned char, max_size> m_byte_buffer;
    int offset;
};

}

#endif // BYTE_BUFFER_HPP
