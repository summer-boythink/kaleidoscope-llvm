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

### KaleidoscopeJIT 类

```cpp
// src/jit.hpp
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

#include <memory>

namespace kaleidoscope {

class KaleidoscopeJIT {
private:
    std::unique_ptr<llvm::orc::LLJIT> TheJIT;

public:
    KaleidoscopeJIT();
    ~KaleidoscopeJIT() = default;

    // 添加模块到 JIT
    llvm::Error addModule(std::unique_ptr<llvm::orc::ThreadSafeModule> TSM);

    // 查找符号并获取函数指针
    llvm::Expected<llvm::JITTargetAddress> lookup(const std::string &Name);

    // 获取上下文
    llvm::orc::LLJIT *getJIT() { return TheJIT.get(); }
};

} // namespace kaleidoscope

#endif // KALEIDOSCOPE_JIT_HPP
```

### JIT 实现

```cpp
// src/jit.cpp
#include "jit.hpp"
#include "llvm/Support/Error.h"

namespace kaleidoscope {

KaleidoscopeJIT::KaleidoscopeJIT() {
    // 创建 LLJIT 实例
    auto JITBuilder = llvm::orc::LLJITBuilder();
    
    auto JITOrErr = JITBuilder.create();
    if (!JITOrErr) {
        llvm::errs() << "Error creating JIT: " << llvm::toString(JITOrErr.takeError()) << "\n";
        exit(1);
    }
    
    TheJIT = std::move(*JITOrErr);
    
    // 添加进程符号查找器（允许调用外部函数如 sin, cos）
    auto DLSG = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
        llvm::orc::JITDylib::getGlobalPrefix()
    );
    
    if (!DLSG) {
        llvm::errs() << "Error getting process symbols: " 
                     << llvm::toString(DLSG.takeError()) << "\n";
        exit(1);
    }
    
    TheJIT->getMainJITDylib().addGenerator(std::move(*DLSG));
}

llvm::Error KaleidoscopeJIT::addModule(std::unique_ptr<llvm::orc::ThreadSafeModule> TSM) {
    return TheJIT->addIRModule(std::move(*TSM));
}

llvm::Expected<llvm::JITTargetAddress> KaleidoscopeJIT::lookup(const std::string &Name) {
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

### Pass 管理器

LLVM 使用 Pass 管理器来组织和运行优化 passes：

```cpp
// src/optimizer.hpp
#ifndef KALEIDOSCOPE_OPTIMIZER_HPP
#define KALEIDOSCOPE_OPTIMIZER_HPP

#include "llvm/IR/Function.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Utils.h"

#include <memory>

namespace kaleidoscope {

class Optimizer {
    llvm::PassBuilder PB;
    llvm::FunctionPassManager FPM;
    llvm::FunctionAnalysisManager FAM;

public:
    Optimizer();
    
    // 优化函数
    void optimize(llvm::Function *F);
    
    // 优化模块
    void optimize(llvm::Module *M);
};

} // namespace kaleidoscope

#endif // KALEIDOSCOPE_OPTIMIZER_HPP
```

### 优化器实现

```cpp
// src/optimizer.cpp
#include "optimizer.hpp"

namespace kaleidoscope {

Optimizer::Optimizer() {
    // 注册分析 pass
    PB.registerFunctionAnalyses(FAM);
    
    // 添加优化 passes（按顺序执行）
    
    // 1. 死代码消除：删除未使用的代码
    FPM.addPass(llvm::DCEPass());
    
    // 2. 常量传播：计算常量表达式
    // FPM.addPass(llvm::ConstantPropagationPass());
    
    // 3. 指令合并：简化指令
    FPM.addPass(llvm::InstCombinePass());
    
    // 4. 全局值编号：消除冗余计算
    FPM.addPass(llvm::GVNPass());
    
    // 5. 控制流简化：删除无效分支
    FPM.addPass(llvm::CFGSimplificationPass());
    
    // 6. 死存储消除：删除未使用的存储
    FPM.addPass(llvm::DSEPass());
    
    // 7. 提升内存到寄存器
    FPM.addPass(llvm::PromotePass());
    
    // 8. 再次指令合并
    FPM.addPass(llvm::InstCombinePass());
}

void Optimizer::optimize(llvm::Function *F) {
    FPM.run(*F, FAM);
}

void Optimizer::optimize(llvm::Module *M) {
    for (auto &F : *M) {
        if (!F.isDeclaration()) {
            optimize(&F);
        }
    }
}

} // namespace kaleidoscope
```

---

## 优化 Pass 详解

### 1. 死代码消除 (DCE)

删除计算结果未被使用的指令。

**优化前**：
```llvm
%1 = add i32 %a, %b
%2 = mul i32 %a, 2    ; 结果未被使用
ret i32 %1
```

**优化后**：
```llvm
%1 = add i32 %a, %b
ret i32 %1
```

### 2. 常量折叠

编译时计算常量表达式。

**优化前**：
```llvm
%1 = add i32 1, 2
ret i32 %1
```

**优化后**：
```llvm
ret i32 3
```

### 3. 指令合并 (InstCombine)

简化指令模式。

**优化前**：
```llvm
%1 = mul i32 %x, 1    ; 乘以 1 无意义
%2 = add i32 %1, 0    ; 加 0 无意义
ret i32 %2
```

**优化后**：
```llvm
ret i32 %x
```

### 4. 全局值编号 (GVN)

消除重复计算。

**优化前**：
```llvm
%1 = add i32 %a, %b
%2 = add i32 %a, %b  ; 与 %1 相同
ret i32 %2
```

**优化后**：
```llvm
%1 = add i32 %a, %b
ret i32 %1
```

### 5. 控制流简化

删除无效分支。

**优化前**：
```llvm
br i1 true, label %then, label %else  ; 永远走 then
```

**优化后**：
```llvm
br label %then
```

---

## 完整 JIT 集成

### 更新代码生成器

```cpp
// 在 CodeGenerator 类中添加

class CodeGenerator {
    // ... 现有成员 ...
    
    Optimizer TheOptimizer;
    KaleidoscopeJIT TheJIT;

public:
    // 执行顶层表达式
    double executeTopLevelExpr(const FunctionAST *Func);
    
    // 执行函数调用
    double callFunction(const std::string &Name, const std::vector<double> &Args);
};
```

### 执行函数实现

```cpp
double CodeGenerator::executeTopLevelExpr(const FunctionAST *Func) {
    // 生成函数代码
    llvm::Function *F = codegen(Func);
    if (!F) {
        return 0.0;
    }
    
    // 优化函数
    TheOptimizer.optimize(F);
    
    // 打印优化后的 IR（调试用）
    std::cout << "Optimized IR:\n";
    F->print(llvm::outs());
    std::cout << "\n";
    
    // 创建 ThreadSafeModule
    auto TSM = llvm::orc::ThreadSafeModule(
        std::move(TheModule),
        std::move(TheContext)
    );
    
    // 添加到 JIT
    if (auto Err = TheJIT.addModule(std::move(TSM))) {
        llvm::errs() << "Error adding module: " << llvm::toString(std::move(Err)) << "\n";
        return 0.0;
    }
    
    // 查找函数
    auto ExprSymbol = TheJIT.lookup(F->getName().str());
    if (!ExprSymbol) {
        llvm::errs() << "Error looking up symbol: " 
                     << llvm::toString(ExprSymbol.takeError()) << "\n";
        return 0.0;
    }
    
    // 获取函数指针并调用
    double (*FP)() = (double (*)())(*ExprSymbol);
    return FP();
}
```

---

## 交互式 REPL

### 主循环更新

```cpp
// src/main.cpp
#include "codegen.hpp"
#include "jit.hpp"
#include "lexer.hpp"
#include "optimizer.hpp"
#include <iostream>

using namespace kaleidoscope;

int main() {
    std::cout << "Kaleidoscope JIT Interpreter\n";
    std::cout << "Type expressions to evaluate them.\n";
    std::cout << "Type 'def' to define a function.\n";
    std::cout << "Type 'extern' to declare an external function.\n\n";
    
    CodeGenerator CodeGen;
    
    // 预取第一个 Token
    std::cout << "ready> ";
    getNextToken();
    
    while (true) {
        std::cout << "ready> ";
        switch (CurTok) {
        case tok_eof:
            return 0;
        case ';':
            getNextToken();
            break;
        case tok_def: {
            auto FnAST = ParseDefinition();
            if (FnAST) {
                if (auto *FnIR = CodeGen.codegen(FnAST.get())) {
                    std::cout << "Defined function: " << FnIR->getName().str() << "\n";
                }
            } else {
                getNextToken();
            }
            break;
        }
        case tok_extern: {
            auto ProtoAST = ParseExtern();
            if (ProtoAST) {
                if (auto *FnIR = CodeGen.codegen(ProtoAST.get())) {
                    std::cout << "Declared extern: " << FnIR->getName().str() << "\n";
                }
            } else {
                getNextToken();
            }
            break;
        }
        default: {
            // 顶层表达式 - 立即执行
            auto FnAST = ParseTopLevelExpr();
            if (FnAST) {
                if (auto *FnIR = CodeGen.codegen(FnAST.get())) {
                    double Result = CodeGen.executeTopLevelExpr(FnAST.get());
                    std::cout << "= " << Result << "\n";
                }
            } else {
                getNextToken();
            }
            break;
        }
        }
    }
    
    return 0;
}
```

---

## 示例会话

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
    ret double 7.000000e+00
}
= 7
```

### 使用外部函数

```
ready> extern sin(x);
Declared extern: sin

ready> sin(1.5707963267948966);  // π/2
= 1
```

### 递归函数

```
ready> def fib(n) if n < 2 then n else fib(n-1) + fib(n-2);
Defined function: fib

ready> fib(10);
= 55
```

---

## 性能对比

### 无优化

```llvm
define double @example(double %x) {
entry:
    %multmp = fmul double 2.000000e+00, 3.000000e+00
    %addtmp = fadd double %x, %multmp
    ret double %addtmp
}
```

### 有优化

```llvm
define double @example(double %x) {
entry:
    %addtmp = fadd double %x, 6.000000e+00
    ret double %addtmp
}
```

常量折叠将 `2 * 3` 计算为 `6`，减少了运行时计算。

---

## 调试技巧

### 查看 Pass 执行情况

```bash
# 使用 opt 工具查看各 pass 效果
opt -passes='instcombine,gvn,dce' -S input.ll -o output.ll
```

### 验证 IR

```cpp
// 在代码中验证
if (llvm::verifyFunction(*F, &llvm::errs())) {
    std::cerr << "Function verification failed!\n";
}
```

### JIT 调试

```cpp
// 启用 JIT 调试输出
llvm::cl::opt<bool> DebugJIT("debug-jit", 
    llvm::cl::desc("Enable JIT debugging"),
    llvm::cl::init(false));
```

---

## 练习题

### 练习 1：添加优化级别选项

添加命令行选项控制优化级别：

```cpp
enum OptimizationLevel {
    O0,  // 无优化
    O1,  // 基本优化
    O2,  // 标准优化
    O3   // 激进优化
};

// 在 Optimizer 构造函数中根据级别添加不同 passes
Optimizer::Optimizer(OptimizationLevel Level) {
    switch (Level) {
    case O0:
        // 不添加任何 pass
        break;
    case O1:
        FPM.addPass(llvm::InstCombinePass());
        FPM.addPass(llvm::DCEPass());
        break;
    // ...
    }
}
```

### 练习 2：添加函数内联

实现函数内联优化：

```cpp
// 添加内联 pass
FPM.addPass(llvm::InlinerPass());
```

### 练习 3：统计优化效果

计算并显示优化前后的指令数量：

```cpp
int countInstructions(llvm::Function *F) {
    int Count = 0;
    for (auto &BB : *F) {
        Count += BB.size();
    }
    return Count;
}
```

---

## 常见问题

### Q: JIT 与解释器的区别？

**A**: 
- **解释器**：直接执行 AST 或字节码，每次执行都需要解析
- **JIT**：将代码编译为机器码再执行，执行效率更高

### Q: 为什么需要 `ThreadSafeModule`？

**A**: JIT 可能会在多个线程中运行，`ThreadSafeModule` 确保模块访问的线程安全。

### Q: 如何处理编译错误？

**A**: LLVM 使用 `llvm::Expected<T>` 和 `llvm::Error` 进行错误处理：

```cpp
if (auto Err = ...) {
    llvm::errs() << "Error: " << llvm::toString(std::move(Err)) << "\n";
    return;
}
```

### Q: 优化会影响调试吗？

**A**: 是的，优化会改变代码结构，可能使调试变得困难。在开发时可以使用 `-O0`，生产环境使用 `-O2` 或 `-O3`。

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
- [ ] 优化器能简化 IR
- [ ] 能定义和调用函数
- [ ] 能使用外部函数（如 sin, cos）
- [ ] REPL 交互正常工作
