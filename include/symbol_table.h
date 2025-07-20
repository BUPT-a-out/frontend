#pragma once

#include <stdarg.h>
#include <stdbool.h>

// --- Symbol Table ---

// Type of symbol
typedef enum {
    SYMB_VAR,
    SYMB_CONST_VAR,
    SYMB_ARRAY,
    SYMB_CONST_ARRAY,
    SYMB_FUNCTION,
    SYMB_UNKNOWN
} SymbolType;

// Type of data
typedef enum {
    DATA_INT,
    DATA_FLOAT,
    DATA_CHAR,
    DATA_BOOL,
    DATA_VOID,
    DATA_UNKNOWN,
} DataType;

// Information about a constant symbol
typedef struct {
    int int_value;
    float float_value;
} ConstInfo;

// Information about an array symbol
typedef struct {
    int dimensions;
    int* shape;  // An array representing the size of each dimension
} ArrayInfo;

// Information about a function symbol
typedef struct {
    int param_count;
    struct Symbol** params;  // Array of pointers to parameter symbols
} FuncInfo;

// Symbol structure
typedef struct Symbol {
    int id;
    char* name;
    SymbolType symbol_type;
    DataType data_type;

    union {
        ConstInfo const_info;
        ArrayInfo array_info;
        FuncInfo func_info;
    } attributes;

    int lineno;
    int scope_level;  // The scope depth where the symbol is defined
} Symbol, *SymbolPtr;

// The global symbol table storing all symbols
typedef struct SymbolTable {
    Symbol** symbols;
    int symb_count;
    int symb_capacity;
} SymbolTable;

// Lookup symbol in global symbol table
SymbolPtr get_symbol_by_id(int id);

// - Debugging -

const char* symbol_type_to_string(SymbolType type);
const char* data_type_to_string(DataType type);
void print_symbol_table();

// --- Scope Management ---

#define HASH_MAP_SIZE 256

// An entry in a scope's symbol map (name -> symbol ID)
typedef struct ScopeEntry {
    char* name;
    int symbol_id;
    struct ScopeEntry* next;  // Simple linked list for hash collisions
} ScopeEntry;

// A single scope, containing a hash map of symbols defined within it
typedef struct Scope {
    ScopeEntry** entries;  // Hash map implemented as an array of linked lists
    int capacity;
    int level;
} Scope;

// The stack of active scopes
typedef struct ScopeStack {
    Scope** scopes;
    int top;
    int capacity;
} ScopeStack;

// Symbol Table and Scope Management
void init_symbol_management();
void free_symbol_management();

void enter_scope();
void exit_scope();
int get_current_scope_level();

// Symbol Definition (add symbol to both symbol table and scope table)
SymbolPtr define_symbol(const char* name, SymbolType sym_type,
                        DataType data_type, int lineno);

// Lookup symbol in scope table
SymbolPtr lookup_symbol(const char* name);
SymbolPtr lookup_symbol_in_current_scope(const char* name);
