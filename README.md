# Host commands processing library

Use to create multi-parameter command processor for your device, getting input from host or main controller hardware via serial interface.  
E.g.:

        SETRGBCOLOR led1 255 255 255
        LED 3 OFF
        LCDTEXT "Hello!"
        REBOOT
        etc.

Intended to be used with Arduino's framework.  

Original code by Andrej Pakhutin (pakhutin@gmail.com), 2021
* Main repository: <https://github.com/kadavris/host_command>
* The testing framework used is:  
Google C++ Testing and Mocking Framework (Google Test).  
Copyright 2008, Google Inc.  
All rights reserved.  

## Examples:
Create instance with internal buffer of 64 bytes and commands source set to `Serial` object:

        setup()
        {
            host_command hc(64, Serial);

### Define several commands by using quick, printf-like interface:
Note that this list numbering is mirroring the actual internal index

0) Command without parameters:

            hc.new_command( "Command1" );

1) Require 1 boolean argument

            hc.new_command( "Command2", "b" );

2) Require integer argument, followed by quoted string

            hc.new_command( "Some_command", "dq" );

3) Require one boolean arg, followed by two **optional** string arguments:

            hc.new_command( "Other_command", "b ? s s" );
        }

### Now we can ask for some action:

        loop()
        {
            if ( ! hc.get_next_command() )
            {
                do_something_else();
                return;
            }

All indexes start from zero of course

            if ( hc.get_command_id() == 0 )
            {
                    // OK, this is "Command1". no arguments
                    do_something_other();
                    return;
            }

And if our currently processed command has index 2:  
Remember "Some_command" with int, quoted str arguments?

            if ( hc.get_command_id() == 2 )
            {

Now with this command we need device's full attention

                 while( ! hc.is_command_complete() )
                 {

We can get here -1 on error, or 0, 1 for indexes

                     switch( hc.get_parameter_index() )
                     {

*Note that all conversions are simple and (still) there are no validation. So if host put incorrect data somewhere you will receive garbage. In case of int/float it is most probably will be zero number*

                     case 0:
                         first_arg = hc.get_int();
                         break;

Be aware, that `get_str()` always return the pointer to the beginning of the internal buffer. Do not overwrite it contents or you may get unexpected behavior!

                     case 1:
                         strcpy( string_arg, hc.get_str() );
                         break;

                     default:
                         cout << "Some error occured while processing command: " << hc.get_command_name() << endl;

                         break;
                     }//switch param index
                 }//while command is incomplete
            }// command id 2: "Some_command"
        }//loop

## Quick reference
The class name is host_command

Constructors:
* `host_command( size_t buffer_size )` - Will use `Serial` as a commands source. Set buffer size so big to accomodate the longest parameter any command can expect. Plus one.
* `host_command( size_t buffer_size, Stream* source )`

public properties:
* `String* prompt` - if not NULL and **interactive mode** is **ON** it will be printed to host as a new command prompt.
* `Stream* source` - The source of commands. Yes you can switch this on the fly if you're so bold.

Setup methods:
* `int new_command( String command_name, String parameters )` -  return -1 on error.  
  The second parameter uses printf-like codes to define parameters for a command.  
  Format is slightly simpler though: [?][length][type]
  *   ? - this marks beginning of optional parameters
  *   length - integer. set the _maximum_ input length.
  *   type - much as in printf: `b`-bool, `c`-byte, `d`-int, `f`-float, `s`-string, `q`-quoted string

  Spaces also allowed for readability
* `void new_command( String command_name )` - Either this is a command without arguments or you must add parameters definitions via the following methods:
* `void add_bool_param()` - appends boolean parameter to the current command's arguments list
* `void add_byte_param()` - appends single-byte parameter to the current command's arguments list
* `void add_int_param()` - appends integer number parameter to the current command's arguments list
* `void add_float_param()` - appends floating point number parameter to the current command's arguments list
* `void add_str_param( int max_length )` - appends string parameter to the current command's arguments list.  
`max_length` is optional and limits the length of input string. Default is 65535
* `void add_qstr_param( int max_length )` - appends quoted string parameter to the current command's arguments list.  
`max_length` is optional and limits the length of input string. Default is 65535
* `void optional_from_here()` - the next parameters added will be treated as optional. This means that no error will be generated if some will be omitted on input

Processing methods:
* `bool get_next_command()` - request to begin processing of new command from the input stream. Return `true` if new command is available
* `int get_command_id()` - Return `id` or index of the current command being processed. -1 if there are no command data. 0 - based
* `String get_command_name()` - Return current command's name or `""` if none.
* `bool is_command_complete()` - Return `true` if current command's processing is complete and you may pass to the next.
* `bool has_next_parameter()` - Return `true` if there are next parameter's data available.
* `int get_parameter_index()` - Return the index of the current parameter. 0 - based
* `uint32_t get_parameter_info()` - Return internal bitmask with parameter definition. Well, in case you want to process parameters by type, disregarding their positions.

Getters:
* `bool get_bool()` - Return boolean representation of parameter's data.  
This is slightly smarter than others. For the `true` value it expect the one of case-insensitive strings: *"on", "true", "yes", "y"* or any positive, non-zero number, e.g.  *1, 42 or 007*
* `uint8_t get_byte()` - Return the first character of parameter.
* `int get_int()` - Converts input string to `int`. In case of any error the value returned is undefined or zero.
* `float get_float()` - Converts input string to `float`. In case of any error the value returned is undefined or zero.
* `const char* get_str()` - Return input data. Quotes are removed for the quoted string argument type. If there are errors or data inconsistencies it honestly tries to return `""` (empty string).
*Note that this function return value actually is a pointer to the internal buffer. So any mangling with data there may produce unexpected results*

Other public members:
* `void set_interactive(bool is_on, String* new_prompt)` - if true then we'll produce some answer/error messages to host
* `void allow_escape(bool is_on)` - allow the use of escape character `'\'` to mask special characters like end of line or space.  
**Enabled by default.**
* `void discard()` - reset the state and prepare for the next command.  
If current command is still incomplete it will skip all input up to EOL: `'\n'`
