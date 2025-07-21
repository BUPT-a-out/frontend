#include "ir_gen.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
std::unordered_map<int, midend::GlobalVariable*> global_var_tab;
std::unordered_set<int> function_param_symbols;

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

// 辅助函数：创建数组类型
midend::Type* get_array_type(midend::Context* ctx, DataType base_type,
                             int dimensions, int* shape) {
    midend::Type* elem_type = get_ir_type(ctx, base_type);

    // 从最内层开始构建多维数组类型
    for (int i = dimensions - 1; i >= 0; i--) {
        elem_type = midend::ArrayType::get(elem_type, shape[i]);
    }

    return elem_type;
}

// 辅助函数：处理数组初始化列表
std::vector<midend::Constant*> process_array_init_list(ASTNodePtr init_list,
                                                       midend::Context* ctx,
                                                       DataType base_type) {
    std::vector<midend::Constant*> result;
    if (!init_list) return result;

    for (int i = 0; i < init_list->child_count; ++i) {
        ASTNodePtr child = init_list->children[i];
        if (child->node_type == NODE_CONST) {
            if (child->data_type == INT_DATA && base_type == DATA_INT) {
                result.push_back(midend::ConstantInt::get(
                    ctx->getInt32Type(), child->data.direct_int));
            } else if (child->data_type == FLOAT_DATA &&
                       base_type == DATA_FLOAT) {
                result.push_back(midend::ConstantFP::get(
                    ctx->getFloatType(), child->data.direct_float));
            }
        }
    }
    return result;
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

// 辅助函数：获取数组元素指针
midend::Value* get_array_element_ptr(
    SymbolPtr symbol, const std::vector<midend::Value*>& indices,
    midend::IRBuilder& builder,
    const std::unordered_map<int, midend::Value*>& local_vars) {
    auto it = local_vars.find(symbol->id);
    midend::Value* array_ptr = nullptr;
    midend::Type* array_type = nullptr;

    if (it != local_vars.end()) {
        array_ptr = it->second;
        midend::Type* ptr_type = array_ptr->getType();

        if (function_param_symbols.count(symbol->id)) {
            array_type = ptr_type;
        } else if (ptr_type->isPointerType()) {
            array_type =
                static_cast<midend::PointerType*>(ptr_type)->getElementType();
        }
    } else {
        auto global_it = global_var_tab.find(symbol->id);
        if (global_it != global_var_tab.end()) {
            array_ptr = global_it->second;
            array_type = global_it->second->getValueType();
        } else {
            return nullptr;
        }
    }

    if (!array_ptr || indices.empty()) return nullptr;

    return function_param_symbols.count(symbol->id)
               ? builder.createGEP(array_ptr->getType(), array_ptr, indices)
               : builder.createGEP(array_type, array_ptr, indices);
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
                if (type->isPointerType()) {
                    midend::Type* element_type =
                        static_cast<midend::PointerType*>(type)
                            ->getElementType();
                    return element_type->isArrayType()
                               ? it->second
                               : builder.createLoad(it->second,
                                                    std::to_string(temp_idx++));
                } else {
                    return it->second;
                }
            }

            auto global_it = global_var_tab.find(symbol->id);
            if (global_it != global_var_tab.end()) {
                midend::GlobalVariable* global_var = global_it->second;
                if (global_var->getValueType()->isArrayType()) {
                    return global_var;
                } else {
                    return builder.createLoad(global_var);
                }
            }

            return nullptr;
        }

        case NODE_ARRAY_ACCESS: {
            SymbolPtr symbol = node->data.symb_ptr;
            if (node->data_type != SYMB_DATA && !symbol) return nullptr;

            std::vector<midend::Value*> indices;
            for (int i = 0; i < node->child_count; ++i) {
                midend::Value* index = translate_node(
                    node->children[i], builder, current_func, local_vars);
                if (!index) return nullptr;
                indices.push_back(index);
            }
            midend::Value* gep =
                get_array_element_ptr(symbol, indices, builder, local_vars);
            return gep ? builder.createLoad(gep) : nullptr;
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

            // 左值（变量或数组元素）
            ASTNodePtr left_node = node->children[0];

            // 右值（表达式）
            midend::Value* right_value = translate_node(
                node->children[1], builder, current_func, local_vars);
            if (!right_value) return nullptr;

            if (left_node->node_type == NODE_VAR) {
                // 普通变量赋值
                SymbolPtr symbol = left_node->data.symb_ptr;
                if (!symbol) return nullptr;

                std::string var_name = get_symbol_name(symbol);

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
            } else if (left_node->node_type == NODE_ARRAY_ACCESS) {
                SymbolPtr symbol = left_node->data.symb_ptr;
                if (!symbol) return nullptr;

                std::vector<midend::Value*> indices;
                for (int i = 0; i < left_node->child_count; ++i) {
                    midend::Value* index =
                        translate_node(left_node->children[i], builder,
                                       current_func, local_vars);
                    if (!index) return nullptr;
                    indices.push_back(index);
                }
                midend::Value* gep =
                    get_array_element_ptr(symbol, indices, builder, local_vars);
                if (gep) {
                    builder.createStore(right_value, gep);
                    return right_value;
                }
            }

            return nullptr;
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

        case NODE_ARRAY_DEF: {
            // 局部数组定义
            SymbolPtr symbol = node->data.symb_ptr;
            if (node->data_type != SYMB_DATA || !symbol) return nullptr;

            if (symbol->symbol_type != SYMB_ARRAY) return nullptr;

            // 创建数组类型
            midend::Type* array_type =
                get_array_type(builder.getContext(), symbol->data_type,
                               symbol->attributes.array_info.dimensions,
                               symbol->attributes.array_info.shape);

            // 创建局部数组
            std::string array_name = get_symbol_name(symbol);
            midend::Value* array_alloca =
                builder.createAlloca(array_type, nullptr, array_name);
            local_vars[symbol->id] = array_alloca;

            if (node->child_count > 1) {
                ASTNodePtr init_list = node->children[1];
                auto init_values = process_array_init_list(
                    init_list, builder.getContext(), symbol->data_type);
                for (size_t i = 0; i < init_values.size(); ++i) {
                    midend::Value* elem_ptr = builder.createGEP(
                        array_type, array_alloca, {builder.getInt32(i)});
                    builder.createStore(init_values[i], elem_ptr);
                }
            }

            return array_alloca;
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
        ASTNodePtr param_ast = param_node->children[i];
        midend::Type* param_type = nullptr;

        if (param_sym->symbol_type == SYMB_ARRAY) {
            midend::Type* elem_type = get_ir_type(ctx, param_sym->data_type);
            for (int j = 1; j < param_ast->child_count; j++) {
                ASTNodePtr dim_node = param_ast->children[j];
                if (dim_node->node_type == NODE_CONST &&
                    dim_node->data_type == INT_DATA) {
                    elem_type = midend::ArrayType::get(
                        elem_type, dim_node->data.direct_int);
                }
            }
            param_type = midend::PointerType::get(elem_type);
        } else {
            param_type = get_ir_type(ctx, param_sym->data_type);
        }

        param_types.push_back(param_type);
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
        midend::Value* param_value = func->getArg(i);
        function_param_symbols.insert(param_sym->id);
        func_local_vars[param_sym->id] = param_value;
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

    // 先处理全局变量和常量
    for (int i = 0; i < node->child_count; ++i) {
        ASTNodePtr child = node->children[i];
        switch (child->node_type) {
            case NODE_VAR_DEF:
            case NODE_CONST_VAR_DEF: {
                SymbolPtr sym = child->data.symb_ptr;
                if (!sym) break;
                midend::Type* var_type = get_ir_type(ctx, sym->data_type);
                bool is_const = (child->node_type == NODE_CONST_VAR_DEF);
                // 处理初值
                midend::Constant* init = nullptr;
                if (child->child_count > 0 && child->children[0]) {
                    ASTNodePtr init_node = child->children[0];
                    if (init_node->node_type == NODE_CONST) {
                        if (init_node->data_type == INT_DATA) {
                            init = midend::ConstantInt::get(
                                (midend::IntegerType*)var_type,
                                init_node->data.direct_int);
                        } else if (init_node->data_type == FLOAT_DATA) {
                            init = midend::ConstantFP::get(
                                (midend::FloatType*)var_type,
                                init_node->data.direct_float);
                        }
                    }
                }
                auto linkage = is_const
                                   ? midend::GlobalVariable::InternalLinkage
                                   : midend::GlobalVariable::ExternalLinkage;
                midend::GlobalVariable::Create(var_type, is_const, linkage,
                                               init, get_symbol_name(sym),
                                               module.get());
                break;
            }
            case NODE_ARRAY_DEF:
            case NODE_CONST_ARRAY_DEF: {
                SymbolPtr sym = child->data.symb_ptr;
                if (!sym || (sym->symbol_type != SYMB_ARRAY &&
                             sym->symbol_type != SYMB_CONST_ARRAY))
                    break;

                midend::Type* array_type = get_array_type(
                    ctx, sym->data_type, sym->attributes.array_info.dimensions,
                    sym->attributes.array_info.shape);
                bool is_const = (child->node_type == NODE_CONST_ARRAY_DEF);
                midend::Constant* init = nullptr;

                if (child->child_count > 1) {
                    ASTNodePtr init_list = child->children[1];
                    auto init_values =
                        process_array_init_list(init_list, ctx, sym->data_type);
                    if (!init_values.empty()) {
                        init = midend::ConstantArray::get(
                            (midend::ArrayType*)array_type, init_values);
                    }
                }

                auto linkage = is_const
                                   ? midend::GlobalVariable::InternalLinkage
                                   : midend::GlobalVariable::ExternalLinkage;
                midend::GlobalVariable* global_array =
                    midend::GlobalVariable::Create(
                        array_type, is_const, linkage, init,
                        get_symbol_name(sym), module.get());
                global_var_tab[sym->id] = global_array;
                break;
            }
            default:
                break;
        }
    }

    // 再处理函数定义
    ASTNodePtr child;
    for (int i = 0; i < node->child_count; ++i) {
        child = node->children[i];
        switch (child->node_type) {
            case NODE_FUNC_DEF:
                temp_idx = 0;
                translate_func_def(child, module.get());
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
