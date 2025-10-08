# =======================================
# Makefile for file compare program
# =======================================

# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -O2

# Target executable
TARGET = file_compare

# Source files
SRCS = file_compare.c

# Default target
all: $(TARGET)

# Build the executable
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

# Clean build files
clean:
	rm -f $(TARGET) *.o

# Phony targets
.PHONY: all clean

