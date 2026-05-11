#ifndef KALEIDOSCOPE_JIT_HPP
#define KALEIDOSCOPE_JIT_HPP

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Error.h"

#include <memory>
#include <string>

namespace kaleidoscope {

/// KaleidoscopeJIT - JIT 编译器类
/// 封装 LLVM ORC LLJIT，提供简单的接口
class KaleidoscopeJIT {
private:
    std::unique_ptr<llvm::orc::LLJIT> TheJIT;
    // 跟踪匿名表达式的资源，用于清理
    llvm::orc::ResourceTrackerSP AnonExprRT;

public:
    KaleidoscopeJIT();
    ~KaleidoscopeJIT() = default;

    /// 检查 JIT 是否可用
    bool isAvailable() const { return TheJIT != nullptr; }

    /// 添加模块到 JIT
    /// @param TSM ThreadSafeModule 包含要编译的 IR
    /// @param IsAnonExpr 是否是匿名表达式（需要清理之前的）
    /// @return 成功返回 Error::success()，失败返回错误
    llvm::Error addModule(std::unique_ptr<llvm::orc::ThreadSafeModule> TSM, bool IsAnonExpr = false);

    /// 查找符号并获取函数指针
    /// @param Name 符号名称
    /// @return 成功返回地址，失败返回错误
    llvm::Expected<llvm::JITTargetAddress> lookup(const std::string& Name);

    /// 获取 LLJIT 实例
    llvm::orc::LLJIT* getJIT() { return TheJIT.get(); }

    /// 获取数据布局
    const llvm::DataLayout& getDataLayout() const {
        static llvm::DataLayout EmptyLayout("");
        return TheJIT ? TheJIT->getDataLayout() : EmptyLayout;
    }
};

} // namespace kaleidoscope

#endif // KALEIDOSCOPE_JIT_HPP
