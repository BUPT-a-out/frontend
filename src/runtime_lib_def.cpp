#include "runtime_lib_def.h"

#include <stdlib.h>
#include <string.h>

#include <unordered_map>

#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/Module.h"
#include "IR/Type.h"

extern "C" {
#include "sy_parser/symbol_table.h"
}

extern std::unordered_map<int, midend::Function*> func_tab;

// 静态链接库函数记录表
static std::unordered_map<std::string, SymbolPtr> func_name_to_ptr;

void add_runtime_lib_to_symbol_table() {
    SymbolPtr sym;

    // 1. int getint()
    sym = define_symbol("getint", SYMB_FUNCTION, DATA_INT, 0);
    sym->attributes.func_info.param_count = 0;
    sym->attributes.func_info.params = NULL;
    func_name_to_ptr["getint"] = sym;

    // 2. int getch()
    sym = define_symbol("getch", SYMB_FUNCTION, DATA_INT, 0);
    sym->attributes.func_info.param_count = 0;
    sym->attributes.func_info.params = NULL;
    func_name_to_ptr["getch"] = sym;

    // 3. float getfloat()
    sym = define_symbol("getfloat", SYMB_FUNCTION, DATA_FLOAT, 0);
    sym->attributes.func_info.param_count = 0;
    sym->attributes.func_info.params = NULL;
    func_name_to_ptr["getfloat"] = sym;

    // 4. int getarray(int[])
    sym = define_symbol("getarray", SYMB_FUNCTION, DATA_INT, 0);
    enter_scope();
    sym->attributes.func_info.param_count = 1;
    sym->attributes.func_info.params = (SymbolPtr*)malloc(sizeof(SymbolPtr));
    SymbolPtr param = define_symbol("array", SYMB_ARRAY, DATA_INT, 0);
    param->attributes.array_info.dimensions = 1;
    param->attributes.array_info.shape = (int*)malloc(sizeof(int));  // 维度未知
    param->attributes.array_info.shape[0] = 0;
    sym->attributes.func_info.params[0] = param;
    exit_scope();
    func_name_to_ptr["getarray"] = sym;

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
    func_name_to_ptr["getfarray"] = sym;

    // 6. void putint(int)
    sym = define_symbol("putint", SYMB_FUNCTION, DATA_VOID, 0);
    enter_scope();
    sym->attributes.func_info.param_count = 1;
    sym->attributes.func_info.params = (SymbolPtr*)malloc(sizeof(SymbolPtr));
    param = define_symbol("value", SYMB_VAR, DATA_INT, 0);
    sym->attributes.func_info.params[0] = param;
    exit_scope();
    func_name_to_ptr["putint"] = sym;

    // 7. void putch(int)
    sym = define_symbol("putch", SYMB_FUNCTION, DATA_VOID, 0);
    enter_scope();
    sym->attributes.func_info.param_count = 1;
    sym->attributes.func_info.params = (SymbolPtr*)malloc(sizeof(SymbolPtr));
    param = define_symbol("value", SYMB_VAR, DATA_INT, 0);
    sym->attributes.func_info.params[0] = param;
    exit_scope();
    func_name_to_ptr["putch"] = sym;

    // 8. void putfloat(float)
    sym = define_symbol("putfloat", SYMB_FUNCTION, DATA_VOID, 0);
    enter_scope();
    sym->attributes.func_info.param_count = 1;
    sym->attributes.func_info.params = (SymbolPtr*)malloc(sizeof(SymbolPtr));
    param = define_symbol("value", SYMB_VAR, DATA_FLOAT, 0);
    sym->attributes.func_info.params[0] = param;
    exit_scope();
    func_name_to_ptr["putfloat"] = sym;

    // 9. void putarray(int, int[])
    sym = define_symbol("putarray", SYMB_FUNCTION, DATA_VOID, 0);
    enter_scope();
    sym->attributes.func_info.param_count = 2;
    sym->attributes.func_info.params =
        (SymbolPtr*)malloc(2 * sizeof(SymbolPtr));
    param = define_symbol("len", SYMB_VAR, DATA_INT, 0);
    sym->attributes.func_info.params[0] = param;
    param = define_symbol("array", SYMB_ARRAY, DATA_INT, 0);
    param->attributes.array_info.dimensions = 1;
    param->attributes.array_info.shape = (int*)malloc(sizeof(int));
    param->attributes.array_info.shape[0] = 0;
    sym->attributes.func_info.params[1] = param;
    exit_scope();
    func_name_to_ptr["putarray"] = sym;

    // 10. void putfarray(int, float[])
    sym = define_symbol("putfarray", SYMB_FUNCTION, DATA_VOID, 0);
    enter_scope();
    sym->attributes.func_info.param_count = 2;
    sym->attributes.func_info.params =
        (SymbolPtr*)malloc(2 * sizeof(SymbolPtr));
    param = define_symbol("len", SYMB_VAR, DATA_INT, 0);
    sym->attributes.func_info.params[0] = param;
    param = define_symbol("array", SYMB_ARRAY, DATA_FLOAT, 0);
    param->attributes.array_info.dimensions = 1;
    param->attributes.array_info.shape = (int*)malloc(sizeof(int));
    param->attributes.array_info.shape[0] = 0;
    sym->attributes.func_info.params[1] = param;
    exit_scope();
    func_name_to_ptr["putfarray"] = sym;

    // 11. void putf(const char*, int, ...)
    sym = define_symbol("putf", SYMB_FUNCTION, DATA_VOID, 0);
    enter_scope();
    sym->attributes.func_info.param_count = 2;  // 变参函数，前两个参数
    sym->attributes.func_info.params =
        (SymbolPtr*)malloc(2 * sizeof(SymbolPtr));
    param = define_symbol("format_string", SYMB_VAR, DATA_CHAR, 0);
    sym->attributes.func_info.params[0] = param;
    param = define_symbol("value", SYMB_VAR, DATA_INT, 0);
    sym->attributes.func_info.params[1] = param;
    exit_scope();
    func_name_to_ptr["putf"] = sym;

    // 12. void starttime()
    sym = define_symbol("starttime", SYMB_FUNCTION, DATA_VOID, 0);
    sym->attributes.func_info.param_count = 0;
    sym->attributes.func_info.params = NULL;
    func_name_to_ptr["starttime"] = sym;

    // 13. void stoptime()
    sym = define_symbol("stoptime", SYMB_FUNCTION, DATA_VOID, 0);
    sym->attributes.func_info.param_count = 0;
    sym->attributes.func_info.params = NULL;
    func_name_to_ptr["stoptime"] = sym;
}

void add_runtime_lib_to_func_tab(midend::Module* module) {
    if (func_name_to_ptr.empty()) return;
    auto ctx = module->getContext();
    SymbolPtr sym;

    sym = func_name_to_ptr["getint"];
    if (sym->attributes.func_info.call_count) {
        midend::Type* return_type = ctx->getInt32Type();
        std::vector<midend::Type*> param_types;
        std::vector<std::string> param_names;
        midend::FunctionType* func_type =
            midend::FunctionType::get(return_type, param_types);
        midend::Function* func =
            midend::Function::Create(func_type, "getint", param_names, module);
        func_tab[sym->id] = func;
    }

    sym = func_name_to_ptr["getch"];
    if (sym->attributes.func_info.call_count) {
        midend::Type* return_type = ctx->getInt32Type();
        std::vector<midend::Type*> param_types;
        std::vector<std::string> param_names;
        midend::FunctionType* func_type =
            midend::FunctionType::get(return_type, param_types);
        midend::Function* func =
            midend::Function::Create(func_type, "getch", param_names, module);
        func_tab[sym->id] = func;
    }

    sym = func_name_to_ptr["getfloat"];
    if (sym->attributes.func_info.call_count) {
        midend::Type* return_type = ctx->getFloatType();
        std::vector<midend::Type*> param_types;
        std::vector<std::string> param_names;
        midend::FunctionType* func_type =
            midend::FunctionType::get(return_type, param_types);
        midend::Function* func = midend::Function::Create(func_type, "getfloat",
                                                          param_names, module);
        func_tab[sym->id] = func;
    }

    sym = func_name_to_ptr["getarray"];
    if (sym->attributes.func_info.call_count) {
        midend::Type* return_type = ctx->getInt32Type();
        std::vector<midend::Type*> param_types;
        std::vector<std::string> param_names;
        param_types.push_back(midend::PointerType::get(ctx->getInt32Type()));
        param_names.push_back("array");
        midend::FunctionType* func_type =
            midend::FunctionType::get(return_type, param_types);
        midend::Function* func = midend::Function::Create(func_type, "getarray",
                                                          param_names, module);
        func_tab[sym->id] = func;
    }

    sym = func_name_to_ptr["getfarray"];
    if (sym->attributes.func_info.call_count) {
        midend::Type* return_type = ctx->getInt32Type();
        std::vector<midend::Type*> param_types;
        std::vector<std::string> param_names;
        param_types.push_back(midend::PointerType::get(ctx->getFloatType()));
        param_names.push_back("array");
        midend::FunctionType* func_type =
            midend::FunctionType::get(return_type, param_types);
        midend::Function* func = midend::Function::Create(
            func_type, "getfarray", param_names, module);
        func_tab[sym->id] = func;
    }

    sym = func_name_to_ptr["putint"];
    if (sym->attributes.func_info.call_count) {
        midend::Type* return_type = ctx->getVoidType();
        std::vector<midend::Type*> param_types;
        std::vector<std::string> param_names;
        param_types.push_back(ctx->getInt32Type());
        param_names.push_back("value");
        midend::FunctionType* func_type =
            midend::FunctionType::get(return_type, param_types);
        midend::Function* func =
            midend::Function::Create(func_type, "putint", param_names, module);
        func_tab[sym->id] = func;
    }

    sym = func_name_to_ptr["putch"];
    if (sym->attributes.func_info.call_count) {
        midend::Type* return_type = ctx->getVoidType();
        std::vector<midend::Type*> param_types;
        std::vector<std::string> param_names;
        param_types.push_back(ctx->getInt32Type());
        param_names.push_back("value");
        midend::FunctionType* func_type =
            midend::FunctionType::get(return_type, param_types);
        midend::Function* func =
            midend::Function::Create(func_type, "putch", param_names, module);
        func_tab[sym->id] = func;
    }

    sym = func_name_to_ptr["putfloat"];
    if (sym->attributes.func_info.call_count) {
        midend::Type* return_type = ctx->getVoidType();
        std::vector<midend::Type*> param_types;
        std::vector<std::string> param_names;
        param_types.push_back(ctx->getFloatType());
        param_names.push_back("value");
        midend::FunctionType* func_type =
            midend::FunctionType::get(return_type, param_types);
        midend::Function* func = midend::Function::Create(func_type, "putfloat",
                                                          param_names, module);
        func_tab[sym->id] = func;
    }

    sym = func_name_to_ptr["putarray"];
    if (sym->attributes.func_info.call_count) {
        midend::Type* return_type = ctx->getVoidType();
        std::vector<midend::Type*> param_types;
        std::vector<std::string> param_names;
        param_types.push_back(ctx->getInt32Type());
        param_types.push_back(midend::PointerType::get(ctx->getInt32Type()));
        param_names.push_back("len");
        param_names.push_back("array");
        midend::FunctionType* func_type =
            midend::FunctionType::get(return_type, param_types);
        midend::Function* func = midend::Function::Create(func_type, "putarray",
                                                          param_names, module);
        func_tab[sym->id] = func;
    }

    sym = func_name_to_ptr["putfarray"];
    if (sym->attributes.func_info.call_count) {
        midend::Type* return_type = ctx->getVoidType();
        std::vector<midend::Type*> param_types;
        std::vector<std::string> param_names;
        param_types.push_back(ctx->getInt32Type());
        param_types.push_back(midend::PointerType::get(ctx->getFloatType()));
        param_names.push_back("len");
        param_names.push_back("array");
        midend::FunctionType* func_type =
            midend::FunctionType::get(return_type, param_types);
        midend::Function* func = midend::Function::Create(
            func_type, "putfarray", param_names, module);
        func_tab[sym->id] = func;
    }

    sym = func_name_to_ptr["putf"];
    if (sym->attributes.func_info.call_count) {
        midend::Type* return_type = ctx->getVoidType();
        std::vector<midend::Type*> param_types;
        std::vector<std::string> param_names;
        param_types.push_back(midend::PointerType::get(ctx->getInt32Type()));
        param_types.push_back(ctx->getInt32Type());
        param_names.push_back("format_string");
        param_names.push_back("value");
        midend::FunctionType* func_type =
            midend::FunctionType::get(return_type, param_types);
        midend::Function* func =
            midend::Function::Create(func_type, "putf", param_names, module);
        func_tab[sym->id] = func;
    }

    sym = func_name_to_ptr["starttime"];
    if (sym->attributes.func_info.call_count) {
        midend::Type* return_type = ctx->getVoidType();
        std::vector<midend::Type*> param_types;
        std::vector<std::string> param_names;
        midend::FunctionType* func_type =
            midend::FunctionType::get(return_type, param_types);
        midend::Function* func = midend::Function::Create(
            func_type, "starttime", param_names, module);
        func_tab[sym->id] = func;
    }

    sym = func_name_to_ptr["stoptime"];
    if (sym->attributes.func_info.call_count) {
        midend::Type* return_type = ctx->getVoidType();
        std::vector<midend::Type*> param_types;
        std::vector<std::string> param_names;
        midend::FunctionType* func_type =
            midend::FunctionType::get(return_type, param_types);
        midend::Function* func = midend::Function::Create(func_type, "stoptime",
                                                          param_names, module);
        func_tab[sym->id] = func;
    }
}
