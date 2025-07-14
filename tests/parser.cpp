#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "IR/Module.h"
#include "IR/IRPrinter.h"

extern "C" {
#include "AST.h"
#include "symbol_table.h"
}

#include "ir_gen.h"

extern FILE* yyin;
extern "C" int yyparse(void);
extern "C" ASTNodePtr root;

int main(int argc, char** argv) {
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

        printf("\n--- Testing IR Generation ---\n");
        auto module = generate_test_module();
        if (module) {
            printf("IR module generated successfully!\n");
            std::cout << "Generated IR:\n"
                      << midend::IRPrinter::toString(module.get()) << std::endl;
        } else {
            printf("Failed to generate IR module.\n");
        }
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