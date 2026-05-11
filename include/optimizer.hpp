#ifndef KALEIDOSCOPE_OPTIMIZER_HPP
#define KALEIDOSCOPE_OPTIMIZER_HPP

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/DCE.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"

#include <memory>

namespace kaleidoscope {

/// 优化级别枚举
enum class OptimizationLevel {
    O0,  // 无优化
    O1,  // 基本优化
    O2,  // 标准优化
    O3   // 激进优化
};

/// Optimizer 类：管理 LLVM 优化 passes
class Optimizer {
private:
    llvm::PassBuilder PB;
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;
    llvm::FunctionPassManager FPM;

    OptimizationLevel OptLevel;
    bool Initialized = false;

public:
    /// 构造函数
    /// @param Level 优化级别，默认 O2
    explicit Optimizer(OptimizationLevel Level = OptimizationLevel::O2);

    /// 优化单个函数
    void optimize(llvm::Function* F);

    /// 优化整个模块
    void optimize(llvm::Module* M);

    /// 获取优化级别
    OptimizationLevel getOptLevel() const { return OptLevel; }

    /// 设置优化级别
    void setOptLevel(OptimizationLevel Level);

private:
    /// 初始化优化 passes
    void initializePasses();
};

} // namespace kaleidoscope

#endif // KALEIDOSCOPE_OPTIMIZER_HPP
