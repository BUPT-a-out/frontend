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
#include "sy_parser/AST.h"
#include "sy_parser/symbol_table.h"
#include "sy_parser/y.tab.h"

extern int yyparse(void);
extern FILE* yyin;
extern ASTNodePtr root;
}

#include "runtime_lib_def.h"

// 全局标识符（函数、全局变量）
std::unordered_map<int, midend::Function*> func_tab;
std::unordered_map<int, midend::GlobalVariable*> global_var_tab;
std::unordered_set<int> function_param_symbols;

// 控制流break、continue填充
// 基本块及其所在基本块编号
typedef struct {
    midend::BasicBlock* block;
    int block_idx;
} BlockDepthPair;
// 控制流break填充
std::vector<BlockDepthPair> break_pos;
// 控制流continue填充
std::vector<BlockDepthPair> continue_pos;

// IR变量编号
int var_idx;

// IR基本块编号
int block_idx;

midend::Value* get_array_element_ptr(
    SymbolPtr symbol, const std::vector<midend::Value*>& indices,
    midend::IRBuilder& builder,
    const std::unordered_map<int, midend::Value*>& local_vars);

midend::Value* translate_node(
    ASTNodePtr node, midend::IRBuilder& builder, midend::Function* current_func,
    std::unordered_map<int, midend::Value*>& local_vars);

// 辅助函数：判断基本块是否在break或continue填充向量中
bool in_break_continue_pos(midend::BasicBlock* block) {
    for (auto pair : break_pos)
        if (pair.block == block) return true;
    for (auto pair : continue_pos)
        if (pair.block == block) return true;
    return false;
}

// 获取变量在IR中的名称
std::string get_symbol_name(SymbolPtr symbol) {
    return std::string(symbol->name) + "." + std::to_string(symbol->id);
}

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

midend::Value* create_type_tran(midend::IRBuilder& builder,
                                midend::Value* input, DataType des_type) {
    if (!input) return nullptr;
    midend::Context* ctx = builder.getContext();
    midend::Type* input_type = input->getType();
    if (input_type->isFloatType()) {
        switch (des_type) {
            case DATA_INT:
                return builder.createCast(midend::CastInst::CastOps::FPToSI,
                                          input, get_ir_type(ctx, DATA_INT),
                                          std::to_string(var_idx++));
            case DATA_FLOAT:
                return input;
            default:
                return input;
        }
    } else if (input_type->getBitWidth() != 1) {
        switch (des_type) {
            case DATA_FLOAT:
                return builder.createCast(midend::CastInst::CastOps::SIToFP,
                                          input, get_ir_type(ctx, DATA_FLOAT),
                                          std::to_string(var_idx++));
            case DATA_BOOL:
                return builder.createCast(midend::CastInst::CastOps::Trunc,
                                          input, get_ir_type(ctx, DATA_BOOL),
                                          std::to_string(var_idx++));
            case DATA_INT:
                return input;
            default:
                return input;
        }
    } else {
        switch (des_type) {
            case DATA_FLOAT: {
                midend::Value* mid_value = builder.createCast(
                    midend::CastInst::CastOps::SExt, input,
                    get_ir_type(ctx, DATA_INT), std::to_string(var_idx++));
                return builder.createCast(
                    midend::CastInst::CastOps::SIToFP, mid_value,
                    get_ir_type(ctx, DATA_FLOAT), std::to_string(var_idx++));
            }
            case DATA_INT: {
                return builder.createCast(midend::CastInst::CastOps::SIToFP,
                                          input, get_ir_type(ctx, DATA_INT),
                                          std::to_string(var_idx++));
            }
            case DATA_BOOL:
                return input;
            default:
                return input;
        }
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

// 辅助函数：处理数组初始化列表
midend::Constant* process_array_init_list(ASTNodePtr init_list,
                                          midend::Type* target_type,
                                          DataType base_type) {
    if (!init_list || !target_type || !target_type->isArrayType())
        return nullptr;

    // 如果初始化列表为空，创建一个空的常量数组
    if (init_list->child_count == 0) {
        if (target_type->isArrayType()) {
            midend::ArrayType* array_type =
                static_cast<midend::ArrayType*>(target_type);
            std::vector<midend::Constant*> empty_elements;
            return midend::ConstantArray::get(array_type, empty_elements);
        }
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
                    list->data_type == NODEDATA_INT && base_type == DATA_INT) {
                    return midend::ConstantInt::get(
                        static_cast<midend::IntegerType*>(base_elem_type),
                        list->data.direct_int);
                } else if (base_elem_type->isFloatType() &&
                           list->data_type == NODEDATA_FLOAT &&
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

        // 创建常量数组（即使为空）
        return midend::ConstantArray::get(array_type, elements);
    };

    return process_init_sparse(init_list, target_type);
}

// 辅助函数：处理局部数组初始化的递归函数
void process_local_array_init_recursive(
    ASTNodePtr init_list, SymbolPtr symbol, midend::IRBuilder& builder,
    std::unordered_map<int, midend::Value*>& local_vars,
    std::map<int, midend::Value*>& init_values, int& current_pos,
    int current_dim) {
    if (!init_list) return;

    // 数组信息
    ArrayInfo array_info = symbol->attributes.array_info;

    for (int i = 0; i < init_list->child_count; ++i) {
        ASTNodePtr child = init_list->children[i];

        if (child->node_type == NODE_LIST) {
            // 嵌套初始化
            if (current_dim < array_info.dimensions - 1) {
                // 计算子数组的大小
                int subarray_size = 1;
                for (int j = current_dim + 1; j < array_info.dimensions; j++) {
                    subarray_size *= array_info.shape[j];
                }

                // 对齐到子数组边界
                if (current_pos % subarray_size != 0) {
                    current_pos =
                        ((current_pos / subarray_size) + 1) * subarray_size;
                }

                int start_pos = current_pos;
                process_local_array_init_recursive(
                    child, symbol, builder, local_vars, init_values,
                    current_pos, current_dim + 1);

                // 嵌套初始化完成后，移动到下一个子数组的开始位置
                current_pos = start_pos + subarray_size;
            }
        } else {
            // 扁平初始化元素
            if (current_pos < array_info.elem_num) {
                bool consider = true;

                if (child->node_type == NODE_CONST) {
                    // 只考虑不为0的常数
                    if (child->data_type == NODEDATA_INT &&
                        symbol->data_type == DATA_INT) {
                        consider = child->data.direct_int != 0;
                    } else if (child->data_type == NODEDATA_FLOAT &&
                               symbol->data_type == DATA_FLOAT) {
                        consider = child->data.direct_float != 0.0;
                    }
                }

                if (consider) {
                    midend::Value* init_val =
                        translate_node(child, builder, nullptr, local_vars);
                    init_val =
                        create_type_tran(builder, init_val, symbol->data_type);
                    if (init_val) init_values[current_pos] = init_val;
                }
                current_pos++;
            }
        }
    }
}

// 辅助函数：初始化数组元素
void initialize_array_elements(
    ASTNodePtr init_list, SymbolPtr symbol, midend::Value* array_alloca,
    midend::IRBuilder& builder, midend::Function* current_func,
    std::unordered_map<int, midend::Value*>& local_vars) {
    if (array_alloca == nullptr) return;

    // 将多维数组解释为一维数组
    int dim_len = symbol->attributes.array_info.elem_num;
    int one_dim_array_shape[1] = {dim_len};
    midend::Type* one_dim_array_type = get_array_type(
        builder.getContext(), symbol->data_type, 1, one_dim_array_shape);

    // 循环上界
    midend::Value* top_bound = builder.getInt32(dim_len);
    // 循环变量
    midend::Type* var_type = builder.getContext()->getInt32Type();
    std::string var_name = get_symbol_name(symbol) + ".initer";
    midend::Value* i_alloca = builder.createAlloca(var_type, nullptr, var_name);
    // 初始化
    std::string current_block_id = std::to_string(block_idx++);
    builder.createStore(builder.getInt32(0), i_alloca);

    // cond基本块
    midend::BasicBlock* condBB =
        builder.createBasicBlock(var_name + ".while.cond", current_func);
    builder.createBr(condBB);

    // loop基本块
    midend::BasicBlock* loopBB =
        builder.createBasicBlock(var_name + ".while.loop", current_func);
    builder.setInsertPoint(loopBB);
    midend::Value* single_idx =
        builder.createLoad(i_alloca, std::to_string(var_idx++));
    std::vector<midend::Value*> indices;
    indices.push_back(single_idx);
    midend::Value* elem_ptr = builder.createGEP(
        one_dim_array_type, array_alloca, indices, std::to_string(var_idx++));
    // 默认填充0
    midend::Value* fill_data;
    if (symbol->data_type == DATA_INT)
        fill_data = builder.getInt32(0);
    else if (symbol->data_type == DATA_FLOAT)
        fill_data = builder.getFloat(0.0);
    else
        fill_data = builder.getInt32(0);
    builder.createStore(fill_data, elem_ptr);
    midend::Value* i_old_value =
        builder.createLoad(i_alloca, std::to_string(var_idx++));
    midend::Value* i_new_value = builder.createAdd(
        i_old_value, builder.getInt32(1), std::to_string(var_idx++));
    builder.createStore(i_new_value, i_alloca);
    builder.createBr(condBB);

    // merge基本块
    midend::BasicBlock* mergeBB =
        builder.createBasicBlock(var_name + ".while.merge", current_func);

    // 循环转移
    builder.setInsertPoint(condBB);
    midend::Value* i_value =
        builder.createLoad(i_alloca, std::to_string(var_idx++));
    midend::Value* cond = builder.createICmpSLT(
        i_value, top_bound, "lt." + std::to_string(var_idx++));
    builder.createCondBr(cond, loopBB, mergeBB);

    // 继续在merge块中插入代码
    builder.setInsertPoint(mergeBB);

    // 处理初始化列表
    std::map<int, midend::Value*> init_values;
    if (init_list) {
        int current_pos = 0;
        process_local_array_init_recursive(init_list, symbol, builder,
                                           local_vars, init_values, current_pos,
                                           0);
    }

    // 生成store指令，为每个元素赋值
    for (auto p : init_values) {
        int flat_idx = p.first;
        midend::Value* init_value = p.second;

        midend::Value* single_idx = builder.getInt32(flat_idx);
        std::vector<midend::Value*> indices;
        indices.push_back(single_idx);
        midend::Value* elem_ptr =
            builder.createGEP(one_dim_array_type, array_alloca, indices,
                              std::to_string(var_idx++));
        builder.createStore(init_value, elem_ptr);
    }
}

midend::Value* create_binary_op(midend::IRBuilder& builder, midend::Value* left,
                                midend::Value* right, std::string op_name) {
    if (op_name == "+") {
        return builder.createAdd(left, right,
                                 "add." + std::to_string(var_idx++));
    } else if (op_name == "-") {
        return builder.createSub(left, right,
                                 "sub." + std::to_string(var_idx++));
    } else if (op_name == "*") {
        return builder.createMul(left, right,
                                 "mul." + std::to_string(var_idx++));
    } else if (op_name == "/") {
        return builder.createDiv(left, right,
                                 "div." + std::to_string(var_idx++));
    } else if (op_name == "%") {
        return builder.createRem(left, right,
                                 "rem." + std::to_string(var_idx++));
    } else if (op_name == "<") {
        return builder.createICmpSLT(left, right,
                                     "lt." + std::to_string(var_idx++));
    } else if (op_name == "<=") {
        return builder.createICmpSLE(left, right,
                                     "le." + std::to_string(var_idx++));
    } else if (op_name == ">") {
        return builder.createICmpSGT(left, right,
                                     "gt." + std::to_string(var_idx++));
    } else if (op_name == ">=") {
        return builder.createICmpSGE(left, right,
                                     "ge." + std::to_string(var_idx++));
    } else if (op_name == "==") {
        return builder.createICmpEQ(left, right,
                                    "eq." + std::to_string(var_idx++));
    } else if (op_name == "!=") {
        return builder.createICmpNE(left, right,
                                    "ne." + std::to_string(var_idx++));
    } else
        return nullptr;
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
               ? builder.createGEP(array_ptr->getType(), array_ptr, indices,
                                   std::to_string(var_idx++))
               : builder.createGEP(array_type, array_ptr, indices,
                                   std::to_string(var_idx++));
}

midend::Value* def_var(midend::IRBuilder& builder, SymbolPtr symbol,
                       std::unordered_map<int, midend::Value*>& local_vars) {
    SymbolType symb_type = symbol->symbol_type;
    std::string name = get_symbol_name(symbol);
    midend::Type* type = nullptr;
    midend::Value* alloca = nullptr;

    if (symb_type == SYMB_VAR || symb_type == SYMB_CONST_VAR) {
        // 获取变量类型
        type = get_ir_type(builder.getContext(), symbol->data_type);
        // 创建局部变量
        if (type) alloca = builder.createAlloca(type, nullptr, name);
    } else if (symb_type == SYMB_ARRAY || symb_type == SYMB_CONST_ARRAY) {
        // 创建数组类型
        type = get_array_type(builder.getContext(), symbol->data_type,
                              symbol->attributes.array_info.dimensions,
                              symbol->attributes.array_info.shape);
        // 创建局部数组
        if (type) alloca = builder.createAlloca(type, nullptr, name);
    }

    if (alloca) local_vars[symbol->id] = alloca;
    return alloca;
}

// 递归处理AST节点的函数（处理函数内部的语句）
midend::Value* translate_node(
    ASTNodePtr node, midend::IRBuilder& builder, midend::Function* current_func,
    std::unordered_map<int, midend::Value*>& local_vars) {
    if (!node) return nullptr;
    // midend::Context* ctx = builder.getContext();

    switch (node->node_type) {
        case NODE_LIST: {
            // 列表节点，处理所有子节点
            midend::Value* last_value = nullptr;
            for (int i = 0; i < node->child_count; ++i) {
                last_value = translate_node(node->children[i], builder,
                                            current_func, local_vars);
                if (node->children[i]->node_type == NODE_BREAK_STMT ||
                    node->children[i]->node_type == NODE_CONTINUE_STMT ||
                    node->children[i]->node_type == NODE_RETURN_STMT)
                    break;
            }
            return last_value;
        }

        case NODE_CONST: {
            if (node->data_type == NODEDATA_INT) {
                return builder.getInt32(node->data.direct_int);
            } else if (node->data_type == NODEDATA_FLOAT) {
                return builder.getFloat(node->data.direct_float);
            } else {
                return nullptr;
            }
        }

        case NODE_VAR:
        case NODE_CONST_VAR: {
            SymbolPtr symbol = node->data.symb_ptr;
            if (node->data_type != NODEDATA_SYMB && !symbol) return nullptr;

            // 从局部变量映射中查找
            auto it = local_vars.find(symbol->id);
            if (it != local_vars.end())
                return builder.createLoad(it->second,
                                          std::to_string(var_idx++));
            // 从全局变量映射中查找
            auto global_it = global_var_tab.find(symbol->id);
            if (global_it != global_var_tab.end())
                return builder.createLoad(global_it->second,
                                          std::to_string(var_idx++));

            return nullptr;
        }

        case NODE_ARRAY:
        case NODE_CONST_ARRAY: {
            // 变量节点
            SymbolPtr symbol = node->data.symb_ptr;
            if (node->data_type != NODEDATA_SYMB && !symbol) return nullptr;

            // 从局部变量映射中查找
            auto it = local_vars.find(symbol->id);
            if (it != local_vars.end()) return it->second;
            // 从全局变量映射中查找
            auto global_it = global_var_tab.find(symbol->id);
            if (global_it != global_var_tab.end()) return global_it->second;

            return nullptr;
        }

        case NODE_ARRAY_ACCESS:
        case NODE_CONST_ARRAY_ACCESS: {
            SymbolPtr symbol = node->data.symb_ptr;
            if (node->data_type != NODEDATA_SYMB && !symbol) return nullptr;

            std::vector<midend::Value*> indices;
            for (int i = 0; i < node->child_count; ++i) {
                midend::Value* index = translate_node(
                    node->children[i], builder, current_func, local_vars);
                index = create_type_tran(builder, index, DATA_INT);
                if (!index) return nullptr;
                indices.push_back(index);
            }
            midend::Value* gep =
                get_array_element_ptr(symbol, indices, builder, local_vars);
            if (!gep) return nullptr;

            if (node->child_count < symbol->attributes.array_info.dimensions)
                return gep;
            else
                return builder.createLoad(gep, std::to_string(var_idx++));
        }

        case NODE_FUNC_CALL: {
            // 函数调用节点
            SymbolPtr func_sym = node->data.symb_ptr;
            if (node->data_type != NODEDATA_SYMB && !func_sym) return nullptr;

            std::vector<midend::Value*> params;
            for (int i = 0; i < node->child_count; ++i) {
                SymbolPtr param_symb = func_sym->attributes.func_info.params[i];
                midend::Value* param_val = translate_node(
                    node->children[i], builder, current_func, local_vars);
                if (param_symb->symbol_type == SYMB_VAR ||
                    param_symb->symbol_type == SYMB_CONST_VAR)
                    param_val = create_type_tran(builder, param_val,
                                                 param_symb->data_type);
                if (!param_val) return nullptr;
                params.push_back(param_val);
            }

            auto it = func_tab.find(func_sym->id);
            if (it != func_tab.end()) {
                midend::Function* func = it->second;
                return builder.createCall(func, params,
                                          std::to_string(var_idx++));
            }

            return nullptr;
        }

        case NODE_BINARY_OP: {
            // 二元操作节点
            if (node->child_count < 2) return nullptr;

            std::string op_name = node->name ? node->name : "";

            // 处理短路求值的逻辑运算符
            if (op_name == "&&" || op_name == "||") {
                std::string current_block_id = std::to_string(block_idx++);

                // 先计算左操作数
                midend::Value* left = translate_node(node->children[0], builder,
                                                     current_func, local_vars);
                midend::Value* left_cond =
                    create_type_tran(builder, left, DATA_BOOL);
                if (!left) return nullptr;

                // 创建用于短路求值的基本块
                midend::BasicBlock* rhsBB = builder.createBasicBlock(
                    (op_name == "&&" ? "and." : "or.") + current_block_id +
                        ".rhs",
                    current_func);
                midend::BasicBlock* mergeBB = builder.createBasicBlock(
                    (op_name == "&&" ? "and." : "or.") + current_block_id +
                        ".merge",
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
                midend::Value* right_cond =
                    create_type_tran(builder, right, DATA_INT);
                if (!right) return nullptr;

                builder.createBr(mergeBB);
                rhsBB = builder.getInsertBlock();  // 可能已经改变

                // 在合并基本块中创建 PHI 节点
                builder.setInsertPoint(mergeBB);
                midend::PHINode* phi = builder.createPHI(
                    builder.getInt1Type(), (op_name == "&&" ? "and." : "or.") +
                                               current_block_id + ".result");

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

                // 强制类型转换
                bool left_is_float = left->getType()->isFloatType();
                bool right_is_float = right->getType()->isFloatType();
                if (!left_is_float && right_is_float)
                    left = create_type_tran(builder, left, DATA_FLOAT);
                else if (left_is_float && !right_is_float)
                    right = create_type_tran(builder, right, DATA_FLOAT);

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

            if (op_name == "+")
                return operand;
            else if (op_name == "-") {
                if (operand->getType()->getBitWidth() != 1) {
                    return builder.createSub(
                        builder.getInt32(0), operand,
                        "neg." + std::to_string(var_idx++));
                } else {
                    // int1取反，真值不变
                    return operand;
                }
            } else if (op_name == "!") {
                if (operand->getType()->getBitWidth() != 1) {
                    // 如果操作数是 i32，直接用 icmp eq 0 实现逻辑非
                    midend::Value* result = builder.createICmpEQ(
                        operand, builder.getInt32(0),
                        "not." + std::to_string(var_idx++));
                    return create_type_tran(builder, result, DATA_INT);
                } else {
                    // 如果已经是 i1 类型，使用 icmp eq 与 false 比较
                    return builder.createICmpEQ(
                        operand, builder.getFalse(),
                        "not." + std::to_string(var_idx++));
                }
            }

            return nullptr;
        }

        case NODE_ASSIGN_STMT: {
            // 赋值语句
            if (node->child_count < 2) return nullptr;

            // 左值（变量或数组元素）
            ASTNodePtr left_node = node->children[0];
            DataType left_type = DATA_UNKNOWN;

            // 右值
            midend::Value* left_ptr = nullptr;
            if (left_node->node_type == NODE_VAR) {
                // 普通变量赋值
                SymbolPtr symbol = left_node->data.symb_ptr;
                if (!symbol) return nullptr;
                left_type = symbol->data_type;

                // 从局部变量映射中查找
                auto it = local_vars.find(symbol->id);
                if (it != local_vars.end()) left_ptr = it->second;
                // 从全局变量映射中查找
                auto global_it = global_var_tab.find(symbol->id);
                if (global_it != global_var_tab.end())
                    left_ptr = global_it->second;
            } else if (left_node->node_type == NODE_ARRAY_ACCESS) {
                SymbolPtr symbol = left_node->data.symb_ptr;
                if (!symbol) return nullptr;
                left_type = symbol->data_type;

                std::vector<midend::Value*> indices;
                for (int i = 0; i < left_node->child_count; ++i) {
                    midend::Value* index =
                        translate_node(left_node->children[i], builder,
                                       current_func, local_vars);
                    index = create_type_tran(builder, index, DATA_INT);
                    if (!index) return nullptr;
                    indices.push_back(index);
                }
                left_ptr =
                    get_array_element_ptr(symbol, indices, builder, local_vars);
            }

            // 右值（表达式）
            midend::Value* right_value = translate_node(
                node->children[1], builder, current_func, local_vars);
            right_value = create_type_tran(builder, right_value, left_type);
            if (!right_value) return nullptr;
            if (left_ptr) builder.createStore(right_value, left_ptr);
            return right_value;
        }

        case NODE_RETURN_STMT: {
            // 返回语句
            DataType return_type = DATA_UNKNOWN;
            if (node->data_type == NODEDATA_SYMB && node->data.symb_ptr)
                return_type = node->data.symb_ptr->data_type;
            if (node->child_count > 0) {
                midend::Value* return_value = translate_node(
                    node->children[0], builder, current_func, local_vars);
                if (!return_value) {
                    builder.createRetVoid();
                }
                return_value =
                    create_type_tran(builder, return_value, return_type);
                builder.createRet(return_value);
            } else {
                builder.createRetVoid();
            }
            return nullptr;
        }

        case NODE_VAR_DEF:
        case NODE_CONST_VAR_DEF: {
            SymbolPtr symbol = node->data.symb_ptr;
            if (node->data_type != NODEDATA_SYMB || !symbol) return nullptr;

            if (symbol->symbol_type != SYMB_VAR &&
                symbol->symbol_type != SYMB_CONST_VAR)
                return nullptr;

            // 查找局部变量
            midend::Value* alloca = nullptr;
            auto it = local_vars.find(symbol->id);
            if (it != local_vars.end())
                alloca = it->second;
            else
                return nullptr;

            // 如果有初始化值（第一个子节点是常量或表达式）
            if (node->child_count > 0) {
                midend::Value* init_value = translate_node(
                    node->children[0], builder, current_func, local_vars);
                create_type_tran(builder, init_value, symbol->data_type);
                if (init_value) {
                    builder.createStore(init_value, alloca);
                }
            }

            return alloca;
        }

        case NODE_ARRAY_DEF:
        case NODE_CONST_ARRAY_DEF: {
            // 局部数组定义
            SymbolPtr symbol = node->data.symb_ptr;
            if (node->data_type != NODEDATA_SYMB || !symbol) return nullptr;

            if (symbol->symbol_type != SYMB_ARRAY &&
                symbol->symbol_type != SYMB_CONST_ARRAY)
                return nullptr;

            // 创建数组类型
            midend::Value* alloca = nullptr;
            auto it = local_vars.find(symbol->id);
            if (it != local_vars.end())
                alloca = it->second;
            else
                return nullptr;

            if (node->child_count > 1) {
                ASTNodePtr init_list = node->children[1];
                initialize_array_elements(init_list, symbol, alloca, builder,
                                          current_func, local_vars);
            }

            return alloca;
        }

        case NODE_IF_STMT: {
            // if语句处理
            if (node->child_count < 2) return nullptr;
            std::string current_block_id = std::to_string(block_idx++);

            // 计算条件表达式
            midend::Value* cond = translate_node(node->children[0], builder,
                                                 current_func, local_vars);
            cond = create_type_tran(builder, cond, DATA_BOOL);
            if (!cond) return nullptr;
            midend::BasicBlock* block_after_cond = builder.getInsertBlock();

            // then基本块
            midend::BasicBlock* thenBB = builder.createBasicBlock(
                "if." + current_block_id + ".then", current_func);
            builder.setInsertPoint(thenBB);
            translate_node(node->children[1], builder, current_func,
                           local_vars);
            midend::BasicBlock* block_after_then = builder.getInsertBlock();

            // merge基本块
            midend::BasicBlock* mergeBB = builder.createBasicBlock(
                "if." + current_block_id + ".merge", current_func);

            // 如果then块没有终结指令，添加到merge块的跳转
            if (!block_after_then->getTerminator() &&
                !in_break_continue_pos(block_after_then))
                builder.createBr(mergeBB);

            // 根据条件跳转
            builder.setInsertPoint(block_after_cond);
            builder.createCondBr(cond, thenBB, mergeBB);

            // 继续在merge块生成代码
            builder.setInsertPoint(mergeBB);

            return nullptr;
        }

        case NODE_IF_ELSE_STMT: {
            // if-else语句处理
            if (node->child_count < 2) return nullptr;
            std::string current_block_id = std::to_string(block_idx++);

            // 计算条件表达式
            midend::Value* cond = translate_node(node->children[0], builder,
                                                 current_func, local_vars);
            cond = create_type_tran(builder, cond, DATA_BOOL);
            if (!cond) return nullptr;
            midend::BasicBlock* block_after_cond = builder.getInsertBlock();

            // then基本块
            midend::BasicBlock* thenBB = builder.createBasicBlock(
                "if." + current_block_id + ".then", current_func);
            builder.setInsertPoint(thenBB);
            translate_node(node->children[1], builder, current_func,
                           local_vars);
            midend::BasicBlock* block_after_then = builder.getInsertBlock();

            // else基本块
            midend::BasicBlock* elseBB = builder.createBasicBlock(
                "if." + current_block_id + ".else", current_func);
            builder.setInsertPoint(elseBB);
            translate_node(node->children[2], builder, current_func,
                           local_vars);
            midend::BasicBlock* block_after_else = builder.getInsertBlock();

            // 判断是否需要merge块
            bool then_need_merge = !block_after_then->getTerminator() &&
                                   !in_break_continue_pos(block_after_then);
            bool else_need_merge = !block_after_else->getTerminator() &&
                                   !in_break_continue_pos(block_after_else);

            // merge基本块
            midend::BasicBlock* mergeBB = nullptr;
            if (then_need_merge || else_need_merge)
                mergeBB = builder.createBasicBlock(
                    "if." + current_block_id + ".merge", current_func);

            // 如果then、else块没有终结指令，添加到merge块的跳转
            if (then_need_merge) {
                builder.setInsertPoint(block_after_then);
                builder.createBr(mergeBB);
            }
            if (else_need_merge) {
                builder.setInsertPoint(block_after_else);
                builder.createBr(mergeBB);
            }

            // 根据条件跳转
            builder.setInsertPoint(block_after_cond);
            builder.createCondBr(cond, thenBB, elseBB);

            // 继续在merge块生成代码
            if (then_need_merge || else_need_merge)
                builder.setInsertPoint(mergeBB);

            return nullptr;
        }

        case NODE_WHILE_STMT: {
            // while语句处理
            if (node->child_count < 2) return nullptr;
            int current_block_id_int = block_idx++;
            std::string current_block_id = std::to_string(current_block_id_int);

            // 条件判断块
            midend::BasicBlock* condBB = builder.createBasicBlock(
                "while." + current_block_id + ".cond", current_func);

            // 跳转进入当前基本块
            builder.createBr(condBB);

            // 计算条件表达式
            builder.setInsertPoint(condBB);
            midend::Value* cond = translate_node(node->children[0], builder,
                                                 current_func, local_vars);
            cond = create_type_tran(builder, cond, DATA_BOOL);
            if (!cond) return nullptr;
            midend::BasicBlock* block_after_cond = builder.getInsertBlock();

            // loop基本块
            midend::BasicBlock* loopBB = builder.createBasicBlock(
                "while." + current_block_id + ".loop", current_func);
            builder.setInsertPoint(loopBB);
            translate_node(node->children[1], builder, current_func,
                           local_vars);
            if (!in_break_continue_pos(builder.getInsertBlock()))
                builder.createBr(condBB);

            // merge基本块
            midend::BasicBlock* mergeBB = builder.createBasicBlock(
                "while." + current_block_id + ".merge", current_func);

            // block_idx单调递增，新进入的break_block对应的一定较大
            // break指令
            while (!break_pos.empty()) {
                BlockDepthPair pair = break_pos.back();
                if (pair.block_idx < current_block_id_int) break;
                builder.setInsertPoint(pair.block);
                builder.createBr(mergeBB);
                break_pos.pop_back();
            }
            // continue指令
            while (!continue_pos.empty()) {
                BlockDepthPair pair = continue_pos.back();
                if (pair.block_idx < current_block_id_int) break;
                builder.setInsertPoint(pair.block);
                builder.createBr(condBB);
                continue_pos.pop_back();
            }

            // 跳转指令
            builder.setInsertPoint(block_after_cond);
            builder.createCondBr(cond, loopBB, mergeBB);

            // 继续在merge块生成代码
            builder.setInsertPoint(mergeBB);

            return nullptr;
        }

        case NODE_BREAK_STMT: {
            break_pos.push_back({builder.getInsertBlock(), block_idx - 1});
            return nullptr;
        }

        case NODE_CONTINUE_STMT: {
            continue_pos.push_back({builder.getInsertBlock(), block_idx - 1});
            return nullptr;
        }

        default:
            // 处理其他节点类型
            midend::Value* last_value = nullptr;
            for (int i = 0; i < node->child_count; ++i) {
                last_value = translate_node(node->children[i], builder,
                                            current_func, local_vars);
                if (node->children[i]->node_type == NODE_BREAK_STMT ||
                    node->children[i]->node_type == NODE_CONTINUE_STMT ||
                    node->children[i]->node_type == NODE_RETURN_STMT)
                    break;
            }
            return last_value;
    }
}

void translate_func_def(ASTNodePtr node, midend::Module* module) {
    if (!node) return;

    SymbolPtr func_sym = node->data.symb_ptr;
    if (node->node_type != NODE_FUNC_DEF || node->data_type != NODEDATA_SYMB ||
        !func_sym)
        return;

    auto ctx = module->getContext();

    // 从符号表中获取返回类型、函数名和函数的其它信息
    midend::Type* return_type = get_ir_type(ctx, func_sym->data_type);
    std::string func_name = (func_sym->name) ? func_sym->name : "unknown.func";
    FuncInfo func_info = func_sym->attributes.func_info;

    // 函数参数节点（函数参数作为局部变量）
    std::vector<midend::Type*> param_types;
    std::vector<std::string> param_names;
    for (int i = 0; i < func_info.param_count; i++) {
        SymbolPtr param_sym = func_info.params[i];
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
        param_names.push_back("param." + get_symbol_name(param_sym));
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
        midend::BasicBlock::Create(ctx, func_name + ".entry", func);
    midend::IRBuilder builder(entry_bb);

    // 函数局部变量
    std::unordered_map<int, midend::Value*> func_local_vars;

    // 在函数体内部定义函数形参
    for (int i = 0; i < func_info.param_count; i++) {
        SymbolPtr param_sym = func_info.params[i];
        midend::Value* param;
        if (param_sym->symbol_type == SYMB_VAR) {
            param = def_var(builder, param_sym, func_local_vars);
        } else {
            param = func->getArg(i);
        }
        function_param_symbols.insert(param_sym->id);
        func_local_vars[param_sym->id] = param;
    }

    // 定义所有局部变量
    for (int i = 0; i < func_info.var_count; i++) {
        def_var(builder, func_info.vars[i], func_local_vars);
    }

    // 形参赋值给存储在栈中的对象
    for (int i = 0; i < func_info.param_count; i++) {
        SymbolPtr param_sym = func_info.params[i];
        if (param_sym->symbol_type == SYMB_VAR) {
            midend::Value* param_value = func->getArg(i);
            builder.createStore(param_value, func_local_vars[param_sym->id]);
        }
    }

    // 处理函数体
    translate_node(node->children[1], builder, func, func_local_vars);

    // 如果没有显式的return语句，添加一个
    if (!builder.getInsertBlock()->getTerminator()) {
        if (return_type->isVoidType())
            builder.createRetVoid();
        else
            builder.createRet(builder.getInt32(0));
    }
}

// 从根节点开始翻译，处理函数定义
void translate_root(ASTNodePtr node, midend::Module* module) {
    auto ctx = module->getContext();

    if (!node) return;

    // 初始化变量和基本块编号
    var_idx = 0;
    block_idx = 0;

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
                        if (init_node->data_type == NODEDATA_INT) {
                            init = midend::ConstantInt::get(
                                (midend::IntegerType*)var_type,
                                init_node->data.direct_int);
                        } else if (init_node->data_type == NODEDATA_FLOAT) {
                            init = midend::ConstantFP::get(
                                (midend::FloatType*)var_type,
                                init_node->data.direct_float);
                        }
                    }
                }
                auto linkage = is_const
                                   ? midend::GlobalVariable::InternalLinkage
                                   : midend::GlobalVariable::ExternalLinkage;
                midend::GlobalVariable* global_var =
                    midend::GlobalVariable::Create(var_type, is_const, linkage,
                                                   init, get_symbol_name(sym),
                                                   module);
                global_var_tab[sym->id] = global_var;
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
                    init = process_array_init_list(child->children[1],
                                                   array_type, sym->data_type);
                }

                auto linkage = is_const
                                   ? midend::GlobalVariable::InternalLinkage
                                   : midend::GlobalVariable::ExternalLinkage;
                midend::GlobalVariable* global_array =
                    midend::GlobalVariable::Create(
                        array_type, is_const, linkage, init,
                        get_symbol_name(sym), module);
                global_var_tab[sym->id] = global_array;
                break;
            }
            case NODE_FUNC_DEF:
                translate_func_def(child, module);
                break;
            default:
                break;
        }
    }
}

std::unique_ptr<midend::Module> generate_IR(FILE* file_in) {
    if (!file_in) return nullptr;
    yyin = file_in;

    auto ctx = new midend::Context();
    auto module = std::make_unique<midend::Module>("main", ctx);

    init_symbol_management();
    add_runtime_lib_to_symbol_table();

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

    add_runtime_lib_to_func_tab(module.get());
    translate_root(root, module.get());

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
