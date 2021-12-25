/** @file host_command.cpp
 * @brief Used to receive and parse host commands, usually via "Serial" class interface.
 *
 * This module is intended to be used with Arduino framework
*/
#define host_command_cpp
#include "host_command.hpp"

// bytes 0,1 of param definition is the max length
// param types (byte 2):
const uint32_t host_cmd_bool  = 0x00010000;
const uint32_t host_cmd_byte  = 0x00020000;
const uint32_t host_cmd_int   = 0x00040000;
const uint32_t host_cmd_float = 0x00080000;
const uint32_t host_cmd_str   = 0x00100000; //< \S+
const uint32_t host_cmd_qstr  = 0x00200000; //< quoted string

// param flags: 4th byte
//const uint32_t host_cmd_??? = 0x01000000;

const uint32_t host_cmd_flag_interactive = 0x00000001; //< report problems back to host
const uint32_t host_cmd_flag_escapes     = 0x00000002; //< allow escape char '\' to be used

// bitflags used for internal state tracking
const uint32_t cbstate_clean          = 0; //< nothing yet happened
const uint32_t cbstate_complete       = 0x00000001; //< got a full command name or parameter data
const uint32_t cbstate_cmd            = 0x00000002; //< dealing with command name
const uint32_t cbstate_param          = 0x00000004; //< dealing with parameter data
const uint32_t cbstate_EOL            = 0x00000008; //< got an EOL
const uint32_t cbstate_d_quote        = 0x00000010; //< got " - quoted string. used for sanity checking
const uint32_t cbstate_s_quote        = 0x00000020; //< got ' - quoted string. used for sanity checking
const uint32_t cbstate_escape         = 0x00000040; //< got escape symbol 
const uint32_t cbstate_invalid        = 0x10000000; //< got invalid data. waiting for EOL
const uint32_t cbstate_got_some   = cbstate_cmd | cbstate_param; //< use for checking if we stared processing some data already
const uint32_t cbstate_got_quotes = cbstate_d_quote | cbstate_s_quote; //< got 1st quote of quoted string. used for sanity checking

static const char* host_command_errors[] =
{
    "no error",
    "bad parameter's length in definiton",
    "bad char on parameter's definition",
    "attempt to define duplicate command name",
    "required parameter missing",
    "invalid parameters specification for new_command(Source, SPEC)"
};

const int host_command_error_bad_length = 1; //< bad parameter's length on defining stage
const int host_command_error_bad_pcode = 2; //< bad char on parameters defining stage
const int host_command_error_duplicate_command = 3; //< attempt to define duplicate command name
const int host_command_error_required_missing = 4; //< missing argument was not marked as optional
const int host_command_error_invalid_param_spec = 5; //< invalid parameters specification for new_command(x,x)

/**
* @brief Simple, "equal or not" case-insensitive strings comparison
* 
* @param s1: string 1
* @param s2: string 2
* @return true if equal, false is not
*/
static bool same_strings(const char* s1, const char* s2)
{
    if (s1 == s2)
        return true;

    if (s1 == NULL || s2 == NULL)
        return false;

    while (*s1 && *s2)
    {
        if (tolower(*s1) != tolower(*s2))
            return false;

        ++s1;
        ++s2;
    }

    return true;
}

const char* const host_command::errstr() //< return error description
{
    return host_command_errors[err_code];
}

void host_command::_init(size_t _bs, Stream* s)
{
    source = s;
    s->setTimeout(1); // do not wait on commands
    buf_len = _bs;
    buf = new uint8_t[buf_len];
    prompt = nullptr;
    flags = host_cmd_flag_escapes;

    init_for_new_input(cbstate_clean);
}

host_command::host_command(size_t _bs)
{
    _init( _bs, Serial );
}

host_command::host_command( size_t _bs, Stream* src )
{
    _init( _bs, src );
}

host_command::~host_command()
{
    if (buf != nullptr)
        delete buf;
}

/**
 * @brief Internal: reset to process next input command
 * 
 * @param initial state 
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
/** @brief set interactive mode on/off. if true then we'll produce some answer/error messages to host sometimes
* 
* @param bool: new mode
* @return void
 *
 */
void host_command::set_interactive( bool _mode, String *_prompt = nullptr )
{
    if ( _mode )
        flags |= host_cmd_flag_interactive;
    else
        flags &= ~host_cmd_flag_interactive;

    prompt = _prompt;
}

void host_command::allow_escape( bool _mode )
{
    if (_mode)
        flags |= host_cmd_flag_escapes;
    else
        flags &= ~host_cmd_flag_escapes;
}


/** @brief Define the new command in full. Use for quick, C-style definitions
 *
 * @param String: command name
 * @param String: printf-like parameters definition
 * @return int: -1 if _params are incorrect or number of parameters recorded
 *
 * The second parameter uses printf-like codes to define parameters for a command.
 * Format is slightly simpler though:
 * [?][length][type]
 *   ? - this marks beginning of optional parameters
 *   length - integer. set _maximum_ input length.
 *   type - as in printf: b-bool, c-byte, d-int, f-float, s-string, q-quoted string
 */
int host_command::new_command(String _name, String _params)
{
    if (! new_command(_name) )
        return -1;

    host_command_element* cmd = commands.back();

    uint32_t param_info = 0;
    uint32_t param_len = 0;

    for (int i = 0; i < _params.length(); ++i)
    {
        switch (_params[i])
        {
            case '?':
                if (cmd->params.size() == 0 || cmd->optional_start != INT_MAX )
                {
                    err_code = host_command_error_invalid_param_spec;
                    return -1;
                }

                cmd->optional_start = (int)(cmd->params.size()) - 1;
                break;
            case 'b':
                param_info |= host_cmd_bool;
                break;
            case 'c':
                param_info |= host_cmd_byte;
                break;
            case 'd':
                param_info |= host_cmd_int;
                break;
            case 'f':
                param_info |= host_cmd_float;
                break;
            case 'q':
                param_info |= host_cmd_qstr;
                if (param_len == 0)
                    param_len = 65535;
                break;
            case 's':
                param_info |= host_cmd_str;
                if (param_len == 0)
                    param_len = 65535;
                break;
            default:
                if ( isdigit( _params[i] ) ) // length
                {
                    param_len = param_len * 10u + _params[i] - '0';

                    if (param_len > 65535u) //overflow?
                    {
                        err_code = host_command_error_bad_length;
                        return -1;
                    }
                }
                else if ( isspace(_params[i]) ) // allow spaces for readability
                {
                    continue;
                }
                else
                {
                    err_code = host_command_error_bad_pcode;
                    return -1;
                }
        } // switch (_params[i])

        if (param_info & 0x00ff0000) // has command type - saving
        {
            cmd->params.push_back( param_info | param_len );
            param_info = 0;
        }
    } // end of scan through _params

    return (int)(cmd->params.size());
}

/** @brief Start to define the new command. Use this for relaxed, step by step definitions
 *
 * @param String: command name
 * @return bool: if added OK
 *
 */
bool host_command::new_command(String _name)
{
    if (find_command_index(_name.c_str()) != -1)
    {
        err_code = host_command_error_duplicate_command;
        return false;
    }

    host_command_element* cmd = new(host_command_element);
    cmd->name = _name;
    cmd->optional_start = INT_MAX;
    commands.push_back(cmd);

    return true;
}

void host_command::add_bool_param()
{
    commands.back()->params.push_back(host_cmd_bool);
}

void host_command::add_byte_param()
{
    commands.back()->params.push_back(host_cmd_byte);
}

void host_command::add_int_param()
{
    commands.back()->params.push_back(host_cmd_int);
}

void host_command::add_float_param()
{
    commands.back()->params.push_back(host_cmd_float);
}

void host_command::add_str_param(uint16_t len = 65535)
{
    if (len > 65535) //overflow?
    {
        err_code = host_command_error_bad_length;
        len = 65535;
    }

    commands.back()->params.push_back(host_cmd_str | len);
}

void host_command::add_qstr_param(uint16_t len = 65535)
{
    if (len > 65535) //overflow?
    {
        err_code = host_command_error_bad_length;
        len = 65535;
    }

    commands.back()->params.push_back(host_cmd_qstr | len);
}

void host_command::optional_from_here(void)
{
    if (commands.size() == 0)
        return;

    auto cmd = commands.back();

    if (cmd->optional_start == INT_MAX)
        cmd->optional_start = (int)(cmd->params.size()) + 1;
}

/**
* @brief Request to get next command from the input
*
* @return bool: false if none or error, or true if new command has arrived
*/
bool host_command::get_next_command()
{
    discard();

    if (check_input() <= 0)
        return false;

    return true;
}

/**
* @brief Return currently processed command's index
*
* @return int: -1 if none or index of new command available
*/
int host_command::get_command_id()
{
    return cur_cmd;
}

/**
* @brief Return currently processed command's name
*
* @return String: empty if no current command 
*/
String host_command::get_command_name()
{
    if (cur_cmd == -1)
        return String("");

    return commands[cur_cmd]->name;
}

/**
* @brief Check if current command processing is invalidated
*
* @return bool: true if something got broken
*/
bool host_command::is_invalid_input()
{
    return state & cbstate_invalid;
}

/**
* @brief Check if current command processing is done
*
* @return bool: true if complete
*/
bool host_command::is_command_complete()
{
    return cur_cmd == -1 || ( state & (cbstate_EOL | cbstate_invalid) )
           || cur_param + 1 == commands[cur_cmd]->params.size();
}

/**
* @brief Requests to get the next parameter from the input
*
* @return int: false if none/error or true if new parameter's data is available
*/
bool host_command::has_next_parameter()
{
    if (is_command_complete())
        return false;
        
    if (check_input() <= 0)
        return false;

    return true;
}

/**
* @brief Return current parameter's index
*
* @return int: -1 if none or index of new parameter available
*/
int host_command::get_parameter_index()
{
    if (cur_cmd == -1)
        return -1;

    if (cur_param == -1 && check_input() <= 0)
        return -1;

    return cur_param;
}

/**
 * @brief return the value of the current parameter to be extracted.
 *
 * @return uint32_t info bitmask or 0 if something wrong
 */
uint32_t host_command::get_parameter_info()
{
    if (cur_cmd == -1 || cur_param == -1)
        return 0;

    return commands[cur_cmd]->params[cur_param];
}

/**
 * @brief return true if current parameter is optional.
 *
 * @return bool
 */
bool host_command::is_optional()
{
    if( cur_cmd == -1 || cur_param == -1 )
        return false;
    
    return cur_param >= commands[cur_cmd]->optional_start;
}

/**
* @brief Discard the current input and force wait for a new command to arrive if needed
*
* @return void
*/
void host_command::discard()
{
    if (state == cbstate_clean || state & cbstate_EOL)
        init_for_new_input(cbstate_clean); // Clean or already got EOL. just marking clean
    else
        init_for_new_input(cbstate_invalid); // Will wait for EOL
}

/**
 * @brief Internal: Check if there is new command/parameter available to process
 * 
 * @return int -1 on error, 0 if no new data arrived yet, 1 if some
 * 
 * To have a low-memory footprint we'll store and scan
 * for a maximum of one parameter at the time.
 */
int host_command::check_input( )
{
    // checking for previous act's completion and moving forward if necessary
    if ( cur_cmd > -1 && (state & cbstate_complete) ) // have previous parameter complete
    {
        // if got all params already and we're in the complete state, then init for next command
        if ( cur_param + 1 == commands[ cur_cmd ]->params.size() ) // no params or last one
        {
            init_for_new_input( cbstate_clean );
        }
        else // we'll wait for the next parameter then
        {
            cur_param++;
            state = cbstate_param; // we need to reset previous parameter state completely
            buf_pos = 0;
        }
    }

    if ( state & cbstate_EOL ) // maybe got a command with no params or EOL on optional params
    {
        init_for_new_input( cbstate_clean );
    }

    for(;;) // we'll loop while there is still some data in the stream
    {
        int c = source->available();

        if ( c < 0 ) // some error
            return -1;
        
        if ( c == 0 ) // nothing yet
            return 0;

        if ( buf_pos == buf_len ) // overflow. discarding command
        {
            if ( flags & host_cmd_flag_interactive )
                source->println( "\n? Too long input discarded till EOL." );
    
            if ( prompt != nullptr )
                source->print( *prompt );

            discard();
        }

        c = source->read();

        if ( c < 0 ) // error?
            return -1;

        if( state & cbstate_invalid ) // waiting for invalidated input to be ended with LF
        {
            if( c == '\n' )
                init_for_new_input( cbstate_clean );

            continue;
        }

        if ( state & cbstate_escape ) // this char is escaped
        {
            state &= ~cbstate_escape;

            buf[ buf_pos++ ] = c;

            continue;
        }

        // always drop leading spaces
        if ( c != '\n' && buf_pos == 0 && !(state & cbstate_got_quotes) && isspace(c))
        {
            continue;
        }

        if ( isspace(c) ) // checking for EOL or end of cmd/param
        {
            if (!(state & cbstate_got_some) && c == '\n') // skipping empty lines quick
                continue;

            if ( state & cbstate_cmd ) // command name
            {
                if (c == '\n')
                    state |= cbstate_EOL;

                state |= cbstate_complete;

                buf[ buf_pos ] = '\0';

                break; // exit loop to validate command name
            }

            // we have parameter here

            host_command_element *cmd = commands[ cur_cmd ];

            // any space is valid in quoted string
            if ( cmd->params[ cur_param ] & host_cmd_qstr )
            {
                buf[ buf_pos++ ] = c;

                continue;
            }

            if ( c == '\n' )
            {
                state |= cbstate_EOL;

                // checking if this or next param is not optional
                if ( buf_pos == 0 || (cur_param + 1 < cmd->params.size() && cur_param + 1 < cmd->optional_start))
                {
                    if ( flags & host_cmd_flag_interactive )
                    {
                        source->print( "\nAttempt to skip non-optional parameter #" );
                        source->println( cur_param + 1 );
                    }

                    if ( prompt != nullptr )
                        source->print( *prompt );

                    err_code = host_command_error_required_missing;

                    state |= cbstate_invalid;

                    return -1;
                }
            } // if EOL

            state |= cbstate_complete;

            buf[buf_pos] = '\0';

            return 1; // got another complete parameter
        } // got isspace()

        if ( (flags & host_cmd_flag_escapes) && c == '\\' )
        {
            state |= cbstate_escape;

            continue;
        }

        if ( state == cbstate_clean || state & cbstate_cmd ) // still waiting for a command name to complete
        {
            buf[ buf_pos++ ] = c;
            state |= cbstate_cmd;

            continue;
        }

        // we wait for parameter here:

        host_command_element *cmd = commands[ cur_cmd ];

        // check for the beginning/ending quote
        if ( cmd->params[ cur_param ] & host_cmd_qstr && ( c == '"' || c == '\'' ) )
        {
            if ( ! ( state & cbstate_got_quotes ) ) // 1st quote only
            {
                // remember what type of quote used at the beginning
                if ( c == '"' )
                    state |= cbstate_d_quote;
                else
                    state |= cbstate_s_quote;

                continue; // don't store quotes
            }

            if ( ( c == '"'  && (state & cbstate_d_quote) )
              || ( c == '\'' && (state & cbstate_s_quote) ) )
            {
                state |= cbstate_complete;

                buf[ buf_pos ] = '\0';

                return 1;
            }
        }

        //TODO: check if no quotes was used
        //TODO: check for maximum string length

        buf[ buf_pos++ ] = c;

    } //for() loop over all available data in stream

    // checking if we know this command
    cur_cmd = find_command_index((const char*)buf);

    if (cur_cmd == -1)
    {
        if (flags & host_cmd_flag_interactive)
            source->println("\nUnknown command.");

        if (prompt != nullptr)
            source->print(*prompt);

        init_for_new_input(cbstate_invalid);

        return -1;
    }

    return 1;
} // int check

/**
* @brief Internal: return index of command by it's name
* 
* @param: char* - name to find
* @return: -1 on error or index
*/
int host_command::find_command_index(const char* _name)
{
    for ( int i = 0; i < commands.size(); ++i)
    {
        if (same_strings(commands[i]->name.c_str(), (char*)_name))
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
bool host_command::get_bool( )
{
    if ( cur_cmd == -1 || state & cbstate_invalid )
        return false;

    if (cur_param == -1 && check_input() <= 0)
        return false;

    // assume that we'll deal with 'ok','on','true','y','yes' or non-zero number as true
    char first = tolower(*buf);

    if (first == 'o' && buf[2] == '\0' && ( tolower(buf[1]) == 'k' || tolower(buf[1]) == 'n' ) )
        return true;

    if (first == 't')
        return same_strings((char*)buf, "true");

    if (first == 'y')
    {
        if (buf[1]) // check for full word
            return same_strings((char*)buf, "yes");
        else
            return true;
    }

    char *c = (char*)buf; // check if non-zero number
    while (isdigit(*c))
    {
        if (*c != '0')
            return true;
        ++c;
    }

     return false;
}

/**
 * @brief Return parameter as single byte value
 * 
 * @return uint8_t 
 */
uint8_t host_command::get_byte( )
{
    if (cur_cmd == -1 || state & cbstate_invalid)
        return 0;

    if (cur_param == -1 && check_input() <= 0)
        return 0;

    return buf[0];
}

/**
 * @brief Return parameter as integer number
 * 
 * @return int 
 */
int host_command::get_int( )
{
    if (cur_cmd == -1 || state & cbstate_invalid)
        return 0;

    if (cur_param == -1 && check_input() <= 0)
        return 0;

    return atoi( (char*)buf );
}

/**
 * @brief Return parameter as floating point number
 * 
 * @return float 
 */
float host_command::get_float()
{
    if (cur_cmd == -1 || state & cbstate_invalid)
        return 0.0f;

    if (cur_param == -1 && check_input() <= 0)
        return 0.0f;

    return (float)atof( (char*)buf );
}

/**
 * @brief Return parameter as raw char. At least try to return complete or empty string
 *
 * @return const char*
 */
const char* host_command::get_str()
{
    if (cur_cmd == -1 || state & cbstate_invalid)
    {
        buf[0] = '\0';
        return (const char*)buf;
    }

    if (cur_param == -1)
        check_input();

    buf[buf_pos] = '\0';

    return (const char*)buf;
}

/**
* @brief Fill destination buffer with requested number of bytes from source
* 
* @param char* dst - destination buffer
* @param int len - amount of data to get
* @return bool: true if OK, false in case of problems
* 
*/
bool host_command::fill_buffer(char* dst, int len)
{
    int pos = 0;
    int need = 0;

    for (;; delay(200)) // we'll loop while there is still some data in the stream
    {
        int ready = source->available();

        if (ready < 0) // some error
            return false;

        if (ready == 0) // nothing yet
            continue;

        need = ready < len - pos ? ready : len - pos;

        ready = (int)(source->readBytes(dst + pos, need));

        if (ready == need)
            return true;
    }
}
