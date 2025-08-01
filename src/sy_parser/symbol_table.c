#include "sy_parser/symbol_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sy_parser/utils.h"

// Symbol Table instance
static SymbolTable permanent_table;

// Scope Stack instance
static ScopeStack scope_stack;

// Function scope (in which function)
static SymbolPtr func_scope;

void init_symbol_management() {
    permanent_table.symb_count = 0;
    permanent_table.symb_capacity = 64;
    permanent_table.symbols =
        (Symbol**)malloc(permanent_table.symb_capacity * sizeof(SymbolPtr));

    scope_stack.top = -1;
    scope_stack.capacity = 16;
    scope_stack.scopes = (Scope**)malloc(scope_stack.capacity * sizeof(Scope*));

    // Init function scope
    func_scope = NULL;

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

void add_symbol_to_symbol_table(SymbolPtr symbol) {
    if (permanent_table.symb_count >= permanent_table.symb_capacity) {
        permanent_table.symb_capacity *= 2;
        permanent_table.symbols = (Symbol**)realloc(
            permanent_table.symbols,
            permanent_table.symb_capacity * sizeof(SymbolPtr));
    }
    permanent_table.symbols[permanent_table.symb_count++] = symbol;
}

SymbolPtr get_symbol_by_id(int id) {
    if (id >= 0 && id < permanent_table.symb_count) {
        return permanent_table.symbols[id];
    }
    return NULL;
}

Scope* create_scope() {
    Scope* scope = (Scope*)malloc(sizeof(Scope));
    scope->capacity = HASH_MAP_SIZE;
    scope->entries = (ScopeEntry**)calloc(scope->capacity, sizeof(ScopeEntry*));
    scope->level = scope_stack.top + 1;
    return scope;
}

void free_scope(Scope* scope) {
    ScopeEntry *entry, *temp;
    for (int i = 0; i < scope->capacity; ++i) {
        entry = scope->entries[i];
        while (entry) {
            temp = entry;
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

void add_symbol_to_current_scope(SymbolPtr symbol) {
    const char* name = symbol->name;
    Scope* current_scope = scope_stack.scopes[scope_stack.top];
    unsigned long index = my_str_hash(name) % current_scope->capacity;
    ScopeEntry* new_entry = (ScopeEntry*)malloc(sizeof(ScopeEntry));
    new_entry->name = my_strdup(name);
    new_entry->symbol_id = symbol->id;
    new_entry->next = current_scope->entries[index];
    current_scope->entries[index] = new_entry;
}

void enter_function(SymbolPtr func_symb) {
    if (func_symb->symbol_type != SYMB_FUNCTION) return;
    func_scope = func_symb;
}

void exit_function() { func_scope = NULL; }

SymbolPtr get_current_function_scope() { return func_scope; }

void add_symbol_to_function_vars(SymbolPtr symbol) {
    FuncInfo func_scope_info = func_scope->attributes.func_info;
    if (!func_scope_info.var_capacity) {
        func_scope_info.var_capacity = 2;
        func_scope_info.vars = (Symbol**)malloc(2 * sizeof(SymbolPtr));
    }
    if (func_scope_info.var_count >= func_scope_info.var_capacity) {
        func_scope_info.var_capacity *= 2;
        func_scope_info.vars =
            (Symbol**)realloc(func_scope_info.vars,
                              func_scope_info.var_capacity * sizeof(SymbolPtr));
    }
    func_scope_info.vars[func_scope_info.var_count++] = symbol;
    func_scope->attributes.func_info = func_scope_info;
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
    SymbolPtr new_sym = (SymbolPtr)malloc(sizeof(Symbol));
    new_sym->id = permanent_table.symb_count;
    new_sym->name = my_strdup(name);
    new_sym->function = get_current_function_scope();
    new_sym->symbol_type = sym_type;
    new_sym->data_type = data_type;
    new_sym->lineno = lineno;
    new_sym->scope_level = get_current_scope_level();

    // Initialize attributes union
    if (sym_type == SYMB_FUNCTION) {
        FuncInfo func_info;
        func_info.params = NULL;
        func_info.vars = NULL;
        func_info.param_count = 0;
        func_info.var_count = 0;
        func_info.var_capacity = 0;
        func_info.call_count = 0;
        new_sym->attributes.func_info = func_info;
    } else if (sym_type == SYMB_ARRAY || sym_type == SYMB_CONST_ARRAY) {
        ArrayInfo array_info;
        array_info.shape = NULL;
        array_info.dimensions = 0;
        array_info.elem_num = 1;
        new_sym->attributes.array_info = array_info;
    }

    // Add symbol to several position
    add_symbol_to_symbol_table(new_sym);
    add_symbol_to_current_scope(new_sym);
    if (func_scope) add_symbol_to_function_vars(new_sym);

    return new_sym;
}

SymbolPtr lookup_symbol(const char* name) {
    SymbolPtr symbol;
    for (int i = scope_stack.top; i >= 0; i--) {
        symbol = lookup_symbol_in_scope(name, scope_stack.scopes[i]);
        if (symbol) {
            return symbol;
        }
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
    printf("%-5s %-20s %-15s %-10s %-20s %-10s\n", "ID", "Name", "Type",
           "Data Type", "Function", "Shape");
    printf(
        "----------------------------------------------------------------------"
        "---------------\n");
    SymbolPtr sym_ptr;
    for (int i = 0; i < permanent_table.symb_count; i++) {
        sym_ptr = permanent_table.symbols[i];
        printf("%-5d %-20s %-15s %-10s", sym_ptr->id, sym_ptr->name,
               symbol_type_to_string(sym_ptr->symbol_type),
               data_type_to_string(sym_ptr->data_type));
        if (sym_ptr->function)
            printf(" %-20s", sym_ptr->function->name);
        else
            printf(" %-20s", "N/A");
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
        "---------------\n\n");
}
