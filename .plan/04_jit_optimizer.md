---
title: 第四章 - JIT 编译和优化器
chapter: 4
prerequisites: chapter 3
estimated_time: 3-4 hours
---

# 第四章：JIT 编译和优化器支持

## 学习目标

完成本章后，你将能够：

1. 理解 JIT 编译的工作原理
2. 使用 LLVM ORC JIT 执行代码
3. 配置和运行优化 pass
4. 实现交互式 REPL
5. 优化生成的代码性能

---

## 什么是 JIT 编译？

### JIT vs AOT 编译

| 类型 | 全称 | 时机 | 示例 |
|------|------|------|------|
| AOT | Ahead-Of-Time | 运行前编译 | C/C++, Rust |
| JIT | Just-In-Time | 运行时编译 | JavaScript, Java, Python |

### JIT 的优势

1. **交互性**：可以立即执行用户输入的代码
2. **动态优化**：根据运行时信息优化代码
3. **增量编译**：只编译修改的部分

### LLVM ORC JIT

LLVM 提供了 ORC（On-Request Compilation）JIT 引擎：

```
用户代码 → Parser → AST → LLVM IR → Optimization → Machine Code → Execution
                                                         ↑
                                                      JIT Layer
```

---

## JIT 架构设计

### ORC JIT 核心概念

LLVM ORC JIT 使用分层架构：

```
┌─────────────────────────────────────────────────────────────┐
│                        LLJIT                                 │
│  ┌─────────────────────────────────────────────────────┐   │
│  │                 IRCompileLayer                        │   │
│  │            (IR → Object Code)                         │   │
│  └─────────────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │            ObjectLinkingLayer                         │   │
│  │          (Object Code → Memory)                       │   │
│  └─────────────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │              JITDylib                                 │   │
│  │          (Symbol Management)                          │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

- **IRCompileLayer**：将 LLVM IR 编译为目标代码
- **ObjectLinkingLayer**：将目标代码链接到内存
- **JITDylib**：管理符号（函数、变量）的查找

### KaleidoscopeJIT 类设计

```cpp
// include/jit.hpp
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

    // 保存上下文和模块，确保生命周期正确
    std::unique_ptr<llvm::LLVMContext> TheContext;
    std::unique_ptr<llvm::Module> TheModule;

public:
    KaleidoscopeJIT();
    ~KaleidoscopeJIT() = default;

    /// 添加模块到 JIT
    /// @param TSM ThreadSafeModule 包含要编译的 IR
    /// @return 成功返回 Error::success()，失败返回错误
    llvm::Error addModule(std::unique_ptr<llvm::orc::ThreadSafeModule> TSM);

    /// 查找符号并获取函数指针
    /// @param Name 符号名称
    /// @return 成功返回地址，失败返回错误
    llvm::Expected<llvm::JITTargetAddress> lookup(const std::string &Name);

    /// 获取 LLJIT 实例
    llvm::orc::LLJIT* getJIT() { return TheJIT.get(); }

    /// 获取数据布局
    const llvm::DataLayout& getDataLayout() const {
        return TheJIT->getDataLayout();
    }
};

} // namespace kaleidoscope

#endif // KALEIDOSCOPE_JIT_HPP
```

### JIT 实现

```cpp
// src/jit.cpp
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
        std::cerr << "Error creating JIT: " << ErrMsg << "\n";
        exit(1);
    }
    
    TheJIT = std::move(*JITOrErr);
    
    // 添加进程符号查找器
    // 这允许 JIT 调用外部函数（如 sin, cos, printf 等）
    auto DLSG = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
        TheJIT->getDataLayout().getGlobalPrefix()
    );
    
    if (!DLSG) {
        std::string ErrMsg = llvm::toString(DLSG.takeError());
        std::cerr << "Error getting process symbols: " << ErrMsg << "\n";
        exit(1);
    }
    
    // 将符号查找器添加到主 JITDylib
    TheJIT->getMainJITDylib().addGenerator(std::move(*DLSG));
    
    std::cout << "JIT initialized successfully\n";
    std::cout << "Target triple: " << TheJIT->getTargetTriple().str() << "\n";
}

llvm::Error KaleidoscopeJIT::addModule(
    std::unique_ptr<llvm::orc::ThreadSafeModule> TSM) {
    
    if (!TSM) {
        return llvm::make_error<llvm::StringError>(
            "Null ThreadSafeModule",
            llvm::inconvertibleErrorCode()
        );
    }
    
    return TheJIT->addIRModule(std::move(*TSM));
}

llvm::Expected<llvm::JITTargetAddress> KaleidoscopeJIT::lookup(const std::string &Name) {
    // 在 JIT 中查找符号
    auto Sym = TheJIT->lookup(Name);
    
    if (!Sym) {
        return Sym.takeError();
    }
    
    return Sym->getValue();
}

} // namespace kaleidoscope
```

---

## 优化 Pass 设置

### Pass 管理器架构

LLVM 17 使用新的 Pass 管理器：

```
PassBuilder
    │
    ├── ModulePassManager (模块级优化)
    │       └── FunctionPassManager (函数级优化)
    │               └── LoopPassManager (循环级优化)
    │
    └── AnalysisManagers
            ├── ModuleAnalysisManager
            ├── FunctionAnalysisManager
            └── LoopAnalysisManager
```

### Optimizer 类设计

```cpp
// include/optimizer.hpp
#ifndef KALEIDOSCOPE_OPTIMIZER_HPP
#define KALEIDOSCOPE_OPTIMIZER_HPP

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Utils.h"
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
    llvm::FunctionPassManager FPM;
    llvm::FunctionAnalysisManager FAM;
    
    OptimizationLevel OptLevel;

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
```

### 优化器实现

```cpp
// src/optimizer.cpp
#include "optimizer.hpp"
#include <iostream>

namespace kaleidoscope {

Optimizer::Optimizer(OptimizationLevel Level) 
    : OptLevel(Level) {
    initializePasses();
}

void Optimizer::initializePasses() {
    // 注册分析 passes
    PB.registerFunctionAnalyses(FAM);
    
    // 根据优化级别配置 passes
    switch (OptLevel) {
    case OptimizationLevel::O0:
        // 无优化，不添加任何 pass
        break;
        
    case OptimizationLevel::O1:
        // 基本优化
        FPM.addPass(llvm::InstCombinePass());     // 指令合并
        FPM.addPass(llvm::DCEPass());              // 死代码消除
        FPM.addPass(llvm::CFGSimplificationPass()); // 控制流简化
        break;
        
    case OptimizationLevel::O2:
        // 标准优化
        FPM.addPass(llvm::PromotePass());          // mem2reg：提升内存到寄存器
        FPM.addPass(llvm::InstCombinePass());      // 指令合并
        FPM.addPass(llvm::ReassociatePass());      // 重新关联表达式
        FPM.addPass(llvm::GVNPass());              // 全局值编号
        FPM.addPass(llvm::CFGSimplificationPass()); // 控制流简化
        FPM.addPass(llvm::InstCombinePass());      // 再次指令合并
        FPM.addPass(llvm::DCEPass());              // 死代码消除
        break;
        
    case OptimizationLevel::O3:
        // 激进优化
        FPM.addPass(llvm::PromotePass());
        FPM.addPass(llvm::InstCombinePass());
        FPM.addPass(llvm::ReassociatePass());
        FPM.addPass(llvm::GVNPass());
        FPM.addPass(llvm::CFGSimplificationPass());
        FPM.addPass(llvm::InstCombinePass());
        FPM.addPass(llvm::ReassociatePass());
        FPM.addPass(llvm::GVNPass());
        FPM.addPass(llvm::CFGSimplificationPass());
        FPM.addPass(llvm::InstCombinePass());
        FPM.addPass(llvm::DCEPass());
        break;
    }
}

void Optimizer::setOptLevel(OptimizationLevel Level) {
    OptLevel = Level;
    // 需要重新创建 PassManager
    FPM = llvm::FunctionPassManager();
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
```

---

## 优化 Pass 详解

### 1. mem2reg (PromotePass)

将栈内存变量提升到 SSA 寄存器：

**优化前**：
```llvm
%x.addr = alloca double
store double 1.0, double* %x.addr
%x.val = load double, double* %x.addr
ret double %x.val
```

**优化后**：
```llvm
ret double 1.0
```

这是处理 `var` 语句的关键优化。

### 2. 指令合并 (InstCombine)

合并和简化指令：

**优化前**：
```llvm
%1 = fadd double %x, 0.0    ; 加 0 无意义
%2 = fmul double %1, 1.0    ; 乘 1 无意义
ret double %2
```

**优化后**：
```llvm
ret double %x
```

### 3. 重新关联 (Reassociate)

重新排列表达式以暴露更多优化机会：

**优化前**：
```llvm
%1 = fadd double %a, %b
%2 = fadd double %1, %c
%3 = fadd double %d, %e
%4 = fadd double %2, %3
```

**优化后**（可能重新排列以利用结合律）：
```llvm
; 优化器可以识别公共子表达式
```

### 4. 全局值编号 (GVN)

消除重复计算：

**优化前**：
```llvm
%1 = fadd double %a, %b
%2 = fadd double %a, %b    ; 与 %1 相同
%3 = fadd double %1, %2
```

**优化后**：
```llvm
%1 = fadd double %a, %b
%3 = fadd double %1, %1    ; 重用 %1
```

### 5. 死代码消除 (DCE)

删除未使用的计算：

**优化前**：
```llvm
%1 = fadd double %a, %b
%2 = fmul double %c, %d    ; 结果未使用
ret double %1
```

**优化后**：
```llvm
%1 = fadd double %a, %b
ret double %1
```

### 6. 控制流简化 (CFGSimplification)

简化控制流图：

**优化前**：
```llvm
br i1 true, label %then, label %else
then:
  br label %merge
else:
  br label %merge
merge:
  ret double 1.0
```

**优化后**：
```llvm
ret double 1.0
```

---

## 集成 JIT 和优化器

### 更新代码生成器

```cpp
// 在 include/codegen.hpp 中添加 JIT 和优化器支持

#ifndef KALEIDOSCOPE_CODEGEN_HPP
#define KALEIDOSCOPE_CODEGEN_HPP

#include "ast.hpp"
#include "lexer.hpp"
#include "jit.hpp"
#include "optimizer.hpp"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace kaleidoscope {

class CodeGenerator {
public:
    CodeGenerator();
    
    /// 初始化模块（用于新的编译单元）
    void InitializeModule();
    
    /// 代码生成入口
    llvm::Value* codegen(const ExprAST* Expr);
    llvm::Function* codegen(const PrototypeAST* Proto);
    llvm::Function* codegen(const FunctionAST* Func);
    
    /// JIT 执行
    double executeTopLevelExpr(const FunctionAST* Func);
    
    /// 获取模块
    llvm::Module* getModule() const { return TheModule.get(); }
    
    /// 获取上下文
    llvm::LLVMContext* getContext() const { return TheContext.get(); }
    
    /// 打印 IR
    void printIR();
    
    /// 获取函数
    llvm::Function* getFunction(const std::string& Name);
    
    /// 添加原型
    void addPrototype(std::unique_ptr<PrototypeAST> Proto);
    
    /// 获取错误信息
    const std::string& getLastError() const { return LastError; }
    
    /// 获取 JIT
    KaleidoscopeJIT* getJIT() { return TheJIT.get(); }
    
    /// 获取优化器
    Optimizer* getOptimizer() { return TheOptimizer.get(); }

private:
    /// LLVM 核心对象
    std::unique_ptr<llvm::LLVMContext> TheContext;
    std::unique_ptr<llvm::Module> TheModule;
    std::unique_ptr<llvm::IRBuilder<>> Builder;
    
    /// JIT 编译器
    std::unique_ptr<KaleidoscopeJIT> TheJIT;
    
    /// 优化器
    std::unique_ptr<Optimizer> TheOptimizer;
    
    /// 符号表
    std::unordered_map<std::string, llvm::Value*> NamedValues;
    
    /// 函数原型表
    std::unordered_map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
    
    /// 错误信息
    std::string LastError;
    
    /// 各类型 AST 节点的代码生成
    llvm::Value* codegenNumber(const NumberExprAST* Expr);
    llvm::Value* codegenVariable(const VariableExprAST* Expr);
    llvm::Value* codegenBinary(const BinaryExprAST* Expr);
    llvm::Value* codegenUnary(const UnaryExprAST* Expr);
    llvm::Value* codegenCall(const CallExprAST* Expr);
    
    /// 错误处理
    llvm::Value* LogErrorV(const char* Str);
};

} // namespace kaleidoscope

#endif // KALEIDOSCOPE_CODEGEN_HPP
```

### 执行顶层表达式实现

```cpp
// 在 src/codegen.cpp 中添加

double CodeGenerator::executeTopLevelExpr(const FunctionAST* Func) {
    // 1. 生成函数代码
    llvm::Function* F = codegen(Func);
    if (!F) {
        std::cerr << "Error: Failed to generate code for expression\n";
        return 0.0;
    }
    
    // 2. 优化函数
    TheOptimizer->optimize(F);
    
    // 3. 打印优化后的 IR（调试用）
    std::cout << "Optimized IR:\n";
    F->print(llvm::outs());
    std::cout << "\n";
    
    // 4. 验证函数
    if (llvm::verifyFunction(*F, &llvm::errs())) {
        std::cerr << "Error: Function verification failed after optimization\n";
        return 0.0;
    }
    
    // 5. 创建 ThreadSafeModule
    // 注意：需要转移所有权给 ThreadSafeModule
    auto TSM = llvm::orc::ThreadSafeModule(
        std::move(TheModule),
        std::move(TheContext)
    );
    
    // 6. 添加到 JIT
    if (auto Err = TheJIT->addModule(std::make_unique<llvm::orc::ThreadSafeModule>(std::move(TSM)))) {
        std::cerr << "Error: Failed to add module to JIT: " 
                  << llvm::toString(std::move(Err)) << "\n";
        return 0.0;
    }
    
    // 7. 查找符号
    auto ExprSymbol = TheJIT->lookup(F->getName().str());
    if (!ExprSymbol) {
        std::cerr << "Error: Failed to lookup symbol: " 
                  << llvm::toString(ExprSymbol.takeError()) << "\n";
        return 0.0;
    }
    
    // 8. 获取函数指针并调用
    double (*FP)() = reinterpret_cast<double(*)()>(*ExprSymbol);
    
    // 9. 执行并返回结果
    double Result = FP();
    
    // 10. 重新初始化模块，为下一次编译做准备
    InitializeModule();
    
    return Result;
}
```

---

## 完整的主程序

```cpp
// src/main.cpp
#include "codegen.hpp"
#include "lexer.hpp"
#include "parser.hpp"

#include "llvm/Support/TargetSelect.h"

#include <iostream>
#include <string>

using namespace kaleidoscope;

/// 初始化 LLVM 目标
static void InitializeLLVM() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
}

/// 打印欢迎信息
static void PrintWelcome() {
    std::cout << "Kaleidoscope JIT Interpreter\n";
    std::cout << "================================\n";
    std::cout << "Type expressions to evaluate them.\n";
    std::cout << "Type 'def name(args) expr' to define a function.\n";
    std::cout << "Type 'extern name(args)' to declare an external function.\n";
    std::cout << "Type 'quit' to exit.\n\n";
}

/// 主循环
int main(int argc, char* argv[]) {
    // 初始化 LLVM
    InitializeLLVM();
    
    // 打印欢迎信息
    PrintWelcome();
    
    // 创建组件
    Lexer lexer;
    Parser parser(lexer);
    CodeGenerator codeGen;
    
    // 主循环
    while (true) {
        std::cout << "ready> ";
        std::string line;
        
        if (!std::getline(std::cin, line)) {
            break;
        }
        
        // 检查退出命令
        if (line == "quit" || line == "exit") {
            break;
        }
        
        // 跳过空行
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }
        
        // 设置输入
        lexer.setInput(line + "\n");
        
        // 获取第一个 Token
        parser.getNextToken();
        
        // 根据 Token 类型处理
        switch (parser.getCurrentToken()) {
        case tok_eof:
            break;
            
        case ';':
            // 忽略顶层分号
            parser.getNextToken();
            break;
            
        case tok_def: {
            auto FnAST = parser.ParseDefinition();
            if (FnAST) {
                if (auto* FnIR = codeGen.codegen(FnAST.get())) {
                    std::cout << "Defined function: " << FnIR->getName().str() << "\n";
                    // 不立即执行，只编译
                } else {
                    std::cerr << "Error: " << codeGen.getLastError() << "\n";
                }
            } else {
                std::cerr << "Error: " << parser.getLastError() << "\n";
            }
            break;
        }
        
        case tok_extern: {
            auto ProtoAST = parser.ParseExtern();
            if (ProtoAST) {
                if (auto* FnIR = codeGen.codegen(ProtoAST.get())) {
                    std::cout << "Declared extern: " << FnIR->getName().str() << "\n";
                } else {
                    std::cerr << "Error: " << codeGen.getLastError() << "\n";
                }
            } else {
                std::cerr << "Error: " << parser.getLastError() << "\n";
            }
            break;
        }
        
        default: {
            // 顶层表达式 - JIT 执行
            auto FnAST = parser.ParseTopLevelExpr();
            if (FnAST) {
                double Result = codeGen.executeTopLevelExpr(FnAST.get());
                std::cout << "= " << Result << "\n";
            } else {
                std::cerr << "Error: " << parser.getLastError() << "\n";
            }
            break;
        }
        }
    }
    
    std::cout << "\nGoodbye!\n";
    return 0;
}
```

---

## 示例会话

### 启动程序

```
Kaleidoscope JIT Interpreter
================================
Type expressions to evaluate them.
Type 'def name(args) expr' to define a function.
Type 'extern name(args)' to declare an external function.
Type 'quit' to exit.

ready> 
```

### 基本计算

```
ready> 1 + 2 * 3;
Optimized IR:
define double @__anon_expr() {
entry:
    ret double 7.000000e+00
}
= 7
```

### 函数定义和调用

```
ready> def add(x y) x + y;
Defined function: add

ready> add(3, 4);
Optimized IR:
define double @__anon_expr() {
entry:
    %calltmp = call double @add(double 3.000000e+00, double 4.000000e+00)
    ret double %calltmp
}
= 7
```

### 使用外部函数

```
ready> extern sin(x);
Declared extern: sin

ready> extern cos(x);
Declared extern: cos

ready> sin(1.57079632679);
Optimized IR:
define double @__anon_expr() {
entry:
    %calltmp = call double @sin(double 1.570796e+00)
    ret double %calltmp
}
= 1
```

### 复杂表达式

```
ready> def test(x) x * x + 2 * x + 1;
Defined function: test

ready> test(5);
Optimized IR:
define double @__anon_expr() {
entry:
    %calltmp = call double @test(double 5.000000e+00)
    ret double %calltmp
}
= 36
```

### 常量折叠优化

```
ready> 2 * 3 + 4 * 5;
Optimized IR:
define double @__anon_expr() {
entry:
    ret double 2.600000e+01
}
= 26
```

注意：优化器将 `2 * 3 + 4 * 5` 直接计算为 `26`，没有运行时计算。

---

## 性能对比

### 无优化版本

```llvm
define double @example(double %x) {
entry:
    %multmp = fmul double 2.000000e+00, 3.000000e+00
    %addtmp = fadd double %x, %multmp
    ret double %addtmp
}
```

### 优化后版本

```llvm
define double @example(double %x) {
entry:
    %addtmp = fadd double %x, 6.000000e+00
    ret double %addtmp
}
```

优化器将 `2 * 3` 在编译时计算为 `6`。

---

## 调试技巧

### 查看 Pass 执行

使用 `opt` 工具单独测试各个 pass：

```bash
# 查看单个 pass 的效果
opt -passes=instcombine -S input.ll -o output.ll

# 查看多个 pass 的效果
opt -passes='instcombine,gvn,dce' -S input.ll -o output.ll

# 查看所有优化后的效果
opt -passes='default<O2>' -S input.ll -o output.ll
```

### 验证 IR

在代码中添加验证：

```cpp
// 验证函数
if (llvm::verifyFunction(*F, &llvm::errs())) {
    std::cerr << "Function verification failed!\n";
    TheModule->print(llvm::errs(), nullptr);
    return nullptr;
}

// 验证模块
if (llvm::verifyModule(*TheModule, &llvm::errs())) {
    std::cerr << "Module verification failed!\n";
    return nullptr;
}
```

### 打印 IR

```cpp
// 打印到标准输出
TheModule->print(llvm::outs(), nullptr);

// 打印到文件
std::error_code EC;
llvm::raw_fd_ostream Out("output.ll", EC);
if (!EC) {
    TheModule->print(Out, nullptr);
}
```

### JIT 调试

启用详细输出：

```cpp
// 在代码中设置环境变量
setenv("LLVM_DEBUG", "1", 1);

// 或者使用命令行
// KALEIDOSCOPE_DEBUG=1 ./kaleidoscope
```

---

## 常见问题

### Q: JIT 与解释器的区别？

**A**: 
- **解释器**：直接执行 AST 或字节码，每次执行都需要遍历树结构
- **JIT**：将代码编译为本地机器码，执行速度接近原生代码

JIT 的优势在于编译一次，可以多次快速执行。

### Q: 为什么需要 ThreadSafeModule？

**A**: ORC JIT 是线程安全的。`ThreadSafeModule` 封装了 `Module` 和 `LLVMContext`，确保：
1. 模块可以在不同线程间安全传递
2. 上下文的生命周期正确管理
3. 避免数据竞争

### Q: 如何处理编译错误？

**A**: LLVM 使用 `llvm::Expected<T>` 和 `llvm::Error` 进行错误处理：

```cpp
auto Result = SomeLLVMFunction();
if (!Result) {
    llvm::Error Err = Result.takeError();
    // 处理错误
    llvm::errs() << "Error: " << llvm::toString(std::move(Err)) << "\n";
    return;
}
// 使用结果
T Value = *Result;
```

### Q: 优化会影响调试吗？

**A**: 是的，优化会改变代码结构：
- 内联会消除函数调用
- 死代码消除会移除未使用的变量
- 常量折叠会改变表达式结构

开发时建议使用 `-O0` 或 `-O1`，生产环境使用 `-O2` 或 `-O3`。

### Q: 为什么函数定义后不能立即执行？

**A**: 在当前实现中，`def` 只编译函数，不执行。要执行需要通过表达式调用：

```
ready> def foo(x) x + 1;    # 定义
ready> foo(5);              # 调用并执行
= 6
```

### Q: 如何支持跨模块函数调用？

**A**: JIT 使用符号查找机制。当函数被添加到 JIT 后，其符号会被注册。后续代码可以通过符号查找调用之前定义的函数。

---

## 练习题

### 练习 1：添加优化级别命令行选项

添加 `-O0`, `-O1`, `-O2`, `-O3` 命令行选项：

```cpp
// 提示：解析命令行参数
int main(int argc, char* argv[]) {
    OptimizationLevel OptLevel = OptimizationLevel::O2;
    
    for (int i = 1; i < argc; ++i) {
        std::string Arg = argv[i];
        if (Arg == "-O0") OptLevel = OptimizationLevel::O0;
        else if (Arg == "-O1") OptLevel = OptimizationLevel::O1;
        else if (Arg == "-O2") OptLevel = OptimizationLevel::O2;
        else if (Arg == "-O3") OptLevel = OptimizationLevel::O3;
    }
    
    // 使用优化级别创建优化器
    Optimizer optimizer(OptLevel);
    // ...
}
```

### 练习 2：添加函数内联

研究如何添加函数内联优化：

```cpp
// 提示：需要使用 ModulePassManager
llvm::ModulePassManager MPM;
MPM.addPass(llvm::createModuleToFunctionPassAdaptor(
    std::move(FPM)
));

// 内联 pass 需要分析管理器
MPM.addPass(llvm::InlinerPass());
```

### 练习 3：统计优化效果

计算并显示优化前后的指令数量：

```cpp
int countInstructions(llvm::Function* F) {
    int Count = 0;
    for (auto& BB : *F) {
        Count += BB.size();
    }
    return Count;
}

// 使用
int Before = countInstructions(F);
optimizer.optimize(F);
int After = countInstructions(F);
std::cout << "Optimized: " << Before << " -> " << After << " instructions\n";
```

### 练习 4：缓存编译结果

为 JIT 添加模块缓存，避免重复编译：

```cpp
// 提示：使用 ORC 的 ObjectCache
class KaleidoscopeCache : public llvm::orc::ObjectCache {
    // 实现缓存接口
};
```

---

## 下一步

完成 JIT 和优化后，你已经有一个可以执行代码的编译器了！

在 [下一章](05_control_flow.md) 中，我们将学习：
- 如何实现 if/then/else 控制流
- 如何实现 for 循环
- 如何处理控制流的 SSA 问题

---

## 验证清单

在进入下一章之前，确保：

- [ ] 你理解了 JIT 编译的基本原理
- [ ] JIT 能正确执行简单表达式
- [ ] 优化器能简化 IR（常量折叠等）
- [ ] 能定义和调用函数
- [ ] 能使用外部函数（如 sin, cos）
- [ ] REPL 交互正常工作
- [ ] 理解了 ThreadSafeModule 的作用
- [ ] 理解了各个优化 pass 的作用
