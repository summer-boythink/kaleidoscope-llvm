---
title: 第六章 - 扩展语言：用户定义运算符
chapter: 6
prerequisites: chapter 5
estimated_time: 2-3 hours
---

# 第六章：扩展语言 - 用户定义运算符

## 学习目标

完成本章后，你将能够：

1. 实现用户定义的二元运算符
2. 实现用户定义的一元运算符
3. 设置运算符优先级
4. 理解运算符重载的设计考量

---

## 运算符扩展概述

### 为什么需要用户定义运算符？

1. **语言扩展性**：用户可以根据需求添加新运算符
2. **领域特定语言**：为特定领域创建专用语法
3. **代码可读性**：用运算符替代函数调用，提高可读性

### 示例

```kaleidoscope
# 定义一个新的二元运算符 "++"（连接字符串/数组）
def binary ++ 10 (a b) concat(a, b);

# 定义一个一元运算符 "!"（逻辑非）
def unary ! (v) if v then 0 else 1;

# 使用自定义运算符
1 ++ 2 ++ 3;   # 使用 binary ++
!1;           # 使用 unary !
```

### 运算符定义语法

```
二元运算符: def binary <op> <precedence> (<args>) <body>
一元运算符: def unary <op> (<args>) <body>

例如:
def binary | 10 (a b) ...    # 优先级为 10 的 | 运算符
def unary - (v) ...          # 一元负号
```

---

## 二元运算符实现

### 1. 更新 PrototypeAST

```cpp
// 更新 ast.hpp 中的 PrototypeAST

class PrototypeAST {
    std::string Name;
    std::vector<std::string> Args;
    
    // 新增：是否为运算符及优先级
    bool IsOperator;
    unsigned Precedence;  // 0 = 最高优先级

public:
    PrototypeAST(const std::string &Name, 
                 std::vector<std::string> Args,
                 bool IsOperator = false, 
                 unsigned Precedence = 0)
        : Name(Name), Args(std::move(Args)), 
          IsOperator(IsOperator), Precedence(Precedence) {}
    
    const std::string &getName() const { return Name; }
    const std::vector<std::string> &getArgs() const { return Args; }
    
    bool isOperator() const { return IsOperator; }
    unsigned getPrecedence() const { return Precedence; }
    
    // 获取运算符字符
    std::string getOperatorName() const {
        assert(IsOperator);
        return Name;
    }
    
    // 获取二元运算符名称
    // 例如 "binary+" 返回 "+"
    std::string getBinaryOperator() const {
        if (Name.length() > 6 && Name.substr(0, 6) == "binary") {
            return Name.substr(6);
        }
        return "";
    }
    
    // 获取一元运算符名称
    std::string getUnaryOperator() const {
        if (Name.length() > 5 && Name.substr(0, 5) == "unary") {
            return Name.substr(5);
        }
        return "";
    }
};
```

### 2. 更新解析器

```cpp
// 在 parser.cpp 中更新 ParsePrototype

std::unique_ptr<PrototypeAST> Parser::ParsePrototype() {
    std::string FnName;
    unsigned Kind = 0;         // 0 = 普通标识符, 1 = 一元, 2 = 二元
    unsigned BinaryPrecedence = 30;
    
    switch (CurTok) {
    default:
        return LogErrorP("Expected function name in prototype");
    case tok_identifier:
        FnName = IdentifierStr;
        Kind = 0;
        getNextToken();
        break;
    case tok_unary:
        getNextToken();
        if (!isascii(CurTok)) {
            return LogErrorP("Expected unary operator");
        }
        FnName = "unary";
        FnName += (char)CurTok;
        Kind = 1;
        getNextToken();
        break;
    case tok_binary:
        getNextToken();
        if (!isascii(CurTok)) {
            return LogErrorP("Expected binary operator");
        }
        FnName = "binary";
        FnName += (char)CurTok;
        Kind = 2;
        getNextToken();
        
        // 读取优先级
        if (CurTok == tok_number) {
            if (NumVal < 1 || NumVal > 100) {
                return LogErrorP("Invalid precedence: must be 1..100");
            }
            BinaryPrecedence = (unsigned)NumVal;
            getNextToken();
        }
        break;
    }
    
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
    
    // 验证参数数量
    if (Kind == 1 && ArgNames.size() != 1) {
        return LogErrorP("Invalid number of operands for unary operator");
    }
    if (Kind == 2 && ArgNames.size() != 2) {
        return LogErrorP("Invalid number of operands for binary operator");
    }
    
    getNextToken();  // 消费 ')'
    
    return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames),
                                           Kind != 0, BinaryPrecedence);
}
```

### 3. 更新运算符优先级

```cpp
// 在 parser.cpp 中更新 GetTokPrecedence

int Parser::GetTokPrecedence() {
    if (!isascii(CurTok)) {
        return -1;
    }
    
    // 检查是否是用户定义的运算符
    if (CurTok == tok_identifier) {
        auto it = BinopPrecedence.find(IdentifierStr);
        if (it != BinopPrecedence.end()) {
            return it->second;
        }
    }
    
    // 内置运算符优先级
    int TokPrec = -1;
    switch (CurTok) {
    case '<': TokPrec = 10; break;
    case '+': TokPrec = 20; break;
    case '-': TokPrec = 20; break;
    case '*': TokPrec = 40; break;
    case '/': TokPrec = 40; break;
    default:
        // 检查是否是用户定义的二元运算符
        if (auto *P = FunctionProtos["binary" + std::string(1, CurTok)]) {
            TokPrec = P->getPrecedence();
        }
        break;
    }
    
    return TokPrec;
}

// 动态添加用户运算符优先级
void Parser::installBinaryOperator(char Op, unsigned Precedence) {
    BinopPrecedence[std::string(1, Op)] = Precedence;
}
```

### 4. 代码生成更新

```cpp
// 在 codegen.cpp 中更新函数原型生成

llvm::Function *CodeGenerator::codegen(const PrototypeAST *Proto) {
    // ... 现有代码 ...
    
    // 如果是运算符，保存优先级
    if (Proto->isOperator()) {
        std::string Op = Proto->getBinaryOperator();
        if (!Op.empty()) {
            BinopPrecedence[Op] = Proto->getPrecedence();
        }
    }
    
    return F;
}
```

### 5. 二元运算符调用代码生成

```cpp
// 在 ParseBinOpRHS 中，当遇到运算符时
// 需要检查是否是用户定义的

std::unique_ptr<ExprAST> Parser::ParseBinOpRHS(int ExprPrec,
                                                std::unique_ptr<ExprAST> LHS) {
    while (true) {
        int TokPrec = GetTokPrecedence();
        
        if (TokPrec < ExprPrec) {
            return LHS;
        }
        
        int BinOp = CurTok;
        getNextToken();
        
        auto RHS = ParsePrimary();
        if (!RHS) {
            return nullptr;
        }
        
        int NextPrec = GetTokPrecedence();
        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
            if (!RHS) {
                return nullptr;
            }
        }
        
        // 检查是否是内置运算符
        if (BinOp == '+' || BinOp == '-' || BinOp == '*' || 
            BinOp == '/' || BinOp == '<') {
            LHS = std::make_unique<BinaryExprAST>(BinOp, 
                                                    std::move(LHS), 
                                                    std::move(RHS));
        } else {
            // 用户定义的运算符：生成函数调用
            std::string FnName = "binary";
            FnName += (char)BinOp;
            
            std::vector<std::unique_ptr<ExprAST>> Args;
            Args.push_back(std::move(LHS));
            Args.push_back(std::move(RHS));
            
            LHS = std::make_unique<CallExprAST>(FnName, std::move(Args));
        }
    }
}
```

---

## 一元运算符实现

### 1. 解析一元运算符

```cpp
// 在 parser.cpp 中更新 ParsePrimary

std::unique_ptr<ExprAST> Parser::ParsePrimary() {
    switch (CurTok) {
    // ... 其他 case ...
    
    case tok_identifier:
        return ParseIdentifierExpr();
    
    case tok_number:
        return ParseNumberExpr();
    
    case '(':
        return ParseParenExpr();
    
    case tok_if:
        return ParseIfExpr();
    
    case tok_for:
        return ParseForExpr();
    
    default:
        // 检查是否是用户定义的一元运算符
        if (isascii(CurTok)) {
            std::string FnName = "unary";
            FnName += (char)CurTok;
            
            // 检查运算符是否存在
            if (FunctionProtos.find(FnName) != FunctionProtos.end()) {
                getNextToken();  // 消费运算符
                
                if (auto Operand = ParseUnary()) {
                    std::vector<std::unique_ptr<ExprAST>> Args;
                    Args.push_back(std::move(Operand));
                    return std::make_unique<CallExprAST>(FnName, std::move(Args));
                }
            }
        }
        return LogError("unknown token when expecting an expression");
    }
}
```

### 2. 解析一元后缀

```cpp
/// ParseUnary - 解析一元运算符
/// unary ::= primary | '!' unary | '-' unary
std::unique_ptr<ExprAST> Parser::ParseUnary() {
    // 检查是否是内置一元运算符
    if (CurTok == '-') {
        getNextToken();
        if (auto Operand = ParseUnary()) {
            return std::make_unique<UnaryExprAST>('-', std::move(Operand));
        }
        return nullptr;
    }
    
    // 检查是否是用户定义的一元运算符
    if (isascii(CurTok) && CurTok != '(' && CurTok != ')') {
        std::string FnName = "unary";
        FnName += (char)CurTok;
        
        // 检查运算符是否存在
        if (FunctionProtos.find(FnName) != FunctionProtos.end()) {
            int OpChar = CurTok;
            getNextToken();  // 消费运算符
            
            if (auto Operand = ParseUnary()) {
                return std::make_unique<UnaryExprAST>(OpChar, std::move(Operand));
            }
        }
    }
    
    return ParsePrimary();
}
```

---

## 示例：定义和使用运算符

### 定义新的数学运算符

```kaleidoscope
# 定义一个幂运算符 "^"
def binary ^ 30 (x y)
  if y < 1 then
    1
  else
    x * (x ^ (y - 1));

# 使用
2 ^ 10;  # 应该等于 1024
```

### 定义逻辑运算符

```kaleidoscope
# 定义逻辑或 "||"
def binary || 5 (a b)
  if a then
    1
  else if b then
    1
  else
    0;

# 定义逻辑与 "&&"
def binary && 6 (a b)
  if a then
    if b then 1 else 0
  else
    0;

# 定义逻辑非 "!"
def unary ! (a)
  if a then 0 else 1;

# 使用
1 || 0;      # = 1
1 && 0;      # = 0
!1;          # = 0
```

### 定义列表/数组操作

```kaleidoscope
# 定义连接运算符 "++"
extern concat(a b);
def binary ++ 15 (a b) concat(a, b);

# 定义包含检查运算符 ":"
extern contains(list elem);
def binary : 10 (list elem) contains(list, elem);
```

---

## 运算符优先级管理

### 优先级表

| 运算符 | 优先级 | 结合性 |
|--------|--------|--------|
| `||` | 5 | 左结合 |
| `&&` | 6 | 左结合 |
| `<`, `>`, `==` | 10 | 左结合 |
| `+`, `-` | 20 | 左结合 |
| `*`, `/` | 40 | 左结合 |
| `^` | 30 | 右结合（幂运算） |

### 查看优先级

```cpp
void Parser::PrintBinopPrecedence() {
    std::cout << "Current operator precedence:\n";
    for (const auto &[Op, Prec] : BinopPrecedence) {
        std::cout << "  " << Op << " : " << Prec << "\n";
    }
}
```

---

## 代码生成完整示例

### 输入

```kaleidoscope
def binary | 10 (a b)
  if a < b then a else b;

def foo(a b)
  a | b | 3;
```

### 生成的 IR

```llvm
; 定义 min 运算符
define double @"binary|"(double %a, double %b) {
entry:
    %cmptmp = fcmp ult double %a, %b
    %ifcond = fcmp one i1 %cmptmp, false
    br i1 %ifcond, label %then, label %else

then:
    br label %ifcont

else:
    br label %ifcont

ifcont:
    %iftmp = phi double [%a, %then], [%b, %else]
    ret double %iftmp
}

; 使用运算符
define double @foo(double %a, double %b) {
entry:
    %0 = call double @"binary|"(double %a, double %b)
    %1 = call double @"binary|"(double %0, double 3.000000e+00)
    ret double %1
}
```

---

## 练习题

### 练习 1：实现三目运算符

实现 C 风格的三目运算符 `cond ? then : else`：

```kaleidoscope
# 使用方式
x < 0 ? 0 : x;  # max(0, x)
```

提示：这需要修改词法分析器添加 `?` 和 `:` Token。

### 练习 2：实现复合赋值运算符

实现 `+=`, `-=`, `*=`, `/=` 运算符：

```kaleidoscope
def binary += 10 (x y) x = x + y;
```

注意：这需要第七章的变量支持。

### 练习 3：实现运算符重载

允许相同运算符有多个定义（根据类型选择）：

```kaleidoscope
# 字符串连接
def binary + 20 (a b) str_concat(a, b);

# 数字加法
def binary + 20 (a b) a + b;
```

---

## 常见问题

### Q: 用户定义运算符的命名限制是什么？

**A**: 运算符必须是单个 ASCII 字符。这是 Kaleidoscope 的简化设计。更复杂的语言可以支持多字符运算符。

### Q: 如何处理运算符冲突？

**A**: 新定义的运算符会覆盖之前的定义。你可以检查 `FunctionProtos` 来检测冲突。

### Q: 为什么用函数名 "binary+" 而不是直接用 "+"？

**A**: LLVM 函数名必须是有效的标识符。`binary+` 是一个有效的函数名，同时也清楚地表示这是一个二元运算符实现。

### Q: 运算符优先级可以动态修改吗？

**A**: 可以！每次定义运算符时，优先级都会更新。这允许 REPL 中动态调整运算符行为。

---

## 下一步

完成用户定义运算符后，你的语言已经非常灵活了！

在 [下一章](07_mutable_variables.md) 中，我们将学习：
- 如何实现可变变量
- 如何实现赋值操作
- 如何处理变量的 SSA 问题

---

## 验证清单

在进入下一章之前，确保：

- [ ] 你理解了用户定义运算符的概念
- [ ] 能定义和使用二元运算符
- [ ] 能定义和使用一元运算符
- [ ] 运算符优先级正确处理
- [ ] 能覆盖内置运算符
- [ ] 测试程序运行正常
