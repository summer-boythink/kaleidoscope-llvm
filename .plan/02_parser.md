---
title: 第二章 - 语法分析器 (Parser) 和 AST
chapter: 2
prerequisites: chapter 1
estimated_time: 3-4 hours
---

# 第二章：语法分析器 (Parser) 和 AST

## 学习目标

完成本章后，你将能够：

1. 理解抽象语法树 (AST) 的作用
2. 设计 AST 节点类层次结构
3. 实现递归下降语法分析器
4. 解析表达式、函数定义和函数调用
5. 处理运算符优先级

---

## 什么是抽象语法树 (AST)？

### 概念理解

AST 是源代码的树形表示，它捕获了代码的**结构**，但忽略了不必要的语法细节。

### 对比：解析树 vs AST

假设我们有表达式 `1 + 2 * 3`：

**解析树 (Parse Tree)**：包含所有语法细节
```
        表达式
       /       \
    项         +  项
    |             /   \
   因子        因子  *  因子
    |           |        |
   数字1       数字2    数字3
```

**抽象语法树 (AST)**：更简洁
```
      +
     / \
    1   *
       / \
      2   3
```

AST 更简洁，因为它抽象掉了语法细节（如 "项"、"因子" 这些语法概念）。

---

## Kaleidoscope 的 AST 结构

### 节点类型

Kaleidoscope 需要以下 AST 节点类型：

| 节点类型 | 说明 | 示例 |
|---------|------|------|
| `NumberExprAST` | 数字字面量 | `1.0`, `3.14` |
| `VariableExprAST` | 变量引用 | `x`, `foo` |
| `BinaryExprAST` | 二元运算 | `a + b`, `x * 2` |
| `UnaryExprAST` | 一元运算 | `-x`, `!flag` |
| `CallExprAST` | 函数调用 | `sin(x)`, `add(1, 2)` |
| `PrototypeAST` | 函数原型 | `foo(x y)` |
| `FunctionAST` | 函数定义 | `def foo(x) x + 1` |

### 类图

```
         ExprAST (表达式基类)
            │
    ┌───────┼───────┬───────────┐
    │       │       │           │
NumberExprAST VariableExprAST BinaryExprAST UnaryExprAST CallExprAST

PrototypeAST (函数原型)    FunctionAST (函数定义)
         │                        │
         └─────── 包含 ────────────┘
```

---

## AST 节点定义

### 基类和表达式节点

```cpp
// src/ast.hpp
#ifndef KALEIDOSCOPE_AST_HPP
#define KALEIDOSCOPE_AST_HPP

#include "lexer.hpp"
#include <memory>
#include <string>
#include <vector>

namespace kaleidoscope {

//===----------------------------------------------------------------------===//
// AST 基类
//===----------------------------------------------------------------------===//

/// ExprAST - 所有表达式节点的基类
class ExprAST {
public:
    virtual ~ExprAST() = default;
    
    // 注意：我们会在下一章添加 codegen() 方法
    // virtual llvm::Value *codegen() = 0;
};

/// NumberExprAST - 数字字面量表达式，如 "1.0"
class NumberExprAST : public ExprAST {
    double Val;

public:
    NumberExprAST(double Val) : Val(Val) {}
    
    double getValue() const { return Val; }
};

/// VariableExprAST - 变量引用表达式，如 "x"
class VariableExprAST : public ExprAST {
    std::string Name;

public:
    VariableExprAST(const std::string &Name) : Name(Name) {}
    
    const std::string &getName() const { return Name; }
};

/// BinaryExprAST - 二元运算表达式，如 "a + b"
class BinaryExprAST : public ExprAST {
    char Op;                        // 运算符，如 '+', '-', '*', '/'
    std::unique_ptr<ExprAST> LHS;   // 左操作数
    std::unique_ptr<ExprAST> RHS;   // 右操作数

public:
    BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                  std::unique_ptr<ExprAST> RHS)
        : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
    
    char getOp() const { return Op; }
    const ExprAST *getLHS() const { return LHS.get(); }
    const ExprAST *getRHS() const { return RHS.get(); }
};

/// UnaryExprAST - 一元运算表达式，如 "-x"
/// 注意：这是第六章添加的，这里提前定义
class UnaryExprAST : public ExprAST {
    char Opcode;                         // 运算符，如 '-', '!'
    std::unique_ptr<ExprAST> Operand;    // 操作数

public:
    UnaryExprAST(char Opcode, std::unique_ptr<ExprAST> Operand)
        : Opcode(Opcode), Operand(std::move(Operand)) {}
    
    char getOpcode() const { return Opcode; }
    const ExprAST *getOperand() const { return Operand.get(); }
};

/// CallExprAST - 函数调用表达式，如 "sin(1.0)"
class CallExprAST : public ExprAST {
    std::string Callee;                           // 被调用的函数名
    std::vector<std::unique_ptr<ExprAST>> Args;   // 参数列表

public:
    CallExprAST(const std::string &Callee,
                std::vector<std::unique_ptr<ExprAST>> Args)
        : Callee(Callee), Args(std::move(Args)) {}
    
    const std::string &getCallee() const { return Callee; }
    const std::vector<std::unique_ptr<ExprAST>> &getArgs() const { return Args; }
};

//===----------------------------------------------------------------------===//
// 函数定义相关
//===----------------------------------------------------------------------===//

/// PrototypeAST - 函数原型（函数签名）
/// 表示函数的名称和参数名列表，如 "foo(x y z)"
class PrototypeAST {
    std::string Name;                // 函数名
    std::vector<std::string> Args;   // 参数名列表

public:
    PrototypeAST(const std::string &Name, std::vector<std::string> Args)
        : Name(Name), Args(std::move(Args)) {}
    
    const std::string &getName() const { return Name; }
    const std::vector<std::string> &getArgs() const { return Args; }
};

/// FunctionAST - 完整的函数定义
/// 包含原型和函数体
class FunctionAST {
    std::unique_ptr<PrototypeAST> Proto;   // 函数原型
    std::unique_ptr<ExprAST> Body;          // 函数体

public:
    FunctionAST(std::unique_ptr<PrototypeAST> Proto,
                std::unique_ptr<ExprAST> Body)
        : Proto(std::move(Proto)), Body(std::move(Body)) {}
    
    const PrototypeAST *getProto() const { return Proto.get(); }
    const ExprAST *getBody() const { return Body.get(); }
};

} // namespace kaleidoscope

#endif // KALEIDOSCOPE_AST_HPP
```

---

## 语法分析器实现

### 什么是递归下降分析？

递归下降分析是一种直观的解析技术：

- 每个语法规则对应一个函数
- 函数之间互相调用（递归）来处理嵌套结构
- 从最高层次的规则开始，逐步下降到细节

### Kaleidoscope 语法（简化版）

```
顶层定义:
    definition  ::= 'def' prototype expression
    external    ::= 'extern' prototype
    toplevel    ::= expression | definition | external

表达式:
    primary     ::= identifierexpr | numberexpr | parenexpr
    expression  ::= primary binoprhs

详细语法:
    identifierexpr ::= identifier | identifier '(' expression* ')'
    numberexpr     ::= number
    parenexpr      ::= '(' expression ')'
    binoprhs       ::= ('+' primary)*
```

### 解析器框架

```cpp
// src/parser.hpp
#ifndef KALEIDOSCOPE_PARSER_HPP
#define KALEIDOSCOPE_PARSER_HPP

#include "ast.hpp"
#include "lexer.hpp"
#include <memory>
#include <utility>

namespace kaleidoscope {

//===----------------------------------------------------------------------===//
// 解析器类
//===----------------------------------------------------------------------===//

class Parser {
    int CurTok;                 // 当前 Token
    int getNextToken();         // 获取下一个 Token
    
    // 错误处理辅助函数
    std::unique_ptr<ExprAST> LogError(const char *Str);
    std::unique_ptr<PrototypeAST> LogErrorP(const char *Str);
    
    // 表达式解析
    std::unique_ptr<ExprAST> ParseExpression();
    std::unique_ptr<ExprAST> ParsePrimary();
    std::unique_ptr<ExprAST> ParseNumberExpr();
    std::unique_ptr<ExprAST> ParseParenExpr();
    std::unique_ptr<ExprAST> ParseIdentifierExpr();
    
    // 运算符优先级解析
    std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                           std::unique_ptr<ExprAST> LHS);
    int GetTokPrecedence();
    
    // 函数解析
    std::unique_ptr<PrototypeAST> ParsePrototype();
    std::unique_ptr<FunctionAST> ParseDefinition();
    std::unique_ptr<PrototypeAST> ParseExtern();
    std::unique_ptr<FunctionAST> ParseTopLevelExpr();
    
public:
    // 主解析循环
    void MainLoop();
};

} // namespace kaleidoscope

#endif // KALEIDOSCOPE_PARSER_HPP
```

---

## 详细解析器实现

### 1. 基本框架和错误处理

```cpp
// src/parser.cpp
#include "parser.hpp"
#include <iostream>
#include <map>

namespace kaleidoscope {

/// getNextToken - 从词法分析器获取下一个 Token
int Parser::getNextToken() {
    return CurTok = gettok();
}

/// LogError - 打印错误信息并返回 nullptr
std::unique_ptr<ExprAST> Parser::LogError(const char *Str) {
    std::cerr << "Error: " << Str << "\n";
    return nullptr;
}

/// LogErrorP - 打印错误信息并返回 nullptr（用于原型）
std::unique_ptr<PrototypeAST> Parser::LogErrorP(const char *Str) {
    LogError(Str);
    return nullptr;
}
```

### 2. 数字表达式解析

```cpp
/// ParseNumberExpr - 解析数字字面量
/// numberexpr ::= number
std::unique_ptr<ExprAST> Parser::ParseNumberExpr() {
    auto Result = std::make_unique<NumberExprAST>(NumVal);
    getNextToken();  // 消费数字
    return Result;
}
```

**解析过程示例**：
```
输入: "123"
Token: tok_number, NumVal = 123

1. 创建 NumberExprAST(123)
2. 获取下一个 Token
3. 返回 AST 节点
```

### 3. 括号表达式解析

```cpp
/// ParseParenExpr - 解析括号表达式
/// parenexpr ::= '(' expression ')'
std::unique_ptr<ExprAST> Parser::ParseParenExpr() {
    getNextToken();  // 消费 '('
    
    auto V = ParseExpression();  // 解析括号内的表达式
    if (!V) {
        return nullptr;
    }
    
    if (CurTok != ')') {
        return LogError("expected ')'");
    }
    
    getNextToken();  // 消费 ')'
    return V;
}
```

**解析过程示例**：
```
输入: "(1 + 2)"
Token: '(', tok_number(1), '+', tok_number(2), ')'

1. 消费 '('
2. 递归调用 ParseExpression()
   - 解析 "1 + 2"
   - 返回 BinaryExprAST
3. 检查 ')'
4. 消费 ')'
5. 返回括号内的表达式
```

### 4. 标识符表达式解析

```cpp
/// ParseIdentifierExpr - 解析标识符（变量或函数调用）
/// identifierexpr ::= identifier | identifier '(' expression* ')'
std::unique_ptr<ExprAST> Parser::ParseIdentifierExpr() {
    std::string IdName = IdentifierStr;
    getNextToken();  // 消费标识符
    
    if (CurTok != '(') {
        // 简单变量引用，如 "x"
        return std::make_unique<VariableExprAST>(IdName);
    }
    
    // 函数调用，如 "foo(1, 2)"
    getNextToken();  // 消费 '('
    std::vector<std::unique_ptr<ExprAST>> Args;
    
    if (CurTok != ')') {
        while (true) {
            if (auto Arg = ParseExpression()) {
                Args.push_back(std::move(Arg));
            } else {
                return nullptr;
            }
            
            if (CurTok == ')') {
                break;
            }
            
            if (CurTok != ',') {
                return LogError("Expected ')' or ',' in argument list");
            }
            
            getNextToken();  // 消费 ','
        }
    }
    
    getNextToken();  // 消费 ')'
    
    return std::make_unique<CallExprAST>(IdName, std::move(Args));
}
```

**解析过程示例**：
```
输入: "add(1, x)"
Token: tok_identifier(add), '(', tok_number(1), ',', tok_identifier(x), ')'

1. 保存标识符名 "add"
2. 消费 'add'
3. 看到 '('，知道是函数调用
4. 消费 '('
5. 解析参数列表:
   - 解析表达式 "1" → NumberExprAST
   - 看到 ',' 继续
   - 解析表达式 "x" → VariableExprAST
   - 看到 ')' 结束
6. 消费 ')'
7. 创建 CallExprAST("add", [NumberExprAST(1), VariableExprAST(x)])
```

### 5. 基本表达式入口

```cpp
/// ParsePrimary - 解析基本表达式
/// primary ::= identifierexpr | numberexpr | parenexpr
std::unique_ptr<ExprAST> Parser::ParsePrimary() {
    switch (CurTok) {
    case tok_identifier:
        return ParseIdentifierExpr();
    case tok_number:
        return ParseNumberExpr();
    case '(':
        return ParseParenExpr();
    default:
        return LogError("unknown token when expecting an expression");
    }
}
```

---

## 运算符优先级解析

### 为什么需要处理优先级？

考虑表达式：`1 + 2 * 3`

正确的解析应该是 `1 + (2 * 3)`，因为 `*` 优先级高于 `+`。

### 运算符优先级表

```cpp
/// GetTokPrecedence - 获取当前运算符的优先级
int Parser::GetTokPrecedence() {
    // 定义运算符优先级（数值越大，优先级越高）
    static std::map<char, int> BinopPrecedence = {
        {'<', 10},
        {'+', 20},
        {'-', 20},
        {'*', 40},
        {'/', 40},
    };
    
    // 确保 Current Token 是运算符
    if (CurTok < 0 || CurTok > 127) {
        return -1;
    }
    
    auto it = BinopPrecedence.find(CurTok);
    if (it == BinopPrecedence.end()) {
        return -1;
    }
    
    return it->second;
}
```

### 优先级解析算法

```cpp
/// ParseBinOpRHS - 解析二元运算符右侧
/// binoprhs ::= ('+' primary)*
std::unique_ptr<ExprAST> Parser::ParseBinOpRHS(int ExprPrec,
                                                std::unique_ptr<ExprAST> LHS) {
    while (true) {
        int TokPrec = GetTokPrecedence();
        
        // 如果当前运算符优先级小于表达式优先级，返回
        if (TokPrec < ExprPrec) {
            return LHS;
        }
        
        // 否则，这个运算符属于这个表达式
        int BinOp = CurTok;
        getNextToken();  // 消费运算符
        
        // 解析运算符右侧的基本表达式
        auto RHS = ParsePrimary();
        if (!RHS) {
            return nullptr;
        }
        
        // 检查下一个运算符
        int NextPrec = GetTokPrecedence();
        
        // 如果下一个运算符优先级更高，先处理它
        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
            if (!RHS) {
                return nullptr;
            }
        }
        
        // 合并 LHS 和 RHS
        LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS),
                                               std::move(RHS));
    }
}
```

### 完整的表达式解析

```cpp
/// ParseExpression - 解析表达式
/// expression ::= primary binoprhs
std::unique_ptr<ExprAST> Parser::ParseExpression() {
    auto LHS = ParsePrimary();
    if (!LHS) {
        return nullptr;
    }
    
    return ParseBinOpRHS(0, std::move(LHS));
}
```

### 解析示例：`1 + 2 * 3`

```
步骤 1: ParseExpression() 调用
  └─ ParsePrimary() 返回 NumberExprAST(1)
  └─ ParseBinOpRHS(0, LHS=1) 开始

步骤 2: 看到 '+', 优先级 = 20
  └─ 20 >= 0, 继续处理
  └─ 消费 '+'
  └─ ParsePrimary() 返回 NumberExprAST(2)
  
步骤 3: 看到 '*', 优先级 = 40
  └─ 40 > 20 (当前运算符优先级)
  └─ 递归调用 ParseBinOpRHS(21, RHS=2)
    └─ 看到 '*', 优先级 = 40
    └─ 40 >= 21, 继续处理
    └─ 消费 '*'
    └─ ParsePrimary() 返回 NumberExprAST(3)
    └─ 下一个 Token 不是运算符
    └─ 返回 BinaryExprAST(*, 2, 3)
  └─ RHS 现在是 BinaryExprAST(*, 2, 3)
  
步骤 4: 合并
  └─ 创建 BinaryExprAST(+, 1, BinaryExprAST(*, 2, 3))

最终 AST:
      +
     / \
    1   *
       / \
      2   3
```

---

## 函数定义解析

### 函数原型解析

```cpp
/// ParsePrototype - 解析函数原型
/// prototype ::= identifier '(' identifier* ')'
std::unique_ptr<PrototypeAST> Parser::ParsePrototype() {
    if (CurTok != tok_identifier) {
        return LogErrorP("Expected function name in prototype");
    }
    
    std::string FnName = IdentifierStr;
    getNextToken();  // 消费函数名
    
    if (CurTok != '(') {
        return LogErrorP("Expected '(' in prototype");
    }
    
    // 解析参数列表
    std::vector<std::string> ArgNames;
    while (getNextToken() == tok_identifier) {
        ArgNames.push_back(IdentifierStr);
    }
    
    if (CurTok != ')') {
        return LogErrorP("Expected ')' in prototype");
    }
    
    getNextToken();  // 消费 ')'
    
    return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}
```

### 函数定义解析

```cpp
/// ParseDefinition - 解析函数定义
/// definition ::= 'def' prototype expression
std::unique_ptr<FunctionAST> Parser::ParseDefinition() {
    getNextToken();  // 消费 'def'
    
    auto Proto = ParsePrototype();
    if (!Proto) {
        return nullptr;
    }
    
    if (auto E = ParseExpression()) {
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    
    return nullptr;
}
```

### 外部声明解析

```cpp
/// ParseExtern - 解析外部声明
/// external ::= 'extern' prototype
std::unique_ptr<PrototypeAST> Parser::ParseExtern() {
    getNextToken();  // 消费 'extern'
    return ParsePrototype();
}
```

### 顶层表达式解析

```cpp
/// ParseTopLevelExpr - 解析顶层表达式
/// toplevelexpr ::= expression
std::unique_ptr<FunctionAST> Parser::ParseTopLevelExpr() {
    if (auto E = ParseExpression()) {
        // 创建一个匿名的原型 "__anon_expr"
        auto Proto = std::make_unique<PrototypeAST>("__anon_expr",
                                                     std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}
```

---

## 主解析循环

```cpp
/// MainLoop - 主解析循环
void Parser::MainLoop() {
    while (true) {
        std::cout << "ready> ";
        switch (CurTok) {
        case tok_eof:
            return;
        case ';':  // 忽略顶层分号
            getNextToken();
            break;
        case tok_def:
            if (auto FnAST = ParseDefinition()) {
                std::cout << "Parsed a function definition.\n";
                // 后续会添加代码生成
            } else {
                // 跳过错误恢复
                getNextToken();
            }
            break;
        case tok_extern:
            if (auto ProtoAST = ParseExtern()) {
                std::cout << "Parsed an extern\n";
                // 后续会添加代码生成
            } else {
                // 跳过错误恢复
                getNextToken();
            }
            break;
        default:
            // 顶层表达式
            if (auto FnAST = ParseTopLevelExpr()) {
                std::cout << "Parsed a top-level expr\n";
                // 后续会添加代码生成
            } else {
                // 跳过错误恢复
                getNextToken();
            }
            break;
        }
    }
}
```

---

## 完整示例程序

```cpp
// src/main_parser_test.cpp
#include "lexer.hpp"
#include "parser.hpp"
#include <iostream>

using namespace kaleidoscope;

int main() {
    std::cout << "Kaleidoscope Parser Test\n";
    
    // 安装标准二元运算符优先级
    // 1 + 2 + 3 会解析为 (1 + 2) + 3
    
    std::cout << "ready> ";
    getNextToken();  // 预取第一个 Token
    
    Parser parser;
    parser.MainLoop();
    
    return 0;
}
```

### 测试输入和预期输出

```
ready> def foo(x y) x + y * 2;
Parsed a function definition.
ready> def bar(a b c) a + b + c;
Parsed a function definition.
ready> extern sin(x);
Parsed an extern
ready> 1 + 2 * 3;
Parsed a top-level expr
ready> (1 + 2) * 3;
Parsed a top-level expr
ready> ^D
```

---

## 练习题

### 练习 1：添加比较运算符

添加 `>`, `==`, `!=` 等比较运算符：

1. 在词法分析器中处理这些运算符
2. 在 `GetTokPrecedence` 中添加优先级
3. 测试新的运算符

### 练习 2：添加一元运算符

添加一元负号 `-x` 和一元正号 `+x`：

```cpp
// 提示：修改 ParsePrimary 处理前缀运算符
std::unique_ptr<ExprAST> Parser::ParsePrimary() {
    switch (CurTok) {
    case '-':
        getNextToken();
        if (auto Operand = ParsePrimary()) {
            return std::make_unique<UnaryExprAST>('-', std::move(Operand));
        }
        return nullptr;
    // ... 其他 case
    }
}
```

### 练习 3：打印 AST

实现一个 AST 打印函数：

```cpp
// 提示：使用访问者模式
void PrintAST(const ExprAST *Expr, int indent = 0) {
    // 根据类型打印不同内容
}
```

---

## 常见问题

### Q: 为什么要用 `std::unique_ptr`？

**A**: `unique_ptr` 提供了自动内存管理。当 AST 节点不再需要时，它会自动释放内存。这避免了手动 `delete` 带来的内存泄漏风险。

### Q: 递归下降解析器的限制是什么？

**A**: 递归下降解析器最适合 LL(1) 文法（向前看一个 Token 就能决定解析路径）。对于更复杂的语法（如 C++），可能需要更强大的解析器（如 LR 解析器）。

### Q: 如何处理语法错误？

**A**: 当前实现简单地返回 `nullptr`。更好的做法是：
1. 记录错误位置（行号、列号）
2. 提供有意义的错误信息
3. 尝试错误恢复，继续解析

---

## 下一步

完成了语法分析器后，你已经构建了编译器的前端！

在 [下一章](03_codegen.md) 中，我们将学习：
- LLVM 的基本概念
- 如何为每个 AST 节点生成 LLVM IR
- 如何生成可执行的机器码

---

## 验证清单

在进入下一章之前，确保：

- [ ] 你理解了 AST 的作用和结构
- [ ] 解析器能正确解析数字表达式
- [ ] 解析器能正确解析变量和函数调用
- [ ] 运算符优先级处理正确
- [ ] 能解析函数定义和外部声明
- [ ] 测试程序运行正常
