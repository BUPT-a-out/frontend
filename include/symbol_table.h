#pragma once

#include "symbol.h"
#include <stdarg.h>

// Forward declaration
struct ASTNode;

// Symbol Table and Scope Management
void init_symbol_management();
void destroy_symbol_management();

void enter_scope();
void exit_scope();
int get_current_scope_level();

// Symbol Definition
SymbolPtr define_symbol(const char* name, const char* type, SymbolType sym_type, struct ASTNode* decl_node);

// Symbol Lookup
SymbolPtr lookup_symbol(const char* name);
SymbolPtr lookup_symbol_in_current_scope(const char* name);
SymbolPtr get_symbol_by_id(int id);

// Debugging
void print_symbol_table(); 