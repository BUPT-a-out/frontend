#include "symbol_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

// Symbol Table instance
static SymbolTable permanent_table;

// Scope Stack instance
static ScopeStack scope_stack;

void init_symbol_management() {
    permanent_table.symb_count = 0;
    permanent_table.symb_capacity = 64;
    permanent_table.symbols =
        (Symbol**)malloc(permanent_table.symb_capacity * sizeof(SymbolPtr));

    scope_stack.top = -1;
    scope_stack.capacity = 16;
    scope_stack.scopes = (Scope**)malloc(scope_stack.capacity * sizeof(Scope*));

    // Global scope
    enter_scope();
}

void free_symbol_management() {
    // Free permanent table
    for (int i = 0; i < permanent_table.symb_count; i++) {
        free(permanent_table.symbols[i]->name);
        free(permanent_table.symbols[i]);
        // Note: other pointers inside symbol might need freeing depending on
        // implementation
    }
    free(permanent_table.symbols);

    // Free scope stack
    while (scope_stack.top >= 0) {
        exit_scope();
    }
    free(scope_stack.scopes);
}

Scope* create_scope() {
    Scope* scope = (Scope*)malloc(sizeof(Scope));
    scope->capacity = HASH_MAP_SIZE;
    scope->entries = (ScopeEntry**)calloc(scope->capacity, sizeof(ScopeEntry*));
    scope->level = scope_stack.top + 1;
    return scope;
}

void free_scope(Scope* scope) {
    for (int i = 0; i < scope->capacity; ++i) {
        ScopeEntry* entry = scope->entries[i];
        while (entry) {
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
    if (scope_stack.top + 1 >= scope_stack.capacity) {
        scope_stack.capacity *= 2;
        scope_stack.scopes = (Scope**)realloc(
            scope_stack.scopes, scope_stack.capacity * sizeof(Scope*));
    }
    scope_stack.top++;
    scope_stack.scopes[scope_stack.top] = create_scope();
}

void exit_scope() {
    if (scope_stack.top < 0) return;
    free_scope(scope_stack.scopes[scope_stack.top]);
    scope_stack.top--;
}

int get_current_scope_level() {
    if (scope_stack.top < 0) return -1;
    return scope_stack.scopes[scope_stack.top]->level;
}

SymbolPtr lookup_symbol_in_scope(const char* name, Scope* scope) {
    unsigned long index = my_str_hash(name) % scope->capacity;
    ScopeEntry* entry = scope->entries[index];
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            return permanent_table.symbols[entry->symbol_id];
        }
        entry = entry->next;
    }
    return NULL;
}

SymbolPtr lookup_symbol_in_current_scope(const char* name) {
    if (scope_stack.top < 0) {
        return NULL;
    }
    return lookup_symbol_in_scope(name, scope_stack.scopes[scope_stack.top]);
}

SymbolPtr define_symbol(const char* name, SymbolType sym_type,
                        DataType data_type, int lineno) {
    if (lookup_symbol_in_current_scope(name) != NULL) {
        fprintf(stderr, "Error at line %d: Redeclaration of symbol '%s'\n",
                lineno, name);
        // In a real compiler we might try to recover, but here we'll exit.
        exit(EXIT_FAILURE);
        return NULL;
    }

    // Add to permanent table
    if (permanent_table.symb_count >= permanent_table.symb_capacity) {
        permanent_table.symb_capacity *= 2;
        permanent_table.symbols = (Symbol**)realloc(
            permanent_table.symbols,
            permanent_table.symb_capacity * sizeof(SymbolPtr));
    }

    SymbolPtr new_sym = (SymbolPtr)malloc(sizeof(Symbol));
    new_sym->id = permanent_table.symb_count;
    new_sym->name = my_strdup(name);
    new_sym->symbol_type = sym_type;
    new_sym->data_type = data_type;
    new_sym->lineno = lineno;
    new_sym->scope_level = get_current_scope_level();
    permanent_table.symbols[permanent_table.symb_count] = new_sym;

    // Initialize attributes union
    if (sym_type == SYMB_FUNCTION) {
        new_sym->attributes.func_info.param_count = 0;
        new_sym->attributes.func_info.params = NULL;
    } else if (sym_type == SYMB_ARRAY || sym_type == SYMB_CONST_ARRAY) {
        new_sym->attributes.array_info.dimensions = 0;
        new_sym->attributes.array_info.shape = NULL;
    }

    // Add to current scope's hash map
    Scope* current_scope = scope_stack.scopes[scope_stack.top];
    unsigned long index = my_str_hash(name) % current_scope->capacity;
    ScopeEntry* new_entry = (ScopeEntry*)malloc(sizeof(ScopeEntry));
    new_entry->name = my_strdup(name);
    new_entry->symbol_id = new_sym->id;
    new_entry->next = current_scope->entries[index];
    current_scope->entries[index] = new_entry;

    permanent_table.symb_count++;

    return new_sym;
}

SymbolPtr lookup_symbol(const char* name) {
    for (int i = scope_stack.top; i >= 0; i--) {
        SymbolPtr symbol = lookup_symbol_in_scope(name, scope_stack.scopes[i]);
        if (symbol) {
            return symbol;
        }
    }
    return NULL;
}

SymbolPtr get_symbol_by_id(int id) {
    if (id >= 0 && id < permanent_table.symb_count) {
        return permanent_table.symbols[id];
    }
    return NULL;
}

const char* symbol_type_to_string(SymbolType type) {
    switch (type) {
        case SYMB_VAR:
            return "var";
        case SYMB_CONST_VAR:
            return "const var";
        case SYMB_ARRAY:
            return "array";
        case SYMB_CONST_ARRAY:
            return "const array";
        case SYMB_FUNCTION:
            return "function";
        default:
            return "unknown";
    }
}

const char* data_type_to_string(DataType type) {
    switch (type) {
        case DATA_INT:
            return "int";
        case DATA_FLOAT:
            return "float";
        case DATA_CHAR:
            return "char";
        case DATA_BOOL:
            return "bool";
        case DATA_VOID:
            return "void";
        default:
            return "unknown";
    }
}

void print_symbol_table() {
    printf("\n--- Permanent Symbol Table ---\n");
    printf("%-5s %-20s %-15s %-10s %-10s %-10s\n", "ID", "Name", "Type",
           "Data Type", "Scope", "Shape");
    printf(
        "----------------------------------------------------------------------"
        "-----\n");
    for (int i = 0; i < permanent_table.symb_count; i++) {
        SymbolPtr sym_ptr = permanent_table.symbols[i];
        printf("%-5d %-20s %-15s %-10s %-10d", sym_ptr->id, sym_ptr->name,
               symbol_type_to_string(sym_ptr->symbol_type),
               data_type_to_string(sym_ptr->data_type), sym_ptr->scope_level);
        if (sym_ptr->symbol_type == SYMB_ARRAY ||
            sym_ptr->symbol_type == SYMB_CONST_ARRAY) {
            for (int j = 0; j < sym_ptr->attributes.array_info.dimensions; j++)
                printf(" %d", sym_ptr->attributes.array_info.shape[j]);
        } else {
            printf(" %-10s", "N/A");
        }
        printf("\n");
    }
    printf(
        "----------------------------------------------------------------------"
        "-----\n\n");
}
