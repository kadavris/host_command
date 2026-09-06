PATH1="."
CPP=g++

optsdebug=-Wall -Wpedantic -ggdb -Og -DHOST_CMD_TEST=1
optsrelease=-Wall -O2 -DHOST_CMD_TEST=1

obj=../src/host_command.o test_Stream.o
includes=-I../include -I.
tsuite=gtest-all.cc gtest/*.h

# by default we make debug compile
all: OPTS=$(optsdebug)
all: tests

%.o: %.cpp
	$(CPP) -c $(OPTS) $< -o $@ $(includes) -DHOST_CMD_TEST

%.o: %.cc
	$(CPP) -c $(OPTS) $< -o $@ $(includes) -DHOST_CMD_TEST

tests: $(obj) $(tsuite) tests.cpp
	$(CPP) $(OPTS) -o $@ tests.cpp gtest-all.cc $(obj) $(includes) -DHOST_CMD_TEST

clean:
	rm -f ../src/*.o *.o tests.exe
