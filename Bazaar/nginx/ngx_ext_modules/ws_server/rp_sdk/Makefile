SOURCES= DataManager.cpp \
	rp_json.cpp

OBJDIR=./objs
SDKOBJDIR=$(OBJDIR)/rp_sdk

CXX=$(CROSS_COMPILE)g++
CXXFLAGS=-c -s -Wall -Wno-reorder -Wno-cpp -Wno-psabi -Os -static -std=c++17 -fPIC -DNDEBUG -I../../../../tools -I.

ifeq ($(DIGITAL_LOOP),true)
CXXFLAGS+=-DIGITAL_LOOP
endif

OBJECTS=$(patsubst %.cpp,$(SDKOBJDIR)/%.o, $(SOURCES))

LIB=librp_sdk.a

all: $(SOURCES) $(LIB)

$(LIB): $(OBJECTS)
	ar rc $(LIB) $(OBJECTS)

$(SDKOBJDIR)/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -rf $(LIB) $(OBJDIR)
