#pragma once

#include <stdbool.h>

// Forward declaration
struct ASTNode;

// Type of symbol
typedef enum {
    SYMB_UNKNOWN,
    SYMB_VARIABLE,
    SYMB_CONSTANT,
    SYMB_FUNCTION,
    SYMB_ARRAY,
    SYMB_CONST_ARRAY
} SymbolType;

// Information about an array symbol
typedef struct {
    int dimensions;
    int* shape; // An array representing the size of each dimension
} ArrayInfo;

// Information about a function symbol
typedef struct {
    int param_count;
    struct Symbol** params; // Array of pointers to parameter symbols
} FuncInfo;

// Symbol structure
typedef struct Symbol {
    int id;
    char* name;
    char* data_type; // e.g., "int", "float", "void"
    SymbolType symbol_type;
    int lineno;
    int scope_level; // The scope depth where the symbol is defined

    union {
        ArrayInfo array_info;
        FuncInfo func_info;
    } attributes;
    
    struct ASTNode* decl_node; // Pointer to the declaration node in the AST
} Symbol;

typedef Symbol* SymbolPtr;

// A node in the symbol table, representing a collection of symbols
typedef struct SymbolTable {
    Symbol* symbols;
    int symb_count;
    int symb_capacity;
} SymbolTable; 