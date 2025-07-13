%{
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <stdbool.h>
    #include <ctype.h>
    #include "y.tab.h"
    #include "AST.h"
    #include "symbol_table.h"

    extern int yylineno;
    extern FILE *yyin;
    ASTNodePtr root = NULL;

    extern int yylex(void);
    void yyerror(const char *s);
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

%type <node> file CompUnit Decl BType
%type <node> ConstDecl ConstDefList ConstDef ConstInitVal ConstInitValList ConstExp
%type <node> VarDecl VarDefList VarDef InitVal InitValList DimBrackets
%type <node> FuncDef FuncHead FuncFParams FuncFParam FuncRParams
%type <node> Block BlockEnter BlockItem
%type <node> Stmt Cond LVal PrimaryExp Number
%type <node> Exp UnaryExp UnaryOp MulExp AddExp RelExp EqExp LAndExp LOrExp

%%

file:
    CompUnit ENDMARKER {
        root = $1;
        YYACCEPT;
    }
    ;

CompUnit:
    Decl { $$ = create_ast_node(NODE_ROOT, NULL, 1, $1); }
    | FuncDef { $$ = create_ast_node(NODE_ROOT, NULL, 1, $1); }
    | CompUnit Decl { add_child($1, $2); $$ = $1; }
    | CompUnit FuncDef { add_child($1, $2); $$ = $1; }
    ;

Decl:
    ConstDecl { $$ = $1; }
    | VarDecl { $$ = $1; }
    ;

BType:
    INT { $$ = create_leaf(NODE_TYPE, "int"); }
    | FLOAT { $$ = create_leaf(NODE_TYPE, "float"); }
    ;

ConstDecl:
    CONST BType ConstDefList ';' {
        ASTNodePtr type_node = $2;
        ASTNodePtr def_list_node = $3;
        // Post-process the definitions to add type info and create symbols
        for (int i = 0; i < def_list_node->child_count; i++) {
            ASTNodePtr def_node = def_list_node->children[i]; // NODE_CONST_DEF
            ASTNodePtr ident_node = def_node->children[0];
            SymbolType sym_type = SYMB_CONSTANT;
            
            // Check for dimensions to determine if it's an array
            if (def_node->child_count > 1 && def_node->children[1]->node_type == NODE_LIST) {
                sym_type = SYMB_CONST_ARRAY;
            }

            SymbolPtr sym = define_symbol(ident_node->value, type_node->value, sym_type, ident_node);
            ident_node->symb_ptr = sym; // Attach symbol to ident
            
            if (sym_type == SYMB_CONST_ARRAY) {
                ASTNodePtr dims_node = def_node->children[1];
                sym->attributes.array_info.dimensions = dims_node->child_count;
                // You would typically evaluate constant expressions here to get dimensions
                // For now, we just link the expression nodes.
            }
        }
        $$ = create_ast_node(NODE_CONST_DECL, type_node->value, 1, def_list_node);
        free_ast(type_node); // We've used its value
    }
    ;

ConstDefList:
    ConstDef { $$ = create_ast_node(NODE_LIST, "ConstDefs", 1, $1); }
    | ConstDefList ',' ConstDef { add_child($1, $3); $$ = $1; }
    ;

ConstDef:
    IDENTIFIER DimBrackets '=' ConstInitVal {
        ASTNodePtr ident_node = create_leaf(NODE_IDENT, $1);
        $$ = create_ast_node(NODE_CONST_DEF, NULL, 3, ident_node, $2, $4);
    }
    | IDENTIFIER '=' ConstInitVal {
        ASTNodePtr ident_node = create_leaf(NODE_IDENT, $1);
        $$ = create_ast_node(NODE_CONST_DEF, NULL, 2, ident_node, $3);
    }
    ;

DimBrackets:
    '[' ConstExp ']' { $$ = create_ast_node(NODE_LIST, "Dims", 1, $2); }
    | DimBrackets '[' ConstExp ']' { add_child($1, $3); $$ = $1; }
    ;

ConstInitVal:
    ConstExp { $$ = $1; }
    | '{' '}' { $$ = create_ast_node(NODE_LIST, "InitList", 0); }
    | '{' ConstInitValList '}' { $$ = $2; }
    ;

ConstInitValList:
    ConstInitVal { $$ = create_ast_node(NODE_LIST, "InitList", 1, $1); }
    | ConstInitValList ',' ConstInitVal { add_child($1, $3); $$ = $1; }
    ;

VarDecl:
    BType VarDefList ';' {
        ASTNodePtr type_node = $1;
        ASTNodePtr def_list_node = $2;
        // Post-process the definitions to add type info and create symbols
        for (int i = 0; i < def_list_node->child_count; i++) {
            ASTNodePtr def_node = def_list_node->children[i]; // NODE_VAR_DEF
            ASTNodePtr ident_node = def_node->children[0];
            SymbolType sym_type = SYMB_VARIABLE;

            int dims_child_idx = -1;
            if (def_node->child_count > 1) {
                // Check if the second child is a DimBrackets list or an InitVal
                ASTNodePtr second_child = def_node->children[1];
                 if (second_child->node_type == NODE_LIST && strcmp(second_child->value, "Dims") == 0) {
                    sym_type = SYMB_ARRAY;
                    dims_child_idx = 1;
                }
            }

            SymbolPtr sym = define_symbol(ident_node->value, type_node->value, sym_type, ident_node);
            ident_node->symb_ptr = sym;

            if (sym_type == SYMB_ARRAY) {
                ASTNodePtr dims_node = def_node->children[dims_child_idx];
                sym->attributes.array_info.dimensions = dims_node->child_count;
            }
        }
        $$ = create_ast_node(NODE_VAR_DECL, type_node->value, 1, def_list_node);
        free_ast(type_node);
    }
    ;

VarDefList:
    VarDef { $$ = create_ast_node(NODE_LIST, "VarDefs", 1, $1); }
    | VarDefList ',' VarDef { add_child($1, $3); $$ = $1; }
    ;

VarDef:
    IDENTIFIER {
        $$ = create_ast_node(NODE_VAR_DEF, NULL, 1, create_ast_node(NODE_IDENT, $1, 0));
    }
    | IDENTIFIER DimBrackets {
        ASTNodePtr ident_node = create_leaf(NODE_IDENT, $1);
        $$ = create_ast_node(NODE_VAR_DEF, NULL, 2, ident_node, $2);
    }
    | IDENTIFIER '=' InitVal {
        ASTNodePtr ident_node = create_leaf(NODE_IDENT, $1);
        $$ = create_ast_node(NODE_VAR_DEF, NULL, 2, ident_node, $3);
    }
    | IDENTIFIER DimBrackets '=' InitVal {
        ASTNodePtr ident_node = create_leaf(NODE_IDENT, $1);
        $$ = create_ast_node(NODE_VAR_DEF, NULL, 3, ident_node, $2, $4);
    }
    ;

InitVal:
    Exp { $$ = $1; }
    | '{' '}' { $$ = create_ast_node(NODE_LIST, "InitList", 0); }
    | '{' InitValList '}' { $$ = $2; }
    ;

InitValList:
    InitVal { $$ = create_ast_node(NODE_LIST, "InitList", 1, $1); }
    | InitValList ',' InitVal { add_child($1, $3); $$ = $1; }
    ;

FuncDef:
    FuncHead Block {
        $$ = create_ast_node(NODE_FUNC_DEF, NULL, 2, $1, $2);
        exit_scope(); // Pop scope for params and function body
    }
    ;

FuncHead:
    BType IDENTIFIER '(' FuncFParams ')' {
        ASTNodePtr type_node = $1;
        ASTNodePtr ident_node = create_leaf(NODE_IDENT, $2);
        ASTNodePtr params_node = $4; // This is a LIST of VAR_DEFs

        SymbolPtr func_sym_initial = define_symbol($2, type_node->value, SYMB_FUNCTION, ident_node);
        int func_sym_id = func_sym_initial->id;

        enter_scope(); // New scope for parameters and body
        
        if (params_node) {
            int param_count = params_node->child_count;
            if (param_count > 0) {
                for (int i=0; i < param_count; ++i) {
                    ASTNodePtr param_def_node = params_node->children[i]; 
                    ASTNodePtr p_type_node = param_def_node->children[0];
                    ASTNodePtr p_ident_node = param_def_node->children[1];

                    SymbolType p_sym_type = SYMB_VARIABLE;
                    if (param_def_node->child_count > 2) { // type, ident, dims...
                        p_sym_type = SYMB_ARRAY;
                    }
                    
                    SymbolPtr p_sym = define_symbol(p_ident_node->value, p_type_node->value, p_sym_type, p_ident_node);
                    p_ident_node->symb_ptr = p_sym;

                    if (p_sym_type == SYMB_ARRAY) {
                        // Handle array parameter dimensions if necessary
                    }
                }
                
                SymbolPtr func_sym = get_symbol_by_id(func_sym_id);
                func_sym->attributes.func_info.param_count = param_count;
                func_sym->attributes.func_info.params = (SymbolPtr*)malloc(sizeof(SymbolPtr) * param_count);

                for (int i=0; i < param_count; ++i) {
                    // param_def_node -> p_ident_node -> symb_ptr
                    func_sym->attributes.func_info.params[i] = params_node->children[i]->children[1]->symb_ptr;
                }
            }
        }
        $$ = create_ast_node(NODE_FUNC_HEAD, NULL, 3, type_node, ident_node, params_node);
        $$->symb_ptr = get_symbol_by_id(func_sym_id);
    }
    | BType IDENTIFIER '(' ')' {
        ASTNodePtr type_node = $1;
        ASTNodePtr ident_node = create_leaf(NODE_IDENT, $2);
        SymbolPtr func_sym = define_symbol($2, type_node->value, SYMB_FUNCTION, ident_node);
        enter_scope();
        $$ = create_ast_node(NODE_FUNC_HEAD, NULL, 2, type_node, ident_node);
        $$->symb_ptr = func_sym;
    }
    | VOID IDENTIFIER '(' FuncFParams ')' {
        ASTNodePtr type_node = create_leaf(NODE_TYPE, "void");
        ASTNodePtr ident_node = create_leaf(NODE_IDENT, $2);
        ASTNodePtr params_node = $4;
        SymbolPtr func_sym_initial = define_symbol($2, "void", SYMB_FUNCTION, ident_node);
        int func_sym_id = func_sym_initial->id;

        enter_scope();
        if (params_node) {
             int param_count = params_node->child_count;
            if (param_count > 0) {
                for (int i=0; i < param_count; ++i) {
                    ASTNodePtr param_def_node = params_node->children[i];
                    ASTNodePtr p_type_node = param_def_node->children[0];
                    ASTNodePtr p_ident_node = param_def_node->children[1];
                    SymbolType p_sym_type = SYMB_VARIABLE;
                    if(param_def_node->child_count > 2) p_sym_type = SYMB_ARRAY;
                    
                    SymbolPtr p_sym = define_symbol(p_ident_node->value, p_type_node->value, p_sym_type, p_ident_node);
                    p_ident_node->symb_ptr = p_sym;
                }

                SymbolPtr func_sym = get_symbol_by_id(func_sym_id);
                func_sym->attributes.func_info.param_count = param_count;
                func_sym->attributes.func_info.params = (SymbolPtr*)malloc(sizeof(SymbolPtr) * param_count);
                for (int i=0; i < param_count; ++i) {
                    func_sym->attributes.func_info.params[i] = params_node->children[i]->children[1]->symb_ptr;
                }
            }
        }
        $$ = create_ast_node(NODE_FUNC_HEAD, NULL, 3, type_node, ident_node, params_node);
        $$->symb_ptr = get_symbol_by_id(func_sym_id);
    }
    | VOID IDENTIFIER '(' ')' {
        ASTNodePtr type_node = create_leaf(NODE_TYPE, "void");
        ASTNodePtr ident_node = create_leaf(NODE_IDENT, $2);
        SymbolPtr func_sym = define_symbol($2, "void", SYMB_FUNCTION, ident_node);
        enter_scope();
        $$ = create_ast_node(NODE_FUNC_HEAD, NULL, 2, type_node, ident_node);
        $$->symb_ptr = func_sym;
    }
    ;

FuncFParams:
    FuncFParam { $$ = create_ast_node(NODE_LIST, "FParams", 1, $1); }
    | FuncFParams ',' FuncFParam { add_child($1, $3); $$ = $1; }
    ;

FuncFParam:
    BType IDENTIFIER { $$ = create_ast_node(NODE_VAR_DEF, "param", 2, $1, create_leaf(NODE_IDENT, $2)); }
    | BType IDENTIFIER DimBrackets {
        $$ = create_ast_node(NODE_VAR_DEF, "array_param", 3, $1, create_leaf(NODE_IDENT, $2), $3);
    }
    ;

BlockEnter:
    '{' { enter_scope(); }
    ;

BlockItem:
    /* empty */  { $$ = create_ast_node(NODE_LIST, "BlockItems", 0); }
    | BlockItem Decl { add_child($1, $2); $$ = $1; }
    | BlockItem Stmt { add_child($1, $2); $$ = $1; }
    ;

Block:
    BlockEnter BlockItem '}' { $$ = $2; exit_scope(); }
    ;

Stmt:
    LVal '=' Exp ';' { $$ = create_ast_node(NODE_ASSIGN_STMT, "=", 2, $1, $3); }
    | Exp ';' { $$ = create_ast_node(NODE_EXPR_STMT, NULL, 1, $1); }
    | Block { $$ = $1; }
    | ';' { $$ = NULL; } // Return NULL for empty statements
    | IF '(' Cond ')' Stmt { $$ = create_ast_node(NODE_IF_STMT, NULL, 2, $3, $5); }
    | IF '(' Cond ')' Stmt ELSE Stmt { $$ = create_ast_node(NODE_IF_STMT, "ifelse", 3, $3, $5, $7); }
    | WHILE '(' Cond ')' Stmt { $$ = create_ast_node(NODE_WHILE_STMT, NULL, 2, $3, $5); }
    | BREAK ';' { $$ = create_ast_node(NODE_BREAK_STMT, NULL, 0); }
    | CONTINUE ';' { $$ = create_ast_node(NODE_CONTINUE_STMT, NULL, 0); }
    | RETURN ';' { $$ = create_ast_node(NODE_RETURN_STMT, NULL, 0); }
    | RETURN Exp ';' { $$ = create_ast_node(NODE_RETURN_STMT, NULL, 1, $2); }
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
          $$ = create_leaf(NODE_IDENT, $1);
          $$->symb_ptr = sym;
      }
    | LVal '[' Exp ']' { $$ = create_ast_node(NODE_ARRAY_ACCESS, "[]", 2, $1, $3); }
    ;

PrimaryExp:
    '(' Exp ')' { $$ = $2; }
    | LVal { $$ = $1; }
    | Number { $$ = $1; }
    ;

Number:
    INT_CONST { $$ = create_leaf(NODE_INT_CONST, $1); }
    | FLOAT_CONST { $$ = create_leaf(NODE_FLOAT_CONST, $1); }
    ;

UnaryExp:
    /* empty */  PrimaryExp { $$ = $1; }
    | UnaryOp UnaryExp { $$ = create_ast_node(NODE_UNARY_OP, $1->value, 1, $2); free_ast($1); }
    | IDENTIFIER '(' ')' {
        SymbolPtr sym = lookup_symbol($1);
        if (!sym) {
            yyerror("Undeclared function");
            YYERROR;
        }
        ASTNodePtr ident_leaf = create_leaf(NODE_IDENT, $1);
        ident_leaf->symb_ptr = sym;
        $$ = create_ast_node(NODE_FUNC_CALL, $1, 1, ident_leaf);
    }
    | IDENTIFIER '(' FuncRParams ')' {
        SymbolPtr sym = lookup_symbol($1);
        if (!sym) {
            yyerror("Undeclared function");
            YYERROR;
        }
        ASTNodePtr ident_leaf = create_leaf(NODE_IDENT, $1);
        ident_leaf->symb_ptr = sym;
        $$ = create_ast_node(NODE_FUNC_CALL, $1, 2, ident_leaf, $3);
    }
    ;

UnaryOp:
    '+' { $$ = create_ast_node(NODE_UNARY_OP, "+", 0); }
    | '-' { $$ = create_ast_node(NODE_UNARY_OP, "-", 0); }
    | '!' { $$ = create_ast_node(NODE_UNARY_OP, "!", 0); }
    ;

FuncRParams:
    Exp { $$ = create_ast_node(NODE_ARG_LIST, NULL, 1, $1); }
    | FuncRParams ',' Exp { add_child($1, $3); $$ = $1; }
    ;

MulExp:
    UnaryExp { $$ = $1; }
    | MulExp '*' UnaryExp { $$ = create_ast_node(NODE_BINARY_OP, "*", 2, $1, $3); }
    | MulExp '/' UnaryExp { $$ = create_ast_node(NODE_BINARY_OP, "/", 2, $1, $3); }
    | MulExp '%' UnaryExp { $$ = create_ast_node(NODE_BINARY_OP, "%", 2, $1, $3); }
    ;

AddExp:
    MulExp { $$ = $1; }
    | AddExp '+' MulExp { $$ = create_ast_node(NODE_BINARY_OP, "+", 2, $1, $3); }
    | AddExp '-' MulExp { $$ = create_ast_node(NODE_BINARY_OP, "-", 2, $1, $3); }
    ;

RelExp:
    AddExp { $$ = $1; }
    | RelExp '<' AddExp { $$ = create_ast_node(NODE_BINARY_OP, "<", 2, $1, $3); }
    | RelExp '>' AddExp { $$ = create_ast_node(NODE_BINARY_OP, ">", 2, $1, $3); }
    | RelExp LEQUAL AddExp { $$ = create_ast_node(NODE_BINARY_OP, "<=", 2, $1, $3); }
    | RelExp GEQUAL AddExp { $$ = create_ast_node(NODE_BINARY_OP, ">=", 2, $1, $3); }
    ;

EqExp:
    RelExp { $$ = $1; }
    | EqExp EQUAL RelExp { $$ = create_ast_node(NODE_BINARY_OP, "==", 2, $1, $3); }
    | EqExp NEQUAL RelExp { $$ = create_ast_node(NODE_BINARY_OP, "!=", 2, $1, $3); }
    ;

LAndExp:
    EqExp { $$ = $1; }
    | LAndExp AND EqExp { $$ = create_ast_node(NODE_BINARY_OP, "&&", 2, $1, $3); }
    ;

LOrExp:
    LAndExp { $$ = $1; }
    | LOrExp OR LAndExp { $$ = create_ast_node(NODE_BINARY_OP, "||", 2, $1, $3); }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "%d %s\n", yylineno, s);
}

/* int main(int argc, char **argv) {
    if (argc > 1) {  
        yyin = fopen(argv[1], "r");
        if (!yyin) {  
            perror(argv[1]);  
            return 1;  
        }  
    } else {
        yyin = stdin;
    }

    init_symbol_management();

    if (yyparse() == 0) {
        printf("Parsing completed successfully.\n\n");
        printf("--- Abstract Syntax Tree ---\n");
        print_ast(root, 0);
        print_symbol_table();
    } else {
        printf("Parsing failed.\n");
    }

    if (root) {
        free_ast(root);
    }
    
    destroy_symbol_management();

    if (yyin && yyin != stdin) fclose(yyin);

    return 0;
} */
