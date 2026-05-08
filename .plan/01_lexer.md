---
title: 第一章 - 词法分析器 (Lexer)
chapter: 1
prerequisites: none
estimated_time: 2-3 hours
---

# 第一章：词法分析器 (Lexer)

## 学习目标

完成本章后，你将能够：

1. 理解词法分析的基本概念
2. 设计和实现一个简单的词法分析器
3. 处理标识符、数字、运算符和关键字
4. 为后续的语法分析器做好准备

---

## 什么是词法分析？

词法分析是编译器的第一个阶段，它将源代码字符串转换为一系列 **Token（标记）**。

### 类比理解

想象你在阅读一篇文章：
- 你的眼睛识别出一个个**单词**（词法分析）
- 你的大脑理解这些单词组成的**句子结构**（语法分析）
- 最后理解句子的**含义**（语义分析和代码生成）

词法分析器就像眼睛，它不关心语法是否正确，只负责识别出一个个"单词"（Token）。

### 示例

输入代码：
```
def foo(x y) x + y * 2;
```

输出 Token 序列：
```
[def] [foo] [(] [x] [y] [)] [x] [+] [y] [*] [2] [;]
```

---

## Kaleidoscope 语言特性

在开始之前，让我们了解一下 Kaleidoscope 的基本语法：

### 基本语法示例

```kaleidoscope
# 定义一个函数
def add(x y) x + y;

# 调用函数
add(1, 2);

# 外部函数声明
extern sin(x);
extern cos(x);

# 使用 if/then/else
def foo(x) 
  if x < 10 then
    x * 2
  else
    x + 1;
```

### 语言特点

1. **简单**：只有一种数据类型——浮点数（64位双精度）
2. **函数式**：支持函数定义和调用
3. **可扩展**：支持用户定义运算符

---

## Token 类型定义

让我们定义 Kaleidoscope 需要的 Token 类型：

### Token 枚举

```cpp
// src/lexer.hpp
#ifndef KALEIDOSCOPE_LEXER_HPP
#define KALEIDOSCOPE_LEXER_HPP

#include <string>
#include <utility>

namespace kaleidoscope {

// Token 枚举：每个 Token 都有一个对应的数字
// 0-255 范围留给 ASCII 字符（如 '+', '-', '*' 等）
enum Token {
    tok_eof = -1,           // 文件结束
    
    // 命令
    tok_def = -2,           // def 关键字
    tok_extern = -3,        // extern 关键字
    
    // 标识符和数字
    tok_identifier = -4,    // 标识符（变量名、函数名）
    tok_number = -5,        // 数字字面量
    
    // 控制流（第五章添加）
    tok_if = -6,            // if 关键字
    tok_then = -7,          // then 关键字
    tok_else = -8,          // else 关键字
    tok_for = -9,           // for 关键字
    tok_in = -10,           // in 关键字
    
    // 运算符（第六章添加）
    tok_binary = -11,       // binary 关键字
    tok_unary = -12,        // unary 关键字
    
    // 变量定义（第七章添加）
    tok_var = -13,          // var 关键字
};

// 全局变量：存储当前标识符和数值
// 注意：这是简单的实现方式，后续可以封装成类
static std::string IdentifierStr;  // 存储 tok_identifier 的文本
static double NumVal;               // 存储 tok_number 的值

} // namespace kaleidoscope

#endif // KALEIDOSCOPE_LEXER_HPP
```

---

## 词法分析器实现

### 基本框架

```cpp
// src/lexer.cpp
#include "lexer.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace kaleidoscope {

// 关键字映射表
static std::unordered_map<std::string, Token> Keywords = {
    {"def", tok_def},
    {"extern", tok_extern},
    {"if", tok_if},
    {"then", tok_then},
    {"else", tok_else},
    {"for", tok_for},
    {"in", tok_in},
    {"binary", tok_binary},
    {"unary", tok_unary},
    {"var", tok_var},
};

/// gettok - 返回下一个 Token
/// 这是词法分析器的核心函数
static int gettok() {
    static int LastChar = ' ';  // 上一个读取的字符
    
    // 1. 跳过空白字符（空格、制表符、换行符）
    while (isspace(LastChar)) {
        LastChar = getchar();
    }
    
    // 2. 处理标识符：[a-zA-Z_][a-zA-Z0-9_]*
    if (isalpha(LastChar) || LastChar == '_') {
        IdentifierStr = LastChar;
        while (isalnum((LastChar = getchar())) || LastChar == '_') {
            IdentifierStr += LastChar;
        }
        
        // 检查是否是关键字
        auto it = Keywords.find(IdentifierStr);
        if (it != Keywords.end()) {
            return it->second;
        }
        return tok_identifier;
    }
    
    // 3. 处理数字：[0-9.]+
    if (isdigit(LastChar) || LastChar == '.') {
        std::string NumStr;
        do {
            NumStr += LastChar;
            LastChar = getchar();
        } while (isdigit(LastChar) || LastChar == '.');
        
        NumVal = strtod(NumStr.c_str(), nullptr);
        return tok_number;
    }
    
    // 4. 处理注释：# 开头到行尾
    if (LastChar == '#') {
        // 跳过注释直到行尾
        do {
            LastChar = getchar();
        } while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');
        
        if (LastChar != EOF) {
            return gettok();  // 递归调用，处理下一行
        }
    }
    
    // 5. 检查文件结束
    if (LastChar == EOF) {
        return tok_eof;
    }
    
    // 6. 返回单个字符作为 Token
    // 这处理了 '+', '-', '*', '/', '(', ')', ',', ';', ':' 等
    int ThisChar = LastChar;
    LastChar = getchar();
    return ThisChar;
}

/// getNextToken - 获取下一个 Token 并缓存
static int CurTok;
static int getNextToken() {
    return CurTok = gettok();
}

} // namespace kaleidoscope
```

---

## 代码详解

### 1. 跳过空白字符

```cpp
while (isspace(LastChar)) {
    LastChar = getchar();
}
```

**为什么需要这个？**
- 空格、制表符、换行符在 Kaleidoscope 中没有意义（不像 Python）
- 我们需要跳过它们，找到有意义的字符

### 2. 识别标识符

```cpp
if (isalpha(LastChar) || LastChar == '_') {
    IdentifierStr = LastChar;
    while (isalnum((LastChar = getchar())) || LastChar == '_') {
        IdentifierStr += LastChar;
    }
    // ...
}
```

**标识符规则**：
- 必须以字母或下划线开头
- 后面可以跟字母、数字或下划线
- 例如：`foo`, `_bar`, `add2`, `my_func`

**关键字处理**：
```cpp
auto it = Keywords.find(IdentifierStr);
if (it != Keywords.end()) {
    return it->second;  // 返回关键字 Token
}
return tok_identifier;   // 否则是普通标识符
```

### 3. 识别数字

```cpp
if (isdigit(LastChar) || LastChar == '.') {
    std::string NumStr;
    do {
        NumStr += LastChar;
        LastChar = getchar();
    } while (isdigit(LastChar) || LastChar == '.');
    
    NumVal = strtod(NumStr.c_str(), nullptr);
    return tok_number;
}
```

**数字规则**：
- 简单实现：接受 `123`, `123.456`, `.456`, `123.`
- 注意：这个实现允许 `1.2.3` 这样的错误格式，我们暂时接受这个限制

### 4. 处理注释

```cpp
if (LastChar == '#') {
    do {
        LastChar = getchar();
    } while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');
    
    if (LastChar != EOF) {
        return gettok();  // 递归调用
    }
}
```

**注释规则**：
- `#` 开头，到行尾结束
- 类似 Python 和 Shell 的注释风格
- 注释会被完全忽略

### 5. 单字符运算符

```cpp
int ThisChar = LastChar;
LastChar = getchar();
return ThisChar;
```

**支持的运算符**：
- 数学运算：`+`, `-`, `*`, `/`
- 比较：`<`（后续会添加 `>`, `==` 等）
- 括号：`(`, `)`
- 分隔符：`,`, `;`

---

## 完整示例

让我们创建一个测试程序来验证词法分析器：

```cpp
// src/main_lexer_test.cpp
#include "lexer.hpp"
#include <iostream>
#include <string>

using namespace kaleidoscope;

// Token 名称映射（用于调试）
std::string getTokenName(int tok) {
    switch (tok) {
        case tok_eof:       return "EOF";
        case tok_def:       return "def";
        case tok_extern:    return "extern";
        case tok_identifier:return "identifier(" + IdentifierStr + ")";
        case tok_number:    return "number(" + std::to_string(NumVal) + ")";
        case tok_if:        return "if";
        case tok_then:      return "then";
        case tok_else:      return "else";
        case tok_for:       return "for";
        case tok_in:        return "in";
        default:            
            if (tok > 0 && tok < 256) {
                return std::string("char('") + (char)tok + "')";
            }
            return "unknown(" + std::to_string(tok) + ")";
    }
}

int main() {
    std::cout << "Kaleidoscope Lexer Test\n";
    std::cout << "Enter some Kaleidoscope code (Ctrl+D to end):\n\n";
    
    int tok;
    do {
        tok = gettok();
        std::cout << "Token: " << getTokenName(tok) << "\n";
    } while (tok != tok_eof);
    
    return 0;
}
```

### 测试输入

```
def add(x y) x + y;
extern sin(x);
# 这是一个注释
add(1, 2.5);
```

### 预期输出

```
Token: def
Token: identifier(add)
Token: char('(')
Token: identifier(x)
Token: identifier(y)
Token: char(')')
Token: identifier(x)
Token: char('+')
Token: identifier(y)
Token: char(';')
Token: extern
Token: identifier(sin)
Token: char('(')
Token: identifier(x)
Token: char(')')
Token: char(';')
Token: identifier(add)
Token: char('(')
Token: number(1.000000)
Token: char(',')
Token: number(2.500000)
Token: char(')')
Token: char(';')
Token: EOF
```

---

## 练习题

### 练习 1：添加新关键字

在 Kaleidoscope 中添加 `return` 关键字：

1. 在 `Token` 枚举中添加 `tok_return`
2. 在 `Keywords` 映射表中添加 `"return", tok_return`
3. 更新 `getTokenName` 函数

### 练习 2：改进数字解析

当前的数字解析允许 `1.2.3` 这样的格式。改进它：

```cpp
// 提示：使用更严格的逻辑
if (isdigit(LastChar)) {
    std::string NumStr;
    bool hasDot = false;
    do {
        if (LastChar == '.') {
            if (hasDot) break;  // 已经有小数点，停止
            hasDot = true;
        }
        NumStr += LastChar;
        LastChar = getchar();
    } while (isdigit(LastChar) || LastChar == '.');
    // ...
}
```

### 练习 3：添加字符串字面量

添加对字符串的支持（提示：用 `"` 包围）：

1. 添加 `tok_string = -14` Token
2. 添加全局变量 `static std::string StringVal;`
3. 在 `gettok` 中处理字符串

---

## 常见问题

### Q: 为什么要用负数作为 Token 值？

**A**: 这是一种常见的技巧。ASCII 字符的范围是 0-255，所以我们用负数来表示特殊的 Token。这样，像 `+`, `-`, `*` 这样的运算符可以直接用它们的 ASCII 值作为 Token，简化代码。

### Q: 为什么用 `static` 变量存储状态？

**A**: 这是为了教程的简洁。在实际项目中，你应该把这些状态封装到一个 `Lexer` 类中：

```cpp
class Lexer {
    std::string IdentifierStr;
    double NumVal;
    int LastChar;
public:
    int gettok();
    // ...
};
```

### Q: 为什么注释只在行尾结束？

**A**: 这是 Kaleidoscope 的设计选择，简化了实现。你可以扩展它支持多行注释 `/* ... */` 作为练习。

---

## 下一步

完成了词法分析器后，你已经迈出了编译器的第一步！

在 [下一章](02_parser.md) 中，我们将学习：
- 如何定义抽象语法树 (AST)
- 如何构建语法分析器 (Parser)
- 如何将 Token 序列转换为 AST

---

## 验证清单

在进入下一章之前，确保：

- [ ] 你理解了 Token 的概念
- [ ] 词法分析器能正确识别标识符
- [ ] 词法分析器能正确识别数字
- [ ] 词法分析器能正确识别关键字
- [ ] 词法分析器能正确跳过空白和注释
- [ ] 测试程序运行正常
