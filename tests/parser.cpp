#include <cstdio>
#include <cstdlib>
#include <memory>

#include "IR/Module.h"
#include "IR/IRPrinter.h"

#include "ir_gen.h"

void test();

int main(int argc, char **argv) {
    FILE* file_in = nullptr;
    if (argc > 1) {  
        file_in = fopen(argv[1], "r");
        if (!file_in) {  
            perror(argv[1]);  
            return 1;  
        }  
    } else {
        file_in = stdin;
    }

    generate_IR(file_in);
    // test();

    if (file_in && file_in != stdin) fclose(file_in);
    return 0;
}
