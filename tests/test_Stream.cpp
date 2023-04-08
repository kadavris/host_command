/**
 * @file test_Stream.cpp
 * @author Andrej Pakhutin (pakhutin <at> gmail.com)
 * @brief Mockup of Arduino's framework class Stream
 * @version 1.0.31
 * @date 2023-04-08
 *
 * @copyright Copyright (c) 2023
 *
 */
#include <iostream>
#include "test_Stream.hpp"
#include "../include/host_command.hpp"

test_Stream Serial;

// test_Stream members:
const char* test_Stream_tag = ". test_Stream: ";

void test_Stream::add_input(std::string _s)
{
    buf += _s;
}

void test_Stream::add_input(char* _s)
{
    buf += _s;
}

void test_Stream::clear()
{
    buf.clear();
    pos = 0;
}

test_Stream::test_Stream()
{
    pos = 0;
    fail_percentage = 0;
    //std::cout << test_Stream_tag << "Stream mockup created." << std::endl;
}

void test_Stream::setTimeout(int t) {}

test_Stream::~test_Stream()
{
    //std::cout << test_Stream_tag << "Stream mockup done." << std::endl;
}

// return how many bytes are available to read
int test_Stream::available()
{
    if (pos >= (int)buf.length())
        return 0;

    if (fail_percentage > 0 && rand() % 100 <= fail_percentage)
        return 0;

    return (int)(buf.length()) - pos;
}

// return the next byte or -1 on error
int test_Stream::read()
{
    if (fail_percentage > 0 && rand() % 100 <= fail_percentage)
        return -1;
        
    if (pos < (int)buf.length())
        return buf[pos++];

    if (pos > 0)
    {
        buf.clear();
        pos = 0;
    }

    return -1;
}

size_t test_Stream::readBytes(char* buf, int len)
{
    //TODO: 
    throw new std::runtime_error("test_Stream::readBytes is not implemented");
}

template<typename T> void test_Stream::print(T p)
{
    std::cout << test_Stream_tag << p;
}

template void test_Stream::print(char*);
template void test_Stream::print(char const*);
template void test_Stream::print(std::string);
template void test_Stream::print(int);

template<typename T> void test_Stream::println(T p)
{
    std::cout << test_Stream_tag << p << std::endl;
}

template void test_Stream::println(char*);
template void test_Stream::println(char const*);
template void test_Stream::println(std::string);
template void test_Stream::println(int);
