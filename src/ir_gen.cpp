#include <memory>

#include "IR/BasicBlock.h"
#include "IR/Function.h"
#include "IR/IRBuilder.h"
#include "IR/Module.h"
#include "IR/Type.h"

std::unique_ptr<midend::Module> generate_test_module() {
    auto ctx = std::make_unique<midend::Context>();
    auto module = std::make_unique<midend::Module>("test_module", ctx.get());

    auto* int32Ty = ctx->getInt32Type();
    auto* int32PtrTy = midend::PointerType::get(int32Ty);
    auto* funcType = midend::FunctionType::get(int32Ty, {int32PtrTy, int32Ty});
    auto* func = midend::Function::Create(funcType, "sum_array",
                                          {"array", "size"}, module.get());

    auto* entryBB = midend::BasicBlock::Create(ctx.get(), "entry", func);
    auto* loopCondBB = midend::BasicBlock::Create(ctx.get(), "loop_cond", func);
    auto* loopBodyBB = midend::BasicBlock::Create(ctx.get(), "loop_body", func);
    auto* exitBB = midend::BasicBlock::Create(ctx.get(), "exit", func);

    midend::IRBuilder builder(entryBB);

    auto* sumAlloca = builder.createAlloca(int32Ty, nullptr, "sum_ptr");
    auto* indexAlloca = builder.createAlloca(int32Ty, nullptr, "index_ptr");

    builder.createStore(builder.getInt32(0), sumAlloca);
    builder.createStore(builder.getInt32(0), indexAlloca);
    builder.createBr(loopCondBB);

    builder.setInsertPoint(loopCondBB);
    auto* currentIndex = builder.createLoad(indexAlloca, "current_index");
    auto* arraySize = func->getArg(1);
    auto* loopCond =
        builder.createICmpSLT(currentIndex, arraySize, "index_lt_size");
    builder.createCondBr(loopCond, loopBodyBB, exitBB);

    builder.setInsertPoint(loopBodyBB);
    auto* arrayPtr = func->getArg(0);
    auto* index = builder.createLoad(indexAlloca, "index");

    auto* elementPtr = builder.createGEP(arrayPtr, index, "element_ptr");
    auto* elementValue = builder.createLoad(elementPtr, "element_value");

    auto* currentSum = builder.createLoad(sumAlloca, "current_sum");
    auto* newSum = builder.createAdd(currentSum, elementValue, "new_sum");
    builder.createStore(newSum, sumAlloca);

    auto* nextIndex =
        builder.createAdd(index, builder.getInt32(1), "next_index");
    builder.createStore(nextIndex, indexAlloca);
    builder.createBr(loopCondBB);

    builder.setInsertPoint(exitBB);
    auto* finalSum = builder.createLoad(sumAlloca, "final_sum");
    builder.createRet(finalSum);

    return module;
}