%{
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <stdbool.h>
    #include <ctype.h>
    #include <math.h>
    #include "y.tab.h"
    #include "AST.h"
    #include "symbol_table.h"

    extern int yylineno;
    extern FILE *yyin;
    ASTNodePtr root;

    extern int yylex(void);
    void yyerror(const char *s);

    ASTNodePtr function_def(char *name, DataType type, ASTNodePtr params);
    void varlist_def(ASTNodePtr list_root, DataType data_type);
    ASTNodePtr try_fold_constant(ASTNodePtr node);
%}

%union {
    char *str;
    struct ASTNode *node;
}

%token<str> IDENTIFIER INT_CONST FLOAT_CONST

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
    '[' Exp ']' { $$ = create_ast_node(NODE_LIST, "Dims", yylineno, 1, $2); }
    | DimBrackets '[' Exp ']' { add_child($1, $3); $$ = $1; }
    ;

ConstDimBrackets:
    '[' ConstExp ']' { $$ = create_ast_node(NODE_LIST, "Dims", yylineno, 1, $2); }
    | ConstDimBrackets '[' ConstExp ']' { add_child($1, $3); $$ = $1; }
    ;

ConstDecl:
    CONST BType ConstDefList ';' {
        varlist_def($3, $2->data.data_type);
        free_ast($2);
        $$ = $3;
    }
    ;

ConstDefList:
    ConstDef { $$ = create_ast_node(NODE_LIST, "ConstDefs", yylineno, 1, $1); }
    | ConstDefList ',' ConstDef { add_child($1, $3); $$ = $1; }
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
    BType VarDefList ';' {
        varlist_def($2, $1->data.data_type);
        free_ast($1);
        $$ = $2;
    }
    ;

VarDefList:
    VarDef { $$ = create_ast_node(NODE_LIST, "VarDefs", yylineno, 1, $1); }
    | VarDefList ',' VarDef { add_child($1, $3); $$ = $1; }
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
        NodeData data;
        data.symb_ptr = define_symbol($2, SYMB_VAR, $1->data.data_type, yylineno);
        $$ = create_ast_node(NODE_VAR_DEF, "Param", yylineno, 0);
        set_ast_node_data($$, HOLD_NODETYPE, NULL, data, SYMB_DATA, -1);
    }
    | BType IDENTIFIER '[' ']' {
        NodeData data;
        data.symb_ptr = define_symbol($2, SYMB_ARRAY, $1->data.data_type, yylineno);
        ASTNodePtr dims_list = create_ast_node(NODE_EMPTY, NULL, yylineno, 0);
        $$ = create_ast_node(NODE_ARRAY_DEF, "Param", yylineno, 1, dims_list);
        set_ast_node_data($$, HOLD_NODETYPE, NULL, data, SYMB_DATA, -1);
    }
    | BType IDENTIFIER '[' ']' ConstDimBrackets {
        NodeData data;
        data.symb_ptr = define_symbol($2, SYMB_ARRAY, $1->data.data_type, yylineno);
        ASTNodePtr dims_list = create_ast_node(NODE_EMPTY, NULL, yylineno, 0);
        $$ = create_ast_node(NODE_ARRAY_DEF, "Param", yylineno, 1, dims_list);
        shift_child($5, $$);
        free_ast($5);
        set_ast_node_data($$, HOLD_NODETYPE, NULL, data, SYMB_DATA, -1);
    }
    ;

BlockEnter:
    '{' { enter_scope(); }
    ;

BlockItem:
    /* empty */  { $$ = create_ast_node(NODE_LIST, "Block", yylineno, 0); }
    | BlockItem Decl { shift_child($2, $1); free_ast($2); $$ = $1;}
    | BlockItem Stmt { if ($2->child_count) add_child($1, $2); $$ = $1;}
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
        $$ = create_ast_node(NODE_IF_STMT, NULL, yylineno, 3, $3, if_2, if_3);
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
        $$ = create_ast_node(NODE_VAR, NULL, yylineno, 0);
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
        set_ast_node_data($2, NODE_ARRAY_ACCESS, NULL, data, SYMB_DATA, yylineno);
        $$ = $2;
    }
    ;

PrimaryExp:
    '(' Exp ')' { $$ = $2; }
    | LVal { $$ = $1; }
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
        $$ = try_fold_constant($1);
    }
    | IDENTIFIER '(' ')' {
        SymbolPtr sym = lookup_symbol($1);
        if (!sym) {
            yyerror("Undeclared function");
            YYERROR;
        }
        NodeData data;
        data.symb_ptr = sym;
        $$ = create_ast_node(NODE_FUNC_CALL, NULL, yylineno, 0);
        set_ast_node_data($$, HOLD_NODETYPE, NULL, data, SYMB_DATA, -1);
    }
    | IDENTIFIER '(' FuncRParams ')' {
        SymbolPtr sym = lookup_symbol($1);
        if (!sym) {
            yyerror("Undeclared function");
            YYERROR;
        }
        NodeData data;
        data.symb_ptr = sym;
        set_ast_node_data($3, NODE_FUNC_CALL, NULL, data, SYMB_DATA, yylineno);
        $$ = $3;
    }
    ;

UnaryOp:
    '+' { $$ = create_ast_node(NODE_UNARY_OP, "+", yylineno, 0); }
    | '-' { $$ = create_ast_node(NODE_UNARY_OP, "-", yylineno, 0); }
    | '!' { $$ = create_ast_node(NODE_UNARY_OP, "!", yylineno, 0); }
    ;

FuncRParams:
    Exp { $$ = create_ast_node(NODE_LIST, "RParams", yylineno, 1, $1); }
    | FuncRParams ',' Exp { add_child($1, $3); $$ = $1; }
    ;

MulExp:
    UnaryExp { $$ = $1; }
    | MulExp '*' UnaryExp { $$ = try_fold_constant(create_ast_node(NODE_BINARY_OP, "*", yylineno, 2, $1, $3)); }
    | MulExp '/' UnaryExp { $$ = try_fold_constant(create_ast_node(NODE_BINARY_OP, "/", yylineno, 2, $1, $3)); }
    | MulExp '%' UnaryExp { $$ = try_fold_constant(create_ast_node(NODE_BINARY_OP, "%", yylineno, 2, $1, $3)); }
    ;

AddExp:
    MulExp { $$ = $1; }
    | AddExp '+' MulExp { $$ = try_fold_constant(create_ast_node(NODE_BINARY_OP, "+", yylineno, 2, $1, $3)); }
    | AddExp '-' MulExp { $$ = try_fold_constant(create_ast_node(NODE_BINARY_OP, "-", yylineno, 2, $1, $3)); }
    ;

RelExp:
    AddExp { $$ = $1; }
    | RelExp '<' AddExp { $$ = try_fold_constant(create_ast_node(NODE_BINARY_OP, "<", yylineno, 2, $1, $3)); }
    | RelExp '>' AddExp { $$ = try_fold_constant(create_ast_node(NODE_BINARY_OP, ">", yylineno, 2, $1, $3)); }
    | RelExp LEQUAL AddExp { $$ = try_fold_constant(create_ast_node(NODE_BINARY_OP, "<=", yylineno, 2, $1, $3)); }
    | RelExp GEQUAL AddExp { $$ = try_fold_constant(create_ast_node(NODE_BINARY_OP, ">=", yylineno, 2, $1, $3)); }
    ;

EqExp:
    RelExp { $$ = $1; }
    | EqExp EQUAL RelExp { $$ = try_fold_constant(create_ast_node(NODE_BINARY_OP, "==", yylineno, 2, $1, $3)); }
    | EqExp NEQUAL RelExp { $$ = try_fold_constant(create_ast_node(NODE_BINARY_OP, "!=", yylineno, 2, $1, $3)); }
    ;

LAndExp:
    EqExp { $$ = $1; }
    | LAndExp AND EqExp { $$ = try_fold_constant(create_ast_node(NODE_BINARY_OP, "&&", yylineno, 2, $1, $3)); }
    ;

LOrExp:
    LAndExp { $$ = $1; }
    | LOrExp OR LAndExp { $$ = try_fold_constant(create_ast_node(NODE_BINARY_OP, "||", yylineno, 2, $1, $3)); }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "%d %s\n", yylineno, s);
}

void varlist_def(ASTNodePtr list_root, DataType data_type) {
    if (!list_root) return;

    int sym_count = list_root->child_count;
    NodeData data;
    SymbolType sym_type;
    for (int i = 0; i < sym_count; ++i) {
        ASTNodePtr var = list_root->children[i];
        switch (var->node_type) {
            case NODE_VAR_DEF:
                sym_type = SYMB_VAR;
                break;
            case NODE_CONST_VAR_DEF:
                sym_type = SYMB_CONST_VAR;
                break;
            case NODE_ARRAY_DEF:
                sym_type = SYMB_ARRAY;
                break;
            case NODE_CONST_ARRAY_DEF:
                sym_type = SYMB_CONST_ARRAY;
                break;
            default:
                sym_type = SYMB_UNKNOWN;
                break;
        }
        SymbolPtr sym = define_symbol(var->name, sym_type, data_type, var->lineno);
        switch (var->node_type) {
            case NODE_ARRAY_DEF:
            case NODE_CONST_ARRAY_DEF:
                ASTNodePtr dim = var->children[0];
                sym->attributes.array_info.dimensions = dim->child_count;
                sym->attributes.array_info.shape = (int*)malloc(sizeof(int) * dim->child_count);
                for (int j = 0; j < dim->child_count; ++j) {
                    if (dim->children[j]->data_type != INT_DATA) break;
                    sym->attributes.array_info.shape[j] = dim->children[j]->data.direct_int;
                }
                free_ast(dim);
                var->children[0] = NULL;
                break;
            default:
                break;
        }
        data.symb_ptr = sym;
        set_ast_node_data(var, HOLD_NODETYPE, NULL, data, SYMB_DATA, -1);
    }
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
                func_sym->attributes.func_info.params[i] = params->children[i]->data.symb_ptr;
            }
        }
    }
    NodeData data;
    data.symb_ptr = func_sym;
    ASTNodePtr output = create_ast_node(NODE_FUNC_DEF, "FuncDef", yylineno, 1, params);
    set_ast_node_data(output, HOLD_NODETYPE, NULL, data, SYMB_DATA, -1);
    return output;
}

ASTNodePtr try_fold_constant(ASTNodePtr node) {
    if (!node) return node;
    // 只处理一元和二元运算
    if (node->node_type == NODE_BINARY_OP) {
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
                // free_ast(node); // 不再在这里释放，由调用者决定
                return folded;
            }
        }
    } else if (node->node_type == NODE_UNARY_OP) {
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
                // free_ast(node); // 不再在这里释放，由调用者决定
                return folded;
            }
        }
    }
    return node;
}
