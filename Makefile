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
	rm -f examples/glibc_test examples/glibc_cpp_test examples/glibc_sdl2_test
	rm -rf bin

examples: all
	@echo "Building glibc example test binaries..."
	@./fglibc gcc examples/glibc_test.c -o examples/glibc_test
	@./fglibc g++ -std=c++11 -pthread examples/glibc_cpp_test.cpp -o examples/glibc_cpp_test
	@./fglibc gcc examples/glibc_sdl2_test.c -lSDL2 -lGL -o examples/glibc_sdl2_test
	@echo "Examples built successfully."

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/fglibc
	@echo "Installed to $(DESTDIR)$(BINDIR)/fglibc"

test: all examples
	./scripts/stress_test.sh

.PHONY: all clean install test examples
