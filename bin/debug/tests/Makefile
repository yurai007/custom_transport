PATH_TO_SOURCES :=  ../../../src/tests/
PATH_TO_EXT_SOURCES :=  ../../../src/custom_transport/
CXXFLAGS += -std=c++14 -W -Wall -g
program_NAME := tests

program_CXX_SRCS := $(wildcard $(PATH_TO_EXT_SOURCES)*.cpp $(PATH_TO_SOURCES)*.cpp)
program_CXX_OBJS := ${program_CXX_SRCS:.cpp=.o}
program_OBJS := $(program_CXX_OBJS)
program_INCLUDE_DIRS := /home/djurczak/Downloads/boost_1_58_0
program_INCLUDE_DIRS2 := /home/djurczak/Downloads/boost_1_58_0/process/boost-process
program_LIBRARY_DIRS := /home/djurczak/Downloads/boost_1_58_0/bin.v2/libs/system/build/gcc-5.2.0/release/link-static/threading-multi
program_LIBRARIES := boost_system

CPPFLAGS += $(foreach includedir,$(program_INCLUDE_DIRS),-I$(includedir))
CPPFLAGS += $(foreach includedir,$(program_INCLUDE_DIRS2),-I$(includedir))
LDFLAGS += $(foreach librarydir,$(program_LIBRARY_DIRS),-L$(librarydir))
LDFLAGS += $(foreach library,$(program_LIBRARIES),-l$(library))

.PHONY: all clean distclean

all: $(program_OBJS)
	$(LINK.cc) $(program_OBJS) -o $(program_NAME) -lboost_system -lpthread

clean:
	@- $(RM) $(program_NAME)
	@- $(RM) $(program_OBJS)

distclean: clean
