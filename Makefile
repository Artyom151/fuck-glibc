CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Iinclude
LDFLAGS ?= 

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

SRCS = src/main.cpp src/config.cpp src/utils.cpp src/elf_parser.cpp src/bootstrap.cpp src/runner.cpp src/installer_ui.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = bin/fglibc

all: $(TARGET)
	@ln -sf $(TARGET) fglibc
	@echo "Build complete: ./fglibc"

$(TARGET): $(OBJS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) fglibc
	rm -rf bin

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/fglibc
	@echo "Installed to $(DESTDIR)$(BINDIR)/fglibc"

test: all
	./scripts/stress_test.sh

.PHONY: all clean install test
