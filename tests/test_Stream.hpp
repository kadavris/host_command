#pragma once
/**
 * @file test_Stream.hpp
 * @author Andrej Pakhutin (pakhutin <at> gmail.com)
 * @brief Header for a mock class test_Stream
 * @version 1.0.31
 * @date 2023-04-08
 *
 * @copyright Copyright (c) 2023
 * 
 * This file is a part of the class host_command testing suite
 * The repo is in github.com/kadavris
 */

#include <string>
#include <vector>

#define HOST_CMD_TEST 1


// mockup of Arduino frameworks' class Stream
class test_Stream
{
private:
    std::string buf; // current command being fed to parser
    int pos; // position in buffer

public:
    test_Stream();
    ~test_Stream();

    // testing interface
    void add_input( std::string );
    void add_input( char* );
    void clear();
    int fail_percentage;

    // mockups
    void setTimeout(int);
    int  available();
    int  read();
    size_t readBytes(char*, int);

    template<typename T> void print(T p);
    template<typename T> void println(T p);

};

extern test_Stream Serial;

using Stream = test_Stream;
using String = std::string;

extern unsigned long millis();

#ifdef _MSC_VER
#include <windows.h>
#define delay(a) Sleep((a))

#else
#include <stdlib.h>
#define delay(a) sleep((a)/1000 + 1)

#endif
