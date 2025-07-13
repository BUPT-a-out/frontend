# Executable name
TARGET = parser

# Compiler and Linker
CXX = g++
CC = gcc

# Compiler and Linker flags
CFLAGS = -g -Wall -Wno-deprecated
LDFLAGS =

# List of object files
OBJS = AST.o symbol_table.o y.tab.o lex.yy.o

# Default target
all: $(TARGET)

# Link the executable
$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# Compilation rules for our C files
AST.o: AST.c AST.h
	$(CC) $(CFLAGS) -c $< -o $@

symbol_table.o: symbol_table.c symbol_table.h AST.h
	$(CC) $(CFLAGS) -c $< -o $@

# Compilation rules for generated files
y.tab.o: y.tab.c AST.h symbol_table.h
	$(CC) $(CFLAGS) -c $< -o $@

lex.yy.o: lex.yy.c y.tab.h
	$(CC) $(CFLAGS) -c $< -o $@

# Rules to generate sources from Flex and Bison
y.tab.c y.tab.h: sysy_yacc.y
	bison -dy $<

lex.yy.c: sysy_flex.l y.tab.h
	flex -o $@ $<

# Clean up generated files
clean:
	rm -f $(TARGET) $(OBJS) lex.yy.c y.tab.c y.tab.h

# Test rule
test: $(TARGET)
ifndef T
	# Assuming 'make test' is run from 'frontend' and test files are in a sibling directory
	./$(TARGET) < ../functional/functional/00_main.sy
else
	./$(TARGET) < ${T}
endif

.PHONY: all clean test