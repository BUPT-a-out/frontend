#include "symbol_table.h"
#include "AST.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

extern int yylineno;

// --- Data Structures ---

// An entry in a scope's symbol map (name -> symbol ID)
typedef struct ScopeEntry {
    char* name;
    int symbol_id;
    struct ScopeEntry* next; // Simple linked list for hash collisions
} ScopeEntry;

// A single scope, containing a hash map of symbols defined within it
typedef struct Scope {
    ScopeEntry** entries; // Hash map implemented as an array of linked lists
    int capacity;
    int level;
} Scope;

// The global, permanent symbol table storing all symbols
static SymbolTable permanent_table;

// The stack of active scopes
static Scope** scope_stack;
static int scope_stack_top;
static int scope_stack_capacity;
static int next_symbol_id = 0;

#define HASH_MAP_SIZE 256

// --- Helper Functions ---

static unsigned long hash(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash;
}

// --- Symbol Table and Scope Management ---

void init_symbol_management() {
    permanent_table.symb_count = 0;
    permanent_table.symb_capacity = 64;
    permanent_table.symbols = (Symbol*)malloc(permanent_table.symb_capacity * sizeof(Symbol));

    scope_stack_top = -1;
    scope_stack_capacity = 16;
    scope_stack = (Scope**)malloc(scope_stack_capacity * sizeof(Scope*));
    
    // Global scope
    enter_scope();
}

void destroy_symbol_management() {
    // Free permanent table
    for (int i = 0; i < permanent_table.symb_count; i++) {
        free(permanent_table.symbols[i].name);
        free(permanent_table.symbols[i].data_type);
        // Note: other pointers inside symbol might need freeing depending on implementation
    }
    free(permanent_table.symbols);

    // Free scope stack
    while(scope_stack_top >= 0) {
        exit_scope();
    }
    free(scope_stack);
}

Scope* create_scope() {
    Scope* scope = (Scope*)malloc(sizeof(Scope));
    scope->capacity = HASH_MAP_SIZE;
    scope->entries = (ScopeEntry**)calloc(scope->capacity, sizeof(ScopeEntry*));
    scope->level = scope_stack_top + 1;
    return scope;
}

void free_scope(Scope* scope) {
    for(int i = 0; i < scope->capacity; ++i) {
        ScopeEntry* entry = scope->entries[i];
        while(entry) {
            ScopeEntry* temp = entry;
            entry = entry->next;
            free(temp->name);
            free(temp);
        }
    }
    free(scope->entries);
    free(scope);
}

void enter_scope() {
    if (scope_stack_top + 1 >= scope_stack_capacity) {
        scope_stack_capacity *= 2;
        scope_stack = (Scope**)realloc(scope_stack, scope_stack_capacity * sizeof(Scope*));
    }
    scope_stack_top++;
    scope_stack[scope_stack_top] = create_scope();
}

void exit_scope() {
    if (scope_stack_top < 0) return;
    free_scope(scope_stack[scope_stack_top]);
    scope_stack_top--;
}

int get_current_scope_level() {
    if (scope_stack_top < 0) return -1;
    return scope_stack[scope_stack_top]->level;
}

SymbolPtr lookup_symbol_in_scope(const char* name, Scope* scope) {
    unsigned long index = hash(name) % scope->capacity;
    ScopeEntry* entry = scope->entries[index];
    while(entry) {
        if (strcmp(entry->name, name) == 0) {
            return &permanent_table.symbols[entry->symbol_id];
        }
        entry = entry->next;
    }
    return NULL;
}

SymbolPtr lookup_symbol_in_current_scope(const char* name) {
    if (scope_stack_top < 0) {
        return NULL;
    }
    return lookup_symbol_in_scope(name, scope_stack[scope_stack_top]);
}


SymbolPtr define_symbol(const char* name, const char* type, SymbolType sym_type, ASTNodePtr decl_node) {
    if (lookup_symbol_in_current_scope(name) != NULL) {
        fprintf(stderr, "Error at line %d: Redeclaration of symbol '%s'\n", yylineno, name);
        // In a real compiler we might try to recover, but here we'll exit.
        exit(EXIT_FAILURE);
        return NULL;
    }

    // Add to permanent table
    if (permanent_table.symb_count >= permanent_table.symb_capacity) {
        permanent_table.symb_capacity *= 2;
        permanent_table.symbols = (Symbol*)realloc(permanent_table.symbols, permanent_table.symb_capacity * sizeof(Symbol));
    }
    
    SymbolPtr new_sym = &permanent_table.symbols[permanent_table.symb_count];
    new_sym->id = next_symbol_id;
    new_sym->name = strdup(name);
    new_sym->data_type = strdup(type);
    new_sym->symbol_type = sym_type;
    new_sym->lineno = yylineno;
    new_sym->scope_level = get_current_scope_level();
    new_sym->decl_node = decl_node;

    // Initialize attributes union
    if (sym_type == SYMB_FUNCTION) {
        new_sym->attributes.func_info.param_count = 0;
        new_sym->attributes.func_info.params = NULL;
    } else if (sym_type == SYMB_ARRAY || sym_type == SYMB_CONST_ARRAY) {
        new_sym->attributes.array_info.dimensions = 0;
        new_sym->attributes.array_info.shape = NULL;
    }
    
    // Add to current scope's hash map
    Scope* current_scope = scope_stack[scope_stack_top];
    unsigned long index = hash(name) % current_scope->capacity;
    ScopeEntry* new_entry = (ScopeEntry*)malloc(sizeof(ScopeEntry));
    new_entry->name = strdup(name);
    new_entry->symbol_id = new_sym->id;
    new_entry->next = current_scope->entries[index];
    current_scope->entries[index] = new_entry;
    
    if (decl_node) {
        decl_node->symb_id = new_sym->id;
        decl_node->symb_ptr = new_sym;
    }

    permanent_table.symb_count++;
    next_symbol_id++;

    return new_sym;
}

SymbolPtr lookup_symbol(const char* name) {
    for (int i = scope_stack_top; i >= 0; i--) {
        SymbolPtr symbol = lookup_symbol_in_scope(name, scope_stack[i]);
        if (symbol) {
            return symbol;
        }
    }
    return NULL;
}

SymbolPtr get_symbol_by_id(int id) {
    if (id >= 0 && id < permanent_table.symb_count) {
        return &permanent_table.symbols[id];
    }
    return NULL;
}

void print_symbol_table() {
    printf("\n--- Permanent Symbol Table ---\n");
    printf("%-5s %-20s %-15s %-10s %-10s\n", "ID", "Name", "Type", "Data Type", "Scope");
    printf("------------------------------------------------------------------\n");
    for (int i = 0; i < permanent_table.symb_count; i++) {
        SymbolPtr s = &permanent_table.symbols[i];
        const char* type_str;
        switch(s->symbol_type) {
            case SYMB_VARIABLE: type_str = "Variable"; break;
            case SYMB_CONSTANT: type_str = "Constant"; break;
            case SYMB_FUNCTION: type_str = "Function"; break;
            case SYMB_ARRAY: type_str = "Array"; break;
            case SYMB_CONST_ARRAY: type_str = "Const Array"; break;
            default: type_str = "Unknown"; break;
        }
        printf("%-5d %-20s %-15s %-10s %-10d\n", s->id, s->name, type_str, s->data_type, s->scope_level);
    }
    printf("------------------------------------------------------------------\n\n");
} 