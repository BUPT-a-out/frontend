#include "symbol_table.h"
#include <stdlib.h>
#include <string.h>

// �������ʱ�⺯�������ű�
void add_runtime_lib_symbols() {
    // 1. int getint()
    SymbolPtr sym = define_symbol("getint", SYMB_FUNCTION, DATA_INT, 0);
    sym->attributes.func_info.param_count = 0;
    sym->attributes.func_info.params = NULL;

    // 2. int getch()
    sym = define_symbol("getch", SYMB_FUNCTION, DATA_INT, 0);
    sym->attributes.func_info.param_count = 0;
    sym->attributes.func_info.params = NULL;

    // 3. float getfloat()
    sym = define_symbol("getfloat", SYMB_FUNCTION, DATA_FLOAT, 0);
    sym->attributes.func_info.param_count = 0;
    sym->attributes.func_info.params = NULL;

    // 4. int getarray(int[])
    sym = define_symbol("getarray", SYMB_FUNCTION, DATA_INT, 0);
    enter_scope();
    sym->attributes.func_info.param_count = 1;
    sym->attributes.func_info.params = (SymbolPtr*)malloc(sizeof(SymbolPtr));
    SymbolPtr param = define_symbol("array", SYMB_ARRAY, DATA_INT, 0);
    param->attributes.array_info.dimensions = 1;
    param->attributes.array_info.shape = (int*)malloc(sizeof(int)); // ά��δ֪
    param->attributes.array_info.shape[0] = 0;
    sym->attributes.func_info.params[0] = param;
    exit_scope();

    // 5. int getfarray(float[])
    sym = define_symbol("getfarray", SYMB_FUNCTION, DATA_INT, 0);
    enter_scope();
    sym->attributes.func_info.param_count = 1;
    sym->attributes.func_info.params = (SymbolPtr*)malloc(sizeof(SymbolPtr));
    param = define_symbol("array", SYMB_ARRAY, DATA_FLOAT, 0);
    param->attributes.array_info.dimensions = 1;
    param->attributes.array_info.shape = (int*)malloc(sizeof(int));
    param->attributes.array_info.shape[0] = 0;
    sym->attributes.func_info.params[0] = param;
    exit_scope();

    // 6. void putint(int)
    sym = define_symbol("putint", SYMB_FUNCTION, DATA_VOID, 0);
    enter_scope();
    sym->attributes.func_info.param_count = 1;
    sym->attributes.func_info.params = (SymbolPtr*)malloc(sizeof(SymbolPtr));
    param = define_symbol("value", SYMB_VAR, DATA_INT, 0);
    sym->attributes.func_info.params[0] = param;
    exit_scope();

    // 7. void putch(int)
    sym = define_symbol("putch", SYMB_FUNCTION, DATA_VOID, 0);
    enter_scope();
    sym->attributes.func_info.param_count = 1;
    sym->attributes.func_info.params = (SymbolPtr*)malloc(sizeof(SymbolPtr));
    param = define_symbol("value", SYMB_VAR, DATA_INT, 0);
    sym->attributes.func_info.params[0] = param;
    exit_scope();

    // 8. void putfloat(float)
    sym = define_symbol("putfloat", SYMB_FUNCTION, DATA_VOID, 0);
    enter_scope();
    sym->attributes.func_info.param_count = 1;
    sym->attributes.func_info.params = (SymbolPtr*)malloc(sizeof(SymbolPtr));
    param = define_symbol("value", SYMB_VAR, DATA_FLOAT, 0);
    sym->attributes.func_info.params[0] = param;
    exit_scope();

    // 9. void putarray(int, int[])
    sym = define_symbol("putarray", SYMB_FUNCTION, DATA_VOID, 0);
    enter_scope();
    sym->attributes.func_info.param_count = 2;
    sym->attributes.func_info.params = (SymbolPtr*)malloc(2 * sizeof(SymbolPtr));
    param = define_symbol("len", SYMB_VAR, DATA_INT, 0);
    sym->attributes.func_info.params[0] = param;
    param = define_symbol("array", SYMB_ARRAY, DATA_INT, 0);
    param->attributes.array_info.dimensions = 1;
    param->attributes.array_info.shape = (int*)malloc(sizeof(int));
    param->attributes.array_info.shape[0] = 0;
    sym->attributes.func_info.params[1] = param;
    exit_scope();

    // 10. void putfarray(int, float[])
    sym = define_symbol("putfarray", SYMB_FUNCTION, DATA_VOID, 0);
    enter_scope();
    sym->attributes.func_info.param_count = 2;
    sym->attributes.func_info.params = (SymbolPtr*)malloc(2 * sizeof(SymbolPtr));
    param = define_symbol("len", SYMB_VAR, DATA_INT, 0);
    sym->attributes.func_info.params[0] = param;
    param = define_symbol("array", SYMB_ARRAY, DATA_FLOAT, 0);
    param->attributes.array_info.dimensions = 1;
    param->attributes.array_info.shape = (int*)malloc(sizeof(int));
    param->attributes.array_info.shape[0] = 0;
    sym->attributes.func_info.params[1] = param;
    exit_scope();

    // 11. void putf(const char*, int, ...)
    sym = define_symbol("putf", SYMB_FUNCTION, DATA_VOID, 0);
    enter_scope();
    sym->attributes.func_info.param_count = 2; // ��κ�����ǰ��������
    sym->attributes.func_info.params = (SymbolPtr*)malloc(2 * sizeof(SymbolPtr));
    param = define_symbol("format_string", SYMB_VAR, DATA_CHAR, 0);
    sym->attributes.func_info.params[0] = param;
    param = define_symbol("value", SYMB_VAR, DATA_INT, 0);
    sym->attributes.func_info.params[1] = param;
    exit_scope();
    // ��������Ϊ�ɱ����

    // 12. void starttime()
    sym = define_symbol("starttime", SYMB_FUNCTION, DATA_VOID, 0);
    sym->attributes.func_info.param_count = 0;
    sym->attributes.func_info.params = NULL;

    // 13. void stoptime()
    sym = define_symbol("stoptime", SYMB_FUNCTION, DATA_VOID, 0);
    sym->attributes.func_info.param_count = 0;
    sym->attributes.func_info.params = NULL;
}
