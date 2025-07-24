#pragma once

#include "IR/Module.h"

// 添加运行时库函数到符号表
void add_runtime_lib_to_symbol_table();

// 添加运行时库函数到函数记录表
void add_runtime_lib_to_func_tab(midend::Module* module);
