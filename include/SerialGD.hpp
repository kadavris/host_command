#pragma once
/**
 * @file SerialGD.hpp
 * @author Andrej Pakhutin (pakhutin <at> gmail.com)
 * @brief Header for a Stream superclass with guaranteed delivery
 * @version 1.0.0
 * @date 2023-07-29
 *
 * @copyright Copyright (c) 2023
 * 
 * This file is a part of the host_command library
 * The repo is in github.com/kadavris
 */

#include <inttypes.h>
#include <Stream.h>

const int SGD_max_frame_size = 512; //< frame's data size cant' grow bigger. keep it divisible by 64 (smallest frame size)
const int SGD_xmits_before_going_up = 50; //< how many succeful trasmits should we have before upping frame size?
const int SGD_min_fail_ratio = 5; //< minimal ratio of (total frames / failed frames) to trigger frame downsizing. 5 = 20%, 4 = 25%...

struct SGD_frame_stats { //< a per frame size statistics. will be used to determine the course of actions
    uint32_t total_sent;
    uint32_t total_fail;
};

enum { 
    SGDS_SYNC,       //< Input: sync. waiting for the header tag to come up
    SGDS_INCOMPLETE, //< Input: got header, receiving data. Output: still has less data to send than advertized. Not necessary a full-frame.
    SGDS_SEND_HDR,   //< Output: header being sent
    SGDS_SEND_DATA,  //< Output: data being sent
    SGDS_WAIT_ACK    //< Output: sent all. waiting for ACK
} SGD_state; //< the state of in or out communication subsystem

class SerialGD : public Stream
{
private:
    uint8_t inbuf[SGD_max_frame_size];  //< current input buffer
    uint8_t outbuf[SGD_max_frame_size];  //< current output buffer
    int inbuf_pos, outbuf_pos;  //< position in buffer
    int inbuf_fill, outbuf_fill;  //< buffer filled to position
    int in_frame_type, out_frame_type;  //< index in fstats. Denotes the current frame length here
    int in_frame_number, out_frame_number;  //< number of the currently processed frame
    SGD_state inbuf_state, outbuf_state;  //< status allowing various read, find, etc functions to actually return data
    struct SGD_frame_stats fstats[SGD_max_frame_size / 64]; //< 64 byte to SGD_max_frame_size byte frames
    uint32_t cur_frm_transferred; //< current frame type: transferred total
    uint32_t cur_frm_retransmits; //< current frame type: failed xmits

    Stream* work_stream;  //< communications agent

    bool get_frame();
    bool synchronize();

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

    int timedRead();  //< read stream with timeout
    int timedPeek();  //< peek stream with timeout
    int peekNextDigit(LookaheadMode lookahead, bool detectDecimal);  //< returns the next numeric digit in the stream or -1 if timeout

public:
    SerialGD(Stream* _serial);
    ~SerialGD();

    int available(); //< return how many bytes are available for reading
    void clear();  //< clears the output buffer

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

    int peek();  //< return current byte, without removing it from the buffer
    void process(); //< run a bit of I/O jobs to do. returns after some internal timeout
    int read();  //< read single byte

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

    bool send();  //< sends output buffer content immediately
    size_t space_left(); //< how much space left in outgoing buffer

    size_t write(const uint8_t *buffer, size_t size);
    size_t write(const char *str){ return write( (const uint8_t*)str, strlen(str) ) };
    size_t write(const char *buffer, size_t size){ return write( (const uint8_t*)buffer, size ) };

    template<typename T> void print(T p);
    template<typename T> void println(T p);

};

