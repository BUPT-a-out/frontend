#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include "symbol.h"

// --- Enum for Node Types ---
typedef enum {
    // Top-level
    NODE_ROOT,
    NODE_EMPTY,
    NODE_LIST,

    // Declarations
    NODE_CONST_DECL,
    NODE_VAR_DECL,
    NODE_FUNC_DEF,
    NODE_FUNC_HEAD,
    
    // Definitions
    NODE_CONST_DEF,
    NODE_VAR_DEF,
    NODE_PARAM_LIST,

    // Types
    NODE_TYPE,

    // Statements
    NODE_ASSIGN_STMT,
    NODE_IF_STMT,
    NODE_WHILE_STMT,
    NODE_BLOCK_STMT,
    NODE_RETURN_STMT,
    NODE_BREAK_STMT,
    NODE_CONTINUE_STMT,
    NODE_EXPR_STMT,

    // Expressions
    NODE_BINARY_OP,
    NODE_UNARY_OP,
    NODE_FUNC_CALL,
    NODE_ARG_LIST,
    NODE_ARRAY_ACCESS,
    NODE_FUNC_F_PARAMS,
    NODE_FUNC_R_PARAMS,
    NODE_CONST_ARRAY_INIT_LIST,
    NODE_ARRAY_INIT_LIST,
    NODE_ARRAY_IDENT,
    NODE_CONST_ARRAY_IDENT,

    // Literals & identifiers
    NODE_IDENT,
    NODE_INT_CONST,
    NODE_FLOAT_CONST,

} NodeType;


// --- AST Node Structure ---
typedef struct ASTNode {
    NodeType node_type;
    char* value;
    int lineno;
    
    struct ASTNode** children;
    int child_count;
    int child_capacity;

    int symb_id;
    SymbolPtr symb_ptr;
    char* data_type;
} ASTNode;

typedef ASTNode* ASTNodePtr;


// --- AST Creation/Deletion Functions ---
ASTNodePtr create_ast_node(NodeType type, const char* value, int num_children, ...);
ASTNodePtr create_leaf(NodeType type, const char* value);
void add_child(ASTNodePtr parent, ASTNodePtr child);
void free_ast(ASTNodePtr node);


// --- AST Traversal/Printing Functions ---
void print_ast(ASTNodePtr node, int level);
const char* node_type_to_string(NodeType type);

// --- Semantic/Symbol Functions (might move) ---
void set_node_symbol(ASTNodePtr node, SymbolPtr symbol);


#ifdef __cplusplus
}
#endif