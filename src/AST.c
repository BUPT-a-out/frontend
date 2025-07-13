#include "AST.h"
#include "symbol.h" // For SymbolPtr in print_ast
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

extern int yylineno;

ASTNodePtr create_ast_node(NodeType type, const char* value, int num_children, ...) {
    ASTNodePtr node = (ASTNodePtr)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Memory allocation failed for ASTNode\n");
        exit(EXIT_FAILURE);
    }

    node->node_type = type;
    node->value = value ? strdup(value) : NULL;
    node->lineno = yylineno;
    node->child_count = num_children;
    node->child_capacity = num_children > 0 ? num_children : 4;
    node->children = (ASTNodePtr*)malloc(node->child_capacity * sizeof(ASTNodePtr));
    if (!node->children && node->child_capacity > 0) {
        fprintf(stderr, "Memory allocation failed for ASTNode children\n");
        exit(EXIT_FAILURE);
    }
    
    node->data_type = NULL;
    node->symb_id = -1;
    node->symb_ptr = NULL;

    va_list args;
    va_start(args, num_children);
    for (int i = 0; i < num_children; i++) {
        node->children[i] = va_arg(args, ASTNodePtr);
    }
    va_end(args);

    return node;
}

ASTNodePtr create_leaf(NodeType type, const char* value) {
    return create_ast_node(type, value, 0);
}

void add_child(ASTNodePtr parent, ASTNodePtr child) {
    if (!parent) return;
    if (!child) return; // Do not add null children

    if (parent->child_count >= parent->child_capacity) {
        parent->child_capacity = (parent->child_capacity == 0) ? 4 : parent->child_capacity * 2;
        parent->children = (ASTNodePtr*)realloc(parent->children, parent->child_capacity * sizeof(ASTNodePtr));
        if (!parent->children) {
            fprintf(stderr, "Memory reallocation failed for resizing children\n");
            exit(EXIT_FAILURE);
        }
    }
    parent->children[parent->child_count++] = child;
}

void free_ast(ASTNodePtr node) {
    if (!node) return;
    for (int i = 0; i < node->child_count; i++) {
        free_ast(node->children[i]);
    }
    free(node->children);
    free(node->value);
    free(node->data_type);
    // Note: Does not free symb_ptr, as that is owned by the symbol table.
    free(node);
}

// Helper to get string representation of NodeType
const char* node_type_to_string(NodeType type) {
    switch(type) {
        case NODE_ROOT: return "ROOT";
        case NODE_EMPTY: return "EMPTY";
        case NODE_LIST: return "LIST";
        case NODE_CONST_DECL: return "CONST_DECL";
        case NODE_VAR_DECL: return "VAR_DECL";
        case NODE_FUNC_DEF: return "FUNC_DEF";
        case NODE_FUNC_HEAD: return "FUNC_HEAD";
        case NODE_CONST_DEF: return "CONST_DEF";
        case NODE_VAR_DEF: return "VAR_DEF";
        case NODE_PARAM_LIST: return "PARAM_LIST";
        case NODE_TYPE: return "TYPE";
        case NODE_ASSIGN_STMT: return "ASSIGN_STMT";
        case NODE_IF_STMT: return "IF_STMT";
        case NODE_WHILE_STMT: return "WHILE_STMT";
        case NODE_BLOCK_STMT: return "BLOCK_STMT";
        case NODE_RETURN_STMT: return "RETURN_STMT";
        case NODE_BREAK_STMT: return "BREAK_STMT";
        case NODE_CONTINUE_STMT: return "CONTINUE_STMT";
        case NODE_EXPR_STMT: return "EXPR_STMT";
        case NODE_BINARY_OP: return "BINARY_OP";
        case NODE_UNARY_OP: return "UNARY_OP";
        case NODE_FUNC_CALL: return "FUNC_CALL";
        case NODE_ARG_LIST: return "ARG_LIST";
        case NODE_ARRAY_ACCESS: return "ARRAY_ACCESS";
        case NODE_FUNC_F_PARAMS: return "FUNC_F_PARAMS";
        case NODE_FUNC_R_PARAMS: return "FUNC_R_PARAMS";
        case NODE_CONST_ARRAY_INIT_LIST: return "CONST_ARRAY_INIT_LIST";
        case NODE_ARRAY_INIT_LIST: return "ARRAY_INIT_LIST";
        case NODE_ARRAY_IDENT: return "ARRAY_IDENT";
        case NODE_CONST_ARRAY_IDENT: return "CONST_ARRAY_IDENT";
        case NODE_IDENT: return "IDENT";
        case NODE_INT_CONST: return "INT_CONST";
        case NODE_FLOAT_CONST: return "FLOAT_CONST";
        default: return "UNKNOWN_NODE";
    }
}


void print_ast(ASTNodePtr node, int level) {
    if (!node) {
        return;
    }

    // Indentation
    for (int i = 0; i < level; i++) {
        printf("|   ");
    }

    // Node information
    printf("+-- %s", node_type_to_string(node->node_type));
    if (node->value) {
        printf(": %s", node->value);
    }
    if (node->symb_ptr) {
        printf(" (sym: %s, id: %d)", node->symb_ptr->name, node->symb_ptr->id);
    }
    printf("\n");

    // Children
    for (int i = 0; i < node->child_count; i++) {
        print_ast(node->children[i], level + 1);
    }
} 