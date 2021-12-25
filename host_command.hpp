#pragma once
/*
  This module is Copyright by Andrej Pakhutin pakhutin <at> gmail.com

  The purpose is to receive and parse host commands,
  usually coming via Arduino framework's "Serial" class interface.
*/

#if defined(HOST_CMD_TEST)
using namespace std;
#include "tests/test_Stream.hpp"

#else
#include <Arduino.h>
#include <Stream.h>

#endif

#include <string>
#include <vector>
#include <stdint.h>

struct host_command_element //< internal: command's definition
{
    String name;         //< command's name
    int optional_start;  //< start of optional parameters
    vector<uint32_t> params;   //< array of param types and flags
};

/* Main class */
class host_command
{
public:
    host_command(size_t);
    host_command(size_t,Stream*);
    ~host_command();

    String* prompt;       //< if not NULL it will be printed as a new command prompt.
    Stream* source;       //< source of commands

    const char* const errstr(); //< return error description

    // setup methods
    void set_interactive(bool, String*); // if true then we'll produce some answer/error messages to host:
    void allow_escape(bool);

    int new_command(String, String); // name, params: return -1 on error

    bool new_command(String); // just name
    void add_bool_param();
    void add_byte_param();
    void add_int_param();
    void add_float_param();
    void add_str_param(uint16_t);
    void add_qstr_param(uint16_t);
    void optional_from_here();

    // processing methods
    bool     get_next_command();
    int      get_command_id();
    String   get_command_name();
    bool     is_command_complete();
    bool     is_invalid_input();

    bool     has_next_parameter();
    int      get_parameter_index();
    uint32_t get_parameter_info();
    bool     is_optional();

    bool     get_bool();
    uint8_t  get_byte();
    int      get_int();
    float    get_float();
    const char* get_str();

    void     discard();

    bool     fill_buffer(char*, int);

private:
    vector<host_command_element*> commands; //< array of definitions
    int cur_cmd;    //< index into commands or -1 - incomplete or -2 - not in list
    int cur_param;  //< index of the current param available. -1 if none
    uint32_t flags;      //< behavior changing settings. see host_cmd_flag_*
    uint8_t* buf;        //< internal: temporary buffer
    size_t buf_len;         //< internal: length of the buffer needed
    int buf_pos;         //< internal: pos into buffer where a new char will be stored
    uint32_t state;      //< internal: state flags (bitfield actually)
    int err_code;        //< last error code

    void _init(size_t, Stream*);
    void init_for_new_input(uint32_t);
    int check_input();
    int find_command_index(const char*);
};

