# Kaleidoscope 编译器

基于 LLVM 的 Kaleidoscope 语言编译器实现，跟随 LLVM 官方教程完成。

## 项目状态

✅ 第一章：词法分析器
✅ 第二章：语法分析器和 AST
✅ 第三章：代码生成到 LLVM IR
⬜ 第四章：JIT 和优化器
⬜ 第五章：控制流
⬜ 第六章：用户定义运算符
⬜ 第七章：可变变量
⬜ 第八章：目标代码
⬜ 第九章：调试信息

## 环境要求

- **LLVM**: 版本 15.x 或更高
- **CMake**: 3.16 或更高
- **C++ 编译器**: 支持 C++17 (GCC 9+, Clang 10+)

## 安装依赖 (Ubuntu)

```bash
# 安装 LLVM 和相关工具
sudo apt update
sudo apt install -y llvm-17 llvm-17-dev clang-17 cmake build-essential

# 如果需要特定版本，可以调整版本号
# 例如：llvm-18 llvm-18-dev clang-18
```

## 构建

### 使用 CMake（推荐）

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 使用 Makefile

```bash
make
```

## 运行

```bash
./kaleidoscope
# 或
./build/bin/kaleidoscope
```

## 使用示例

### 启动 REPL

```
Kaleidoscope Compiler
Type 'def' to define a function, 'extern' to declare one.
Type any expression to generate LLVM IR.
Type 'quit' to exit.

ready> 1 + 2 * 3;
Read top-level expression:
define double @__anon_expr() {
entry:
  %multmp = fmul double 2.000000e+00, 3.000000e+00
  %addtmp = fadd double 1.000000e+00, %multmp
  ret double %addtmp
}

ready> def foo(x y) x + y;
Read function definition:
define double @foo(double %x, double %y) {
entry:
  %addtmp = fadd double %x, %y
  ret double %addtmp
}

ready> extern sin(x);
Read extern:
declare double @sin(double %x)

ready> foo(1, 2);
Read top-level expression:
define double @__anon_expr() {
entry:
  %calltmp = call double @foo(double 1.000000e+00, double 2.000000e+00)
  ret double %calltmp
}

ready> quit

Goodbye!
```

## 项目结构

```
llvma/
├── include/               # 头文件
│   ├── lexer.hpp         # 词法分析器
│   ├── ast.hpp           # AST 节点定义
│   ├── parser.hpp        # 语法分析器
│   └── codegen.hpp       # 代码生成器
├── src/                   # 源文件
│   ├── main.cpp          # 主程序入口
│   ├── lexer.cpp         # 词法分析器实现
│   ├── parser.cpp        # 语法分析器实现
│   └── codegen.cpp       # 代码生成器实现
├── .plan/                 # 项目计划文档
│   ├── README.md         # 总览
│   ├── 01_lexer.md       # 第一章指南
│   ├── 02_parser.md      # 第二章指南
│   └── 03_codegen.md     # 第三章指南
├── CMakeLists.txt        # CMake 构建配置
├── Makefile              # 简化构建
└── README.md             # 本文件
```

## 语言特性

### 已实现

- ✅ 数字字面量（浮点数）
- ✅ 变量引用
- ✅ 二元运算符（`+`, `-`, `*`, `/`, `<`）
- ✅ 函数定义
- ✅ 函数调用
- ✅ 外部函数声明

### 待实现

- ⬜ if/then/else 控制流
- ⬜ for 循环
- ⬜ 用户定义运算符
- ⬜ 可变变量
- ⬜ JIT 执行
- ⬜ 目标代码生成
- ⬜ 调试信息

## 学习资源

- [LLVM 官方教程](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html)
- [项目计划文档](.plan/README.md)

## 许可证

MIT License
