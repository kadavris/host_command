/** @file host_command.cpp
 * @brief Use to receive and parse commands, usually via "Serial" class interface.
 * @author Andrej Pakhutin (pakhutin <at> gmail.com)
 * @brief Contains class host_command implementation
 * @version 1.0.42
 *
 * @copyright Copyright (c) 2023+
 *
 * This module is intended to be used with Arduino framework
 * The repo is in: https://github.com/kadavris
*/
#define host_command_cpp
#include "host_command.hpp"
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <string.h>
#include <utility>

typedef struct host_command_element  //< internal: command's definition
{
    const char* name;    //< command's name
    int optional_start;  //< start of optional parameters
    std::vector<uint32_t> params;   //< array of param types and flags
} host_command_element;

const size_t min_buf_size = 128; // we need at least this much to be able to process all int and float parameters. We don't want to throw exceptions in embedded, so pretend it was a happy accident if the user requested less.

// bytes 0,1 of param definition is the max length for variable-length types (string and quoted string). For other types it is ignored.
// byte 2: param types:
const uint32_t hcmd_t_bool  = 0x00010000;
//const uint32_t hcmd_t_byte  = 0x00020000; // byte is deprecated. Use unquoted string of 1 byte length instead
const uint32_t hcmd_t_int   = 0x00040000;
const uint32_t hcmd_t_float = 0x00080000;
const uint32_t hcmd_t_str   = 0x00100000; //< \S+
const uint32_t hcmd_t_qstr  = 0x00200000; //< quoted string
const uint32_t hcmd_t_code_is_set = 0x00ff0000; //< type code is set

const char command_code_optional = '?';
const char command_code_bool  = 'b';
const char command_code_int   = 'd';
const char command_code_float = 'f';
const char command_code_qstr  = 'q';
const char command_code_str   = 's';

// param flags: 4th byte
//const uint32_t host_cmd_??? = 0x01000000;

const uint32_t hc_flag_interactive = 0x00000001; //< report problems back to host
const uint32_t hc_flag_escapes     = 0x00000002; //< allow escape char '\' to be used

// bitflags used for internal state tracking
const uint32_t hc_state_clean          = 0; //< nothing yet happened
const uint32_t hc_state_part_complete  = 0x00000001; //< got a full command name or a parameter value
const uint32_t hc_state_cmd_name       = 0x00000002; //< dealing with command name
const uint32_t hc_state_param          = 0x00000004; //< dealing with parameter data
const uint32_t hc_state_EOL            = 0x00000008; //< got an EOL
const uint32_t hc_state_d_quote        = 0x00000010; //< got " - quoted string. used for sanity checking
const uint32_t hc_state_s_quote        = 0x00000020; //< got ' - quoted string. used for sanity checking
const uint32_t hc_state_escape         = 0x00000040; //< got escape symbol 
const uint32_t hc_state_skip_till_EOL  = 0x00000080; //< skip input till the next param (used if there are max length specified)
const uint32_t hc_state_float_point    = 0x00000100; //< fp parameter: got a decimal point. used for sanity checking
const uint32_t hc_state_float_exp      = 0x00000200; //< fp parameter: got an exponent. used for sanity checking
const uint32_t hc_state_invalid        = 0x10000000; //< got invalid data. waiting for EOL
constexpr uint32_t hc_state_got_some   = hc_state_cmd_name | hc_state_param; //< if we started to process cmd parts already
constexpr uint32_t hc_state_got_quotes = hc_state_d_quote | hc_state_s_quote; //< got a 1st quote of quoted string. used for sanity checking

static const char* hc_errors[] =
{
    /* 0*/"no error",
    /* 1*/"bad parameter's length in a definition",
    /* 2*/"bad char on parameter's definition",
    /* 3*/"attempt to define duplicate command name",
    /* 4*/"required parameter missing",
    /* 5*/"invalid parameters specification for new_command(Source, SPEC)",
    /* 6*/"parameter length exceeded or user requested too small buffer",
    /* 7*/"expected quoted string but got no quote",
    /* 8*/"parameter data is invalid",
};

const int hc_error_no_error = 0;
const int hc_error_bad_length = 1; //< bad parameter's length on defining stage
const int hc_error_bad_pcode = 2; //< bad char on parameters defining stage
const int hc_error_duplicate_command = 3; //< attempt to define duplicate command name
const int hc_error_required_missing = 4; //< missing argument was not marked as optional
const int hc_error_invalid_param_spec = 5; //< invalid parameters specification for new_command(x,x)
const int hc_error_param_too_long = 6; //< parameter length exceeded or user requested too small buffer
const int hc_error_missing_quotes = 7; //< expected quoted string but got no quote
const int hc_error_invalid_param_data = 8; //< input data does not match parameter type definition

/**
* @brief Simple, "equal or not" case-insensitive strings comparison
* 
* @param s1: string 1
* @param s2: string 2
* @return bool: true if equal, false if not
*/
static bool same_strings(const char* s1, const char* s2)
{
    if (s1 == s2)
        return true;

    if ( s1 == nullptr || s2 == nullptr )
        return false;

    while( *s1 && *s2 )
    {
        if (tolower(*s1) != tolower(*s2))
            return false;

        ++s1;
        ++s2;
    }

    return *s1 == *s2;
}

/**
 * @brief return last error description
 * 
 * @return const char* const 
 */
const char* const host_command::errstr() const
{
    return hc_errors[err_code];
}

/**
 * @brief Internal: initializes class data 
 * 
 * @param size_t: Buffer size
 * @param Stream*: Source of commands
 */
void host_command::_init(size_t _bs, Stream* s)
{
    source = s;
    s->setTimeout(1); // do not wait on commands

    if (_bs < min_buf_size)
        buf_len = min_buf_size;
    else
        buf_len = static_cast<int>( _bs );

    buf = new uint8_t[buf_len];
    flags = hc_flag_escapes;
    max_time = -1; // no limit
    commands.reserve(10); // Attempt to avoid memory fragmentation

    init_for_new_command( hc_state_clean );
}

/**
 * @brief Construct a new host_command object
 * 
 * @param size_t: Buffer size
 */
host_command::host_command(size_t _bs)
{
    _init( _bs, &Serial );
}

/**
 * @brief Construct a new host_command object
 * 
 * @param size_t: Buffer size
 * @param Stream*: Source of commands
 */
host_command::host_command( size_t _bs, Stream* src )
{
    _init( _bs, src );
}

/**
 * @brief Move constructor
 *
 * @param host_command &&: Original object
 */
host_command::host_command( host_command&& src ) noexcept
{
    buf = src.buf;
    buf_len = src.buf_len;
    buf_pos = src.buf_pos;
    src.buf = nullptr;
    src.buf_len = src.buf_pos = 0;

    commands = std::move(src.commands);
    cur_cmd = src.cur_cmd;
    cur_param = src.cur_param;
    err_code = src.err_code;
    flags = src.flags;
    max_time = src.max_time;
    prompt = src.prompt;
    src.prompt = nullptr;
    source = src.source;
    state = src.state;
}

host_command::~host_command()
{
    if ( buf != nullptr )
        delete[] buf;

    while ( ! commands.empty() )
    {
        delete commands.back();
        commands.pop_back();
    }
}

/**
 * @brief Internal: reset to process next input command
 * 
 * @param uint32_t: initial state after reset
 */
void host_command::init_for_new_command( uint32_t _state )
{
    cur_cmd = -1;
    cur_param = -1;
    buf_pos = 0;
    state = _state;
    buf[0] = '\0';
    err_code = 0;
}

/**
* @brief Set interactive mode on/off. if true then we'll produce some answer/error messages to host sometimes
* 
* @param bool: new mode
* @return void
*/
void host_command::set_interactive( bool _mode, const char* _prompt = nullptr )
{
    if ( _mode )
        flags |= hc_flag_interactive;
    else
        flags &= ~hc_flag_interactive;

    prompt = _prompt;
}

/**
 * @brief Enables or disables use of escape character '\'
 * 
 * @param bool: _mode 
 */
void host_command::allow_escape( bool _mode )
{
    if ( _mode )
        flags |= hc_flag_escapes;
    else
        flags &= ~hc_flag_escapes;
}

/**
 * @brief sets maximum time for internal processes. Use to prevent timely blocks on long inputs.
 * 
 * @param int _millis: milliseconds. set < 0 for no timeout
 */
void host_command::limit_time( int _millis )
{
    max_time = _millis;
}


/** @brief Define the new command in full. Use for quick, C-style definitions
 *
 * The second parameter uses printf-like codes to define command parameters if any.
 * The format is: [?][length]type, where:
 *   ? - this marks the beginning of optional parameters
 *   length - unsigned int. set _maximum_ input length.
 *   type - printf-like: b-bool, d-int, f-float, s-string, q-quoted string
 *          see known_command_codes enum
 *
 * @param const char*: command name
 * @param const char*: parameters definition
 * @return int: -1 if _params are incorrect or number of parameters recorded
 */
int host_command::new_command( const char* _name, const char* _params )
{
    if ( ! new_command( _name ) )
        return -1;

    host_command_element* cmd = commands.back();

    uint32_t param_info = 0;
    uint32_t param_len = 0;
    bool has_length = false; // undicated if user implicitly specified length for a parameter.
    unsigned _plen = (unsigned)strlen(_params);

    for ( unsigned i = 0; i < _plen; ++i )
    {
        switch ( _params[i] )
        {
            case command_code_optional:
                if ( cmd->params.size() == 0 || cmd->optional_start != INT_MAX )
                {
                    err_code = hc_error_invalid_param_spec;
                    delete cmd;
                    commands.pop_back(); // try to do a basic clean up. Probably not worth it anyway
                    return -1;
                }

                cmd->optional_start = static_cast<int>(cmd->params.size()); // for "p1 ? p2" the question mark is _before p2_, so optional starts with p2
                break;

            case command_code_bool:
                param_info |= hcmd_t_bool;
                break;

            case command_code_int:
                param_info |= hcmd_t_int;
                break;

            case command_code_float:
                param_info |= hcmd_t_float;
                break;

            case command_code_qstr:
                param_info |= hcmd_t_qstr;
                break;

            case command_code_str:
                param_info |= hcmd_t_str;
                break;

            default:
                if ( isdigit( _params[i] ) ) // length. comes before parameter type. in case you forgot...
                {
                    param_len = param_len * 10u + _params[i] - '0';
                    has_length = true;
                }

                else if ( isspace(_params[i]) ) // allow spaces for readability
                {
                    continue;
                }

                else
                {
                    err_code = hc_error_bad_pcode;
                    delete cmd;
                    commands.pop_back();
                    return -1;
                }
        } // switch (_params[i])

        if ( param_info & hcmd_t_code_is_set ) // command type is set - saving
        {
            if ( param_info & (hcmd_t_qstr | hcmd_t_str) ) // check length attribute validity
            {
                if ( param_len == 0 ) // default: set to max
                {
                    if (has_length) // user specified 0 length, which is invalid
                    {
                        if (flags & hc_flag_interactive)
                            source->println("Error: string parameter with zero length.");

                        err_code = hc_error_bad_length;
                        delete cmd;
                        commands.pop_back();
                        return -1; 
                    }

                    param_len = buf_len - 1;
                }

                else if ( static_cast<int>(param_len) > buf_len - 1 ) //overflow?
                {
                    if (flags & hc_flag_interactive)
                        source->println("Warning: buffer size is too small for requested string parameter.");

                    err_code = hc_error_bad_length;
                    delete cmd;
                    commands.pop_back(); // try to do a basic clean up. Probably not worth it anyway
                    return -1;
                }
            }

            cmd->params.push_back( param_info | param_len );
            param_info = param_len = 0;
            has_length = false;
        } // if command type is set - saving
    } // end of scan through _params

    return static_cast<int>( cmd->params.size() );
}

/** @brief Start to define a new command. Use this for relaxed, step by step definitions
 *
 * @param const char*: command name
 * @return bool: if added OK
 */
bool host_command::new_command( const char* _name )
{
    if ( find_command_index( _name ) != -1 )
    {
        err_code = hc_error_duplicate_command;
        return false;
    }

    host_command_element* cmd = new host_command_element;
    cmd->name = _name;
    cmd->optional_start = INT_MAX;
    commands.push_back( cmd );

    return true;
}

/**@brief Continue to define a new command: inform that the next added parameters will be treated as optional
 * 
 * A new_command() should be called before to have a command to add parameters to.
 * Error processing: if there are no parameters yet or optional parameters were already marked
 * then no error will be generated and previous state will be unchanged.
 *
 * @return void
 */
void host_command::optional_from_here(void)
{
    if ( commands.size() == 0 )
        return;

    auto cmd = commands.back();

    if ( cmd->optional_start == INT_MAX )
        cmd->optional_start = static_cast<int>( cmd->params.size() );
}

/**
* @brief Request to get the next command from the input
*
* @return bool: false if none or error, or true if new command has arrived
*/
bool host_command::get_next_command(void)
{
    discard(); // complete or not, we will wait for EOL then

    return check_input() > 0;
}

/**
* @brief Return currently processed command's index
*
* @return int: -1 if none or index of new command available
*/
int host_command::get_command_id(void) const
{
    return cur_cmd;
}

/**
* @brief Return the name of the command being currently processed
*
* @return const char*: empty if no current command 
*/
const char*host_command::get_command_name(void) const
{
    if ( cur_cmd == -1 )
        return "";

    return commands[cur_cmd]->name;
}

/**
* @brief Check if current command processing is invalidated
*
* @return bool: true if something got broken
*/
bool host_command::is_invalid_input(void) const
{
    return state & hc_state_invalid;
}

/**
* @brief Check if current command processing is formally done
*
* Note that return value here depends not only on error, no command,
* or command with EOL received, but also
*     1) if current parameter is optional
*     2) or if current parameter is the last by definition and received complete
*
* @return bool: true if complete
*/
bool host_command::is_command_complete(void) const
{
    if( cur_cmd == -1 || ( state & (hc_state_EOL | hc_state_invalid) )
        || is_optional() 
        || commands[cur_cmd]->params.size() == 0 )
        return true;

    // if it is the last parameter and is already complete?
    return ( state & hc_state_part_complete ) &&
           ( cur_param + 1 == static_cast<int>( commands[cur_cmd]->params.size() ) );
}

/**
* @brief Requests to get the next parameter from the input
*
* @return int: false if none/error or true if new parameter's data is available
*/
bool host_command::fetch_next_parameter(void)
{
    if ( no_more_parameters() )
        return false; // staying where we are

    cur_param++;
    state = hc_state_param; // we need to reset previous parameter state completely
    buf_pos = 0;

    if ( check_input() <= 0 )
        return false;

    return 0 != (state & hc_state_part_complete);
}

/**
* @brief Return current parameter's index
*
* @return int: -1 if none or index of new parameter available
*/
int host_command::get_parameter_index(void) const
{
    return cur_param;
}

/**
 * @brief return the value of the current parameter to be extracted.
 *
 * @return uint32_t info bitmask or 0 if something wrong
 */
uint32_t host_command::get_parameter_info(void) const
{
    if ( cur_cmd == -1 || cur_param == -1 )
        return 0;

    return commands[cur_cmd]->params[cur_param];
}

/**
 * @brief return true if current parameter is optional.
 *
 * @return bool
 */
bool host_command::is_optional(void) const
{
    if (cur_cmd == -1 || cur_param == -1)
    {
        if (flags & hc_flag_interactive)
            source->println("\nWARNING: no command is being processed for is_optional().");

        return false;
    }
    
    return cur_param >= commands[cur_cmd]->optional_start;
}

/**
 * @brief return true if all possible parameters were received, including optional ones
 *
 * @return bool
 */
bool host_command::no_more_parameters(void) const
{
    if( cur_cmd == -1 || state & ( hc_state_EOL | hc_state_invalid )
        || commands[cur_cmd]->params.size() == 0 )
        return true;

    // if it is the last parameter and is already complete?
    return ( state & hc_state_part_complete ) && 
           ( cur_param + 1 == static_cast<int>( commands[cur_cmd]->params.size() ) );
} 

/**
* @brief Discard the current input and force wait for a new command to arrive if needed
*
* @return void
*/
void host_command::discard(void)
{
    if (state == hc_state_clean || state & hc_state_EOL)
        init_for_new_command( hc_state_clean ); // Clean or already got EOL. Just marking as clean
    else
        init_for_new_command( hc_state_invalid ); // Will wait for EOL
}

/**
 * @brief Internal: Validate the command name from the input and set state for the next step
 *
 * On return mimics check_input() return values, and it is expected to be called only from check_input() when we already have a complete command name in the buffer.
 * 
 * @return int: -1 on error, 1 if all is OK
 */
int host_command::validate_command_name(void)
{
    state |= hc_state_part_complete;
    buf[buf_pos] = '\0';
    cur_cmd = find_command_index((const char*)buf); // checking if we know this command

    if (cur_cmd == -1)
    {
        if (flags & hc_flag_interactive)
        {
            source->println("\nUnknown command.");

            if (prompt != nullptr)
                source->print(prompt);
        }

        init_for_new_command(hc_state_invalid);

        return -1;
    }
    
    return 1;
}

/**
 * @brief Internal: Check if there is new command/parameter available to process
 * 
 * To have a low-memory footprint we'll store and scan one parameter at the time maximum.
 *
 * @return int: -1 on error, 0 if no new command part arrived yet, 1 if some
 */
int host_command::check_input(void)
{
    // checking for previous act's completion and moving forward if necessary
    if ( cur_cmd > -1 && (state & hc_state_part_complete) ) // have previous parameter complete
    {
        // if got all params already and we're in the complete state, then init for next command
        if ( cur_param + 1 == static_cast<int>( commands[ cur_cmd ]->params.size() ) ) // no params or last one
        {
            init_for_new_command( hc_state_clean );
        }

        else // we'll wait for the next parameter then
        {
            cur_param++;
            state = hc_state_param; // we need to reset previous parameter state completely
            buf_pos = 0;
        }
    }

    if ( state & hc_state_EOL ) // maybe got a command with no params or EOL on optional params
    {
        init_for_new_command( hc_state_clean );
    }

    unsigned work_till = max_time > 0 ? millis() + max_time : 0;

    for(;;) // we'll loop while there is still some data in the stream... or time is out
    {
        if (buf_pos == buf_len) // overflow. discarding command
        {
            if (flags & hc_flag_interactive)
            {
                source->println("\n? Too long input. Will be discarded till EOL.");

                if (prompt != nullptr)
                    source->print(prompt);
            }

            discard();

            err_code = hc_error_param_too_long;

            return -1;
        } // if buffer overflow

        if ( work_till > 0 && millis() > work_till )
                return 0;  // out of time

        int c = source->available();

        if ( c < 0 ) // some error
            return -1;
        
        if ( c == 0 ) // nothing yet
            return 0;

        c = source->read();

        if ( c < 0 ) // error?
            return -1;

        bool c_is_eol = c == '\n' || c == '\r';
        bool c_is_space = c == ' ' || c == '\t';

        if ( state & hc_state_escape ) // this char is escaped
        {
            state &= ~hc_state_escape;
            buf[ buf_pos++ ] = c;
            continue;
        }

        if (buf_pos == 0 && (state & hc_state_EOL) && !(c_is_eol || c_is_space)) // reset for a new command after EOL
            state = hc_state_clean;

        if (state & (hc_state_invalid | hc_state_skip_till_EOL)) // waiting for invalidated command to be ended with EOL
        {
            if (c_is_eol)
                init_for_new_command(hc_state_clean);

            continue;
        }
        
        if ((flags & hc_flag_escapes) && c == '\\')
        {
            state |= hc_state_escape;
            continue;
        }

        if (buf_pos == 0 && c_is_space)  // space at the beginning of the input
        {
            if (state & hc_state_got_quotes) // quoted string
            {
                buf[buf_pos++] = c; // overflows will be dealt with at the next loop
                continue;
            }

            continue; // always drop leading spaces in simple cases
        }

        // checking for a token delimiter, but only if we are not in a quoted string
        if ( !(state & hc_state_got_quotes) )
        {
            if ( c_is_eol ) // gettin EOL noted here
            {
                state |= hc_state_EOL;

                if ( ! (state & hc_state_got_some) ) // skipping empty lines quick
                    continue;
            }

            // Here we might have received a full command name or a parameter data.
            // Checking for the end of cmd/param. At this point we must have some data already
            if ( c_is_eol || c_is_space )
            {
                if ( state & hc_state_cmd_name )  // received a complete command name
                    return validate_command_name();

                // Now we have a complete parameter here
                host_command_element *cmd = commands[ cur_cmd ];

                if ( c_is_eol )
                {
                    // checking if this or next param is not optional
                    if ( buf_pos == 0 ||
                         ( cur_param + 1 < static_cast<int>(cmd->params.size()) &&
                           cur_param + 1 < cmd->optional_start ) )
                    {
                        if ( flags & hc_flag_interactive )
                        {
                            source->print( "\nAttempt to skip non-optional parameter #" );
                            source->println( cur_param + 1 );

                            if ( prompt != nullptr )
                                source->print( prompt );
                        }

                        err_code = hc_error_required_missing;

                        state |= hc_state_invalid;

                        return -1;
                    }
                } // if EOL

                state |= hc_state_part_complete;

                buf[ buf_pos ] = '\0';

                return 1; // got another complete parameter
            } // got EOL or space
        } // if not in a quoted string

        if ( state & hc_state_skip_till_EOL )
            continue;

        if ( state == hc_state_clean || (state & hc_state_cmd_name) ) // still waiting for a command name to complete
        {
            buf[ buf_pos++ ] = c;
            state |= hc_state_cmd_name;

            continue;
        }

        // we wait for parameter here:

        host_command_element *cmd = commands[ cur_cmd ];

        // checking if our parameter is within user-requested size
        // NOTE: (now) this is used for strings only
        if ( cmd->params.size() == 0 )
        {
            state |= hc_state_skip_till_EOL;
            buf[ buf_pos ] = '\0';
            continue;
        }

        // Quoted strings
        if ( cmd->params[ cur_param ] & hcmd_t_qstr )
        {
            if (c == '"' || c == '\'') // check for the beginning/ending quote
            {
                if ( ! (state & hc_state_got_quotes) ) // is it the opening quote?
                {
                    // remember what type of quote was used at the beginning
                    if (c == '"')
                        state |= hc_state_d_quote;
                    else
                        state |= hc_state_s_quote;

                    continue; // don't store quotes
                }

                else if ( (c == '"' && (state & hc_state_d_quote)) // got a matching closing quote?
                       || (c == '\'' && (state & hc_state_s_quote)) )
                {
                    state |= hc_state_part_complete;

                    buf[buf_pos] = '\0';

                    return 1;
                }
            } // got quote

            // if 1st char is not a quote then set error state
            else if ( buf_pos == 0 && ! (state & hc_state_got_quotes))
            {
                err_code = hc_error_missing_quotes;

                state |= hc_state_invalid;
                
                return -1;
            }
        } // if quoted string?

        //TODO: check if no quotes was used
        //TODO: check for maximum string length

        // should we add new char to the current parameter's value?
        uint16_t param_len = cmd->params[cur_param] & 0xffff;
        if ( param_len == 0u || param_len > buf_pos) // check for max length
            buf[buf_pos++] = c;
        else
            buf[buf_pos] = '\0';
    } //for() loop over all available data in stream

    return 1;
} // int check

/**
* @brief Internal: return index of command by it's name
* 
* @param const char* - name to find
* @return -1 on error or index
*/
int host_command::find_command_index(const char* _name)
{
    for ( unsigned i = 0; i < commands.size(); ++i)
    {
        if ( same_strings(commands[i]->name, _name) )
            return i;
    }

    return -1;
}

//==========================================================
// Getters:

/**
 * @brief Return boolean representation of the current parameter
 * 
 * @return bool: true/false
 */
bool host_command::get_bool( void ) const
{
    if ( cur_cmd == -1 || state & hc_state_invalid || cur_param == -1 )
        return false;

    // assume that we'll deal with 'ok','on','true','y','yes' or non-zero number as true
    char first = tolower(*buf);

    // on/ok
    if (first == 'o' && buf[2] == '\0' && ( tolower(buf[1]) == 'k' || tolower(buf[1]) == 'n' ) )
        return true;

    if (first == 't')
        return same_strings( (const char*)buf, "true" );

    if (first == 'y')  // y/yes
    {
        if ( buf[1] ) // check for full word
            return same_strings( (const char*)buf, "yes" );
        else
            return true;
    }

    char *c = (char*)buf; // check if a non-zero number
    while ( isdigit(*c) )
    {
        if ( *c != '0' )
            return true;
        ++c;
    }

    return false;
}

/**
 * @brief Return parameter as an integer number
 * 
 * @return int 
 */
int host_command::get_int( void ) const
{
    if ( cur_cmd == -1 || state & hc_state_invalid || cur_param == -1 )
        return 0;

    return atoi( (const char*)buf );
}

/**
 * @brief Return parameter as a floating point number
 * 
 * @return float 
 */
float host_command::get_float( void ) const
{
    if ( cur_cmd == -1 || state & hc_state_invalid || cur_param == -1 )
        return 0.0f;

    return static_cast<float>(atof( (char*)buf ));
}

/**
 * @brief Return parameter as a raw char*.
 *
 * At least it'll try to return either complete or empty string.
 * NOTE: This is a non-const function. The end of the buffer contents will be set to '\0' to make sure that returned string is null-terminated.
 * So do not expect that the buffer will be unchanged after this call.
 *
 * @return const char*
 */
const char* host_command::get_str( void )
{
    if ( cur_cmd == -1 || state & hc_state_invalid || cur_param == -1 )
    {
        buf[0] = '\0';
        return (const char*)buf;
    }

    if (buf_pos == buf_len) // should not happen because of checks in check_input() but just in case if there will be amends later
    {
        buf[buf_pos - 1] = '\0';
        state |= hc_state_invalid;
    }
    else
    {
        buf[buf_pos] = '\0';
    }

    return (const char*)buf;
}

/**
* @brief Fill arbitrary buffer with requested number of bytes from the pre-set source for this object.
*
* Use this if you want to get some raw data instead of text parameters.
* Usually you want to set up a command without parameters for a fixed-size package or with a data length parameter
* and then use this method to get the data you need.
* Function will block until max_time is out or requested amount of data is received.
* Use limit_time() to set max_time if you want to have a timeout.
* 
* @param char* - destination buffer
* @param int - amount of data to get
* @return bool: true if OK, false in case of problems or timeout (if max_time is set)
*/
bool host_command::fill_buffer(char* dst, int len)
{
    int dst_offset = 0;
    int need_count = 0;
    unsigned work_till = max_time > 0 ? millis() + max_time : 0;


    for (; len ; delay(200)) // we'll loop while there is still some data in the stream
    {
        if ( work_till ) // timeout is set - checking
        {
            if ( millis() >= work_till )
                return false;
        }

        int count = source->available();

        if (count < 0) // some error
            return false;

        if (count == 0) // nothing yet
            continue;

        need_count = count < len ? count : len;

        count = static_cast<int>(source->readBytes(dst + dst_offset, need_count));
        
        if (count < 0) // some error
            return false;

        dst_offset += count;
        len -= count;
    }
    
    return true;
}
