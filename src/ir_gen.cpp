#include "ir_gen.h"

#include <map>
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

// 辅助函数：计算数组总大小
int calculate_array_size(midend::Type* type) {
    if (!type->isArrayType()) return 1;
    midend::ArrayType* array_type = static_cast<midend::ArrayType*>(type);
    return array_type->getNumElements() *
           calculate_array_size(array_type->getElementType());
}

// 辅助函数：获取多维数组的维度信息
void get_array_dimensions(midend::Type* type, std::vector<int>& dims) {
    if (!type->isArrayType()) return;
    midend::ArrayType* array_type = static_cast<midend::ArrayType*>(type);
    dims.push_back(array_type->getNumElements());
    get_array_dimensions(array_type->getElementType(), dims);
}

// 辅助函数：将扁平索引转换为多维索引
std::vector<int> flat_to_multi_index(int flat_index,
                                     const std::vector<int>& dims) {
    std::vector<int> indices(dims.size());
    for (int i = dims.size() - 1; i >= 0; i--) {
        indices[i] = flat_index % dims[i];
        flat_index /= dims[i];
    }
    return indices;
}

// 辅助函数：处理数组初始化列表（支持扁平和嵌套初始化）
void process_global_array_init_recursive(
    ASTNodePtr init_list, midend::Context* ctx, midend::Type* target_type,
    DataType base_type, std::vector<midend::Constant*>& flat_elements,
    int& current_pos, const std::vector<int>& target_dims, int current_dim) {
    if (!init_list || !target_type) return;

    for (int i = 0; i < init_list->child_count; ++i) {
        ASTNodePtr child = init_list->children[i];

        if (child->node_type == NODE_LIST) {
            // 嵌套初始化
            if (current_dim < target_dims.size() - 1) {
                // 计算子数组的大小
                int subarray_size = 1;
                for (int j = current_dim + 1; j < target_dims.size(); j++) {
                    subarray_size *= target_dims[j];
                }

                // 对齐到子数组边界
                if (current_pos % subarray_size != 0) {
                    current_pos =
                        ((current_pos / subarray_size) + 1) * subarray_size;
                }

                int start_pos = current_pos;
                process_global_array_init_recursive(
                    child, ctx, target_type, base_type, flat_elements,
                    current_pos, target_dims, current_dim + 1);

                // 嵌套初始化完成后，移动到下一个子数组的开始位置
                current_pos = start_pos + subarray_size;
            }
        } else if (child->node_type == NODE_CONST) {
            // 扁平初始化元素
            if (current_pos < flat_elements.size()) {
                midend::Type* base_elem_type = target_type;
                while (base_elem_type->isArrayType()) {
                    base_elem_type =
                        static_cast<midend::ArrayType*>(base_elem_type)
                            ->getElementType();
                }

                midend::Constant* elem = nullptr;
                if (base_elem_type->isIntegerType() &&
                    child->data_type == INT_DATA && base_type == DATA_INT) {
                    elem = midend::ConstantInt::get(
                        static_cast<midend::IntegerType*>(base_elem_type),
                        child->data.direct_int);
                } else if (base_elem_type->isFloatType() &&
                           child->data_type == FLOAT_DATA &&
                           base_type == DATA_FLOAT) {
                    elem = midend::ConstantFP::get(
                        static_cast<midend::FloatType*>(base_elem_type),
                        child->data.direct_float);
                }

                if (elem) {
                    flat_elements[current_pos++] = elem;
                }
            }
        }
    }
}

// 辅助函数：处理数组初始化列表
midend::Constant* process_array_init_list(ASTNodePtr init_list,
                                          midend::Context* ctx,
                                          midend::Type* target_type,
                                          DataType base_type) {
    if (!init_list || !target_type || !target_type->isArrayType())
        return nullptr;

    // 如果初始化列表为空，返回nullptr
    if (init_list->child_count == 0) {
        return nullptr;
    }

    // 获取基本元素类型
    midend::Type* base_elem_type = target_type;
    while (base_elem_type->isArrayType()) {
        base_elem_type =
            static_cast<midend::ArrayType*>(base_elem_type)->getElementType();
    }

    std::function<midend::Constant*(ASTNodePtr, midend::Type*)>
        process_init_sparse =
            [&](ASTNodePtr list, midend::Type* curr_type) -> midend::Constant* {
        if (!list || !curr_type) return nullptr;

        if (!curr_type->isArrayType()) {
            if (list->node_type == NODE_CONST) {
                if (base_elem_type->isIntegerType() &&
                    list->data_type == INT_DATA && base_type == DATA_INT) {
                    return midend::ConstantInt::get(
                        static_cast<midend::IntegerType*>(base_elem_type),
                        list->data.direct_int);
                } else if (base_elem_type->isFloatType() &&
                           list->data_type == FLOAT_DATA &&
                           base_type == DATA_FLOAT) {
                    return midend::ConstantFP::get(
                        static_cast<midend::FloatType*>(base_elem_type),
                        list->data.direct_float);
                }
            }
            return nullptr;
        }

        midend::ArrayType* array_type =
            static_cast<midend::ArrayType*>(curr_type);
        midend::Type* elem_type = array_type->getElementType();
        std::vector<midend::Constant*> elements;

        for (int i = 0; i < list->child_count; ++i) {
            ASTNodePtr child = list->children[i];
            midend::Constant* elem = process_init_sparse(child, elem_type);
            if (elem) {
                elements.push_back(elem);
            }
        }

        // 如果有元素，创建常量数组
        if (!elements.empty()) {
            return midend::ConstantArray::get(array_type, elements);
        }

        return nullptr;
    };

    return process_init_sparse(init_list, target_type);
}

midend::Value* get_array_element_ptr(
    SymbolPtr symbol, const std::vector<midend::Value*>& indices,
    midend::IRBuilder& builder,
    const std::unordered_map<int, midend::Value*>& local_vars);

midend::Value* translate_node(
    ASTNodePtr node, midend::IRBuilder& builder, midend::Function* current_func,
    std::unordered_map<int, midend::Value*>& local_vars);

// 辅助函数：处理局部数组初始化的递归函数
void process_local_array_init_recursive(
    ASTNodePtr init_list, SymbolPtr symbol, midend::IRBuilder& builder,
    std::unordered_map<int, midend::Value*>& local_vars,
    std::vector<midend::Value*>& init_values, int& current_pos,
    const std::vector<int>& target_dims, int current_dim) {
    if (!init_list) return;

    for (int i = 0; i < init_list->child_count; ++i) {
        ASTNodePtr child = init_list->children[i];

        if (child->node_type == NODE_LIST) {
            // 嵌套初始化
            if (current_dim < target_dims.size() - 1) {
                // 计算子数组的大小
                int subarray_size = 1;
                for (int j = current_dim + 1; j < target_dims.size(); j++) {
                    subarray_size *= target_dims[j];
                }

                // 对齐到子数组边界
                if (current_pos % subarray_size != 0) {
                    current_pos =
                        ((current_pos / subarray_size) + 1) * subarray_size;
                }

                int start_pos = current_pos;
                process_local_array_init_recursive(
                    child, symbol, builder, local_vars, init_values,
                    current_pos, target_dims, current_dim + 1);

                // 嵌套初始化完成后，移动到下一个子数组的开始位置
                current_pos = start_pos + subarray_size;
            }
        } else {
            // 扁平初始化元素
            if (current_pos < init_values.size()) {
                midend::Value* init_val = nullptr;

                if (child->node_type == NODE_CONST) {
                    if (child->data_type == INT_DATA &&
                        symbol->data_type == DATA_INT) {
                        init_val = builder.getInt32(child->data.direct_int);
                    } else if (child->data_type == FLOAT_DATA &&
                               symbol->data_type == DATA_FLOAT) {
                        init_val = builder.getFloat(child->data.direct_float);
                    }
                } else if (child->node_type == NODE_ARRAY_ACCESS) {
                    // 处理数组访问表达式
                    init_val =
                        translate_node(child, builder, nullptr, local_vars);
                } else {
                    // 处理其他表达式
                    init_val =
                        translate_node(child, builder, nullptr, local_vars);
                }

                if (init_val) {
                    init_values[current_pos++] = init_val;
                }
            }
        }
    }
}

// 辅助函数：初始化数组元素
void initialize_array_elements(
    ASTNodePtr init_list, SymbolPtr symbol, midend::Type* array_type,
    midend::IRBuilder& builder,
    std::unordered_map<int, midend::Value*>& local_vars) {
    // 获取数组维度信息
    std::vector<int> dims;
    get_array_dimensions(array_type, dims);

    // 计算总元素数
    int total_size = calculate_array_size(array_type);

    // 创建默认值
    midend::Value* default_value = nullptr;
    if (symbol->data_type == DATA_INT) {
        default_value = builder.getInt32(0);
    } else if (symbol->data_type == DATA_FLOAT) {
        default_value = builder.getFloat(0.0f);
    }

    // 初始化所有元素为默认值
    std::vector<midend::Value*> init_values(total_size, default_value);

    // 处理初始化列表
    if (init_list) {
        int current_pos = 0;
        process_local_array_init_recursive(init_list, symbol, builder,
                                           local_vars, init_values, current_pos,
                                           dims, 0);
    }

    // 生成store指令，为每个元素赋值
    for (int i = 0; i < total_size; i++) {
        if (init_values[i]) {
            // 将扁平索引转换为多维索引
            std::vector<int> multi_indices = flat_to_multi_index(i, dims);

            // 创建GEP索引
            std::vector<midend::Value*> gep_indices;
            for (int idx : multi_indices) {
                gep_indices.push_back(builder.getInt32(idx));
            }

            // 获取元素指针并存储值
            midend::Value* elem_ptr =
                get_array_element_ptr(symbol, gep_indices, builder, local_vars);
            builder.createStore(init_values[i], elem_ptr);
        }
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
        return builder.createAdd(left, right,
                                 "add_" + std::to_string(temp_idx++));
    } else if (op_name == "-") {
        return builder.createSub(left, right,
                                 "sub_" + std::to_string(temp_idx++));
    } else if (op_name == "*") {
        return builder.createMul(left, right,
                                 "mul_" + std::to_string(temp_idx++));
    } else if (op_name == "/") {
        return builder.createDiv(left, right,
                                 "div_" + std::to_string(temp_idx++));
    } else if (op_name == "%") {
        return builder.createRem(left, right,
                                 "rem_" + std::to_string(temp_idx++));
    } else if (op_name == "<") {
        return builder.createICmpSLT(left, right,
                                     "lt_" + std::to_string(temp_idx++));
    } else if (op_name == "<=") {
        return builder.createICmpSLE(left, right,
                                     "le_" + std::to_string(temp_idx++));
    } else if (op_name == ">") {
        return builder.createICmpSGT(left, right,
                                     "gt_" + std::to_string(temp_idx++));
    } else if (op_name == ">=") {
        return builder.createICmpSGE(left, right,
                                     "ge_" + std::to_string(temp_idx++));
    } else if (op_name == "==") {
        return builder.createICmpEQ(left, right,
                                    "eq_" + std::to_string(temp_idx++));
    } else if (op_name == "!=") {
        return builder.createICmpNE(left, right,
                                    "ne_" + std::to_string(temp_idx++));
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

        case NODE_VAR:
        case NODE_ARRAY: {
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
                midend::Value* param_val = translate_node(
                    node->children[i], builder, current_func, local_vars);
                if (!param_val) return nullptr;
                params.push_back(param_val);
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

            std::string op_name = node->name ? node->name : "";

            // 处理短路求值的逻辑运算符
            if (op_name == "&&" || op_name == "||") {
                // 先计算左操作数
                midend::Value* left = translate_node(node->children[0], builder,
                                                     current_func, local_vars);
                if (!left) return nullptr;

                // 如果左操作数不是 i1 类型，需要转换
                midend::Value* left_cond = left;
                if (left->getType()->getBitWidth() != 1) {
                    left_cond = builder.createICmpNE(
                        left, builder.getInt32(0),
                        "tobool." + std::to_string(temp_idx++));
                }

                // 创建用于短路求值的基本块
                midend::BasicBlock* rhsBB = builder.createBasicBlock(
                    (op_name == "&&" ? "and.rhs." : "or.rhs.") +
                        std::to_string(temp_idx++),
                    current_func);
                midend::BasicBlock* mergeBB = builder.createBasicBlock(
                    (op_name == "&&" ? "and.merge." : "or.merge.") +
                        std::to_string(temp_idx++),
                    current_func);

                // 保存当前基本块
                midend::BasicBlock* currentBB = builder.getInsertBlock();

                if (op_name == "&&") {
                    // 对于 &&：如果左边为假，跳到 merge；否则计算右边
                    builder.createCondBr(left_cond, rhsBB, mergeBB);
                } else {  // op_name == "||"
                    // 对于 ||：如果左边为真，跳到 merge；否则计算右边
                    builder.createCondBr(left_cond, mergeBB, rhsBB);
                }

                // 在右操作数基本块中计算右操作数
                builder.setInsertPoint(rhsBB);
                midend::Value* right = translate_node(
                    node->children[1], builder, current_func, local_vars);
                if (!right) return nullptr;

                // 如果右操作数不是 i1 类型，需要转换
                midend::Value* right_cond = right;
                if (right->getType()->getBitWidth() != 1) {
                    right_cond = builder.createICmpNE(
                        right, builder.getInt32(0),
                        "tobool." + std::to_string(temp_idx++));
                }

                builder.createBr(mergeBB);
                rhsBB = builder.getInsertBlock();  // 可能已经改变

                // 在合并基本块中创建 PHI 节点
                builder.setInsertPoint(mergeBB);
                midend::PHINode* phi = builder.createPHI(
                    builder.getInt1Type(),
                    (op_name == "&&" ? "and.result." : "or.result.") +
                        std::to_string(temp_idx++));

                if (op_name == "&&") {
                    // 对于 &&：左边为假时结果为假，否则结果为右边的值
                    phi->addIncoming(builder.getFalse(), currentBB);
                    phi->addIncoming(right_cond, rhsBB);
                } else {  // op_name == "||"
                    // 对于 ||：左边为真时结果为真，否则结果为右边的值
                    phi->addIncoming(builder.getTrue(), currentBB);
                    phi->addIncoming(right_cond, rhsBB);
                }

                return phi;
            } else {
                // 对于其他二元运算符，正常计算两个操作数
                midend::Value* left = translate_node(node->children[0], builder,
                                                     current_func, local_vars);
                midend::Value* right = translate_node(
                    node->children[1], builder, current_func, local_vars);

                if (!left || !right) return nullptr;
                return create_binary_op(builder, left, right, op_name);
            }
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
                                         "neg_" + std::to_string(temp_idx++));
            } else if (op_name == "!") {
                // 如果操作数是 i32，直接用 icmp eq 0 实现逻辑非
                if (operand->getType()->getBitWidth() != 1) {
                    return builder.createICmpEQ(
                        operand, builder.getInt32(0),
                        "not." + std::to_string(temp_idx++));
                } else {
                    // 如果已经是 i1 类型，使用 icmp eq 与 false 比较
                    return builder.createICmpEQ(
                        operand, builder.getFalse(),
                        "not." + std::to_string(temp_idx++));
                }
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
                initialize_array_elements(init_list, symbol, array_type,
                                          builder, local_vars);
            }

            return array_alloca;
        }

        case NODE_IF_STMT: {
            // if语句处理
            if (node->child_count < 2) return nullptr;

            // 计算条件表达式
            midend::Value* cond = translate_node(node->children[0], builder,
                                                 current_func, local_vars);
            if (!cond) return nullptr;

            // 创建then和merge基本块，使用唯一的标签
            midend::BasicBlock* thenBB = builder.createBasicBlock(
                "if.then." + std::to_string(temp_idx++), current_func);
            midend::BasicBlock* mergeBB = builder.createBasicBlock(
                "if.merge." + std::to_string(temp_idx++), current_func);

            // 根据条件跳转
            builder.createCondBr(cond, thenBB, mergeBB);

            // 生成then分支的代码
            builder.setInsertPoint(thenBB);
            translate_node(node->children[1], builder, current_func,
                           local_vars);

            // 如果then块没有终结指令，添加到merge块的跳转
            if (!builder.getInsertBlock()->getTerminator()) {
                builder.createBr(mergeBB);
            }

            // 继续在merge块生成代码
            builder.setInsertPoint(mergeBB);

            return nullptr;
        }

        case NODE_IF_ELSE_STMT: {
            // if-else语句处理
            if (node->child_count < 2) return nullptr;

            // 计算条件表达式
            midend::Value* cond = translate_node(node->children[0], builder,
                                                 current_func, local_vars);
            if (!cond) return nullptr;

            // 创建then和else基本块，使用唯一的标签
            midend::BasicBlock* thenBB = builder.createBasicBlock(
                "if.then." + std::to_string(temp_idx++), current_func);
            midend::BasicBlock* elseBB = builder.createBasicBlock(
                "if.else." + std::to_string(temp_idx++), current_func);
            midend::BasicBlock* mergeBB = builder.createBasicBlock(
                "if.merge." + std::to_string(temp_idx++), current_func);

            // 根据条件跳转
            builder.createCondBr(cond, thenBB, elseBB);

            // 生成then分支的代码
            builder.setInsertPoint(thenBB);
            translate_node(node->children[1], builder, current_func,
                           local_vars);
            builder.createBr(mergeBB);

            // 生成else分支的代码
            builder.setInsertPoint(elseBB);
            translate_node(node->children[2], builder, current_func,
                           local_vars);

            // 如果else块没有终结指令，添加到merge块的跳转
            if (!builder.getInsertBlock()->getTerminator()) {
                builder.createBr(mergeBB);
            }

            // 继续在merge块生成代码
            builder.setInsertPoint(mergeBB);

            return nullptr;
        }

        case NODE_WHILE_STMT: {
            // while语句处理
            if (node->child_count < 2) return nullptr;

            // 条件判断块
            midend::BasicBlock* condBB = builder.createBasicBlock(
                "while.cond." + std::to_string(temp_idx++), current_func);

            // 跳转进入当前基本块
            builder.createBr(condBB);

            // 计算条件表达式
            builder.setInsertPoint(condBB);
            midend::Value* cond = translate_node(node->children[0], builder,
                                                 current_func, local_vars);
            if (!cond) return nullptr;
            midend::BasicBlock* block_after_cond = builder.getInsertBlock();

            // loop基本块
            midend::BasicBlock* loopBB = builder.createBasicBlock(
                "while.loop." + std::to_string(temp_idx++), current_func);
            builder.setInsertPoint(loopBB);
            translate_node(node->children[1], builder, current_func,
                           local_vars);
            builder.createBr(condBB);

            // merge基本块
            midend::BasicBlock* mergeBB = builder.createBasicBlock(
                "while.merge." + std::to_string(temp_idx++), current_func);

            // 跳转指令
            builder.setInsertPoint(block_after_cond);
            builder.createCondBr(cond, loopBB, mergeBB);

            // 继续在merge块生成代码
            builder.setInsertPoint(mergeBB);

            return nullptr;
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
        midend::Type* param_type = nullptr;

        if (param_sym->symbol_type == SYMB_ARRAY) {
            midend::Type* elem_type = get_ir_type(ctx, param_sym->data_type);
            // 对于函数参数，我们需要从符号表中获取维度信息
            // 并从第二维开始构建数组类型（第一维作为指针）
            if (param_sym->attributes.array_info.dimensions > 1) {
                // 从最内层维度开始构建（逆序）
                for (int j = param_sym->attributes.array_info.dimensions - 1;
                     j >= 1; j--) {
                    int dim_size = param_sym->attributes.array_info.shape[j];
                    if (dim_size > 0) {  // 0表示未知大小
                        elem_type = midend::ArrayType::get(elem_type, dim_size);
                    }
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
                    init = process_array_init_list(child->children[1], ctx,
                                                   array_type, sym->data_type);
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
    add_runtime_lib_symbols();

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
