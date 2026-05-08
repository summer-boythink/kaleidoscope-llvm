---
title: LLVM Kaleidoscope 语言实现计划
created: 2026-05-08
status: planning
---

# LLVM Kaleidoscope 语言实现项目

## 项目概述

Kaleidoscope 是一个简单的编程语言，我们将使用 LLVM 来实现它的完整编译器。通过这个项目，你将学习到：

- 如何设计和实现词法分析器 (Lexer)
- 如何构建语法分析器 (Parser) 和抽象语法树 (AST)
- 如何使用 LLVM 生成中间表示 (IR)
- 如何添加 JIT 编译和优化支持
- 如何扩展语言特性（控制流、运算符、变量）
- 如何编译到目标代码
- 如何添加调试信息

---

## 目录

1. [第一章：词法分析器](01_lexer.md) - 实现词法分析
2. [第二章：语法分析器](02_parser.md) - 构建解析器和 AST
3. [第三章：代码生成](03_codegen.md) - 生成 LLVM IR
4. [第四章：JIT 和优化](04_jit_optimizer.md) - 添加即时编译支持
5. [第五章：控制流](05_control_flow.md) - if/then/else 和 for 循环
6. [第六章：用户定义运算符](06_user_operators.md) - 可扩展的运算符
7. [第七章：可变变量](07_mutable_variables.md) - 赋值和变量
8. [第八章：目标代码](08_object_code.md) - 编译到目标文件
9. [第九章：调试信息](09_debug_info.md) - 添加调试支持
10. [第十章：总结](10_conclusion.md) - 扩展方向和资源

---

## 项目结构

```
llvma/
├── .plan/                    # 计划文档
│   ├── README.md            # 总览（本文件）
│   ├── 01_lexer.md          # 词法分析器指南
│   ├── 02_parser.md         # 语法分析器指南
│   ├── 03_codegen.md        # 代码生成指南
│   ├── 04_jit_optimizer.md  # JIT 和优化指南
│   ├── 05_control_flow.md   # 控制流指南
│   ├── 06_user_operators.md # 用户运算符指南
│   ├── 07_mutable_variables.md # 可变变量指南
│   ├── 08_object_code.md    # 目标代码指南
│   ├── 09_debug_info.md     # 调试信息指南
│   └── 10_conclusion.md     # 总结
├── src/                      # 源代码目录
│   ├── lexer.cpp            # 词法分析器
│   ├── parser.cpp           # 语法分析器
│   ├── ast.cpp              # AST 节点定义
│   ├── codegen.cpp          # 代码生成
│   └── main.cpp             # 主程序入口
├── CMakeLists.txt           # 构建配置
└── README.md                # 项目说明
```

---

## 开始之前

### 你需要了解的知识

- **C++ 基础**：熟悉 C++11/14 特性，包括智能指针、RAII 等
- **编译原理基础**：了解词法分析、语法分析的基本概念会有帮助
- **LLVM 基础**：无需先验知识，我们会在过程中学习

### 环境要求

- **LLVM**: 版本 15.x 或更高
- **CMake**: 3.16 或更高
- **C++ 编译器**: 支持 C++17 (GCC 9+, Clang 10+)
- **操作系统**: Linux 或 macOS

---

## 如何使用这个计划

1. **按顺序阅读**：章节之间有依赖关系，建议按顺序学习
2. **动手实践**：每章都有完整的代码示例，建议自己敲一遍
3. **验证成果**：每章末尾有验证步骤，确保你的实现正确
4. **循序渐进**：完成后面的章节时，前面的代码可能需要修改

---

## 快速开始

准备好了吗？让我们从 [第一章：词法分析器](01_lexer.md) 开始！

---

## 学习进度追踪

| 章节 | 状态 | 完成日期 |
|------|------|----------|
| 1. 词法分析器 | ⬜ 未开始 | - |
| 2. 语法分析器 | ⬜ 未开始 | - |
| 3. 代码生成 | ⬜ 未开始 | - |
| 4. JIT 和优化 | ⬜ 未开始 | - |
| 5. 控制流 | ⬜ 未开始 | - |
| 6. 用户定义运算符 | ⬜ 未开始 | - |
| 7. 可变变量 | ⬜ 未开始 | - |
| 8. 目标代码 | ⬜ 未开始 | - |
| 9. 调试信息 | ⬜ 未开始 | - |
| 10. 总结 | ⬜ 未开始 | - |

---

## 参考资源

- [LLVM 官方文档](https://llvm.org/docs/)
- [LLVM Kaleidoscope 教程原文](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html)
- [LLVM Language Reference Manual](https://llvm.org/docs/LangRef.html)
