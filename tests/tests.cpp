/**
 * @file tests.cpp
 * @author Andrej Pakhutin (pakhutin <at> gmail.com)
 * @brief Contains testing for host_command class
 * @version 0.1
 * @date 2021-12-11
 * 
 * @copyright Copyright (c) 2021
 * 
 */

// Using Google Test
#include "gtest/gtest.h"
#include "test_Stream.hpp"
#include "../host_command.hpp"

//===================================================================
namespace {
    class host_commandTest : public ::testing::Test
    {
    protected:

        host_commandTest()
        {
            Serial.clear();
        }

        ~host_commandTest() override
        {
            Serial.clear();
        }

        //void SetUp() override {}
        //void TearDown() override { }

    };

    //======================================================
    TEST_F(host_commandTest, test_bad_Constructor_Parameters)
    {
        EXPECT_ANY_THROW({ host_command bad(-1); }); // bad buffer size
    }

    //======================================================
    TEST_F(host_commandTest, test_Duplicate_Command_Name)
    {
        host_command hc(1);

        EXPECT_EQ( hc.new_command("C1", "d"),  1 );
        EXPECT_EQ( hc.new_command("C2", "10sf"), 2 );

        EXPECT_EQ( hc.new_command("C1", "q"),  -1 );
        EXPECT_FALSE( hc.new_command("C2") );
    }

    //======================================================
    TEST_F(host_commandTest, test_bad_Parameters)
    {
        host_command hc(1);

        EXPECT_EQ(hc.new_command("C1", "?d"), -1);
        EXPECT_EQ(hc.new_command("C2", "BAD"),  -1);
        EXPECT_EQ(hc.new_command("C3", "-10q"), -1);
        EXPECT_EQ(hc.new_command("C4", "d?d?d"), -1);
    }

    //======================================================
    TEST_F(host_commandTest, test_Clean_State)
    {
        host_command hc(1);

        EXPECT_EQ(hc.get_command_id(), -1);
        EXPECT_STREQ(hc.get_command_name().c_str(), "");
        EXPECT_EQ(hc.get_parameter_index(), -1);
        EXPECT_TRUE(hc.is_command_complete());
    }

    //======================================================
    TEST_F(host_commandTest, test_method_discard)
    {
        host_command hc(64);

        hc.discard();
        EXPECT_EQ(hc.get_command_id(), -1);
        EXPECT_STREQ(hc.get_command_name().c_str(), "");
        EXPECT_EQ(hc.get_parameter_index(), -1);
        EXPECT_TRUE(hc.is_command_complete());

        EXPECT_EQ(hc.new_command("CMD1", "b"), 1);
        EXPECT_EQ(hc.new_command("CMD2", "bb"), 2);
        EXPECT_EQ(hc.new_command("CMD3", "bdd"), 3);

        // process command with all parameters:
        Serial.add_input("CMD1 True\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_TRUE(hc.get_bool());
        EXPECT_EQ(hc.get_command_id(), 0);
        EXPECT_EQ(hc.get_parameter_index(), 0);
        EXPECT_TRUE(hc.is_command_complete());

        hc.discard();

        // process only 1st non-optional parameter:
        Serial.add_input("CMD2 ok nope\n");

        EXPECT_EQ(hc.get_command_id(), -1);
        EXPECT_EQ(hc.get_parameter_index(), -1);
        EXPECT_TRUE(hc.is_command_complete());

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 1);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_TRUE(hc.get_bool()); // ok
        EXPECT_EQ(hc.get_parameter_index(), 0);

        EXPECT_FALSE(hc.is_command_complete());

        hc.discard();

        EXPECT_TRUE(hc.is_command_complete());

        // should get all parameters after previous improper discard():
        Serial.add_input("CMD3 y 4 2 \n");

        EXPECT_EQ(hc.get_command_id(), -1);
        EXPECT_EQ(hc.get_parameter_index(), -1);
        EXPECT_TRUE(hc.is_command_complete());

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 2);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_TRUE(hc.get_bool()); // y
        EXPECT_EQ(hc.get_parameter_index(), 0);

        EXPECT_FALSE(hc.is_command_complete());

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_int(), 4);
        EXPECT_EQ(hc.get_parameter_index(), 1);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_int(), 2);
        EXPECT_EQ(hc.get_parameter_index(), 2);

        EXPECT_TRUE(hc.is_command_complete());
    }

    //======================================================
    TEST_F(host_commandTest, test_Invalid_State)
    {
        host_command hc(64);

        // should abort on reading 1st parameter due to missing 2nd one
        EXPECT_EQ(hc.new_command("C1", "d s"), 2);
        Serial.add_input("C1 123\n");

        EXPECT_TRUE( hc.get_next_command() );
        EXPECT_EQ( hc.get_command_id(), 0 );

        EXPECT_EQ( hc.get_parameter_index(), -1 );
        EXPECT_FALSE( hc.is_command_complete() );

        EXPECT_FALSE( hc.has_next_parameter() );
        EXPECT_EQ( hc.get_int(), 0 );
        EXPECT_TRUE(hc.is_invalid_input());

        // should abort on reading of the 1st parameter due to missing 2nd one
        // Variant with optional_index set
        EXPECT_EQ(hc.new_command("C2", "d d ?s"), 3);
        Serial.add_input("C2 456 \n789 abcd\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 1);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 0);
        EXPECT_EQ(hc.get_int(), 456);
        EXPECT_FALSE(hc.is_invalid_input());

        EXPECT_FALSE(hc.is_command_complete());

        EXPECT_FALSE(hc.has_next_parameter());

        EXPECT_TRUE(hc.is_invalid_input());
        EXPECT_TRUE(hc.is_command_complete());

        EXPECT_FALSE(hc.get_next_command());
    }

    //======================================================
    TEST_F(host_commandTest, test_Optional_Parameters)
    {
        host_command hc(64);

        EXPECT_EQ(hc.new_command("CO1", "d ? d"), 2);
        EXPECT_EQ(hc.new_command("CO2", "d ? d ? d"), -1); // slightly buggy. the command will be in the list though
        EXPECT_EQ(hc.new_command("CO3", "d ? ddd"), 4);

        Serial.add_input("Co1 123 -456\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 0);

        EXPECT_EQ(hc.get_parameter_index(), -1);
        EXPECT_FALSE(hc.is_command_complete());

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_int(), 123);
        EXPECT_TRUE(hc.is_command_complete()); // optionals are not counted
        EXPECT_FALSE(hc.no_more_parameters()); // but here they are

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_TRUE(hc.is_optional());
        EXPECT_EQ(hc.get_int(), -456);

        EXPECT_TRUE(hc.is_command_complete());
        EXPECT_TRUE(hc.no_more_parameters());

        // CO2 command has buggy param definition, so we skip it now

        Serial.add_input("co3 12 34 56\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 2);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 0);
        EXPECT_EQ(hc.get_int(), 12);

        EXPECT_TRUE(hc.is_command_complete());
        EXPECT_FALSE(hc.no_more_parameters());
        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 1);
        EXPECT_EQ(hc.get_int(), 34);

        EXPECT_TRUE(hc.is_command_complete());
        EXPECT_FALSE(hc.no_more_parameters());
        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 2);
        EXPECT_EQ(hc.get_int(), 56);

        EXPECT_TRUE(hc.is_command_complete());
        EXPECT_TRUE(hc.no_more_parameters());

        EXPECT_FALSE(hc.is_invalid_input());

        EXPECT_FALSE(hc.get_next_command());
    }

    //======================================================
    TEST_F(host_commandTest, test_Extra_Params)
    {
        host_command hc(64);

        EXPECT_EQ(hc.new_command("C1", "d"), 1);
        EXPECT_EQ(hc.new_command("C2", "s d"), 2);
        Serial.add_input("C1 123 C2 str 42\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 0);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_int(), 123);

        EXPECT_TRUE(hc.is_command_complete());
        EXPECT_TRUE(hc.no_more_parameters());

        EXPECT_FALSE(hc.has_next_parameter());
        EXPECT_FALSE(hc.is_invalid_input());

        EXPECT_FALSE(hc.get_next_command()); //should skip extra till EOL
        EXPECT_FALSE(hc.is_invalid_input());
    }

    //======================================================
    TEST_F(host_commandTest, test_Spaces)
    {
        host_command hc(64);

        EXPECT_EQ(hc.new_command("C1", "d s"), 2);
        EXPECT_EQ(hc.new_command("C2", "c d"), 2);

        Serial.add_input("     C1         123       abcd\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 0);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 0);
        EXPECT_EQ(hc.get_int(), 123);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_STREQ(hc.get_str(), "abcd");

        EXPECT_TRUE(hc.is_command_complete());

        Serial.add_input("\n\n   \n  C2    A       2021\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 1);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 0);
        EXPECT_EQ(hc.get_byte(), 'A');

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 1);
        EXPECT_EQ(hc.get_int(), 2021);
    }

    //======================================================
    TEST_F(host_commandTest, test_Bool_Params)
    {
        host_command hc(64, &Serial);

        EXPECT_EQ( hc.new_command("CMD1", "b"), 1);
        EXPECT_EQ( hc.new_command("CMD2", "bb"), 2);
        EXPECT_EQ( hc.new_command("CMD3", "bbb"), 3);
        EXPECT_EQ( hc.new_command("CMD4", "bbbb"), 4);

        Serial.add_input( "CMD1 True\n" );
        
        EXPECT_EQ( hc.get_command_id(), -1 );

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_STREQ( hc.get_command_name().c_str(), "CMD1");

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_TRUE(hc.get_bool());
        EXPECT_EQ(hc.get_parameter_index(), 0);
        EXPECT_TRUE(hc.is_command_complete());

        Serial.add_input("CMD2 yes false\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 1);
        EXPECT_STREQ(hc.get_command_name().c_str(), "CMD2");
        
        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 0);
        EXPECT_TRUE(hc.get_bool());
        
        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_FALSE(hc.get_bool());
        
        EXPECT_TRUE(hc.is_command_complete());

        Serial.add_input("CMD3 On 005 YES\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 2);
        EXPECT_STREQ(hc.get_command_name().c_str(), "CMD3");

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_TRUE(hc.get_bool());

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_TRUE(hc.get_bool());

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_TRUE(hc.get_bool());

        EXPECT_EQ(hc.get_parameter_index(), 2);
        EXPECT_TRUE(hc.is_command_complete());

        Serial.add_input("CMD4 ");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 3);
        EXPECT_STREQ(hc.get_command_name().c_str(), "CMD4");

        Serial.add_input("000 ");

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_FALSE(hc.get_bool());

        Serial.add_input("TRUE ");
        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_TRUE(hc.get_bool());

        Serial.add_input("nope ");
        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_FALSE(hc.get_bool());

        Serial.add_input("1\n");
        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_TRUE(hc.get_bool());

        EXPECT_EQ(hc.get_parameter_index(), 3);
        EXPECT_TRUE(hc.is_command_complete());
    }

    //======================================================
    TEST_F(host_commandTest, test_Int_Params)
    {
        host_command hc(64, &Serial);

        EXPECT_EQ(hc.new_command("c1", "d"), 1);
        EXPECT_EQ(hc.new_command("cmd2", "d d"), 2);
        EXPECT_EQ(hc.new_command("command3", "dd d "), 3);
        EXPECT_EQ(hc.new_command("4", " dd dd"), 4);

        Serial.add_input("c1 1\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 0);

        EXPECT_STREQ(hc.get_command_name().c_str(), "c1");

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_int(), 1);
        EXPECT_EQ(hc.get_parameter_index(), 0);
        EXPECT_TRUE(hc.is_command_complete());

        Serial.add_input("cMD2 42 1234567\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 1);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_int(), 42);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_int(), 1234567);

        EXPECT_TRUE(hc.is_command_complete());

        Serial.add_input("coMManD3 123.456 -9856 12a34\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 2);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_int(), 123);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_int(), -9856);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_int(), 12);

        EXPECT_TRUE(hc.is_command_complete());

        Serial.add_input("4 a bb7 000005e3 00000\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 3);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_int(), 0);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_int(), 0);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_int(), 5);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_int(), 0);

        EXPECT_TRUE(hc.is_command_complete());
    }

    //======================================================
    TEST_F(host_commandTest, test_Float_Params)
    {
        host_command hc(64, &Serial);

        EXPECT_EQ(hc.new_command("f1", "f"), 1);
        EXPECT_EQ(hc.new_command("f2", "ff"), 2);
        EXPECT_EQ(hc.new_command("f3", "fff"), 3);
        EXPECT_EQ(hc.new_command("f4", "ffff"), 4);

        Serial.add_input("f1 42\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 0);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_FLOAT_EQ(hc.get_float(), 42.0f);
        EXPECT_EQ(hc.get_parameter_index(), 0);

        EXPECT_TRUE(hc.is_command_complete());

        Serial.add_input("f2 -21.43 91234567.0\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 1);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_FLOAT_EQ(hc.get_float(), -21.43f);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_FLOAT_EQ(hc.get_float(), 91234567.0f);

        EXPECT_TRUE(hc.is_command_complete());

        Serial.add_input("f3 123. .98356 -.4623\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 2);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_FLOAT_EQ(hc.get_float(), 123.0f);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_FLOAT_EQ(hc.get_float(), 0.98356f);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_FLOAT_EQ(hc.get_float(), -0.4623f);

        EXPECT_TRUE(hc.is_command_complete());

        Serial.add_input("f4 12.34e05 023.67e-003 -.000001e12 0e1\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 3);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_float(), 12.34e5f);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_float(), 23.67e-3f);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_float(), -.000001e12f);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_float(), 0e1f);

        EXPECT_TRUE(hc.is_command_complete());
    }

    //======================================================
    TEST_F(host_commandTest, test_String_Params)
    {
        host_command hc(64, &Serial);

        EXPECT_EQ(hc.new_command("s1", "s"), 1);
        EXPECT_EQ(hc.new_command("s2", "ss"), 2);
        EXPECT_EQ(hc.new_command("s3", "sss"), 3);

        Serial.add_input("s1 2021\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 0);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 0);
        EXPECT_STREQ(hc.get_str(), "2021");
        EXPECT_EQ(hc.get_int(), 2021);

        Serial.add_input("s2 1\\ 2 3\\ \n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 1);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 0);
        EXPECT_STREQ(hc.get_str(), "1 2");

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 1);
        EXPECT_STREQ(hc.get_str(), "3 ");

        Serial.add_input("s3 \\     12\\3\\4     56\\ 78\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 2);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 0);
        EXPECT_STREQ(hc.get_str(), " ");

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 1);
        EXPECT_STREQ(hc.get_str(), "1234");

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 2);
        EXPECT_STREQ(hc.get_str(), "56 78");

        hc.allow_escape(false);

        Serial.add_input("s3 \\     12\\3\\4     56\\ 78\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 2);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 0);
        EXPECT_STREQ(hc.get_str(), "\\");

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 1);
        EXPECT_STREQ(hc.get_str(), "12\\3\\4");

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 2);
        EXPECT_STREQ(hc.get_str(), "56\\");

        EXPECT_TRUE(hc.is_command_complete());
        EXPECT_FALSE(hc.is_invalid_input());
    }

    //======================================================
    TEST_F(host_commandTest, test_Quoted_String_Params)
    {
        host_command hc(64, &Serial);

        EXPECT_EQ(hc.new_command("q1", "q"), 1);
        EXPECT_EQ(hc.new_command("q2", "qq"), 2);
        EXPECT_EQ(hc.new_command("q3", "qqq"), 3);

        Serial.add_input("q1 '42'\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 0);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 0);
        EXPECT_STREQ(hc.get_str(), "42");

        Serial.add_input("q2 '4\"2' \"5 ' 6\"\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 1);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 0);
        EXPECT_STREQ(hc.get_str(), "4\"2");

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 1);
        EXPECT_STREQ(hc.get_str(), "5 ' 6");

        Serial.add_input("q3 '1\n222\n333' \"'4444'\" '\"5\"'\n");

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 2);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 0);
        EXPECT_STREQ(hc.get_str(), "1\n222\n333");

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 1);
        EXPECT_STREQ(hc.get_str(), "'4444'");

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 2);
        EXPECT_STREQ(hc.get_str(), "\"5\"");

        Serial.add_input("q2 '\\'42\\'' \"\\\"24\\\"\"\n"); // '42' "24" :-() ;)

        EXPECT_TRUE(hc.get_next_command());
        EXPECT_EQ(hc.get_command_id(), 1);

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 0);
        EXPECT_STREQ(hc.get_str(), "'42'");

        EXPECT_TRUE(hc.has_next_parameter());
        EXPECT_EQ(hc.get_parameter_index(), 1);
        EXPECT_STREQ(hc.get_str(), "\"24\"");

    }
};

//===================================================================
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
