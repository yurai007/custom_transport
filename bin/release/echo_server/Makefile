PATH_TO_SOURCES :=  ../../../src/echo_server/
PATH_TO_EXT_SOURCES :=  ../../../src/custom_transport/
CXXFLAGS += -std=c++14 -W -Wall -g -Ofast
program_NAME := echo_server

program_CXX_SRCS := $(wildcard $(PATH_TO_EXT_SOURCES)*.cpp $(PATH_TO_SOURCES)*.cpp)
program_CXX_OBJS := ${program_CXX_SRCS:.cpp=.o}
program_OBJS := $(program_CXX_OBJS)
program_INCLUDE_DIRS :=
program_LIBRARY_DIRS :=

CPPFLAGS += $(foreach includedir,$(program_INCLUDE_DIRS),-I$(includedir))
LDFLAGS += $(foreach librarydir,$(program_LIBRARY_DIRS),-L$(librarydir))

.PHONY: all clean distclean

all: $(program_OBJS)
	$(LINK.cc) $(program_OBJS) -o $(program_NAME)

clean:
	@- $(RM) $(program_NAME)
	@- $(RM) $(program_OBJS)

distclean: clean
