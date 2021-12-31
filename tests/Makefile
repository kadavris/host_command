PATH1="."
CPP=g++

optsdebug=-Wall -ggdb -Og -DHOST_CMD_TEST=1
optsrelease=-Wall -O2 -DHOST_CMD_TEST=1

obj=../src/host_command.o test_Stream.o gtest-all.cc

# by default we make debug compile
all: OPTS=$(optsdebug)
all: tests

%.o: %.cpp
	$(CPP) -c $(OPTS) $< -o $@

tests: $(obj)
	$(CPP) $(OPTS) -o $@ tests.cpp $(obj)

clean:
	rm -f ../src/*.o *.o tests.exe
