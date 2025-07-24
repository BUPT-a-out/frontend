#include "sy_parser/AST.h"

#include <stdio.h>
#include <stdlib.h>

#include "sy_parser/symbol_table.h"
#include "sy_parser/utils.h"

ASTNodePtr create_ast_node(NodeType type, const char* name, int lineno,
                           int num_children, ...) {
    ASTNodePtr node = (ASTNodePtr)malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Memory allocation failed for ASTNode\n");
        exit(EXIT_FAILURE);
    }

    node->node_type = type;
    node->name = my_strdup(name);
    node->lineno = lineno;
    node->data_type = NO_DATA;
    node->child_count = num_children;
    node->child_capacity = num_children > 0 ? num_children : 4;
    node->children =
        (ASTNodePtr*)malloc(node->child_capacity * sizeof(ASTNodePtr));
    if (!node->children && node->child_capacity > 0) {
        fprintf(stderr, "Memory allocation failed for ASTNode children\n");
        exit(EXIT_FAILURE);
    }

    va_list args;
    va_start(args, num_children);
    for (int i = 0; i < num_children; i++) {
        node->children[i] = va_arg(args, ASTNodePtr);
    }
    va_end(args);

    return node;
}

void set_ast_node_data(ASTNodePtr node, NodeType type, const char* name,
                       NodeData data, NodeDataType data_type, int lineno) {
    if (type != HOLD_NODETYPE) node->node_type = type;
    if (name) node->name = my_strdup(name);
    if (data_type != HOLD_NODEDATATYPE) {
        node->data = data;
        node->data_type = data_type;
    }
    if (lineno >= 0) node->lineno = lineno;
}

void add_child(ASTNodePtr parent, ASTNodePtr child) {
    if (!parent || !child) return;  // Do not add null children

    if (parent->child_count >= parent->child_capacity) {
        parent->child_capacity =
            (parent->child_capacity == 0) ? 4 : parent->child_capacity * 2;
        parent->children = (ASTNodePtr*)realloc(
            parent->children, parent->child_capacity * sizeof(ASTNodePtr));
        if (!parent->children) {
            fprintf(stderr,
                    "Memory reallocation failed for resizing children\n");
            exit(EXIT_FAILURE);
        }
    }
    parent->children[parent->child_count++] = child;
}

void shift_child(ASTNodePtr src, ASTNodePtr des) {
    if (!src || !des) return;

    for (int i = 0; i < src->child_count; i++) {
        add_child(des, src->children[i]);
        src->children[i] = NULL;
    }
    src->child_count = 0;
}

void free_ast(ASTNodePtr node) {
    if (!node) return;
    for (int i = 0; i < node->child_count; i++) {
        free_ast(node->children[i]);
    }
    free(node->children);
    free(node->name);
    // Note: Does not free symb_ptr, as that is owned by the symbol table.
    free(node);
}

const char* node_type_to_string(NodeType type) {
    switch (type) {
        case NODE_ROOT:
            return "ROOT";
        case NODE_EMPTY:
            return "EMPTY";
        case NODE_LIST:
            return "LIST";
        case NODE_TYPE:
            return "TYPE";
        case NODE_VAR_DEF:
            return "VAR_DEF";
        case NODE_CONST_VAR_DEF:
            return "CONST_VAR_DEF";
        case NODE_ARRAY_DEF:
            return "ARRAY_DEF";
        case NODE_CONST_ARRAY_DEF:
            return "CONST_ARRAY_DEF";
        case NODE_ARRAY_INIT_LIST:
            return "ARRAY_INIT_LIST";
        case NODE_CONST_ARRAY_INIT_LIST:
            return "CONST_ARRAY_INIT_LIST";
        case NODE_FUNC_DEF:
            return "FUNC_DEF";
        case NODE_ASSIGN_STMT:
            return "ASSIGN_STMT";
        case NODE_IF_STMT:
            return "IF_STMT";
        case NODE_IF_ELSE_STMT:
            return "IF_ELSE_STMT";
        case NODE_WHILE_STMT:
            return "WHILE_STMT";
        case NODE_RETURN_STMT:
            return "RETURN_STMT";
        case NODE_BREAK_STMT:
            return "BREAK_STMT";
        case NODE_CONTINUE_STMT:
            return "CONTINUE_STMT";
        case NODE_CONST:
            return "CONST";
        case NODE_VAR:
            return "VAR";
        case NODE_CONST_VAR:
            return "CONST_VAR";
        case NODE_ARRAY:
            return "ARRAY";
        case NODE_CONST_ARRAY:
            return "CONST_ARRAY";
        case NODE_ARRAY_ACCESS:
            return "ARRAY_ACCESS";
        case NODE_CONST_ARRAY_ACCESS:
            return "CONST_ARRAY_ACCESS";
        case NODE_FUNC_CALL:
            return "FUNC_CALL";
        case NODE_UNARY_OP:
            return "UNARY_OP";
        case NODE_BINARY_OP:
            return "BINARY_OP";
        default:
            return "UNKNOWN_NODE";
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
    if (node->name) {
        printf(": %s", node->name);
    }
    switch (node->data_type) {
        case SYMB_DATA:
            printf(" (sym: %s, id: %d)", node->data.symb_ptr->name,
                   node->data.symb_ptr->id);
            break;
        case INT_DATA:
            printf(" (int value: %d)", node->data.direct_int);
            break;
        case FLOAT_DATA:
            printf(" (float value: %f)", node->data.direct_float);
            break;
        case STRING_DATA:
            printf(" (string: %s)", node->data.direct_str);
            break;
        case TYPE_DATA:
            printf(" (type: %s)", data_type_to_string(node->data.data_type));
            break;
        default:
            break;
    }
    printf("\n");

    // Children
    if (node->children) {
        for (int i = 0; i < node->child_count; i++)
            print_ast(node->children[i], level + 1);
    }
}
