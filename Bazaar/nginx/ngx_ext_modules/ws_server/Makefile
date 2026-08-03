CXX=$(CROSS_COMPILE)g++
CXXFLAGS=-c -Wall -Os -s -static -Wno-psabi -std=c++17 -Iwebsocketpp $(SYSROOT) -L. -lboost_system -DWEBSOCKETPP_STRICT_MASKING -Wno-reorder -Wno-cpp

SOURCES= rp_websocket_server.cpp \
	ws_server.cpp \
	rp_sdk/rp_json.cpp

OBJECTS=$(SOURCES:.cpp=.o)
LIB=libws_server.a

RP_MANAGER_DIR=./rp_sdk
RP_MANAGER_LIB=$(RP_MANAGER_DIR)/librp_sdk.a

all: $(RP_MANAGER_LIB) $(SOURCES) $(LIB)

$(LIB): $(OBJECTS)
	ar rc $(LIB) $(OBJECTS)

.cpp.o:
	$(CXX) $(CXXFLAGS) $< -o $@

$(RP_MANAGER_LIB):
	cd $(RP_MANAGER_DIR); $(MAKE)

clean:
	rm -rf $(LIB) $(OBJECTS)
	$(MAKE) -C $(RP_MANAGER_DIR) clean
