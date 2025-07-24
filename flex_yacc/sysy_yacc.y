%{
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <stdbool.h>
    #include <ctype.h>
    #include <math.h>
    #include "sy_parser/y.tab.h"
    #include "sy_parser/AST.h"
    #include "sy_parser/symbol_table.h"

    extern int yylineno;
    extern FILE *yyin;
    ASTNodePtr root;

    extern int yylex(void);
    void yyerror(const char *s);

    SymbolType node_type_to_sym_type(NodeType node_type);
    void var_def(ASTNodePtr var_node, DataType data_type);
    ASTNodePtr function_def(char *name, DataType type, ASTNodePtr params);
    ASTNodePtr get_const_value(ASTNodePtr node);
    ASTNodePtr fold_unary_exp(ASTNodePtr node);
    ASTNodePtr fold_binary_exp(ASTNodePtr node);
%}

%union {
    char *str;
    struct ASTNode *node;
}

%token<str> IDENTIFIER INT_CONST FLOAT_CONST STRING_CONST

%token '-' '+' '*' '/' '%' ';' ',' ':' '?' '!' '<' '>' '(' ')' '[' ']' '{' '}' '.' '='
%token LEQUAL GEQUAL EQUAL NEQUAL AND OR
%token INT FLOAT VOID CONST IF ELSE WHILE BREAK CONTINUE RETURN
%token ENDMARKER

%type <node> file CompUnit Decl BType DimBrackets ConstDimBrackets
%type <node> ConstDecl ConstDefList ConstDef ConstInitVal ConstInitValList ConstExp
%type <node> VarDecl VarDefList VarDef InitVal InitValList
%type <node> FuncDef FuncHead FuncFParams FuncFParam FuncRParams
%type <node> Block BlockEnter BlockItem
%type <node> Stmt Cond LVal PrimaryExp Number
%type <node> Exp UnaryExp UnaryOp MulExp AddExp RelExp EqExp LAndExp LOrExp

%%

file:
    CompUnit ENDMARKER { root = $1; YYACCEPT; }
    ;

CompUnit:
    /* empty */ { $$ = create_ast_node(NODE_ROOT, NULL, yylineno, 0); }
    | CompUnit Decl { shift_child($2, $1); free_ast($2); $$ = $1; }
    | CompUnit FuncDef { add_child($1, $2); $$ = $1; }
    ;

Decl:
    ConstDecl { $$ = $1; }
    | VarDecl { $$ = $1; }
    ;

BType:
    INT {
        NodeData data;
        data.data_type = DATA_INT;
        $$ = create_ast_node(NODE_TYPE, NULL, yylineno, 0);
        set_ast_node_data($$, HOLD_NODETYPE, NULL, data, TYPE_DATA, -1);
    }
    | FLOAT {
        NodeData data;
        data.data_type = DATA_FLOAT;
        $$ = create_ast_node(NODE_TYPE, NULL, yylineno, 0);
        set_ast_node_data($$, HOLD_NODETYPE, NULL, data, TYPE_DATA, -1);
    }
    ;

DimBrackets:
    '[' ']' {
        NodeData data;
        data.direct_int = 0;
        ASTNodePtr first_dim_node = create_ast_node(NODE_CONST, NULL, yylineno, 0);
        set_ast_node_data(first_dim_node, HOLD_NODETYPE, NULL, data, INT_DATA, -1);
        $$ = create_ast_node(NODE_LIST, "Dims", yylineno, 1, first_dim_node);
    }
    | '[' Exp ']' { $$ = create_ast_node(NODE_LIST, "Dims", yylineno, 1, $2); }
    | DimBrackets '[' Exp ']' { add_child($1, $3); $$ = $1; }
    ;

ConstDimBrackets:
    '[' ']' {
        NodeData data;
        data.direct_int = 0;
        ASTNodePtr first_dim_node = create_ast_node(NODE_CONST, NULL, yylineno, 0);
        set_ast_node_data(first_dim_node, HOLD_NODETYPE, NULL, data, INT_DATA, -1);
        $$ = create_ast_node(NODE_LIST, "Dims", yylineno, 1, first_dim_node);
    }
    | '[' ConstExp ']' { $$ = create_ast_node(NODE_LIST, "Dims", yylineno, 1, $2); }
    | ConstDimBrackets '[' ConstExp ']' { add_child($1, $3); $$ = $1; }
    ;

ConstDecl:
    ConstDefList ';' { $$ = $1; }
    ;

ConstDefList:
    CONST BType ConstDef {
        var_def($3, $2->data.data_type);
        $$ = create_ast_node(NODE_LIST, "ConstDefs", yylineno, 1, $3);
    }
    | ConstDefList ',' ConstDef {
        DataType data_type = DATA_INT;
        if ($1->children[0]) {
            if ($1->children[0]->data.symb_ptr)
                data_type = $1->children[0]->data.symb_ptr->data_type;
        }
        var_def($3, data_type);
        add_child($1, $3);
        $$ = $1;
    }
    ;

ConstDef:
    IDENTIFIER '=' ConstExp {
        $$ = create_ast_node(NODE_CONST_VAR_DEF, $1, yylineno, 1, $3);
    }
    | IDENTIFIER ConstDimBrackets '=' '{' ConstInitValList '}' {
        $$ = create_ast_node(NODE_CONST_ARRAY_DEF, $1, yylineno, 2, $2, $5);
    }
    ;

ConstInitVal:
    ConstExp { $$ = $1; }
    | '{' ConstInitValList '}' { $$ = $2; }
    ;

ConstInitValList:
    /* empty */ { $$ = create_ast_node(NODE_LIST, "ConstIniter", yylineno, 0); }
    | ConstInitVal { $$ = create_ast_node(NODE_LIST, "ConstIniter", yylineno, 1, $1); }
    | ConstInitValList ',' ConstInitVal { add_child($1, $3); $$ = $1; }
    ;

VarDecl:
    VarDefList ';' { $$ = $1; }
    ;

VarDefList:
    BType VarDef {
        var_def($2, $1->data.data_type);
        $$ = create_ast_node(NODE_LIST, "VarDefs", yylineno, 1, $2);
    }
    | VarDefList ',' VarDef {
        DataType data_type = DATA_INT;
        if ($1->children[0]) {
            if ($1->children[0]->data.symb_ptr)
                data_type = $1->children[0]->data.symb_ptr->data_type;
        }
        var_def($3, data_type);
        add_child($1, $3);
        $$ = $1;
    }
    ;

VarDef:
    IDENTIFIER { $$ = create_ast_node(NODE_VAR_DEF, $1, yylineno, 0); }
    | IDENTIFIER '=' Exp { $$ = create_ast_node(NODE_VAR_DEF, $1, yylineno, 1, $3); }
    | IDENTIFIER ConstDimBrackets {
        $$ = create_ast_node(NODE_ARRAY_DEF, $1, yylineno, 1, $2);
    }
    | IDENTIFIER ConstDimBrackets '=' '{' InitValList '}' {
        $$ = create_ast_node(NODE_ARRAY_DEF, $1, yylineno, 2, $2, $5);
    }
    ;

InitVal:
    Exp { $$ = $1; }
    | '{' InitValList '}' { $$ = $2; }
    ;

InitValList:
    /* empty */ { $$ = create_ast_node(NODE_LIST, "ArrayIniter", yylineno, 0); }
    | InitVal { $$ = create_ast_node(NODE_LIST, "ArrayIniter", yylineno, 1, $1); }
    | InitValList ',' InitVal { add_child($1, $3); $$ = $1; }
    ;

FuncDef:
    FuncHead Block {
        add_child($1, $2);
        $$ = $1;
        exit_scope(); // Pop scope for params and function body
    }
    ;

FuncHead:
    BType IDENTIFIER '(' FuncFParams ')' { $$ = function_def($2, $1->data.data_type, $4); }
    | VOID IDENTIFIER '(' FuncFParams ')' { $$ = function_def($2, DATA_VOID, $4); }
    ;

FuncFParams:
    /* empty */ { $$ = create_ast_node(NODE_LIST, "FParams", yylineno, 0); }
    | FuncFParam { $$ = create_ast_node(NODE_LIST, "FParams", yylineno, 1, $1); }
    | FuncFParams ',' FuncFParam { add_child($1, $3); $$ = $1; }
    ;

FuncFParam:
    BType IDENTIFIER {
        ASTNodePtr var_node = create_ast_node(NODE_VAR_DEF, $2, yylineno, 0);
        $$ = create_ast_node(NODE_LIST, "Param", yylineno, 2, $1, var_node);
    }
    | BType IDENTIFIER ConstDimBrackets {
        ASTNodePtr var_node = create_ast_node(NODE_ARRAY_DEF, $2, yylineno, 1, $3);
        $$ = create_ast_node(NODE_LIST, "Param", yylineno, 2, $1, var_node);
    }
    ;

BlockEnter:
    '{' { enter_scope(); }
    ;

BlockItem:
    /* empty */  { $$ = create_ast_node(NODE_LIST, "Block", yylineno, 0); }
    | BlockItem Decl { shift_child($2, $1); free_ast($2); $$ = $1; }
    | BlockItem Stmt {
        if ($2->node_type == NODE_LIST) {
            if ($2->child_count) add_child($1, $2);
        } else add_child($1, $2);
        $$ = $1;
    }
    ;

Block:
    BlockEnter BlockItem '}' { $$ = $2; exit_scope(); }
    ;

Stmt:
    LVal '=' Exp ';' { $$ = create_ast_node(NODE_ASSIGN_STMT, NULL, yylineno, 2, $1, $3); }
    | Exp ';' { $$ = $1; }
    | Block { $$ = $1; }
    | ';' { $$ = create_ast_node(NODE_LIST, "Empty", yylineno, 0); }
    | IF '(' Cond ')' Stmt {
        ASTNodePtr if_2;
        NodeData foo;
        if ($5->node_type == NODE_LIST) {
            set_ast_node_data($5, HOLD_NODETYPE, "If-2", foo, HOLD_NODEDATATYPE, -1);
            if_2 = $5;
        } else if_2 = create_ast_node(NODE_LIST, "If-2", yylineno, 1, $5);
        $$ = create_ast_node(NODE_IF_STMT, NULL, yylineno, 2, $3, if_2);
    }
    | IF '(' Cond ')' Stmt ELSE Stmt {
        ASTNodePtr if_2, if_3;
        NodeData foo;
        if ($5->node_type == NODE_LIST) {
            set_ast_node_data($5, HOLD_NODETYPE, "If-2", foo, HOLD_NODEDATATYPE, -1);
            if_2 = $5;
        } else if_2 = create_ast_node(NODE_LIST, "If-2", yylineno, 1, $5);
        if ($7->node_type == NODE_LIST) {
            set_ast_node_data($7, HOLD_NODETYPE, "If-3", foo, HOLD_NODEDATATYPE, -1);
            if_3 = $7;
        } else if_3 = create_ast_node(NODE_LIST, "If-3", yylineno, 1, $7);
        $$ = create_ast_node(NODE_IF_ELSE_STMT, NULL, yylineno, 3, $3, if_2, if_3);
    }
    | WHILE '(' Cond ')' Stmt {
        ASTNodePtr while_2;
        NodeData foo;
        if ($5->node_type == NODE_LIST) {
            set_ast_node_data($5, HOLD_NODETYPE, "While-2", foo, HOLD_NODEDATATYPE, -1);
            while_2 = $5;
        } else while_2 = create_ast_node(NODE_LIST, "While-2", yylineno, 1, $5);
        $$ = create_ast_node(NODE_WHILE_STMT, NULL, yylineno, 2, $3, while_2);
    }
    | BREAK ';' { $$ = create_ast_node(NODE_BREAK_STMT, NULL, yylineno, 0); }
    | CONTINUE ';' { $$ = create_ast_node(NODE_CONTINUE_STMT, NULL, yylineno, 0); }
    | RETURN ';' { $$ = create_ast_node(NODE_RETURN_STMT, NULL, yylineno, 0); }
    | RETURN Exp ';' { $$ = create_ast_node(NODE_RETURN_STMT, NULL, yylineno, 1, $2); }
    ;

Cond:
    LOrExp { $$ = $1; }
    ;

Exp:
    AddExp { $$ = $1; }
    ;

ConstExp:
    AddExp { $$ = $1; }
    ;

LVal:
    IDENTIFIER {
        SymbolPtr sym = lookup_symbol($1);
        if (!sym) {
            yyerror("Undeclared identifier");
            YYERROR;
        }
        NodeData data;
        data.symb_ptr = sym;
        switch (sym->symbol_type) {
            case SYMB_VAR:
                $$ = create_ast_node(NODE_VAR, NULL, yylineno, 0);
                break;
            case SYMB_CONST_VAR:
                $$ = create_ast_node(NODE_CONST_VAR, NULL, yylineno, 0);
                break;
            case SYMB_ARRAY:
                $$ = create_ast_node(NODE_ARRAY, NULL, yylineno, 0);
                break;
            case SYMB_CONST_ARRAY:
                $$ = create_ast_node(NODE_CONST_ARRAY, NULL, yylineno, 0);
                break;
            default:
                yyerror("Invalid symbol type");
                YYERROR; 
                break;
        }
        set_ast_node_data($$, HOLD_NODETYPE, NULL, data, SYMB_DATA, -1);
    }
    | IDENTIFIER DimBrackets {
        SymbolPtr sym = lookup_symbol($1);
        if (!sym) {
            yyerror("Undeclared identifier");
            YYERROR;
        }
        NodeData data;
        data.symb_ptr = sym;
        switch (sym->symbol_type) {
            case SYMB_ARRAY:
                set_ast_node_data(
                    $2, NODE_ARRAY_ACCESS, NULL, data, SYMB_DATA, yylineno);
                break;
            case SYMB_CONST_ARRAY:
                set_ast_node_data(
                    $2, NODE_CONST_ARRAY_ACCESS, NULL, data, SYMB_DATA, yylineno);
                break;
            default:
                yyerror("Invalid symbol type");
                YYERROR;
                break;
        }
        $$ = $2;
    }
    | STRING_CONST {
        NodeData data;
        data.direct_str = $1;
        $$ = create_ast_node(NODE_CONST, NULL, yylineno, 0);
        set_ast_node_data($$, HOLD_NODETYPE, NULL, data, STRING_DATA, -1);
    }
    ;

PrimaryExp:
    LVal { $$ = get_const_value($1); }
    | '(' Exp ')' { $$ = $2; }
    | Number { $$ = $1; }
    ;

Number:
    INT_CONST {
        NodeData data;
        data.direct_int = (int)strtol($1, NULL, 0);
        $$ = create_ast_node(NODE_CONST, NULL, yylineno, 0);
        set_ast_node_data($$, HOLD_NODETYPE, NULL, data, INT_DATA, -1);
    }
    | FLOAT_CONST {
        NodeData data;
        data.direct_float = (float)strtof($1, NULL);
        $$ = create_ast_node(NODE_CONST, NULL, yylineno, 0);
        set_ast_node_data($$, HOLD_NODETYPE, NULL, data, FLOAT_DATA, -1);
    }
    ;

UnaryExp:
    PrimaryExp { $$ = $1; }
    | UnaryOp UnaryExp {
        NodeData data;
        set_ast_node_data($1, HOLD_NODETYPE, NULL, data, HOLD_NODEDATATYPE, yylineno);
        add_child($1, $2);
        $$ = fold_unary_exp($1);
    }
    | IDENTIFIER '(' FuncRParams ')' {
        SymbolPtr sym = lookup_symbol($1);
        if (!sym) {
            yyerror("Undeclared function");
            YYERROR;
        }
        NodeData data;
        data.symb_ptr = sym;
        set_ast_node_data($3, NODE_FUNC_CALL, $1, data, SYMB_DATA, yylineno);
        $$ = $3;
    }
    ;

UnaryOp:
    '+' { $$ = create_ast_node(NODE_UNARY_OP, "+", yylineno, 0); }
    | '-' { $$ = create_ast_node(NODE_UNARY_OP, "-", yylineno, 0); }
    | '!' { $$ = create_ast_node(NODE_UNARY_OP, "!", yylineno, 0); }
    ;

FuncRParams:
    /* empty */ { $$ = create_ast_node(NODE_LIST, "RParams", yylineno, 0); }
    | Exp { $$ = create_ast_node(NODE_LIST, "RParams", yylineno, 1, $1); }
    | FuncRParams ',' Exp { add_child($1, $3); $$ = $1; }
    ;

MulExp:
    UnaryExp { $$ = $1; }
    | MulExp '*' UnaryExp {
        $$ = create_ast_node(NODE_BINARY_OP, "*", yylineno, 2, $1, $3);
        $$ = fold_binary_exp($$);
    }
    | MulExp '/' UnaryExp {
        $$ = create_ast_node(NODE_BINARY_OP, "/", yylineno, 2, $1, $3);
        $$ = fold_binary_exp($$);
    }
    | MulExp '%' UnaryExp {
        $$ = create_ast_node(NODE_BINARY_OP, "%", yylineno, 2, $1, $3);
        $$ = fold_binary_exp($$);
    }
    ;

AddExp:
    MulExp { $$ = $1; }
    | AddExp '+' MulExp {
        $$ = create_ast_node(NODE_BINARY_OP, "+", yylineno, 2, $1, $3);
        $$ = fold_binary_exp($$);
    }
    | AddExp '-' MulExp {
        $$ = create_ast_node(NODE_BINARY_OP, "-", yylineno, 2, $1, $3);
        $$ = fold_binary_exp($$);
    }
    ;

RelExp:
    AddExp { $$ = $1; }
    | RelExp '<' AddExp {
        $$ = create_ast_node(NODE_BINARY_OP, "<", yylineno, 2, $1, $3);
        $$ = fold_binary_exp($$);
    }
    | RelExp '>' AddExp {
        $$ = create_ast_node(NODE_BINARY_OP, ">", yylineno, 2, $1, $3);
        $$ = fold_binary_exp($$);
    }
    | RelExp LEQUAL AddExp {
        $$ = create_ast_node(NODE_BINARY_OP, "<=", yylineno, 2, $1, $3);
        $$ = fold_binary_exp($$);
    }
    | RelExp GEQUAL AddExp {
        $$ = create_ast_node(NODE_BINARY_OP, ">=", yylineno, 2, $1, $3);
        $$ = fold_binary_exp($$);
    }
    ;

EqExp:
    RelExp { $$ = $1; }
    | EqExp EQUAL RelExp {
        $$ = create_ast_node(NODE_BINARY_OP, "==", yylineno, 2, $1, $3);
        $$ = fold_binary_exp($$);
    }
    | EqExp NEQUAL RelExp {
        $$ = create_ast_node(NODE_BINARY_OP, "!=", yylineno, 2, $1, $3);
        $$ = fold_binary_exp($$);
    }
    ;

LAndExp:
    EqExp { $$ = $1; }
    | LAndExp AND EqExp {
        $$ = create_ast_node(NODE_BINARY_OP, "&&", yylineno, 2, $1, $3);
        $$ = fold_binary_exp($$);
    }
    ;

LOrExp:
    LAndExp { $$ = $1; }
    | LOrExp OR LAndExp {
        $$ = create_ast_node(NODE_BINARY_OP, "||", yylineno, 2, $1, $3);
        $$ = fold_binary_exp($$);
    }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "%d %s\n", yylineno, s);
}

SymbolType node_type_to_sym_type(NodeType node_type) {
    switch (node_type) {
        case NODE_VAR_DEF:
            return SYMB_VAR;
        case NODE_CONST_VAR_DEF:
            return SYMB_CONST_VAR;
        case NODE_ARRAY_DEF:
            return SYMB_ARRAY;
        case NODE_CONST_ARRAY_DEF:
            return SYMB_CONST_ARRAY;
        default:
            return SYMB_UNKNOWN;
    }
}

void var_def(ASTNodePtr var_node, DataType data_type) {
    if (!var_node) return;

    NodeData data;
    SymbolType sym_type = node_type_to_sym_type(var_node->node_type);
    SymbolPtr sym = define_symbol(
        var_node->name, sym_type, data_type, var_node->lineno);
    switch (var_node->node_type) {
        case NODE_CONST_VAR_DEF:
            if (var_node->child_count == 0) return;
            ASTNodePtr init_node = var_node->children[0];
            if (data_type == DATA_INT) {
                int init_value = 0;
                if (init_node->data_type == INT_DATA)
                    init_value = init_node->data.direct_int;
                else if (init_node->data_type == FLOAT_DATA)
                    init_value = (int)init_node->data.direct_float;
                sym->attributes.const_info.int_value = init_value;
            } else if (data_type == DATA_FLOAT) {
                float init_value = 0.0;
                if (init_node->data_type == INT_DATA)
                    init_value = (float)init_node->data.direct_int;
                else if (init_node->data_type == FLOAT_DATA)
                    init_value = init_node->data.direct_float;
                sym->attributes.const_info.float_value = init_value;
            }
            break;
        case NODE_ARRAY_DEF:
        case NODE_CONST_ARRAY_DEF:
            ASTNodePtr dim = var_node->children[0];
            int dim_count = dim->child_count;
            sym->attributes.array_info.dimensions = dim_count;
            sym->attributes.array_info.shape = (int*)malloc(sizeof(int) * dim_count);

            for (int j = 0; j < dim_count; ++j) {
                ASTNodePtr dim_node = dim->children[j];
                int dim_size = 0;
                if (dim_node->data_type == INT_DATA)
                    dim_size = dim_node->data.direct_int;
                else if (dim_node->data_type == FLOAT_DATA)
                    dim_size = (int)dim_node->data.direct_float;
                sym->attributes.array_info.shape[j] = dim_size;
            }

            free_ast(dim);
            var_node->children[0] = NULL;
            // 可根据 valid 变量决定后续处理
            break;
        default:
            break;
    }
    data.symb_ptr = sym;
    set_ast_node_data(var_node, HOLD_NODETYPE, NULL, data, SYMB_DATA, -1);
}

ASTNodePtr function_def(char *name, DataType type, ASTNodePtr params) {
    SymbolPtr func_sym = define_symbol(name, SYMB_FUNCTION, type, yylineno);
    enter_scope();
    if (params) {
        int param_count = params->child_count;
        if (param_count > 0) {
            func_sym->attributes.func_info.param_count = param_count;
            func_sym->attributes.func_info.params = (SymbolPtr*)malloc(
                sizeof(SymbolPtr) * param_count);
            for (int i = 0; i < param_count; ++i) {
                ASTNodePtr param_node = params->children[i];
                ASTNodePtr type_node = param_node->children[0];
                ASTNodePtr var_node = param_node->children[1];
                var_def(var_node, type_node->data.data_type);
                func_sym->attributes.func_info.params[i] = var_node->data.symb_ptr;

                param_node->children[1] = NULL;
                free_ast(param_node);
                params->children[i] = var_node;
            }
        }
    }
    NodeData data;
    data.symb_ptr = func_sym;
    ASTNodePtr output = create_ast_node(NODE_FUNC_DEF, name, yylineno, 1, params);
    set_ast_node_data(output, HOLD_NODETYPE, NULL, data, SYMB_DATA, -1);
    return output;
}

ASTNodePtr get_const_value(ASTNodePtr node) {
    if (!node) return node;
    if (!node->data.symb_ptr) return node;
    SymbolPtr sym = node->data.symb_ptr;

    if (node->node_type == NODE_CONST_VAR) {
        ASTNodePtr valued;
        NodeData data;
        if (sym->data_type == DATA_INT) {
            valued = create_ast_node(NODE_CONST, NULL, node->lineno, 0);
            data.direct_int = sym->attributes.const_info.int_value;
            set_ast_node_data(valued, HOLD_NODETYPE, NULL, data, INT_DATA, -1);
        } else if (sym->data_type == DATA_FLOAT) {
            valued = create_ast_node(NODE_CONST, NULL, node->lineno, 0);
            data.direct_float = sym->attributes.const_info.float_value;
            set_ast_node_data(valued, HOLD_NODETYPE, NULL, data, FLOAT_DATA, -1);
        }
        return valued;
    }
    return node;
}

ASTNodePtr fold_unary_exp(ASTNodePtr node) {
    if (!node) return node;
    if (node->node_type != NODE_UNARY_OP) return node;

    ASTNodePtr child = node->children[0];
    if (child && child->node_type == NODE_CONST) {
        NodeData data;
        int is_float = (child->data_type == FLOAT_DATA);
        double val = is_float ? child->data.direct_float : child->data.direct_int;
        double result = 0;
        int valid = 1;
        if (strcmp(node->name, "+") == 0) result = val;
        else if (strcmp(node->name, "-") == 0) result = -val;
        else if (strcmp(node->name, "!") == 0) result = !val;
        else valid = 0;
        if (valid) {
            ASTNodePtr folded = create_ast_node(NODE_CONST, NULL, node->lineno, 0);
            if (is_float) {
                data.direct_float = (float)result;
                set_ast_node_data(folded, HOLD_NODETYPE, NULL, data, FLOAT_DATA, -1);
            } else {
                data.direct_int = (int)result;
                set_ast_node_data(folded, HOLD_NODETYPE, NULL, data, INT_DATA, -1);
            }
            return folded;
        }
    }

    return node;
}

ASTNodePtr fold_binary_exp(ASTNodePtr node) {
    if (!node) return node;
    if (node->node_type != NODE_BINARY_OP) return node;

    ASTNodePtr lhs = node->children[0];
    ASTNodePtr rhs = node->children[1];
    // 只处理左右都是常量的情况
    if (lhs && rhs && lhs->node_type == NODE_CONST && rhs->node_type == NODE_CONST) {
        int is_float = (lhs->data_type == FLOAT_DATA || rhs->data_type == FLOAT_DATA);
        NodeData data;
        double lval = (lhs->data_type == FLOAT_DATA) ? lhs->data.direct_float : lhs->data.direct_int;
        double rval = (rhs->data_type == FLOAT_DATA) ? rhs->data.direct_float : rhs->data.direct_int;
        double result = 0;
        int valid = 1;
        if (strcmp(node->name, "+") == 0) result = lval + rval;
        else if (strcmp(node->name, "-") == 0) result = lval - rval;
        else if (strcmp(node->name, "*") == 0) result = lval * rval;
        else if (strcmp(node->name, "/") == 0) {
            if (rval == 0) valid = 0;
            else result = lval / rval;
        }
        else if (strcmp(node->name, "%") == 0) {
            if (!is_float && (int)rval != 0) result = (int)lval % (int)rval;
            else valid = 0;
        }
        else if (strcmp(node->name, "<") == 0) result = lval < rval;
        else if (strcmp(node->name, ">") == 0) result = lval > rval;
        else if (strcmp(node->name, "<=") == 0) result = lval <= rval;
        else if (strcmp(node->name, ">=") == 0) result = lval >= rval;
        else if (strcmp(node->name, "==") == 0) result = lval == rval;
        else if (strcmp(node->name, "!=") == 0) result = lval != rval;
        else if (strcmp(node->name, "&&") == 0) result = lval && rval;
        else if (strcmp(node->name, "||") == 0) result = lval || rval;
        else valid = 0;
        if (valid) {
            ASTNodePtr folded = create_ast_node(NODE_CONST, NULL, node->lineno, 0);
            if (is_float) {
                data.direct_float = (float)result;
                set_ast_node_data(folded, HOLD_NODETYPE, NULL, data, FLOAT_DATA, -1);
            } else {
                data.direct_int = (int)result;
                set_ast_node_data(folded, HOLD_NODETYPE, NULL, data, INT_DATA, -1);
            }
            return folded;
        }
    }

    return node;
}
