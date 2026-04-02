#pragma once
/** @file host_command.hpp
 * @brief Header file for class host_command
 * @author Andrej Pakhutin (pakhutin <at> gmail.com)
 * @version 1.0.4
 *
 * @copyright Copyright (c) 2023+
 *
 * This module is intended to be used with Arduino framework
 *
 * The purpose is to receive and parse host commands,
 * usually coming via "Serial" class interface.
 *
 * The repo is in: github.com/kadavris
*/

#if defined(HOST_CMD_TEST)
#include "../tests/test_Stream.hpp"

#else
// NOTE: "use namespace std" is incompatible with Arduino.h!
#include <Arduino.h>
#include <Stream.h>

#endif

#include <stdint.h>
#include <limits.h>
#include <vector>

typedef struct //< internal: command's definition
{
    const char* name;         //< command's name
    int optional_start;  //< start of optional parameters
    std::vector<uint32_t> params;   //< array of param types and flags
} host_command_element;

/* Main class */
class host_command
{
public:
    host_command(size_t);  // Buffer size. 'Serial' is the default interface
    host_command(size_t, Stream *);  // Buffer size, Source of commands
    host_command(const host_command&) = delete;  // copy constructor disabled
    host_command(host_command&&) noexcept;  // move constr
    ~host_command();

    const char* prompt;   //< Being non-nullptr it will be printed as a prompt to enter a new command.
    Stream* source;  //< source of commands

    const char* const errstr() const; //< return error description

    // setup methods
    void allow_escape(bool); //< Enables or disables use of escape character '\'
    void limit_time(int); //< sets maximum time for internal processes in milliseconds. Use to prevent timely blocks on long inputs.
    void set_interactive(bool, const char*); //< if true then we'll produce some answer/error messages to host:

    int new_command(const char*, const char*); //< command name, printf-style params: return -1 on error

    bool new_command(const char*); //< start to define the new command. Use this for relaxed, step by step definitions
    void add_bool_param(); //< Adds another, boolean parameter for the current command
    void add_byte_param(); //< Adds another, byte parameter for the current command
    void add_int_param(); //< Adds another, integer number parameter for the current command
    void add_float_param(); //< Adds another, floating point number parameter for the current command
    void add_str_param(uint16_t); //< Adds another, const char* w/o spaces parameter for the current command
    void add_qstr_param(uint16_t); //< Adds another, quoted const char* parameter for the current command
    void optional_from_here(); //< Indicate that the next added parameters will be treated as optional

    // processing methods
    bool     get_next_command(); //< Request to get next command from the input. return false if there is no data yet or error
    int      get_command_id(); //< return current command's ID. -1 if none
    const char*   get_command_name(); //< return current command's name. "" if none
    bool     is_command_complete() const; //< return true if current command's processing is done nicely or with error.
    bool     is_invalid_input() const; //< return true if erroneous input detected

    bool     has_next_parameter(); //< return true if we have the data of the next parameter
    int      get_parameter_index(); //< return current parameter's ID. -1 if none
    uint32_t get_parameter_info(); //< return very internal parameter's definition block
    bool     is_optional() const; //< return true if this parameter is optional
    bool     no_more_parameters() const; //< return true if all parameters were recieved, including optional ones

    bool     get_bool() const; //< return boolean representation of current parameter's input data
    uint8_t  get_byte() const; //< return byte representation of current parameter's input data
    int      get_int() const; //< return integer number representation of current parameter's input data
    float    get_float() const; //< return floating point number representation of current parameter's input data
    const char* get_str(); //< return const char* representation of current parameter's input data. Actually - ptr to internal buffer.

    void     discard(); //< discard current command's processing completely

    bool     fill_buffer(char *, int); //< buf ptr, buf length. bulk read data from source into user-supplied buffer.

private:
    uint8_t* buf;        //< internal: temporary buffer
    int buf_len;         //< internal: length of the buffer needed
    int buf_pos;         //< internal: pos into buffer where a new char will be stored
    std::vector<host_command_element*> commands; //< array of definitions
    int cur_cmd;    //< index into commands or -1 - incomplete or -2 - not in list
    int cur_param;  //< index of the current param available. -1 if none
    int err_code;        //< last error code (host_command_error_codes)
    uint32_t flags;      //< behavior changing settings. see host_cmd_flag_*
    int max_time;        //< max time for internal processes in milliseconds. no timeout if <= 0
    uint32_t state;      //< internal: state flags (bitfield actually)

    void _init(size_t, Stream *); //< constructor helper
    void init_for_new_input(uint32_t); //< set new state. also reset data before new command processing.
    int check_input(); //< very internal. check source for data and do all incoming data processing.
    int find_command_index(const char *); //< return command's id/index by name. -1 if not found
};

