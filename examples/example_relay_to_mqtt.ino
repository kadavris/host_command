// This code is copyright (c) 2021 by Andrej Pakhutin
// Distributed under the GPL-2.0 license

// This project example uses esp8266 as a simple network communication agent
// for other boards like arduino.
// It connects to MQTT over Wi-Fi and using standard I/O pins to communicate with main board.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include "host_command.hpp"

// ************** SETUP BEGIN *****************
bool debug = true; // used to print more stuff on console when connected to PC

// Quick and dirty wa to initialize command processor
// "command name1", "params",...
const char* commands[] =
{
#define CMD_DEBUG 0    
    "debug", "b", // single, boolean parameter
#define CMD_MSUB 1
    "mqttSub", "s", // string parameter: subscribe to topic
#define CMD_MUNSUB 2
    "mqttUnSub", "s", // string parameter: un-subscribe from topic
#define CMD_PUB 3
    "mqttPub", "ss?b", // string: topic, string: message, optional boolean retain
    NULL, NULL // End of list mark
};
// ************** SETUP END *****************

// For this example we'll just declare some obvious prototypes here.
extern void mqttConnect();
extern void mqttSubscribe(const char* topic);
extern void mqttUnSubscribe(const char* topic);
extern void mqttPublish(const char* topic, void* payload, unsigned length, bool retain = false, int qos = 0 );

host_command hc(1024); // command processor. set buffer to 1024 bytes
const int tmp_buf_size = 1024;
char tmp_buf[tmp_buf_size]; // for lengthy commands

//=========================================================
void connectToWifi()
{
    while ( WiFi.status() != WL_CONNECTED )
    {
        delay( 500 );
    }

    if ( debug ) 
    {
        Serial.println( ". WiFi connected" );
        Serial.print( ". IP address: " );
        Serial.println( WiFi.localIP() );
        Serial.print( ". MAC: " );
        Serial.println( WiFi.macAddress() );
    }

    // Allow the hardware to sort itself out
    delay( 1500 );
}

//=========================================================
void setupHostCmd()
{
    if(debug) Serial.println(". Initializing commands");

    // every even string in "commands" is a command name and the string right after it is parameters
    for ( int i = 0; commands[i] != NULL; i += 2 )
        if ( commands[ i + 1 ] == NULL ) // no parameters for this command - use short new_command() version
            hc.new_command( commands[i] );
        else
            hc.new_command( commands[i], commands[i + 1] );

    if(debug) Serial.println(". done");
}

//=========================================================
void setup()
{
    Serial.begin( 57600 );

    // wifiConnectHandler = WiFi.onStationModeGotIP( onWifiConnect );
    // wifiDisconnectHandler = WiFi.onStationModeDisconnected( onWifiDisconnect );
    WiFi.begin( wifi_ssid, wifi_password );

    connectToWifi();

    setupHostCmd(); // Look up ;)

    mqttConnect();

    if(debug)
    {
        hc.set_interactive( true, "> " ); // hc will produce prompt and error messages
        Serial.println( "\n+READY\n" );
    }
}

//=========================================================
// this is called from loop() to process incoming commands
void processCommandLine()
{
    // these used to store arguments between this function calls
    static bool retain = false;
    static int msg_len = 0;
    static String topic;
    static String msg;

    // check for hext argument or a new command
    // because some ccommands have optional arguments,
    // we need to be sure that none of it will be thrown away
    // so we use no_more_parameters() instead of is_command_complete()
    if ( hc.no_more_parameters() && ! hc.get_next_command() )
        return;

    if( debug )
    {
        Serial.print( ". CMD ");
        Serial.println( hc.get_command_id() );
    }

    switch( hc.get_command_id() )
    {
        case -1: // shouldn't be here really
            return;

        case CMD_DEBUG:
            if ( hc.has_next_parameter() )
                debug = hc.get_bool();  // boolean "true" parameters can be "true", "on", "yes" and so on.
            break;

        case CMD_MSUB: // "mqttSub topic" - subscribe to topic
            if ( hc.has_next_parameter() )
                mqttSubscribe( hc.get_str() );
            break;

        case CMD_MUNSUB: // "mqttUnSub  topic" - un-subscribe from topic
            if ( hc.has_next_parameter() )
                mqttUnSubscribe( hc.get_str() );
            break;

        case CMD_PUB: // "mqttPub topic message [retain]" - publish message to topic, retain
            if ( ! hc.has_next_parameter() )
                break;

            if ( hc.get_parameter_index() == 0 )
                topic = hc.get_str();
            else if ( hc.get_parameter_index() == 1 )
                msg = hc.get_str();
            else if ( hc.get_parameter_index() == 2 )
                retain = hc.get_bool();

            // this is very relaxed check. in fact we'll miss "retain" parameter's value almost always
            // hc.no_more_parameters() will ensure that unless the string is complete with EOL, we still miss some arguments
            if ( hc.is_command_complete() )
            {
                mqttPublish( topic.c_str(), (void*)msg.c_str(), msg.length(), retain );

                retain = false;
            }
            break;

        // we'll reply about the wrong command only if there's human available.
        default:
            if(debug)
            {
                Serial.print("Oww! processCommandLine(): This command is not in switch(): # ");
                Serial.print( hc.get_command_id() );
                Serial.print( ", name: " );
                Serial.println( hc.get_command_name() );
            }
            break;
    } // switch( hc.get_command_id() )
}

//=========================================================
void loop()
{
    if( ! ( mqttClient.connected() && mqttClient.loop() ) )
    {
        delay(100);
        mqttConnect();
    }
    
    delay(20); // for mqtt stability per README

    processCommandLine();
}
