#pragma once
/*
  This module is Copyright by Andrej Pakhutin pakhutin <at> gmail.com

  The purpose is to receive and parse host commands,
  usually coming via Arduino framework's "Serial" class interface.
*/

#if defined(HOST_CMD_TEST)
#include "tests/test_Stream.hpp"
#include <string>

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
    String name;         //< command's name
    int optional_start;  //< start of optional parameters
    std::vector<uint32_t> params;   //< array of param types and flags
} host_command_element;

/* Main class */
class host_command
{
public:
    host_command(size_t);
    host_command(size_t,Stream*);
    ~host_command();

    String prompt;       //< if not NULL it will be printed as a new command prompt.
    Stream* source;       //< source of commands

    const char* const errstr(); //< return error description

    // setup methods
    void set_interactive(bool, const char*); //< if true then we'll produce some answer/error messages to host:
    void allow_escape(bool); //< Enables or disables use of escape character '\'

    int new_command(String, String); //< command name, printf-style params: return -1 on error

    bool new_command(String); //< Start to define the new command. Use this for relaxed, step by step definitions
    void add_bool_param(); //< Adds another, boolean parameter for the current command
    void add_byte_param(); //< Adds another, byte parameter for the current command
    void add_int_param(); //< Adds another, integer number parameter for the current command
    void add_float_param(); //< Adds another, floating point number parameter for the current command
    void add_str_param(uint16_t); //< Adds another, string w/o spaces parameter for the current command
    void add_qstr_param(uint16_t); //< Adds another, quoted string parameter for the current command
    void optional_from_here(); //< Indicate that the next added parameters will be treated as optional

    // processing methods
    bool     get_next_command(); //< Request to get next command from the input. return false if there is no data yet or error
    int      get_command_id(); //< return current command's ID. -1 if none
    String   get_command_name(); //< return current command's name. "" if none
    bool     is_command_complete(); //< return true if current command's processing is done nicely or with error.
    bool     is_invalid_input(); //< return true if erroneous input detected

    bool     has_next_parameter(); //< return true if we have the data of the next parameter
    int      get_parameter_index(); //< return current parameter's ID. -1 if none
    uint32_t get_parameter_info(); //< return very internal parameter's definition block
    bool     is_optional(); //< return true if this parameter is optional
    bool     no_more_parameters(); //< return true if all parameters were recieved, including optional ones

    bool     get_bool(); //< return boolean representation of current parameter's input data
    uint8_t  get_byte(); //< return byte representation of current parameter's input data
    int      get_int(); //< return integer number representation of current parameter's input data
    float    get_float(); //< return floating point number representation of current parameter's input data
    const char* get_str(); //< return string representation of current parameter's input data. Actually - ptr to internal buffer.

    void     discard(); //< discard current command's processing completely

    bool     fill_buffer(char*, int); //< buf ptr, buf length. bulk read data from source into user-supplied buffer.

private:
    std::vector<host_command_element*> commands; //< array of definitions
    int cur_cmd;    //< index into commands or -1 - incomplete or -2 - not in list
    int cur_param;  //< index of the current param available. -1 if none
    uint32_t flags;      //< behavior changing settings. see host_cmd_flag_*
    uint8_t* buf;        //< internal: temporary buffer
    int buf_len;         //< internal: length of the buffer needed
    int buf_pos;         //< internal: pos into buffer where a new char will be stored
    uint32_t state;      //< internal: state flags (bitfield actually)
    int err_code;        //< last error code

    void _init(size_t, Stream*); //< constructor helper
    void init_for_new_input(uint32_t); //< set new state. also reset data before new command processing.
    int check_input(); //< very internal. check source for data and do all incoming data processing.
    int find_command_index(const char*); //< return command's id/index by name. -1 if not found
};

