CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable -I./stubs -I../../libs/wifi---esp32

SRC_RADIO = ../../libs/radio/radio.cpp
SRC_WIFI = ../../libs/wifi---esp32/wifi.cpp
SRC_SOCKET = ../../libs/wifi---esp32/socket.cpp

all: test_intercomm test_coex_reverse

test_intercomm: test_intercomm.cpp mock_state.cpp $(SRC_RADIO) $(SRC_WIFI) $(SRC_SOCKET)
	$(CXX) $(CXXFLAGS) -o $@ $^

test_coex_reverse: test_coex_reverse.cpp mock_state.cpp $(SRC_RADIO) $(SRC_WIFI)
	$(CXX) $(CXXFLAGS) -o $@ $^

run: all
	./test_intercomm
	./test_coex_reverse

clean:
	rm -f test_intercomm test_coex_reverse
