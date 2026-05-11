#include "jit.hpp"
#include <iostream>

namespace kaleidoscope {

KaleidoscopeJIT::KaleidoscopeJIT() {
    // 创建 LLJIT 实例
    auto JITBuilder = llvm::orc::LLJITBuilder();

    // 尝试创建 JIT
    auto JITOrErr = JITBuilder.create();
    if (!JITOrErr) {
        std::string ErrMsg = llvm::toString(JITOrErr.takeError());
        std::cerr << "Warning: Could not create JIT: " << ErrMsg << "\n";
        // JIT 创建失败，保持 TheJIT 为 nullptr
        TheJIT = nullptr;
        return;
    }

    TheJIT = std::move(*JITOrErr);

    // 添加进程符号查找器
    // 这允许 JIT 调用外部函数（如 sin, cos, printf 等）
    auto DLSG = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
        TheJIT->getDataLayout().getGlobalPrefix()
    );

    if (!DLSG) {
        std::string ErrMsg = llvm::toString(DLSG.takeError());
        std::cerr << "Warning: Could not get process symbols: " << ErrMsg << "\n";
        // 继续，只是可能无法调用外部函数
    } else {
        // 将符号查找器添加到主 JITDylib
        TheJIT->getMainJITDylib().addGenerator(std::move(*DLSG));
    }

    std::cout << "JIT initialized successfully\n";
}

llvm::Error KaleidoscopeJIT::addModule(
    std::unique_ptr<llvm::orc::ThreadSafeModule> TSM, bool IsAnonExpr) {

    if (!TheJIT) {
        return llvm::make_error<llvm::StringError>(
            "JIT not available",
            llvm::inconvertibleErrorCode()
        );
    }

    if (!TSM) {
        return llvm::make_error<llvm::StringError>(
            "Null ThreadSafeModule",
            llvm::inconvertibleErrorCode()
        );
    }

    // 如果是匿名表达式，先清理之前的匿名表达式模块
    if (IsAnonExpr && AnonExprRT) {
        if (auto Err = AnonExprRT->remove()) {
            return Err;
        }
        AnonExprRT = nullptr;
    }

    // 创建资源跟踪器
    auto RT = TheJIT->getMainJITDylib().createResourceTracker();

    // 如果是匿名表达式，保存资源跟踪器以便后续清理
    if (IsAnonExpr) {
        AnonExprRT = RT;
    }

    return TheJIT->addIRModule(RT, std::move(*TSM));
}

llvm::Expected<llvm::JITTargetAddress> KaleidoscopeJIT::lookup(const std::string& Name) {
    if (!TheJIT) {
        return llvm::make_error<llvm::StringError>(
            "JIT not available",
            llvm::inconvertibleErrorCode()
        );
    }

    // 在 JIT 中查找符号
    auto Sym = TheJIT->lookup(Name);

    if (!Sym) {
        return Sym.takeError();
    }

    return Sym->getValue();
}

} // namespace kaleidoscope
