#include "ir_gen.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#define DEBUG

#ifdef DEBUG
#include <cstdio>

#include "IR/IRPrinter.h"
#endif

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/Module.h"
#include "IR/Type.h"

extern "C" {
#include "AST.h"
#include "symbol_table.h"
#include "y.tab.h"
extern int yyparse(void);
}

extern FILE* yyin;
extern ASTNodePtr root;

// 全局标识符（函数、全局变量）
std::unordered_map<int, midend::Function*> func_tab;

// IR临时变量编号
int temp_idx;

// 辅助函数：将DataType转换为IR类型
midend::Type* get_ir_type(midend::Context* ctx, DataType data_type) {
    switch (data_type) {
        case DATA_INT:
            return ctx->getInt32Type();
        case DATA_FLOAT:
            return ctx->getFloatType();
        case DATA_CHAR:
            return ctx->getInt32Type();  // 使用int32代替int8
        case DATA_BOOL:
            return ctx->getInt1Type();
        case DATA_VOID:
            return ctx->getVoidType();
        default:
            return ctx->getInt32Type();  // 默认使用int32
    }
}

// 辅助函数：创建常量值
midend::Value* create_constant(midend::IRBuilder& builder, NodeData data,
                               NodeDataType data_type) {
    switch (data_type) {
        case INT_DATA:
            return builder.getInt32(data.direct_int);
        case FLOAT_DATA:
            return builder.getFloat(data.direct_float);
        default:
            return builder.getInt32(0);
    }
}

midend::Value* create_binary_op(midend::IRBuilder& builder, midend::Value* left,
                                midend::Value* right, std::string op_name) {
    if (op_name == "+") {
        return builder.createAdd(left, right, "add_result");
    } else if (op_name == "-") {
        return builder.createSub(left, right, "sub_result");
    } else if (op_name == "*") {
        return builder.createMul(left, right, "mul_result");
    } else if (op_name == "/") {
        return builder.createDiv(left, right, "div_result");
    } else if (op_name == "%") {
        return builder.createRem(left, right, "rem_result");
    } else if (op_name == "<") {
        return builder.createICmpSLT(left, right, "lt_result");
    } else if (op_name == "<=") {
        return builder.createICmpSLE(left, right, "le_result");
    } else if (op_name == ">") {
        return builder.createICmpSGT(left, right, "gt_result");
    } else if (op_name == ">=") {
        return builder.createICmpSGE(left, right, "ge_result");
    } else if (op_name == "==") {
        return builder.createICmpEQ(left, right, "eq_result");
    } else if (op_name == "!=") {
        return builder.createICmpNE(left, right, "ne_result");
    } else if (op_name == "&&") {
        return builder.createLAnd(left, right, "and_result");
    } else if (op_name == "||") {
        return builder.createLOr(left, right, "or_result");
    } else
        return nullptr;
}

std::string get_symbol_name(SymbolPtr symbol) {
    return std::string(symbol->name) + "_" + std::to_string(symbol->id);
}

midend::Value* def_var(midend::IRBuilder& builder, SymbolPtr symbol,
                       std::unordered_map<int, midend::Value*>& local_vars) {
    // 变量名
    std::string var_name = get_symbol_name(symbol);

    // 获取变量类型
    midend::Type* var_type =
        get_ir_type(builder.getContext(), symbol->data_type);

    // 创建局部变量
    midend::Value* alloca = builder.createAlloca(var_type, nullptr, var_name);
    local_vars[symbol->id] = alloca;
    return alloca;
}

// 递归处理AST节点的函数（处理函数内部的语句）
midend::Value* translate_node(
    ASTNodePtr node, midend::IRBuilder& builder, midend::Function* current_func,
    std::unordered_map<int, midend::Value*>& local_vars) {
    if (!node) return nullptr;

    switch (node->node_type) {
        case NODE_LIST: {
            // 列表节点，处理所有子节点
            midend::Value* last_value = nullptr;
            for (int i = 0; i < node->child_count; ++i) {
                last_value = translate_node(node->children[i], builder,
                                            current_func, local_vars);
            }
            return last_value;
        }

        case NODE_CONST: {
            // 常量节点
            return create_constant(builder, node->data, node->data_type);
        }

        case NODE_VAR: {
            // 变量节点
            SymbolPtr symbol = node->data.symb_ptr;
            if (node->data_type != SYMB_DATA && !symbol) return nullptr;

            // 从局部变量映射中查找
            auto it = local_vars.find(symbol->id);
            if (it != local_vars.end()) {
                midend::Type* type = it->second->getType();
                if (type->isPointerType())
                    return builder.createLoad(it->second,
                                              std::to_string(temp_idx++));
                else
                    return it->second;
            }
            return nullptr;
        }

        case NODE_FUNC_CALL: {
            // 函数调用节点
            SymbolPtr func_sym = node->data.symb_ptr;
            if (node->data_type != SYMB_DATA && !func_sym) return nullptr;

            std::vector<midend::Value*> params;
            for (int i = 0; i < node->child_count; ++i) {
                midend::Value* param_sym = translate_node(
                    node->children[i], builder, current_func, local_vars);
                if (!param_sym) return nullptr;
                params.push_back(param_sym);
            }
            auto it = func_tab.find(func_sym->id);
            if (it != func_tab.end()) {
                midend::Function* func = it->second;
                return builder.createCall(func, params,
                                          std::to_string(temp_idx++));
            }

            return nullptr;
        }

        case NODE_BINARY_OP: {
            // 二元操作节点
            if (node->child_count < 2) return nullptr;

            midend::Value* left = translate_node(node->children[0], builder,
                                                 current_func, local_vars);
            midend::Value* right = translate_node(node->children[1], builder,
                                                  current_func, local_vars);

            if (!left || !right) return nullptr;
            std::string op_name = node->name ? node->name : "";

            return create_binary_op(builder, left, right, op_name);
        }

        case NODE_UNARY_OP: {
            // 一元操作节点
            if (node->child_count < 1) return nullptr;

            midend::Value* operand = translate_node(node->children[0], builder,
                                                    current_func, local_vars);
            if (!operand) return nullptr;

            std::string op_name = node->name ? node->name : "";

            if (op_name == "-") {
                return builder.createSub(builder.getInt32(0), operand,
                                         "neg_result");
            } else if (op_name == "!") {
                return builder.createNot(operand, "not_result");
            }

            return nullptr;
        }

        case NODE_ASSIGN_STMT: {
            // 赋值语句
            if (node->child_count < 2) return nullptr;

            // 左值（变量）
            ASTNodePtr left_node = node->children[0];
            SymbolPtr symbol = left_node->data.symb_ptr;
            if (left_node->node_type != NODE_VAR || !symbol) return nullptr;

            // 右值（表达式）
            midend::Value* right_value = translate_node(
                node->children[1], builder, current_func, local_vars);
            if (!right_value) return nullptr;

            // std::string var_name = left_node->name ? left_node->name : "";
            std::string var_name = get_symbol_name(left_node->data.symb_ptr);

            // 检查是否已有局部变量
            auto it = local_vars.find(symbol->id);
            if (it != local_vars.end()) {
                // 更新现有变量
                builder.createStore(right_value, it->second);
                return right_value;
            } else {
                // 创建新的局部变量
                midend::Type* var_type = right_value->getType();
                midend::Value* alloca =
                    builder.createAlloca(var_type, nullptr, var_name);
                builder.createStore(right_value, alloca);
                local_vars[symbol->id] = alloca;
                return right_value;
            }
        }

        case NODE_RETURN_STMT: {
            // 返回语句
            if (node->child_count > 0) {
                midend::Value* return_value = translate_node(
                    node->children[0], builder, current_func, local_vars);
                if (return_value) {
                    builder.createRet(return_value);
                } else {
                    builder.createRetVoid();
                }
            } else {
                builder.createRetVoid();
            }
            return nullptr;
        }

        case NODE_VAR_DEF: {
            SymbolPtr symbol = node->data.symb_ptr;
            if (node->data_type != SYMB_DATA || !symbol) return nullptr;

            // 创建局部变量
            midend::Value* alloca = def_var(builder, symbol, local_vars);

            // 如果有初始化值（第一个子节点是常量或表达式）
            if (node->child_count > 0) {
                midend::Value* init_value = translate_node(
                    node->children[0], builder, current_func, local_vars);
                if (init_value) {
                    builder.createStore(init_value, alloca);
                }
            }

            return alloca;
        }

        default:
            // 处理其他节点类型
            midend::Value* last_value = nullptr;
            for (int i = 0; i < node->child_count; ++i) {
                last_value = translate_node(node->children[i], builder,
                                            current_func, local_vars);
            }
            return last_value;
    }
}

void translate_func_def(ASTNodePtr node, midend::Module* module) {
    if (!node) return;

    SymbolPtr func_sym = node->data.symb_ptr;
    if (node->node_type != NODE_FUNC_DEF || node->data_type != SYMB_DATA ||
        !func_sym)
        return;

    auto ctx = module->getContext();

    // 从符号表中获取返回类型和函数名
    midend::Type* return_type = get_ir_type(ctx, func_sym->data_type);
    std::string func_name = (func_sym->name) ? func_sym->name : "unknown_func";

    // 函数参数节点（函数参数作为局部变量）
    ASTNodePtr param_node = node->children[0];
    SymbolPtr param_sym;
    std::vector<midend::Type*> param_types;
    std::vector<std::string> param_names;
    for (int i = 0; i < param_node->child_count; i++) {
        param_sym = param_node->children[i]->data.symb_ptr;
        midend::Type* var_type = get_ir_type(ctx, param_sym->data_type);
        param_types.push_back(var_type);
        param_names.push_back(get_symbol_name(param_sym));
    }

    // 创建函数类型
    midend::FunctionType* func_type =
        midend::FunctionType::get(return_type, param_types);

    // 创建函数
    midend::Function* func =
        midend::Function::Create(func_type, func_name, param_names, module);
    func_tab[func_sym->id] = func;

    // 创建基本块
    midend::BasicBlock* entry_bb =
        midend::BasicBlock::Create(ctx, "entry", func);
    midend::IRBuilder builder(entry_bb);

    // 在函数体内部定义函数形参
    std::unordered_map<int, midend::Value*> func_local_vars;
    for (int i = 0; i < param_node->child_count; i++) {
        param_sym = param_node->children[i]->data.symb_ptr;
        func_local_vars[param_sym->id] = func->getArg(i);
    }

    // 处理函数体
    translate_node(node->children[1], builder, func, func_local_vars);

    // 如果没有显式的return语句，添加一个
    if (!entry_bb->getTerminator()) {
        if (return_type->isVoidType())
            builder.createRetVoid();
        else
            builder.createRet(builder.getInt32(0));
    }
}

// 从根节点开始翻译，处理函数定义
std::unique_ptr<midend::Module> translate_root(ASTNodePtr node) {
    auto ctx = new midend::Context();
    auto module = std::make_unique<midend::Module>("main", ctx);

    if (!node) return module;

    // 遍历根节点的所有子节点，寻找函数定义
    ASTNodePtr child;
    for (int i = 0; i < node->child_count; ++i) {
        child = node->children[i];
        switch (child->node_type) {
            case NODE_FUNC_DEF:
                temp_idx = 0;
                translate_func_def(child, module.get());
                break;
            case NODE_VAR_DEF:
                break;
            case NODE_CONST_VAR_DEF:
                break;
            case NODE_ARRAY_DEF:
                break;
            case NODE_CONST_ARRAY_DEF:
                break;
            default:
                break;
        }
    }

    return module;
}

std::unique_ptr<midend::Module> generate_IR(FILE* file_in) {
    if (!file_in) return nullptr;
    yyin = file_in;

    init_symbol_management();

#ifdef DEBUG
    if (!yyparse()) {
        printf("Parsing completed successfully.\n\n");
        printf("--- Abstract Syntax Tree ---\n");
        print_ast(root, 0);
        print_symbol_table();
    } else {
        printf("Parsing failed.\n");
    }
#else
    yyparse();
#endif

    auto module = translate_root(root);

#ifdef DEBUG
    if (module) {
        printf("--- Generated IR ---\n");
        std::string ir_output = midend::IRPrinter::toString(module.get());
        printf("%s\n", ir_output.c_str());
    } else {
        printf("IR generation failed!\n");
    }
#endif

    if (root) free_ast(root);
    free_symbol_management();
    return module;
}
