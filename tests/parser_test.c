#include <stdio.h>
#include <stdlib.h>
#include "AST.h"

extern FILE* yyin;
extern int yyparse(void);
extern ASTNodePtr root;

int main(int argc, char **argv) {
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
}