---
title: 第九章 - 添加调试信息
chapter: 9
prerequisites: chapter 8
estimated_time: 3-4 hours
---

# 第九章：添加调试信息

## 学习目标

完成本章后，你将能够：

1. 理解 DWARF 调试格式
2. 添加源代码位置信息
3. 为函数和变量添加调试信息
4. 使用 GDB/LLDB 调试 Kaleidoscope 程序
5. 生成完整的调试构建

---

## 调试信息概述

### DWARF 格式

DWARF 是标准的调试信息格式，用于描述：
- 源文件和行号
- 变量名和类型
- 函数和作用域
- 调用栈信息

### 调试信息的作用

```
源代码                    调试信息映射
────────────────────────────────────────────────
def foo(x)          →    地址 0x1000, 文件 test.k, 行 1
  x + 1;            →    地址 0x1010, 文件 test.k, 行 2

调试器可以：
- 设置断点：break test.k:1
- 单步执行：step, next
- 查看变量：print x
- 显示调用栈：backtrace
```

---

## LLVM 调试信息 API

### 核心类

```cpp
// src/debug_info.hpp
#ifndef KALEIDOSCOPE_DEBUG_INFO_HPP
#define KALEIDOSCOPE_DEBUG_INFO_HPP

#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"

#include <memory>
#include <string>

namespace kaleidoscope {

class DebugInfoManager {
    std::unique_ptr<llvm::DIBuilder> DBuilder;
    
    // 编译单元
    llvm::DICompileUnit *TheCU;
    
    // 当前源文件
    llvm::DIFile *TheFile;
    
    // 当前函数
    llvm::DISubprogram *CurrentFunction;
    
    // 当前作用域
    llvm::DIScope *CurrentScope;
    
    // 源文件路径
    std::string SourceFile;
    
public:
    DebugInfoManager(llvm::Module *M, const std::string &SourceFile);
    
    // 创建调试信息
    llvm::DIFile *createFile(const std::string &Filename);
    llvm::DISubprogram *createFunction(
        llvm::Function *F, 
        const std::string &Name,
        unsigned LineNo
    );
    llvm::DIVariable *createParameterVariable(
        llvm::Function *F,
        const std::string &Name,
        unsigned ArgNo,
        unsigned LineNo
    );
    llvm::DIVariable *createLocalVariable(
        const std::string &Name,
        unsigned LineNo
    );
    
    // 设置位置
    void setLocation(llvm::IRBuilder<> &Builder, unsigned LineNo, unsigned ColNo = 0);
    
    // 完成调试信息
    void finalize();
    
    // 获取当前作用域
    llvm::DIScope *getCurrentScope() const { return CurrentScope; }
    
    // 设置/恢复作用域
    void pushScope(llvm::DIScope *Scope);
    void popScope();
    
    // 获取基本类型
    llvm::DIType *getDoubleType();
};

} // namespace kaleidoscope

#endif // KALEIDOSCOPE_DEBUG_INFO_HPP
```

### 调试信息管理器实现

```cpp
// src/debug_info.cpp
#include "debug_info.hpp"

namespace kaleidoscope {

DebugInfoManager::DebugInfoManager(llvm::Module *M, const std::string &SourceFile)
    : SourceFile(SourceFile) {
    
    // 创建 DIBuilder
    DBuilder = std::make_unique<llvm::DIBuilder>(*M);
    
    // 创建编译单元
    TheFile = DBuilder->createFile(
        SourceFile,
        llvm::StringRef(),  // 目录
        llvm::DIFile::ChecksumKind::None,
        llvm::None  // 校验和
    );
    
    TheCU = DBuilder->createCompileUnit(
        llvm::dwarf::DW_LANG_C,  // 语言 ID（没有专门的 Kaleidoscope ID）
        TheFile,
        "Kaleidoscope Compiler",
        false,           // 是否优化
        llvm::StringRef(),  // 标志
        0                // 运行时版本
    );
    
    CurrentScope = TheCU;
}

llvm::DIFile *DebugInfoManager::createFile(const std::string &Filename) {
    return DBuilder->createFile(Filename, llvm::StringRef());
}

llvm::DISubprogram *DebugInfoManager::createFunction(
    llvm::Function *F, 
    const std::string &Name,
    unsigned LineNo
) {
    // 创建函数类型
    llvm::DISubroutineType *FuncType = DBuilder->createSubroutineType(
        DBuilder->getOrCreateTypeArray({getDoubleType()})
    );
    
    // 创建函数
    llvm::DISubprogram *SP = DBuilder->createFunction(
        TheCU,           // 编译单元
        Name,            // 函数名
        Name,            // 链接名
        TheFile,         // 文件
        LineNo,          // 行号
        FuncType,        // 类型
        LineNo,          // 作用域行号
        llvm::DINode::FlagPrototyped,
        llvm::DISubprogram::SPFlagDefinition
    );
    
    F->setSubprogram(SP);
    CurrentFunction = SP;
    CurrentScope = SP;
    
    return SP;
}

llvm::DIVariable *DebugInfoManager::createParameterVariable(
    llvm::Function *F,
    const std::string &Name,
    unsigned ArgNo,
    unsigned LineNo
) {
    // 创建参数变量
    llvm::DILocalVariable *Var = DBuilder->createParameterVariable(
        CurrentFunction,  // 作用域
        Name,             // 名称
        ArgNo,            // 参数编号
        TheFile,          // 文件
        LineNo,           // 行号
        getDoubleType(),  // 类型
        true              // 始终保留
    );
    
    return Var;
}

llvm::DIVariable *DebugInfoManager::createLocalVariable(
    const std::string &Name,
    unsigned LineNo
) {
    llvm::DILocalVariable *Var = DBuilder->createAutoVariable(
        CurrentScope,     // 作用域
        Name,             // 名称
        TheFile,          // 文件
        LineNo,           // 行号
        getDoubleType()   // 类型
    );
    
    return Var;
}

void DebugInfoManager::setLocation(llvm::IRBuilder<> &Builder, 
                                    unsigned LineNo, 
                                    unsigned ColNo) {
    llvm::DILocation *Loc = llvm::DILocation::get(
        TheCU->getContext(),
        LineNo,
        ColNo,
        CurrentScope
    );
    
    Builder.SetCurrentDebugLocation(Loc);
}

void DebugInfoManager::finalize() {
    DBuilder->finalize();
}

llvm::DIType *DebugInfoManager::getDoubleType() {
    return DBuilder->createBasicType(
        "double",
        64,  // 大小（位）
        llvm::dwarf::DW_ATE_float
    );
}

} // namespace kaleidoscope
```

---

## 源代码位置追踪

### 词法分析器扩展

```cpp
// 在 lexer.hpp 中添加

struct SourceLocation {
    int Line;
    int Col;
};

static SourceLocation CurLoc;       // 当前位置
static SourceLocation LexLoc = {1, 0};  // 词法分析器位置

static int advance() {
    int LastChar = getchar();
    
    if (LastChar == '\n' || LastChar == '\r') {
        LexLoc.Line++;
        LexLoc.Col = 0;
    } else {
        LexLoc.Col++;
    }
    
    return LastChar;
}

static int gettok() {
    // 保存当前位置
    CurLoc = LexLoc;
    
    // ... 原有逻辑，使用 advance() 代替 getchar() ...
}
```

### AST 节点添加位置信息

```cpp
// 在 ast.hpp 中，为每个 AST 节点添加位置

class ExprAST {
    SourceLocation Loc;
    
public:
    ExprAST(SourceLocation Loc = CurLoc) : Loc(Loc) {}
    
    const SourceLocation &getLocation() const { return Loc; }
    
    virtual ~ExprAST() = default;
};

class NumberExprAST : public ExprAST {
    double Val;
    
public:
    NumberExprAST(double Val, SourceLocation Loc = CurLoc)
        : ExprAST(Loc), Val(Val) {}
    
    // ...
};

// 其他 AST 节点类似...
```

---

## 代码生成集成

### 更新代码生成器

```cpp
// 在 codegen.hpp 中添加

class CodeGenerator {
    // ... 现有成员 ...
    
    std::unique_ptr<DebugInfoManager> DebugInfo;
    
    bool EnableDebugInfo;
    
public:
    void enableDebugInfo(const std::string &SourceFile);
    void disableDebugInfo();
    
    // 添加调试位置
    void emitLocation(const ExprAST *Expr);
    void emitFunctionStart(llvm::Function *F, const PrototypeAST *Proto);
    void emitVariableDeclaration(llvm::Value *Alloca, 
                                   const std::string &Name,
                                   unsigned LineNo);
};
```

### 调试信息生成实现

```cpp
// 在 codegen.cpp 中添加

void CodeGenerator::enableDebugInfo(const std::string &SourceFile) {
    EnableDebugInfo = true;
    DebugInfo = std::make_unique<DebugInfoManager>(TheModule.get(), SourceFile);
}

void CodeGenerator::disableDebugInfo() {
    EnableDebugInfo = false;
    DebugInfo.reset();
}

void CodeGenerator::emitLocation(const ExprAST *Expr) {
    if (!EnableDebugInfo || !DebugInfo) {
        Builder->SetCurrentDebugLocation(llvm::DebugLoc());
        return;
    }
    
    DebugInfo->setLocation(
        *Builder,
        Expr->getLocation().Line,
        Expr->getLocation().Col
    );
}

void CodeGenerator::emitFunctionStart(llvm::Function *F, 
                                       const PrototypeAST *Proto) {
    if (!EnableDebugInfo || !DebugInfo) {
        return;
    }
    
    DebugInfo->createFunction(F, Proto->getName(), 1);
    
    // 为参数添加调试信息
    unsigned ArgNo = 1;
    for (const auto &ArgName : Proto->getArgs()) {
        llvm::DIVariable *Var = DebugInfo->createParameterVariable(
            F, ArgName, ArgNo, 1
        );
        
        // 发出变量声明
        llvm::Value *Alloca = NamedValues[ArgName];
        if (Alloca) {
            llvm::DILocation *Loc = llvm::DILocation::get(
                TheContext,
                1,  // 行号
                0,  // 列号
                DebugInfo->getCurrentScope()
            );
            
            DebugInfo->getDBuilder()->insertDeclare(
                Alloca,
                Var,
                DebugInfo->getDBuilder()->createExpression(),
                Loc,
                Builder->GetInsertBlock()
            );
        }
        
        ArgNo++;
    }
}

void CodeGenerator::emitVariableDeclaration(llvm::Value *Alloca,
                                             const std::string &Name,
                                             unsigned LineNo) {
    if (!EnableDebugInfo || !DebugInfo) {
        return;
    }
    
    llvm::DIVariable *Var = DebugInfo->createLocalVariable(Name, LineNo);
    
    llvm::DILocation *Loc = llvm::DILocation::get(
        TheContext,
        LineNo,
        0,
        DebugInfo->getCurrentScope()
    );
    
    DebugInfo->getDBuilder()->insertDeclare(
        Alloca,
        Var,
        DebugInfo->getDBuilder()->createExpression(),
        Loc,
        Builder->GetInsertBlock()
    );
}
```

---

## 使用调试器

### 编译带调试信息

```bash
# 编译
./kaleidoscope -g program.k -o program

# 或者分别编译和链接
./kaleidoscope -g -c program.k -o program.o
clang -g program.o -o program
```

### GDB 调试会话

```
$ gdb ./program
(gdb) break fib
Breakpoint 1 at 0x400550: file program.k, line 1.

(gdb) run
Starting program: ./program

Breakpoint 1, fib (n=10) at program.k:1
1     def fib(n)

(gdb) step
2       if n < 2 then

(gdb) print n
$1 = 10

(gdb) backtrace
#0  fib (n=10) at program.k:2
#1  main () at program.k:8
```

### LLDB 调试会话

```
$ lldb ./program
(lldb) breakpoint set --file program.k --line 1
Breakpoint 1: where = program`fib + 16 at program.k:1, address = 0x0000000100001550

(lldb) run
Process 12345 launched: './program' (x86_64)
Process 12345 stopped
* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1
    frame #0: 0x0000000100001550 program`fib(n=10) at program.k:1

(lldb) print n
(double) $0 = 10

(lldb) thread backtrace
* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1
  * frame #0: 0x0000000100001550 program`fib(n=10) at program.k:1
    frame #1: 0x0000000100001590 program`main at program.k:8
```

---

## 生成的 DWARF 信息

### 查看 DWARF 信息

```bash
# 使用 readelf 查看 DWARF 段
readelf --debug-dump=info program

# 或使用 dwarfdump
dwarfdump program
```

### 示例 DWARF 输出

```
.debug_info contents:

Compile Unit: length = 0x0000005b, format = DWARF32, version = 0x0004, 
              abbr_offset = 0x0000, addr_size = 0x08

DW_TAG_compile_unit
  DW_AT_name        ("program.k")
  DW_AT_producer    ("Kaleidoscope Compiler")
  DW_AT_language    (DW_LANG_C)

  DW_TAG_subprogram
    DW_AT_name      ("fib")
    DW_AT_low_pc    (0x0000000000400550)
    DW_AT_high_pc   (0x00000000004005d0)
    DW_AT_decl_file (0x01)
    DW_AT_decl_line (1)

    DW_TAG_formal_parameter
      DW_AT_name    ("n")
      DW_AT_type    (0x000000b7 "double")
      DW_AT_location    (DW_OP_fbreg -8)
```

---

## 调试优化代码

### 优化与调试信息

```cpp
// 在优化时保留调试信息
Optimizer::Optimizer() {
    // ... 其他 passes ...
    
    // mem2reg 会更新调试信息
    FPM.addPass(llvm::PromotePass());
    
    // 可以添加专门的调试信息更新 pass
}
```

### 调试优化代码

优化可能会改变代码结构，但调试信息会被保留：

```
源代码:
  x = 1;
  y = x + 1;
  return y;

优化后 IR:
  return 2;

调试器会正确显示:
  - 变量值
  - 源代码位置
  - 调用栈
```

---

## 练习题

### 练习 1：添加类型信息

为 Kaleidoscope 添加更丰富的类型信息：

```cpp
// 提示：创建类型描述
llvm::DIType *createPointerType(llvm::DIType *BaseType) {
    return DBuilder->createPointerType(BaseType, 64);
}
```

### 练习 2：添加作用域信息

为嵌套作用域添加调试信息：

```cpp
void enterScope(llvm::DILexicalBlock *Block) {
    ScopeStack.push_back(CurrentScope);
    CurrentScope = Block;
}

void exitScope() {
    CurrentScope = ScopeStack.back();
    ScopeStack.pop_back();
}
```

### 练习 3：添加表达式求值

在调试器中支持表达式求值：

```bash
# 在 GDB 中
(gdb) print fib(5)
$1 = 5
```

---

## 常见问题

### Q: 为什么调试信息这么大？

**A**: 调试信息包含：
- 所有变量名和类型
- 所有函数信息
- 行号表
- 地址映射

可以使用 `-g1` 或 `-g2` 减少调试信息量。

### Q: 如何调试优化后的代码？

**A**: 使用 `-Og` 优化级别，它会保留足够的调试信息。也可以使用 `-fno-inline` 禁止内联。

### Q: DWARF 版本有哪些？

**A**: 
- DWARF 2: 基本支持
- DWARF 3: 添加更多类型信息
- DWARF 4: 默认版本，广泛支持
- DWARF 5: 最新版本，更好的压缩

### Q: 如何处理多文件调试？

**A**: 为每个源文件创建 `DIFile`，并在编译单元中引用它们。

---

## 下一步

完成调试信息后，你的编译器已经可以生成完整的、可调试的程序了！

在 [下一章](10_conclusion.md) 中，我们将：
- 回顾整个项目
- 讨论扩展方向
- 提供更多学习资源

---

## 验证清单

在进入下一章之前，确保：

- [ ] 调试信息能正确生成
- [ ] GDB/LLDB 能正确显示源代码位置
- [ ] 能设置断点
- [ ] 能查看变量值
- [ ] 能显示调用栈
- [ ] 优化后调试信息仍然正确
