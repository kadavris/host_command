/** @file host_command.cpp
 * @brief Use to receive and parse commands, usually via "Serial" class interface.
 * @author Andrej Pakhutin (pakhutin <at> gmail.com)
 * @brief Contains class host_command implementation
 * @version 1.0.41
 *
 * @copyright Copyright (c) 2023+
 *
 * This module is intended to be used with Arduino framework
 * The repo is in: github.com/kadavris
*/
#define host_command_cpp
#include "host_command.hpp"

// bytes 0,1 of param definition is the max length
// param types (byte 2):
const uint32_t hcmd_t_bool  = 0x00010000;
const uint32_t hcmd_t_byte  = 0x00020000;
const uint32_t hcmd_t_int   = 0x00040000;
const uint32_t hcmd_t_float = 0x00080000;
const uint32_t hcmd_t_str   = 0x00100000; //< \S+
const uint32_t hcmd_t_qstr  = 0x00200000; //< quoted string

const char command_code_optional = '?';
const char command_code_bool  = 'b';
const char command_code_byte  = 'c';
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
const uint32_t hc_state_complete       = 0x00000001; //< got a full command name or parameter data
const uint32_t hc_state_cmd            = 0x00000002; //< dealing with command name
const uint32_t hc_state_param          = 0x00000004; //< dealing with parameter data
const uint32_t hc_state_EOL            = 0x00000008; //< got an EOL
const uint32_t hc_state_d_quote        = 0x00000010; //< got " - quoted string. used for sanity checking
const uint32_t hc_state_s_quote        = 0x00000020; //< got ' - quoted string. used for sanity checking
const uint32_t hc_state_escape         = 0x00000040; //< got escape symbol 
const uint32_t hc_state_skip           = 0x00000080; //< skip input till the next param (used if there are max length specified)
const uint32_t hc_state_invalid        = 0x10000000; //< got invalid data. waiting for EOL
constexpr uint32_t hc_state_got_some   = hc_state_cmd | hc_state_param; //< if we started to process cmd parts already
constexpr uint32_t hc_state_got_quotes = hc_state_d_quote | hc_state_s_quote; //< got a 1st quote of quoted string. used for sanity checking

static const char* hc_errors[] =
{
    /* 0*/"no error",
    /* 1*/"bad parameter's length in definiton",
    /* 2*/"bad char on parameter's definition",
    /* 3*/"attempt to define duplicate command name",
    /* 4*/"required parameter missing",
    /* 5*/"invalid parameters specification for new_command(Source, SPEC)",
    /* 6*/"parameter length exceeded or user requested too small buffer",
    /* 7*/"expected quoted string but got no quote",
};

const int hc_error_no_error = 0;
const int hc_error_bad_length = 1; //< bad parameter's length on defining stage
const int hc_error_bad_pcode = 2; //< bad char on parameters defining stage
const int hc_error_duplicate_command = 3; //< attempt to define duplicate command name
const int hc_error_required_missing = 4; //< missing argument was not marked as optional
const int hc_error_invalid_param_spec = 5; //< invalid parameters specification for new_command(x,x)
const int hc_error_param_too_long = 6; //< parameter length exceeded or user requested too small buffer
const int hc_error_missing_quotes = 7; //< expected quoted string but got no quote

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

    if( _bs < 2 ) // We dont want to throw exceptions in embedded, so pretend it was a happy accident
        buf_len = 64;
    else
        buf_len = static_cast<int>( _bs );

    buf = new uint8_t[buf_len];
    flags = hc_flag_escapes;
    max_time = -1; // no limit

    init_for_new_input( hc_state_clean );
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
void host_command::init_for_new_input( uint32_t _state )
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
 *   length - integer. set _maximum_ input length.
 *   type - printf-like: b-bool, c-byte, d-int, f-float, s-string, q-quoted string
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
    int _plen = strlen(_params);

    for ( unsigned i = 0; i < _plen; ++i )
    {
        switch ( _params[i] )
        {
            case command_code_optional:
                if ( cmd->params.size() == 0 || cmd->optional_start != INT_MAX )
                {
                    err_code = hc_error_invalid_param_spec;
                    return -1;
                }

                cmd->optional_start = static_cast<int>(cmd->params.size());
                break;
            case command_code_bool:
                param_info |= hcmd_t_bool;
                break;
            case command_code_byte:
                param_info |= hcmd_t_byte;
                break;
            case command_code_int:
                param_info |= hcmd_t_int;
                break;
            case command_code_float:
                param_info |= hcmd_t_float;
                break;
            case command_code_qstr:
                param_info |= hcmd_t_qstr;
                if (param_len == 0)
                    param_len = buf_len - 1;
                break;
            case command_code_str:
                param_info |= hcmd_t_str;
                if (param_len == 0)
                    param_len = buf_len - 1;
                break;

            default:
                if ( isdigit( _params[i] ) ) // length
                {
                    param_len = param_len * 10u + _params[i] - '0';

                    if ( param_len == 0 ) // leading zero most probably is a mistake
                    {
                        err_code = hc_error_bad_length;
                        commands.pop_back(); // try to do basic clean up. Probably not worth it anyway
                        return -1;
                    }
                }

                else if ( isspace(_params[i]) ) // allow spaces for readability
                {
                    continue;
                }

                else
                {
                    err_code = hc_error_bad_pcode;
                    commands.pop_back(); // try to do a basic clean up. Probably not worth it anyway
                    return -1;
                }
        } // switch (_params[i])

        if ( param_info & 0x00ff0000 ) // command type is set - saving
        {
            if ( param_info & (hcmd_t_qstr | hcmd_t_str) ) // check length attribute validity
            {
                if ( param_len < 1 || static_cast<int>(param_len) > buf_len - 1 ) //overflow?
                {
                    err_code = hc_error_bad_length;
                    commands.pop_back(); // try to do a basic clean up. Probably not worth it anyway
                    return -1;
                }
            }

            cmd->params.push_back( param_info | param_len );
            param_info = param_len = 0;
        }
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

/** @brief Continue to define a new command: add new boolean parameter
 *
 * A new_command() should be called before to have a command to add parameters to.
 *
 * @return void
 */
void host_command::add_bool_param(void)
{
    commands.back()->params.push_back( hcmd_t_bool );
}

/** @brief Continue to define a new command: add new parameter of a byte type
 *
 * A new_command() should be called before to have a command to add parameters to.
 * 
 * @return void
 */
void host_command::add_byte_param(void)
{
    commands.back()->params.push_back( hcmd_t_byte );
}

/** @brief Continue to define a new command: add new int parameter
 *
 * A new_command() should be called before to have a command to add parameters to.
 * 
 * @return void
 */
void host_command::add_int_param(void)
{
    commands.back()->params.push_back( hcmd_t_int );
}

/** @brief Continue to define a new command: add new float parameter
 *
 * A new_command() should be called before to have a command to add parameters to.
 * 
 * @return void
 */
void host_command::add_float_param(void)
{
    commands.back()->params.push_back( hcmd_t_float );
}

/** @brief Continue to define a new command: add new unquoted string parameter
 *
 * A new_command() should be called before to have a command to add parameters to.
 * Error processing: if len is 0 or bigger than buffer length then error code will be set and len will be set to match buffer length - 1.
 *
 * @param uint16_t len: maximum length of the string. Default is your buffer length - 1. Max is 65535.
 * @return void
 */
void host_command::add_str_param( uint16_t len = 65535 )
{
    if ( len == 0 || len > buf_len - 1 ) //overflow?
    {
        err_code = hc_error_bad_length;
        len = buf_len - 1;
    }

    commands.back()->params.push_back( hcmd_t_str | len );
}

/**@brief Continue to define a new command: add new quoted string parameter
 *
 * A new_command() should be called before to have a command to add parameters to.
 *
 * @param uint16_t len: maximum length of the string w/o quotes. Default is your buffer length - 3. Max is 65535.
 * @return void
 */
void host_command::add_qstr_param( uint16_t len = 65535 )
{
    if ( len == 0 || len > buf_len - 3 || buf_len < 4 ) //overflow?
    {
        err_code = hc_error_bad_length;
        len = buf_len - 3;
    }

    commands.back()->params.push_back( hcmd_t_qstr | len );
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
    if ( is_command_complete() )
        discard();

    return check_input() > 0;
}

/**
* @brief Return currently processed command's index
*
* @return int: -1 if none or index of new command available
*/
int host_command::get_command_id(void)
{
    return cur_cmd;
}

/**
* @brief Return the name of the command being currently processed
*
* @return const char*: empty if no current command 
*/
const char*host_command::get_command_name(void)
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
* Note that return valuee here depends not only on error, no command,
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
    return ( state & hc_state_complete ) &&
           ( cur_param + 1 == static_cast<int>( commands[cur_cmd]->params.size() ) );
}

/**
* @brief Requests to get the next parameter from the input
*
* @return int: false if none/error or true if new parameter's data is available
*/
bool host_command::has_next_parameter(void)
{
    if ( no_more_parameters() )
        return false;
        
    return check_input() > 0;
}

/**
* @brief Return current parameter's index
*
* @return int: -1 if none or index of new parameter available
*/
int host_command::get_parameter_index(void)
{
    if ( cur_cmd == -1 )
        return -1;

    return cur_param;
}

/**
 * @brief return the value of the current parameter to be extracted.
 *
 * @return uint32_t info bitmask or 0 if something wrong
 */
uint32_t host_command::get_parameter_info(void)
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
    if( cur_cmd == -1 || cur_param == -1 )
        return false;
    
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
    return ( state & hc_state_complete ) && 
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
        init_for_new_input( hc_state_clean ); // Clean or already got EOL. Just marking as clean
    else
        init_for_new_input( hc_state_invalid ); // Will wait for EOL
}

/**
 * @brief Internal: Check if there is new command/parameter available to process
 * 
 * To have a low-memory footprint we'll store and scan one parameter at the time maximum.
 *
 * @return int: -1 on error, 0 if no new data arrived yet, 1 if some
 */
int host_command::check_input(void)
{
    // checking for previous act's completion and moving forward if necessary
    if ( cur_cmd > -1 && (state & hc_state_complete) ) // have previous parameter complete
    {
        // if got all params already and we're in the complete state, then init for next command
        if ( cur_param + 1 == static_cast<int>( commands[ cur_cmd ]->params.size() ) ) // no params or last one
        {
            init_for_new_input( hc_state_clean );
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
        init_for_new_input( hc_state_clean );
    }

    unsigned work_till = 0; // unititalized state

    for(;;) // we'll loop while there is still some data in the stream... or time is out
    {
        if ( work_till ) // timeout is set - checking
        {
            if ( millis() >= work_till )
                return -1;
        }
        else // set timeout
        {
            work_till = max_time > 0 ? millis() + max_time : ULONG_MAX;
        }

        int c = source->available();

        if ( c < 0 ) // some error
            return -1;
        
        if ( c == 0 ) // nothing yet
            return 0;

        if ( buf_pos == buf_len ) // overflow. discarding command
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
        }

        c = source->read();

        if ( c < 0 ) // error?
            return -1;

        if( state & hc_state_invalid ) // waiting for invalidated input to be ended with LF
        {
            if ( c == '\n' || c == '\r' )
                init_for_new_input( hc_state_clean );

            continue;
        }

        if ( state & hc_state_escape ) // this char is escaped
        {
            state &= ~hc_state_escape;

            buf[ buf_pos++ ] = c;

            continue;
        }

		// always drop leading spaces in simple cases, but not in quoted strings (if we got some already)
        if ( buf_pos == 0 && c != '\n' && c != '\r' && !(state & hc_state_got_quotes)
             && isspace(c))
        {
            continue;
        }

        if ( c == '\n' || c == '\r' || c == ' ' || c == '\t' ) // checking for EOL or end of cmd/param
        {
            if ( ! (state & hc_state_got_some) && ( c == '\n' || c == '\r' ) ) // skipping empty lines quick
                continue;

            if ( state & hc_state_cmd ) // command name
            {
                //if (buf_pos == 0)
                //{
                //    init_for_new_input(hc_state_clean);
                //    continue;
                //}

                if ( c == '\n' || c == '\r' )
                    state |= hc_state_EOL;

                state |= hc_state_complete;

                buf[ buf_pos ] = '\0';

                break; // exit loop to validate command name
            }

            // we have parameter here

            host_command_element *cmd = commands[ cur_cmd ];

            // any space is valid in quoted string (if we're not over the limit though)
            if ( cmd->params[ cur_param ] & hcmd_t_qstr && ! ( state & hc_state_skip ) )
            {
                buf[ buf_pos++ ] = c;

                continue;
            }

            if ( c == '\n' || c == '\r' )
            {
                state |= hc_state_EOL;

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

            state |= hc_state_complete;

            buf[ buf_pos ] = '\0';

            return 1; // got another complete parameter
        } // got EOL or space

        if ( state & hc_state_skip ) // we need to skip till this param end
            continue;

        if ( (flags & hc_flag_escapes) && c == '\\' )
        {
            state |= hc_state_escape;

            continue;
        }

        if ( state == hc_state_clean || state & hc_state_cmd ) // still waiting for a command name to complete
        {
            buf[ buf_pos++ ] = c;
            state |= hc_state_cmd;

            continue;
        }

        // we wait for parameter here:

        host_command_element *cmd = commands[ cur_cmd ];

        // checking if our parameter is within user-requested size
        // NOTE: (now) this is used for strings only
        if ( cmd->params[cur_param] & 0xffff
             && ( static_cast<int>( cmd->params[cur_param] & 0xffff ) == buf_pos ) )
        {
            state |= hc_state_skip;
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
                    // remember what type of quote used at the beginning
                    if (c == '"')
                        state |= hc_state_d_quote;
                    else
                        state |= hc_state_s_quote;

                    continue; // don't store quotes
                }

                else if ( (c == '"' && (state & hc_state_d_quote)) // matching closing quote?
                    || (c == '\'' && (state & hc_state_s_quote)) )
                {
                    state |= hc_state_complete;

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

        buf[ buf_pos++ ] = c;

    } //for() loop over all available data in stream

    // checking if we know this command
    cur_cmd = find_command_index((const char*)buf);

    if ( cur_cmd == -1 )
    {
        if (flags & hc_flag_interactive)
        {
            source->println("\nUnknown command.");

            if (prompt != nullptr)
                source->print(prompt);
        }

        init_for_new_input(hc_state_invalid);

        return -1;
    }

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
 * @brief Return parameter as a single byte value
 * 
 * @return uint8_t 
 */
uint8_t host_command::get_byte( void ) const
{
    if ( cur_cmd == -1 || state & hc_state_invalid || cur_param == -1 )
        return 0;

    return buf[0];
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

    if ( buf_pos == buf_len )
        buf[buf_pos - 1] = '\0';
    else
        buf[buf_pos] = '\0';

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
                return -1;
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
