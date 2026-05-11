#include "optimizer.hpp"
#include <iostream>

namespace kaleidoscope {

Optimizer::Optimizer(OptimizationLevel Level)
    : OptLevel(Level) {
    initializePasses();
}

void Optimizer::initializePasses() {
    // 注册所有分析管理器
    // 这是 LLVM 17 新 Pass Manager 的正确初始化方式
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);

    // 交叉注册代理，允许不同层级的分析互相查询
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    // 根据优化级别配置 passes
    switch (OptLevel) {
    case OptimizationLevel::O0:
        // 无优化，不添加任何 pass
        break;

    case OptimizationLevel::O1:
        // 基本优化
        FPM.addPass(llvm::InstCombinePass());       // 指令合并
        FPM.addPass(llvm::DCEPass());                // 死代码消除
        FPM.addPass(llvm::SimplifyCFGPass());        // 控制流简化
        break;

    case OptimizationLevel::O2:
        // 标准优化
        FPM.addPass(llvm::PromotePass());            // mem2reg：提升内存到寄存器
        FPM.addPass(llvm::InstCombinePass());        // 指令合并
        FPM.addPass(llvm::ReassociatePass());        // 重新关联表达式
        FPM.addPass(llvm::GVNPass());                // 全局值编号
        FPM.addPass(llvm::SimplifyCFGPass());        // 控制流简化
        FPM.addPass(llvm::InstCombinePass());        // 再次指令合并
        FPM.addPass(llvm::DCEPass());                // 死代码消除
        break;

    case OptimizationLevel::O3:
        // 激进优化
        FPM.addPass(llvm::PromotePass());
        FPM.addPass(llvm::InstCombinePass());
        FPM.addPass(llvm::ReassociatePass());
        FPM.addPass(llvm::GVNPass());
        FPM.addPass(llvm::SimplifyCFGPass());
        FPM.addPass(llvm::InstCombinePass());
        FPM.addPass(llvm::ReassociatePass());
        FPM.addPass(llvm::GVNPass());
        FPM.addPass(llvm::SimplifyCFGPass());
        FPM.addPass(llvm::InstCombinePass());
        FPM.addPass(llvm::DCEPass());
        break;
    }

    Initialized = true;
}

void Optimizer::setOptLevel(OptimizationLevel Level) {
    OptLevel = Level;
    // 需要重新创建 PassManager
    FPM = llvm::FunctionPassManager();
    LAM = llvm::LoopAnalysisManager();
    FAM = llvm::FunctionAnalysisManager();
    CGAM = llvm::CGSCCAnalysisManager();
    MAM = llvm::ModuleAnalysisManager();
    initializePasses();
}

void Optimizer::optimize(llvm::Function* F) {
    if (!F || F->isDeclaration()) {
        return;
    }

    // 运行所有注册的 passes
    FPM.run(*F, FAM);
}

void Optimizer::optimize(llvm::Module* M) {
    if (!M) {
        return;
    }

    // 对模块中的每个函数进行优化
    for (auto& F : *M) {
        if (!F.isDeclaration()) {
            optimize(&F);
        }
    }
}

} // namespace kaleidoscope
