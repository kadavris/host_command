/**
 * @file SerialGD.cpp
 * @author Andrej Pakhutin (pakhutin <at> gmail.com)
 * @brief Mockup of Arduino's framework class Stream
 * @version 1.0.0
 * @date 2023-07-29
 *
 * @copyright Copyright (c) 2023
 *
 */
#include <iostream>
#include <crc8.h>
#include <crc16.h>
#include "SerialGD.hpp"

const char* SGD_msg_tag = ". SerialGD: ";
const char* SGD_hdr_tag = "SGD!";

const uint8_t frame_type_MASK = 0xE0;  // 1110
const uint8_t frame_type_HSHK = 0xF0;  // 1111 Handshake
const uint8_t frame_type_DATA = 0xC0;  // 1100 Data
const uint8_t frame_type_ACK  = 0xA0;  // 1010 Acknowledge/Receipt
const uint8_t frame_type_NAK  = 0x60;  // 0110 NON-ACK/Resend

const uint8_t cpabilities_compression = 0x01;  // 

struct SGD_frame_header { //< The header of every frame that is going in or out
    //char tag[4];
    uint8_t hdr_crc;
    uint8_t frame_type;
    uint16_t frame_number;
    uint16_t data_len;
    uint16_t data_crc;
};

//======================================================
SerialGD::SerialGD(Stream* parm_serial)
{
    in_buf_pos = 0;
    in_buf_fill = -1;
    frame_index = 0;
    curr_frame_type = 8; // 512 bytes
    work_stream = parm_serial;
    _timeout = 1000;
    memset( fstats, sizeof(fstats), 0 );
    has_valid_frame = false;
}

//======================================================
SerialGD::~SerialGD()
{
}

//======================================================
bool synchronize()
{
    unsigned timeout = millis() + _timeout;

    inbuf_pos = 0;
    inbuf_fill = -1;
    has_valid_frame = false;

    while( inbuf_fill < SGD_max_frame_size || millis() <= timeout )
    {
       if( inbuf_fill < 4 )
       {
           if( ! work_stream.available() )
               continue;
 
           inbuf[inbuf_fill++] = work_stream.read();
           continue;
       }

       if( inbuf[inbuf_fill - 3] == (uint8_t*)SGD_hdr_tag && inbuf[inbuf_fill - 2] == (uint8_t)SGD_hdr_tag[1]
               && inbuf[inbuf_fill - 1] == (uint8_t)SGD_hdr_tag[2] && inbuf[inbuf_fill] == (uint8_t)SGD_hdr_tag[3] )
       {
           inbuf_fill = -1;

           return true;
       }

       if( ! work_stream.available() )
       {
           delay(10);
           continue;
       }
 
       inbuf[inbuf_fill++] = work_stream.read();
    }

    inbuf_fill = -1;
    return false;
}

//======================================================
bool get_frame()
{
    if ( has_valid_frame && inbuf_fill - inbuf_pos > 0 );
        return true; // still smth left

    if( ! synchronize() )
        return false;

    // getting header
    unsigned timeout = millis() + _timeout;

    while( inbuf_fill < sizeof( struct SGD_frame_header ) || millis() <= timeout )
    {
       if( ! work_stream.available() )
       {
           delay(10);
           continue;
       }

       inbuf[inbuf_fill++] = work_stream.read();
    }

    // header's sanity check
    // TODO: here and in other cases we should check if crc is no good too AND THEN bail out completely
    struct SGD_frame_header* hdr = inbuf;
    int problems = 0;

    uint8_t frame_kind = hdr.frame_type & frame_type_MASK;
    uint8_t frame_type_index = hdr.frame_type & (~frame_type_MASK);

    if ( frame_kind != frame_type_DATA && frame_kind != frame_type_ACK && frame_kind != frame_type_NAK )
        ++problems; // bad type

    if ( frame_type_index > SGD_max_frame_size / 64 )
        ++problems; // frame size is off

    if ( hdr.data_len > frame_type_index * 64 || hdr.data_len > SGD_max_frame_size )
        return false;

    if ( hdr.frame_number < frame_number || hdr.frame_number > frame_number + 1 )
        ++problems; // lost some frame probably.


}

//======================================================
int SerialGD::available()
{
    if ( has_valid_frame || get_frame() )
        return inbuf_fill - inbuf_pos;
    
    return 0;
}

//======================================================
int SerialGD::read()
{
    if ( ! get_frame() )
        return -1;

    if ( inbuf_pos <= inbuf_fill )
        return inbuf[inbuf_pos++];

    if (inbuf_pos > 0)   !!!
    {
        buf.clear();
        pos = 0;
    }

    return -1;
}

//======================================================
size_t SerialGD::readBytes(char* buf, int len)
{
    //TODO: 
    throw new std::runtime_error("SerialGD::readBytes is not implemented");
}

//======================================================
    int timedRead(); //< read stream with timeout
    int timedPeek(); //< peek stream with timeout
    int peekNextDigit(LookaheadMode lookahead, bool detectDecimal);  //< returns the next numeric digit in the stream or -1 if timeout

    int available(); //< return how many bytes are available for reading
    int read();  //< read single byte
    int peek();  //< return current byte, without removing it from the buffer

    void setTimeout(unsigned long timeout);  // sets maximum milliseconds to wait for stream data, default is 1 second
    unsigned long getTimeout(void) { return _timeout; }
    
    bool find(char *target);   // reads data from the stream until the target string is found
    bool find(uint8_t *target) { return find ((char *)target); }
    // returns true if target string is found, false if timed out (see setTimeout)

    bool find(char *target, size_t length);   // reads data from the stream until the target string of given length is found
    bool find(uint8_t *target, size_t length) { return find ((char *)target, length); }
    // returns true if target string is found, false if timed out

    bool find(char target) { return find (&target, 1); }

    bool findUntil(char *target, char *terminator);   // as find but search ends if the terminator string is found
    bool findUntil(uint8_t *target, char *terminator) { return findUntil((char *)target, terminator); }

    bool findUntil(char *target, size_t targetLen, char *terminate, size_t termLen);   // as above but search ends if the terminate string is found
    bool findUntil(uint8_t *target, size_t targetLen, char *terminate, size_t termLen) {return findUntil((char *)target, targetLen, terminate, termLen); }

    long parseInt(LookaheadMode lookahead = SKIP_ALL, char ignore = NO_IGNORE_CHAR);
    // returns the first valid (long) integer value from the current position.
    // lookahead determines how parseInt looks ahead in the stream.
    // See LookaheadMode enumeration at the top of the file.
    // Lookahead is terminated by the first character that is not a valid part of an integer.
    // Once parsing commences, 'ignore' will be skipped in the stream.

    float parseFloat(LookaheadMode lookahead = SKIP_ALL, char ignore = NO_IGNORE_CHAR);
    // float version of parseInt

    size_t readBytes( char *buffer, size_t length); // read chars from stream into buffer
    size_t readBytes( uint8_t *buffer, size_t length) { return readBytes((char *)buffer, length); }
    // terminates if length characters have been read or timeout (see setTimeout)
    // returns the number of characters placed in the buffer (0 means no valid data found)

    size_t readBytesUntil( char terminator, char *buffer, size_t length); // as readBytes with terminator character
    size_t readBytesUntil( char terminator, uint8_t *buffer, size_t length) { return readBytesUntil(terminator, (char *)buffer, length); }
    // terminates if length characters have been read, timeout, or if the terminator character  detected
    // returns the number of characters placed in the buffer (0 means no valid data found)

    // Arduino String functions to be added here
    String readString();
    String readStringUntil(char terminator);

    protected:
    long parseInt(char ignore) { return parseInt(SKIP_ALL, ignore); }
    float parseFloat(char ignore) { return parseFloat(SKIP_ALL, ignore); }
    // These overload exists for compatibility with any class that has derived
    // Stream and used parseFloat/Int with a custom ignore character. To keep
    // the public API simple, these overload remains protected.

    struct MultiTarget {
      const char *str;  // string you're searching for
      size_t len;       // length of string you're searching for
      size_t index;     // index used by the search routine.
    };

    // This allows you to search for an arbitrary number of strings.
    // Returns index of the target that is found first or -1 if timeout occurs.
    int findMulti(struct MultiTarget *targets, int tCount);

    size_t write(const char *str);
    size_t write(const uint8_t *buffer, size_t size);
    size_t write(const char *buffer, size_t size);
