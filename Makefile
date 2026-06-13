CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
AR       := ar
ARFLAGS  := rcs

SRC := VirtualMemory.cpp
OBJ := $(SRC:.cpp=.o)
LIB := libVirtualMemory.a

all: $(LIB)

$(LIB): $(OBJ)
	$(AR) $(ARFLAGS) $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(LIB)

.PHONY: all clean