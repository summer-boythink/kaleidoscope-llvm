---
title: 第七章 - 扩展语言：可变变量
chapter: 7
prerequisites: chapter 6
estimated_time: 4-5 hours
---

# 第七章：扩展语言 - 可变变量

## 学习目标

完成本章后，你将能够：

1. 实现可变变量（赋值操作）
2. 理解 SSA 形式与可变变量的冲突
3. 使用 alloca/load/store 处理变量
4. 实现局部变量定义语法
5. 优化内存访问

---

## SSA 与可变变量的冲突

### 问题

SSA 要求每个变量只被赋值一次，但我们想要可变变量：

```kaleidoscope
def foo(x)
  x = x + 1;    # 重新赋值 x
  x;
```

这在 SSA 中是不允许的！

### 解决方案

1. **内存分配**：为变量分配栈空间
2. **load/store**：通过内存读写实现变量访问
3. **mem2reg 优化**：将内存访问优化回 SSA 寄存器

### 工作流程

```
源代码                    LLVM IR
────────────────────────────────────────────────
x = 1                     %x.addr = alloca double
                          store double 1.0, double* %x.addr

y = x                     %x.val = load double, double* %x.addr
                          %y.addr = alloca double
                          store double %x.val, double* %y.addr

x = x + 1                 %x.val2 = load double, double* %x.addr
                          %add = fadd double %x.val2, 1.0
                          store double %add, double* %x.addr

return x                  %x.val3 = load double, double* %x.addr
                          ret double %x.val3
```

---

## 变量语法

### var 语法

```kaleidoscope
# var 语句：定义局部变量
def foo()
  var a = 1 in     # 定义变量 a，初始值为 1
    a + 1;

# 多个变量
def bar()
  var a = 1, b = 2 in
    a + b;

# 嵌套作用域
def baz()
  var x = 1 in
    (var y = x in
       y + 1
    ) + x;
```

### 赋值语法

```kaleidoscope
# 赋值表达式
def counter()
  var count = 0 in
    (count = count + 1,
     count = count + 1,
     count);  # 返回 2
```

---

## AST 节点定义

### 变量表达式

```cpp
// 在 ast.hpp 中添加

/// VarExprAST - var 语句
/// 语法: var name1 = init1, name2 = init2 in body
class VarExprAST : public ExprAST {
    std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;
    std::unique_ptr<ExprAST> Body;

public:
    VarExprAST(
        std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames,
        std::unique_ptr<ExprAST> Body
    ) : VarNames(std::move(VarNames)), Body(std::move(Body)) {}
    
    const auto &getVarNames() const { return VarNames; }
    const ExprAST *getBody() const { return Body.get(); }
};
```

### 赋值表达式

```cpp
/// AssignExprAST - 赋值表达式
/// 语法: name = expr
class AssignExprAST : public ExprAST {
    std::string Name;
    std::unique_ptr<ExprAST> Value;

public:
    AssignExprAST(const std::string &Name, std::unique_ptr<ExprAST> Value)
        : Name(Name), Value(std::move(Value)) {}
    
    const std::string &getName() const { return Name; }
    const ExprAST *getValue() const { return Value.get(); }
};
```

---

## 解析器更新

### 解析 var 表达式

```cpp
/// ParseVarExpr - 解析 var 表达式
/// varexpr ::= 'var' identifier ('=' expression)? 
///              (',' identifier ('=' expression)?)* 'in' expression
std::unique_ptr<ExprAST> Parser::ParseVarExpr() {
    getNextToken();  // 消费 'var'
    
    std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;
    
    // 至少需要一个变量
    if (CurTok != tok_identifier) {
        return LogError("expected identifier after 'var'");
    }
    
    while (true) {
        std::string Name = IdentifierStr;
        getNextToken();  // 消费标识符
        
        // 可选的初始化
        std::unique_ptr<ExprAST> Init = nullptr;
        if (CurTok == '=') {
            getNextToken();  // 消费 '='
            Init = ParseExpression();
            if (!Init) {
                return nullptr;
            }
        }
        
        VarNames.push_back(std::make_pair(Name, std::move(Init)));
        
        // 检查是否有更多变量
        if (CurTok != ',') {
            break;
        }
        getNextToken();  // 消费 ','
        
        if (CurTok != tok_identifier) {
            return LogError("expected identifier after ','");
        }
    }
    
    // 检查 'in'
    if (CurTok != tok_in) {
        return LogError("expected 'in' after variable declarations");
    }
    getNextToken();  // 消费 'in'
    
    // 解析主体
    auto Body = ParseExpression();
    if (!Body) {
        return nullptr;
    }
    
    return std::make_unique<VarExprAST>(std::move(VarNames), std::move(Body));
}

// 在 ParsePrimary 中添加
case tok_var:
    return ParseVarExpr();
```

### 解析赋值表达式

```cpp
/// ParseAssignExpr - 解析赋值表达式
/// 注意：赋值是右结合的，优先级很低
std::unique_ptr<ExprAST> Parser::ParseAssignExpr(std::unique_ptr<ExprAST> LHS) {
    if (auto *VarExpr = dynamic_cast<VariableExprAST*>(LHS.get())) {
        getNextToken();  // 消费 '='
        
        auto RHS = ParseExpression();
        if (!RHS) {
            return nullptr;
        }
        
        return std::make_unique<AssignExprAST>(VarExpr->getName(), std::move(RHS));
    }
    
    return LogError("left side of assignment must be a variable");
}

// 在 ParseBinOpRHS 中添加对 '=' 的处理
std::unique_ptr<ExprAST> Parser::ParseBinOpRHS(int ExprPrec,
                                                std::unique_ptr<ExprAST> LHS) {
    // 赋值是右结合的，优先级为 2
    if (CurTok == '=') {
        return ParseAssignExpr(std::move(LHS));
    }
    
    // ... 原有逻辑 ...
}
```

---

## 代码生成

### 变量地址管理

```cpp
// 在 codegen.hpp 中添加

class CodeGenerator {
    // ... 现有成员 ...
    
    // NamedValues 现在存储变量的地址（指针），而不是值
    // std::unordered_map<std::string, llvm::Value *> NamedValues;
    
    // 创建 alloca 指令
    llvm::AllocaInst *CreateEntryBlockAlloca(llvm::Function *TheFunction,
                                              const std::string &VarName);
};
```

### Alloca 辅助函数

```cpp
// 在 codegen.cpp 中添加

llvm::AllocaInst *CodeGenerator::CreateEntryBlockAlloca(
    llvm::Function *TheFunction, 
    const std::string &VarName
) {
    // 创建 IRBuilder 指向函数入口
    llvm::IRBuilder<> TmpB(
        &TheFunction->getEntryBlock(),
        TheFunction->getEntryBlock().begin()
    );
    
    // 在入口块开头创建 alloca
    return TmpB.CreateAlloca(
        llvm::Type::getDoubleTy(*TheContext), 
        nullptr, 
        VarName
    );
}
```

### 更新变量表达式代码生成

```cpp
llvm::Value *CodeGenerator::codegen(const VariableExprAST *Expr) {
    // 查找变量地址
    llvm::Value *V = NamedValues[Expr->getName()];
    if (!V) {
        std::cerr << "Error: Unknown variable name: " << Expr->getName() << "\n";
        return nullptr;
    }
    
    // 从地址加载值
    return Builder->CreateLoad(
        llvm::Type::getDoubleTy(*TheContext), 
        V, 
        Expr->getName()
    );
}
```

### 赋值表达式代码生成

```cpp
llvm::Value *CodeGenerator::codegen(const AssignExprAST *Expr) {
    // 查找变量地址
    llvm::Value *Variable = NamedValues[Expr->getName()];
    if (!Variable) {
        std::cerr << "Error: Unknown variable name: " << Expr->getName() << "\n";
        return nullptr;
    }
    
    // 生成右值代码
    llvm::Value *Val = codegen(Expr->getValue());
    if (!Val) {
        return nullptr;
    }
    
    // 存储值
    Builder->CreateStore(Val, Variable);
    
    // 返回赋值的值
    return Val;
}
```

### var 表达式代码生成

```cpp
llvm::Value *CodeGenerator::codegen(const VarExprAST *Expr) {
    std::vector<llvm::AllocaInst *> OldBindings;
    
    llvm::Function *TheFunction = Builder->GetInsertBlock()->getParent();
    
    // 为每个变量注册
    for (const auto &[Name, Init] : Expr->getVarNames()) {
        // 创建 alloca
        llvm::AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Name);
        
        // 初始化
        llvm::Value *InitVal;
        if (Init) {
            InitVal = codegen(Init.get());
            if (!InitVal) {
                return nullptr;
            }
        } else {
            // 无初始化，默认为 0.0
            InitVal = llvm::ConstantFP::get(*TheContext, llvm::APFloat(0.0));
        }
        
        // 存储初始值
        Builder->CreateStore(InitVal, Alloca);
        
        // 保存旧绑定
        OldBindings.push_back(
            static_cast<llvm::AllocaInst*>(NamedValues[Name])
        );
        
        // 记住新绑定
        NamedValues[Name] = Alloca;
    }
    
    // 生成主体代码
    llvm::Value *BodyVal = codegen(Expr->getBody());
    if (!BodyVal) {
        return nullptr;
    }
    
    // 恢复旧绑定（作用域结束）
    for (size_t i = 0; i < Expr->getVarNames().size(); ++i) {
        const auto &[Name, _] = Expr->getVarNames()[i];
        if (OldBindings[i]) {
            NamedValues[Name] = OldBindings[i];
        } else {
            NamedValues.erase(Name);
        }
    }
    
    return BodyVal;
}
```

### 更新函数代码生成

```cpp
llvm::Function *CodeGenerator::codegen(const FunctionAST *Func) {
    // ... 创建函数 ...
    
    // 为参数创建 alloca
    for (auto &Arg : TheFunction->args()) {
        // 创建 alloca
        llvm::AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, 
                                                           Arg.getName().str());
        
        // 存储参数值
        Builder->CreateStore(&Arg, Alloca);
        
        // 添加到符号表
        NamedValues[std::string(Arg.getName())] = Alloca;
    }
    
    // ... 生成函数体 ...
}
```

---

## 内存优化

### mem2reg Pass

mem2reg 将栈内存访问优化回 SSA 寄存器：

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

### 添加 mem2reg Pass

```cpp
// 在 optimizer.cpp 中添加

#include "llvm/Transforms/Utils/Mem2Reg.h"

Optimizer::Optimizer() {
    // ... 其他 passes ...
    
    // 重要：promote 内存到寄存器
    FPM.addPass(llvm::PromotePass());  // mem2reg
    
    // ... 后续优化 ...
}
```

---

## 示例

### 简单变量

```kaleidoscope
def foo()
  var x = 1 in
    x + 1;
```

**未优化 IR**：
```llvm
define double @foo() {
entry:
    %x = alloca double
    store double 1.000000e+00, double* %x
    %x.val = load double, double* %x
    %add = fadd double %x.val, 1.000000e+00
    ret double %add
}
```

**优化后 IR**：
```llvm
define double @foo() {
entry:
    ret double 2.000000e+00
}
```

### 变量修改

```kaleidoscope
def counter()
  var count = 0 in
    (count = count + 1,
     count = count + 1,
     count);
```

**优化后 IR**：
```llvm
define double @counter() {
entry:
    ret double 2.000000e+00
}
```

### 嵌套作用域

```kaleidoscope
def nested()
  var x = 1 in
    (var y = 2 in
       x + y
    ) + x;
```

**结果**：`1 + 2 + 1 = 4`

---

## 序列表达式

### 实现逗号运算符

```kaleidoscope
# (a, b, c) 返回 c 的值
def seq(a b c) c;
```

我们可以添加内置的逗号运算符支持：

```cpp
// 在解析器中添加
// expression ::= primary (',' expression)*

std::unique_ptr<ExprAST> Parser::ParseSequenceExpr() {
    auto First = ParseExpression();
    if (!First) return nullptr;
    
    if (CurTok != ',') return First;
    
    std::vector<std::unique_ptr<ExprAST>> Exprs;
    Exprs.push_back(std::move(First));
    
    while (CurTok == ',') {
        getNextToken();  // 消费 ','
        auto E = ParseExpression();
        if (!E) return nullptr;
        Exprs.push_back(std::move(E));
    }
    
    return std::make_unique<SequenceExprAST>(std::move(Exprs));
}
```

---

## 练习题

### 练习 1：实现 +=, -=, *=, /= 运算符

```kaleidoscope
def foo(x)
  var y = x in
    (y += 1,  # y = y + 1
     y);
```

### 练习 2：实现自增/自减运算符

```kaleidoscope
def foo(x)
  var y = x in
    (y++,  # y = y + 1, 返回旧值
     y--,  # y = y - 1, 返回旧值
     y);
```

### 练习 3：实现全局变量

添加全局变量支持：

```kaleidoscope
var g_count = 0;

def increment()
  g_count = g_count + 1;
```

提示：使用 `TheModule->getOrInsertGlobal()` 创建全局变量。

---

## 常见问题

### Q: 为什么不直接使用 SSA 变量？

**A**: SSA 要求每个变量只赋值一次，这与可变变量冲突。使用内存（alloca/load/store）是一个通用的解决方案，LLVM 优化器会将简单的内存访问优化回寄存器。

### Q: alloca 会有性能问题吗？

**A**: 不会。mem2reg 优化会将简单的 alloca 消除。只有真正需要内存的情况（如变量的地址被取走）才会保留 alloca。

### Q: 什么是 "promote memory to register"？

**A**: 这是将栈内存访问转换为 SSA 寄存器访问的优化。它分析 alloca 的使用，确定哪些可以安全地转换为寄存器，然后用 SSA 值替换 load/store。

### Q: 为什么要在入口块创建 alloca？

**A**: alloca 必须在函数入口执行，不能在条件分支中创建。将所有 alloca 放在入口块开头可以确保它们在函数开始时就分配好。

---

## 下一步

完成可变变量后，你的语言已经可以处理大多数编程任务了！

在 [下一章](08_object_code.md) 中，我们将学习：
- 如何编译到目标代码（.o 文件）
- 如何链接生成可执行文件
- 如何生成 LLVM 字节码

---

## 验证清单

在进入下一章之前，确保：

- [ ] 你理解了 SSA 与可变变量的冲突
- [ ] var 语句能正确工作
- [ ] 赋值操作能正确工作
- [ ] 作用域正确处理
- [ ] mem2reg 优化正常工作
- [ ] 测试程序运行正常
