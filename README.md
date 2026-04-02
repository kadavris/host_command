# Host commands processing library

Use to create multi-parameter and non-blocking command processor for your device,
getting input from host or main controller hardware using Stream class interface.  
E.g.:

        SetRGBColor led1 255 255 255
        LED 3 Off
        LCDTEXT "Hello World!"
        REBOOT
        etc.

Intended to be used with Arduino's framework.  

Original code by Andrej Pakhutin (pakhutin@gmail.com), 2021+
* Main repository: <https://github.com/kadavris/host_command>
* The testing framework used is:  
Google C++ Testing and Mocking Framework (Google Test).  
Copyright 2008, Google Inc.  
All rights reserved.  

## Examples:
Create instance with internal buffer of 64 bytes and commands source set to `Serial` object:

```C++
setup()
{
    host_command hc(64, Serial);
```

### Define several commands by using quick, printf-like interface:
Note that this list numbering is mirroring the actual internal index

0) Command without parameters:

```C++
    hc.new_command( "Command1" );
```

1) Require 1 boolean argument

```C++
    hc.new_command( "Command2", "b" );
```

2) Require integer argument, followed by quoted string

```C++
    hc.new_command( "Some_command", "dq" );
```

3) Require one boolean arg, followed by two **optional** string arguments:

```C++
    hc.new_command( "Other_command", "b ? s s" );
}
```

### Now we can ask for some action:

```C++
loop()
{
    // check for hext argument or a new command
    if ( hc.no_more_parameters() && ! hc.get_next_command() )
    {
        and_now_for_something_completely_different();
        return;
    }

    // All indexes start from zero of course
    if ( hc.get_command_id() == 0 )
    {
            // OK, this is "Command1". no arguments
            process_command_1();
            return;
    }

    // And if our currently processed command has index 2:  
    // Remember "Some_command" with 1st int and then quoted str arguments?
    if ( hc.get_command_id() == 2 )
    {
        // Now with this command we need device's full attention
        do
        {
            // check input if next parameter is available
            if ( ! hc.has_next_paramerer() )
	        {
                delay(100);
                continue;
            }

            /* Note that all string to specific type values conversions
               are simple and (still) there are no validation.
               So if sender put incorrect data, you will receive garbage.  
               E.g. In case of int/float it is most probably will be zero */

            switch( hc.get_parameter_index() ) // what parameter we have here?
            {
                case 0:  // first parameter is integer
                    first_arg = hc.get_int();
                    break;

                // Be aware, that `get_str()` always returns 
                // the pointer to the beginning of the internal buffer.
                // Do not mess with contents without copying it or you may get crashes!
                case 1:  // second parameter is quoted string
                    strcpy( safe_location, hc.get_str() );
                    break;

                default:
                    Serial.print("Some error occured while processing command: ");
                    Serial.print(hc.get_command_name());

                    break;
            }//switch param index

        // we have no optional parameters for this command.
        // so it is safe to use relaxed status function:
        } while( ! hc.is_command_complete() );
    } // if ( command id == 2 ): "Some_command int qstr"

} //loop()
```

## Quick reference
The class name is `host_command`

### Constructors:
* `host_command( size_t buffer_size )` - Will use `Serial` as a commands source.
   Set buffer size to be latge enough to accomodate the longest parameter any command can expect. Plus one.

* `host_command( size_t buffer_size, Stream* source )`

### Public properties:
* `const char* prompt` - if not null and **interactive mode** is **ON** it will be printed to host as a new command prompt.

* `Stream* source` - The source of commands. Yes you can switch this on the fly if you're so bold.

### Setup methods:
* `int new_command( const char* command_name, const char* parameters )` -  return -1 on error.  
  The second parameter uses printf-like codes to define parameters for a command.  
  Format is slightly simpler though: [?][length]\<type>
  *   ? - this marks beginning of optional parameters
  *   length - integer. set the _maximum_ input length for **string types**.
  *   type - printf - like: `b`-bool, `c`-byte, `d`-int, `f`-float, `s`-string, `q`-quoted string
  
  Spaces also allowed for readability

* `void new_command( const char* command_name )` - Either this is a command without arguments or you must add parameters definitions via the following methods:

* `void add_bool_param()` - appends boolean parameter to the current command's arguments list

* `void add_byte_param()` - appends single-byte parameter to the current command's arguments list

* `void add_int_param()` - appends integer number parameter to the current command's arguments list

* `void add_float_param()` - appends floating point number parameter to the current command's arguments list

* `void add_str_param( int max_length )` - appends string parameter to the current command's arguments list.  
  `max_length` is optional and limits the length of input string. Default is for it to fit into your buffer

* `void add_qstr_param( int max_length )` - appends quoted string parameter to the current command's arguments list.  
  `max_length` is optional and limits the length of input string. Default is for it to fit into your buffer

* `void optional_from_here()` - **this** and all the parameters added later 
  will be treated as optional. This means that no error will be generated if some will be omitted on input

### Processing methods:
* `bool get_next_command()` - request to begin processing of new command from the input stream. Return `true` if new command is available

* `int get_command_id()` - Return `id` or index of the current command being processed. -1 if there are no command data. 0 - based

* `const char* get_command_name()` - Return current command's name or `""` if none.

* `bool is_command_complete()` - Return `true` if current command's processing is _formally_ complete and you may pass to the next.  
  Note that return value here depends not only on the error state, no command present, or complete command with EOL received, but also:
  1) if current parameter is optional
  2) or if current parameter is the last and received complete
  Thus to make sure that **all** parameters were processed use `no_more_parameters()`

* `bool no_more_parameters()` - Return true if all possible parameters were recieved, including optional ones.  
  This status differ from `is_command_complete()` by accounting for optional parameters too and if there is stil no EOL.

* `bool is_invalid_input()` - Return `true` if last attempt to parse data resulted in invalid state.

* `bool has_next_parameter()` - Return `true` if there are next parameter's data available.

* `int get_parameter_index()` - Return the index of the current parameter. 0 - based

* `uint32_t get_parameter_info()` - Return internal bitmask with parameter definition. Well, in case you want to process parameters by type, disregarding their positions.

* `bool is_optional()` - Return `true` if current parameter is optional

### Getters:
* `bool get_bool()` - Return boolean representation of parameter's data.  
  This is slightly smarter than others. For the `true` value it expect the one of case-insensitive strings: *"on", "true", "yes", "y"* or any positive, non-zero number, e.g.  *1, 42 or 007*

* `uint8_t get_byte()` - Return the first character of parameter.

* `int get_int()` - Converts input string to `int`. In case of any error the value returned is undefined or zero.

* `float get_float()` - Converts input string to `float`. In case of any error the value returned is undefined or zero.

* `const char* get_str()` - Return a pointer into internal buffer where string parameter data begins.
  Quotes are removed for the quoted string argument type.
  If there are errors or data inconsistencies it honestly tries to return an empty string.
  Data is finalized with `'\0'` character, so you can be sure that it is always null-terminated and safe to use as a C-string.
  Also it means that you need to set the buffer size large enough.
  If you want to have a larger data transfers you may want to use `fill_buffer()`

### Other public members:
* `void set_interactive(bool is_on, const char** new_prompt)` - if true then we'll produce some answer/error messages to host

* `void allow_escape(bool is_on)` - allow the use of escape character `'\'` to mask special characters like end of line or space.  
  **Enabled by default.**

* `void discard()` - reset the state and prepare for the next command.  
  If current command is still incomplete it will skip all input up to the next EOL character: `CR or LF`

* `bool fill_buffer(char* dst, int len)` - Fills arbitrary buffer with requested number of bytes from the pre-set source for this object.
  Use this if you want to get some raw data instead of pure-text parameters.  
  Usually you want to set up a command without parameters for a fixed-size package or with a data length parameter
  and then use this method to get the data you need.
  Function will block until max_time is out or requested amount of data is received.
  Use limit_time() to set max_time if you want to have a timeout.
  dst is a pointer to the destination buffer
  len - is the amount of data to get there
  returns true if all went OK, false in case of problems or timeout (if max_time is set)

* `void limit_time(int)` - sets maximum time for internal processes in milliseconds. Use to prevent timely blocks on long inputs.  
  default is -1, which is "infinity".
