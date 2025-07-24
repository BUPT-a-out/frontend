#pragma once

#include <stdarg.h>
#include <stdbool.h>

#include "sy_parser/symbol_table.h"

// --- AST ---

// Enum for Node Types
typedef enum {
    // Top-level
    NODE_ROOT,

    // Placeholding
    NODE_EMPTY,

    // List of anything
    NODE_LIST,

    // Types
    NODE_TYPE,

    // Definitions
    NODE_VAR_DEF,
    NODE_CONST_VAR_DEF,
    NODE_ARRAY_DEF,
    NODE_CONST_ARRAY_DEF,
    NODE_ARRAY_INIT_LIST,
    NODE_CONST_ARRAY_INIT_LIST,
    NODE_FUNC_DEF,

    // Statements
    NODE_ASSIGN_STMT,
    NODE_IF_STMT,
    NODE_IF_ELSE_STMT,
    NODE_WHILE_STMT,
    NODE_RETURN_STMT,
    NODE_BREAK_STMT,
    NODE_CONTINUE_STMT,

    // Expressions
    NODE_CONST,
    NODE_VAR,
    NODE_CONST_VAR,
    NODE_ARRAY,
    NODE_CONST_ARRAY,
    NODE_ARRAY_ACCESS,
    NODE_CONST_ARRAY_ACCESS,
    NODE_FUNC_CALL,
    NODE_UNARY_OP,
    NODE_BINARY_OP,

    // Don't change the type, used in function set_ast_node_data()
    HOLD_NODETYPE,
} NodeType;

// Node Data
typedef union {
    SymbolPtr symb_ptr;
    int direct_int;
    float direct_float;
    char* direct_str;
    DataType data_type;
} NodeData;

typedef enum {
    NO_DATA,
    SYMB_DATA,
    INT_DATA,
    FLOAT_DATA,
    STRING_DATA,
    TYPE_DATA,

    // Like HOLD_NODETYPE
    HOLD_NODEDATATYPE,
} NodeDataType;

// AST Node Structure
typedef struct ASTNode {
    NodeType node_type;
    char* name;
    int lineno;

    NodeData data;
    NodeDataType data_type;

    struct ASTNode** children;
    int child_count;
    int child_capacity;
} ASTNode, *ASTNodePtr;

// - AST Create/Delete Functions -

// Create AST node and set its data_type to NO_DATA
ASTNodePtr create_ast_node(NodeType type, const char* name, int lineno,
                           int num_children, ...);
// Delete AST node and its children recursively
void free_ast(ASTNodePtr node);

// - AST Edit Functions -

// Set AST's node data (except data about children)
void set_ast_node_data(ASTNodePtr node, NodeType type, const char* name,
                       NodeData data, NodeDataType data_type, int lineno);
// Add child (pass NULL child automatically)
void add_child(ASTNodePtr parent, ASTNodePtr child);
// Add src's children to des and set src's children to NULL
void shift_child(ASTNodePtr src, ASTNodePtr des);

// - Debugging -

// Helper to get string representation of NodeType
const char* node_type_to_string(NodeType type);
void print_ast(ASTNodePtr node, int level);
