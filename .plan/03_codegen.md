---
title: 第三章 - 代码生成到 LLVM IR
chapter: 3
prerequisites: chapter 2
estimated_time: 4-5 hours
---

# 第三章：代码生成到 LLVM IR

## 学习目标

完成本章后，你将能够：

1. 理解 LLVM IR 的基本概念
2. 使用 LLVM API 创建模块和函数
3. 为各种 AST 节点生成 LLVM IR
4. 理解 SSA（静态单赋值）形式
5. 生成和查看 LLVM IR 输出

---

## 什么是 LLVM IR？

### LLVM IR 的特点

LLVM IR（Intermediate Representation）是 LLVM 编译器基础设施的核心：

1. **低级但类型安全**：比汇编高级，比高级语言低级
2. **SSA 形式**：每个变量只被赋值一次
3. **平台无关**：同一份 IR 可以针对不同 CPU 生成代码
4. **可读性好**：有文本格式，便于调试

### 示例：C 代码到 LLVM IR

**C 代码**：
```c
int add(int a, int b) {
    return a + b;
}
```

**LLVM IR**：
```llvm
define i32 @add(i32 %a, i32 %b) {
entry:
    %result = add i32 %a, %b
    ret i32 %result
}
```

### SSA 形式理解

**SSA（Static Single Assignment）**：每个变量只被赋值一次

**传统形式**：
```llvm
%x = add i32 1, 2
%x = add i32 %x, 3  ; 错误！x 被重新赋值
```

**SSA 形式**：
```llvm
%x1 = add i32 1, 2
%x2 = add i32 %x1, 3  ; 正确！使用新名字
```

这简化了优化分析和代码生成。

---

## LLVM 核心类

### 类层次结构

```
LLVMContext          // 上下文，管理全局状态
    │
Module               // 一个编译单元（一个源文件）
    │
Function             // 函数定义
    │
BasicBlock           // 基本块（无分支的代码序列）
    │
Instruction          // 单条指令
    │
Value                // 任何可以出现在表达式右边的东西
```

### 类说明

| 类 | 作用 | 示例 |
|----|------|------|
| `LLVMContext` | 管理核心 LLVM 数据结构 | 全局唯一实例 |
| `Module` | 包含函数和全局变量 | 编译单元 |
| `Function` | 表示一个函数 | `@main`, `@add` |
| `BasicBlock` | 连续执行的指令序列 | `entry`, `if.then`, `if.else` |
| `IRBuilder<>` | 创建 IR 指令 | `builder.CreateAdd()` |
| `Value` | 计算结果的基类 | 常量、变量、指令结果 |

---

## 代码生成器设置

### 全局状态

```cpp
// src/codegen.hpp
#ifndef KALEIDOSCOPE_CODEGEN_HPP
#define KALEIDOSCOPE_CODEGEN_HPP

#include "ast.hpp"
#include "lexer.hpp"

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

#include <memory>
#include <string>
#include <unordered_map>

namespace kaleidoscope {

//===----------------------------------------------------------------------===//
// 代码生成器类
//===----------------------------------------------------------------------===//

class CodeGenerator {
    // LLVM 核心对象
    std::unique_ptr<llvm::LLVMContext> TheContext;
    std::unique_ptr<llvm::Module> TheModule;
    std::unique_ptr<llvm::IRBuilder<>> Builder;

    // 符号表：记录当前可见的变量名和对应的 LLVM Value
    std::unordered_map<std::string, llvm::Value *> NamedValues;

    // 函数原型表：记录已声明的函数原型
    std::unordered_map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;

public:
    CodeGenerator();
    
    // 初始化
    void InitializeModule();
    
    // 代码生成入口
    llvm::Value *codegen(const ExprAST *Expr);
    llvm::Function *codegen(const PrototypeAST *Proto);
    llvm::Function *codegen(const FunctionAST *Func);
    
    // 获取模块
    llvm::Module *getModule() const { return TheModule.get(); }
    
    // 辅助方法
    llvm::Function *getFunction(const std::string &Name);
    
    // 打印 IR
    void printIR();
};

} // namespace kaleidoscope

#endif // KALEIDOSCOPE_CODEGEN_HPP
```

### 构造函数

```cpp
// src/codegen.cpp
#include "codegen.hpp"
#include <iostream>

namespace kaleidoscope {

CodeGenerator::CodeGenerator() {
    TheContext = std::make_unique<llvm::LLVMContext>();
    TheModule = std::make_unique<llvm::Module>("Kaleidoscope JIT", *TheContext);
    Builder = std::make_unique<llvm::IRBuilder<>>(*TheContext);
}

void CodeGenerator::InitializeModule() {
    // 重新初始化模块（用于 REPL 循环）
    TheModule = std::make_unique<llvm::Module>("Kaleidoscope JIT", *TheContext);
}

void CodeGenerator::printIR() {
    TheModule->print(llvm::outs(), nullptr);
}

} // namespace kaleidoscope
```

---

## 表达式代码生成

### 数字表达式

```cpp
llvm::Value *CodeGenerator::codegen(const NumberExprAST *Expr) {
    // 创建一个常量浮点数
    // Kaleidoscope 只有 double 类型
    return llvm::ConstantFP::get(*TheContext, llvm::APFloat(Expr->getValue()));
}
```

**生成的 IR**：
```llvm
; 对于数字 1.0
double 1.000000e+00
```

### 变量表达式

```cpp
llvm::Value *CodeGenerator::codegen(const VariableExprAST *Expr) {
    // 在符号表中查找变量
    llvm::Value *V = NamedValues[Expr->getName()];
    if (!V) {
        std::cerr << "Error: Unknown variable name: " << Expr->getName() << "\n";
        return nullptr;
    }
    return V;
}
```

**工作原理**：
- `NamedValues` 符号表存储当前作用域中的变量
- 变量值在函数参数传递时被添加到表中
- 变量引用就是从表中查找

### 二元运算表达式

```cpp
llvm::Value *CodeGenerator::codegen(const BinaryExprAST *Expr) {
    // 递归生成左右操作数的代码
    llvm::Value *L = codegen(Expr->getLHS());
    llvm::Value *R = codegen(Expr->getRHS());
    
    if (!L || !R) {
        return nullptr;
    }
    
    // 根据运算符生成对应的指令
    switch (Expr->getOp()) {
    case '+':
        return Builder->CreateFAdd(L, R, "addtmp");
    case '-':
        return Builder->CreateFSub(L, R, "subtmp");
    case '*':
        return Builder->CreateFMul(L, R, "multmp");
    case '/':
        return Builder->CreateFDiv(L, R, "divtmp");
    case '<':
        // 比较：创建 fcmp 指令
        L = Builder->CreateFCmpULT(L, R, "cmptmp");
        // 将 i1 (bool) 转换为 double (0.0 或 1.0)
        return Builder->CreateUIToFP(L, llvm::Type::getDoubleTy(*TheContext), "booltmp");
    default:
        std::cerr << "Error: Invalid binary operator: " << Expr->getOp() << "\n";
        return nullptr;
    }
}
```

**生成的 IR 示例**：

对于 `a + b`：
```llvm
%addtmp = fadd double %a, %b
```

对于 `a < b`：
```llvm
%cmptmp = fcmp ult double %a, %b
%booltmp = uitofp i1 %cmptmp to double
```

### 函数调用表达式

```cpp
llvm::Value *CodeGenerator::codegen(const CallExprAST *Expr) {
    // 查找函数名
    llvm::Function *CalleeF = getFunction(Expr->getCallee());
    if (!CalleeF) {
        std::cerr << "Error: Unknown function referenced: " << Expr->getCallee() << "\n";
        return nullptr;
    }
    
    // 检查参数数量
    if (CalleeF->arg_size() != Expr->getArgs().size()) {
        std::cerr << "Error: Incorrect number of arguments passed\n";
        return nullptr;
    }
    
    // 生成参数代码
    std::vector<llvm::Value *> ArgsV;
    for (const auto &Arg : Expr->getArgs()) {
        llvm::Value *ArgV = codegen(Arg.get());
        if (!ArgV) {
            return nullptr;
        }
        ArgsV.push_back(ArgV);
    }
    
    // 创建 call 指令
    return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}
```

**生成的 IR 示例**：

对于 `foo(1.0, 2.0)`：
```llvm
%calltmp = call double @foo(double 1.000000e+00, double 2.000000e+00)
```

---

## 函数代码生成

### 获取函数

```cpp
llvm::Function *CodeGenerator::getFunction(const std::string &Name) {
    // 首先检查模块中是否已经有这个函数
    if (auto *F = TheModule->getFunction(Name)) {
        return F;
    }
    
    // 如果没有，检查是否已声明过原型
    auto FI = FunctionProtos.find(Name);
    if (FI != FunctionProtos.end()) {
        return codegen(FI->second.get());
    }
    
    return nullptr;
}
```

### 函数原型代码生成

```cpp
llvm::Function *CodeGenerator::codegen(const PrototypeAST *Proto) {
    // 创建函数类型：double(double, double, ..., double)
    std::vector<llvm::Type *> Doubles(Proto->getArgs().size(),
                                       llvm::Type::getDoubleTy(*TheContext));
    
    llvm::FunctionType *FT = llvm::FunctionType::get(
        llvm::Type::getDoubleTy(*TheContext),  // 返回类型
        Doubles,                                // 参数类型
        false                                   // 不接受可变参数
    );
    
    // 创建函数
    llvm::Function *F = llvm::Function::Create(
        FT,
        llvm::Function::ExternalLinkage,       // 链接类型
        Proto->getName(),                       // 函数名
        TheModule.get()                         // 所属模块
    );
    
    // 设置参数名（用于调试）
    unsigned Idx = 0;
    for (auto &Arg : F->args()) {
        Arg.setName(Proto->getArgs()[Idx++]);
    }
    
    return F;
}
```

**生成的 IR 示例**：

对于 `foo(x y)`：
```llvm
declare double @foo(double %x, double %y)
```

### 函数定义代码生成

```cpp
llvm::Function *CodeGenerator::codegen(const FunctionAST *Func) {
    // 首先检查是否已有此函数的原型
    const std::string &FnName = Func->getProto()->getName();
    auto P = FunctionProtos.find(FnName);
    
    if (P == FunctionProtos.end()) {
        // 没有原型，先创建一个
        FunctionProtos[FnName] = std::unique_ptr<PrototypeAST>(
            new PrototypeAST(*Func->getProto())
        );
    }
    
    // 生成函数原型
    llvm::Function *TheFunction = codegen(Func->getProto());
    if (!TheFunction) {
        return nullptr;
    }
    
    // 如果函数已有定义（之前声明过 extern），需要重新创建
    if (!TheFunction->empty()) {
        std::cerr << "Error: Function cannot be redefined: " << FnName << "\n";
        return nullptr;
    }
    
    // 创建基本块
    llvm::BasicBlock *BB = llvm::BasicBlock::Create(*TheContext, "entry", TheFunction);
    Builder->SetInsertPoint(BB);
    
    // 将参数添加到符号表
    NamedValues.clear();
    for (auto &Arg : TheFunction->args()) {
        NamedValues[std::string(Arg.getName())] = &Arg;
    }
    
    // 生成函数体代码
    if (llvm::Value *RetVal = codegen(Func->getBody())) {
        // 创建返回指令
        Builder->CreateRet(RetVal);
        
        // 验证函数
        if (llvm::verifyFunction(*TheFunction, &llvm::errs())) {
            std::cerr << "Error: Function verification failed\n";
            TheFunction->eraseFromParent();
            return nullptr;
        }
        
        return TheFunction;
    }
    
    // 函数体生成失败，删除函数
    TheFunction->eraseFromParent();
    return nullptr;
}
```

**生成的 IR 示例**：

对于 `def foo(x y) x + y`：
```llvm
define double @foo(double %x, double %y) {
entry:
    %addtmp = fadd double %x, %y
    ret double %addtmp
}
```

---

## 完整代码生成器实现

### 重载 codegen 函数

```cpp
// 使用访问者模式分发到正确的 codegen 方法
llvm::Value *CodeGenerator::codegen(const ExprAST *Expr) {
    if (auto *E = dynamic_cast<const NumberExprAST *>(Expr)) {
        return codegen(E);
    } else if (auto *E = dynamic_cast<const VariableExprAST *>(Expr)) {
        return codegen(E);
    } else if (auto *E = dynamic_cast<const BinaryExprAST *>(Expr)) {
        return codegen(E);
    } else if (auto *E = dynamic_cast<const UnaryExprAST *>(Expr)) {
        return codegen(E);
    } else if (auto *E = dynamic_cast<const CallExprAST *>(Expr)) {
        return codegen(E);
    }
    return nullptr;
}
```

### 一元运算符代码生成

```cpp
llvm::Value *CodeGenerator::codegen(const UnaryExprAST *Expr) {
    llvm::Value *OperandV = codegen(Expr->getOperand());
    if (!OperandV) {
        return nullptr;
    }
    
    switch (Expr->getOpcode()) {
    case '-':
        return Builder->CreateFNeg(OperandV, "negtmp");
    default:
        std::cerr << "Error: Invalid unary operator: " << Expr->getOpcode() << "\n";
        return nullptr;
    }
}
```

---

## 主程序集成

```cpp
// src/main.cpp
#include "codegen.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include <iostream>

using namespace kaleidoscope;

//===----------------------------------------------------------------------===//
// 主程序
//===----------------------------------------------------------------------===//

int main() {
    std::cout << "Kaleidoscope Compiler\n";
    std::cout << "Type 'def' to define a function, 'extern' to declare one.\n";
    std::cout << "Type any expression to evaluate it.\n\n";
    
    CodeGenerator CodeGen;
    
    // 预取第一个 Token
    std::cout << "ready> ";
    getNextToken();
    
    // 主循环
    while (true) {
        std::cout << "ready> ";
        switch (CurTok) {
        case tok_eof:
            return 0;
        case ';':  // 忽略顶层分号
            getNextToken();
            break;
        case tok_def: {
            auto FnAST = ParseDefinition();
            if (FnAST) {
                if (auto *FnIR = CodeGen.codegen(FnAST.get())) {
                    std::cout << "Read function definition:\n";
                    FnIR->print(llvm::outs());
                    std::cout << "\n";
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
                    std::cout << "Read extern:\n";
                    FnIR->print(llvm::outs());
                    std::cout << "\n";
                }
            } else {
                getNextToken();
            }
            break;
        }
        default: {
            // 顶层表达式
            auto FnAST = ParseTopLevelExpr();
            if (FnAST) {
                if (auto *FnIR = CodeGen.codegen(FnAST.get())) {
                    std::cout << "Read top-level expression:\n";
                    FnIR->print(llvm::outs());
                    std::cout << "\n";
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

### 输入
```
ready> def foo(x y) x + y;
```

### 输出
```llvm
Read function definition:
define double @foo(double %x, double %y) {
entry:
    %addtmp = fadd double %x, %y
    ret double %addtmp
}
```

### 输入
```
ready> extern sin(x);
```

### 输出
```llvm
Read extern:
declare double @sin(double %x)
```

### 输入
```
ready> 1 + 2 * 3;
```

### 输出
```llvm
Read top-level expression:
define double @__anon_expr() {
entry:
    %multmp = fmul double 2.000000e+00, 3.000000e+00
    %addtmp = fadd double 1.000000e+00, %multmp
    ret double %addtmp
}
```

---

## LLVM IR 语法参考

### 类型

| IR 类型 | 说明 | C 对应 |
|---------|------|--------|
| `i1` | 1位整数（布尔） | `_Bool` |
| `i32` | 32位整数 | `int` |
| `i64` | 64位整数 | `long` |
| `double` | 双精度浮点 | `double` |
| `float` | 单精度浮点 | `float` |
| `void` | 无类型 | `void` |

### 指令

| 指令 | 说明 | 示例 |
|------|------|------|
| `add` | 整数加法 | `%r = add i32 %a, %b` |
| `fadd` | 浮点加法 | `%r = fadd double %a, %b` |
| `sub`, `fsub` | 减法 | - |
| `mul`, `fmul` | 乘法 | - |
| `sdiv`, `udiv`, `fdiv` | 除法 | - |
| `icmp` | 整数比较 | `%r = icmp slt i32 %a, %b` |
| `fcmp` | 浮点比较 | `%r = fcmp olt double %a, %b` |
| `call` | 函数调用 | `%r = call double @foo(double %x)` |
| `ret` | 返回 | `ret double %r` |
| `br` | 分支 | `br label %next` |
| `phi` | φ节点（SSA 合并） | `%x = phi i32 [%a, %bb1], [%b, %bb2]` |

---

## 练习题

### 练习 1：添加取模运算符

在 Kaleidoscope 中添加 `%` 运算符：

1. 在词法分析器中识别 `%`
2. 在解析器中添加优先级
3. 在代码生成器中使用 `frem` 指令

```cpp
case '%':
    return Builder->CreateFRem(L, R, "modtmp");
```

### 练习 2：添加比较运算符

添加 `>`, `<=`, `>=` 比较运算符：

```cpp
case '>':
    L = Builder->CreateFCmpUGT(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, llvm::Type::getDoubleTy(*TheContext), "booltmp");
```

### 练习 3：添加幂运算

实现 `^` 幂运算符（调用标准库的 `pow` 函数）：

```cpp
case '^': {
    // 生成对 pow 函数的调用
    llvm::Function *PowF = getFunction("pow");
    if (!PowF) {
        // 声明 pow 函数
        // ...
    }
    return Builder->CreateCall(PowF, {L, R}, "powtmp");
}
```

---

## 常见问题

### Q: 为什么使用 `fadd` 而不是 `add`？

**A**: LLVM 区分整数和浮点运算。`add` 是整数加法，`fadd` 是浮点加法。Kaleidoscope 只有 `double` 类型，所以使用 `fadd`。

### Q: 为什么要将 `i1` 转换为 `double`？

**A**: Kaleidoscope 只有 `double` 类型。比较结果是布尔值（`i1`），我们需要将其转换为 `0.0` 或 `1.0` 才能在表达式中使用。

### Q: `eraseFromParent` 是什么？

**A**: 当函数生成失败时，我们需要清理已创建的函数，避免残留无效代码在模块中。`eraseFromParent` 从模块中删除该函数。

### Q: 如何调试 LLVM IR？

**A**: 使用 `llvm::errs()` 输出错误信息，或使用 `llvm::verifyFunction` 检查 IR 正确性。也可以使用 `opt` 工具：

```bash
opt -verify your_module.ll
```

---

## 下一步

完成了代码生成后，你已经可以将 Kaleidoscope 代码转换为 LLVM IR！

在 [下一章](04_jit_optimizer.md) 中，我们将学习：
- 如何使用 LLVM JIT 执行代码
- 如何添加优化 pass
- 如何实现交互式 REPL

---

## 验证清单

在进入下一章之前，确保：

- [ ] 你理解了 LLVM IR 的基本概念
- [ ] 能生成数字和变量表达式
- [ ] 能生成二元运算表达式
- [ ] 能生成函数调用
- [ ] 能生成函数定义
- [ ] 能查看和理解生成的 IR
- [ ] 测试程序运行正常
