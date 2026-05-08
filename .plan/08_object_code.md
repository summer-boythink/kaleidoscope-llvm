---
title: 第八章 - 编译到目标代码
chapter: 8
prerequisites: chapter 7
estimated_time: 2-3 hours
---

# 第八章：编译到目标代码

## 学习目标

完成本章后，你将能够：

1. 将 LLVM IR 编译为目标代码（.o 文件）
2. 生成汇编代码（.s 文件）
3. 生成 LLVM 字节码（.bc 文件）
4. 链接生成可执行文件
5. 支持交叉编译

---

## 编译流程

### 完整的编译流水线

```
源代码 (.k)
    ↓ 词法分析
Token 流
    ↓ 语法分析
AST
    ↓ 代码生成
LLVM IR (.ll)
    ↓ 优化
优化后的 IR
    ↓ 后端代码生成
目标代码 (.o)
    ↓ 链接
可执行文件
```

### 目标代码类型

| 格式 | 扩展名 | 说明 |
|------|--------|------|
| LLVM IR（文本） | `.ll` | 人类可读的中间表示 |
| LLVM 字节码 | `.bc` | 二进制格式的 IR |
| 汇编代码 | `.s` | 目标平台的汇编语言 |
| 目标代码 | `.o` | 可重定位的目标文件 |
| 可执行文件 | `.exe` / 无扩展名 | 可直接执行的程序 |

---

## 目标代码生成

### 目标机器设置

```cpp
// src/target.hpp
#ifndef KALEIDOSCOPE_TARGET_HPP
#define KALEIDOSCOPE_TARGET_HPP

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

#include <string>
#include <memory>

namespace kaleidoscope {

class TargetGenerator {
    const llvm::Target *Target;
    std::unique_ptr<llvm::TargetMachine> TheTargetMachine;
    llvm::TargetOptions Options;
    
public:
    TargetGenerator();
    
    // 初始化目标
    bool initializeTarget(const std::string &TargetTriple = "");
    
    // 生成目标代码
    bool emitObjectCode(llvm::Module *M, const std::string &OutputFile);
    bool emitAssemblyCode(llvm::Module *M, const std::string &OutputFile);
    bool emitLLVMBitcode(llvm::Module *M, const std::string &OutputFile);
    bool emitLLVMIR(llvm::Module *M, const std::string &OutputFile);
    
    // 获取目标信息
    std::string getTargetTriple() const;
    std::string getTargetCPU() const;
};

} // namespace kaleidoscope

#endif // KALEIDOSCOPE_TARGET_HPP
```

### 目标生成器实现

```cpp
// src/target.cpp
#include "target.hpp"
#include "llvm/Support/raw_ostream.h"
#include <iostream>

namespace kaleidoscope {

TargetGenerator::TargetGenerator() {
    // 初始化所有目标
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();
}

bool TargetGenerator::initializeTarget(const std::string &TargetTriple) {
    std::string Triple;
    
    if (TargetTriple.empty()) {
        // 使用默认目标（当前机器）
        Triple = llvm::sys::getDefaultTargetTriple();
    } else {
        Triple = TargetTriple;
    }
    
    std::cout << "Target triple: " << Triple << "\n";
    
    // 查找目标
    std::string Error;
    Target = llvm::TargetRegistry::lookupTarget(Triple, Error);
    if (!Target) {
        std::cerr << "Error: " << Error << "\n";
        return false;
    }
    
    // 创建目标机器
    std::string CPU = "generic";
    std::string Features = "";
    
    llvm::TargetOptions Opt;
    
    // 可选：设置优化级别
    // llvm::Optional<llvm::Reloc::Model> RM = llvm::Reloc::PIC_;
    
    TheTargetMachine = std::unique_ptr<llvm::TargetMachine>(
        Target->createTargetMachine(
            Triple, CPU, Features, Opt, 
            llvm::None,  // Relocation model
            llvm::None,  // Code model
            llvm::CodeGenOpt::Default  // Optimization level
        )
    );
    
    if (!TheTargetMachine) {
        std::cerr << "Error: Could not create target machine\n";
        return false;
    }
    
    return true;
}

bool TargetGenerator::emitObjectCode(llvm::Module *M, const std::string &OutputFile) {
    // 设置模块的目标三元组和数据布局
    M->setTargetTriple(TheTargetMachine->getTargetTriple().str());
    M->setDataLayout(TheTargetMachine->createDataLayout());
    
    // 打开输出文件
    std::error_code EC;
    llvm::raw_fd_ostream Dest(OutputFile, EC, llvm::sys::fs::OF_None);
    
    if (EC) {
        std::cerr << "Error: Could not open output file: " << OutputFile << "\n";
        std::cerr << "  " << EC.message() << "\n";
        return false;
    }
    
    // 创建 Pass 管理器
    llvm::legacy::PassManager PM;
    
    // 添加代码生成 Pass
    if (TheTargetMachine->addPassesToEmitFile(
            PM, Dest, nullptr, 
            llvm::CGFT_ObjectFile)) {
        std::cerr << "Error: Target machine can't emit object file\n";
        return false;
    }
    
    // 运行 Pass
    PM.run(*M);
    
    Dest.flush();
    
    std::cout << "Object code written to " << OutputFile << "\n";
    return true;
}

bool TargetGenerator::emitAssemblyCode(llvm::Module *M, const std::string &OutputFile) {
    // 设置模块的目标
    M->setTargetTriple(TheTargetMachine->getTargetTriple().str());
    M->setDataLayout(TheTargetMachine->createDataLayout());
    
    // 打开输出文件
    std::error_code EC;
    llvm::raw_fd_ostream Dest(OutputFile, EC, llvm::sys::fs::OF_None);
    
    if (EC) {
        std::cerr << "Error: Could not open output file: " << OutputFile << "\n";
        return false;
    }
    
    // 创建 Pass 管理器
    llvm::legacy::PassManager PM;
    
    // 添加汇编代码生成 Pass
    if (TheTargetMachine->addPassesToEmitFile(
            PM, Dest, nullptr, 
            llvm::CGFT_AssemblyFile)) {
        std::cerr << "Error: Target machine can't emit assembly file\n";
        return false;
    }
    
    // 运行 Pass
    PM.run(*M);
    
    Dest.flush();
    
    std::cout << "Assembly code written to " << OutputFile << "\n";
    return true;
}

bool TargetGenerator::emitLLVMBitcode(llvm::Module *M, const std::string &OutputFile) {
    std::error_code EC;
    llvm::raw_fd_ostream Dest(OutputFile, EC, llvm::sys::fs::OF_None);
    
    if (EC) {
        std::cerr << "Error: Could not open output file: " << OutputFile << "\n";
        return false;
    }
    
    // 写入位码
    llvm::WriteBitcodeToFile(*M, Dest);
    
    std::cout << "LLVM bitcode written to " << OutputFile << "\n";
    return true;
}

bool TargetGenerator::emitLLVMIR(llvm::Module *M, const std::string &OutputFile) {
    std::error_code EC;
    llvm::raw_fd_ostream Dest(OutputFile, EC, llvm::sys::fs::OF_Text);
    
    if (EC) {
        std::cerr << "Error: Could not open output file: " << OutputFile << "\n";
        return false;
    }
    
    // 写入文本 IR
    M->print(Dest, nullptr);
    
    std::cout << "LLVM IR written to " << OutputFile << "\n";
    return true;
}

std::string TargetGenerator::getTargetTriple() const {
    return TheTargetMachine->getTargetTriple().str();
}

std::string TargetGenerator::getTargetCPU() const {
    return TheTargetMachine->getTargetCPU();
}

} // namespace kaleidoscope
```

---

## 编译器驱动

### 主程序更新

```cpp
// src/main.cpp（编译模式）

#include "codegen.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "optimizer.hpp"
#include "target.hpp"
#include <iostream>
#include <fstream>

enum class Mode {
    REPL,           // 交互模式
    Compile,        // 编译到目标代码
    EmitLLVM,       // 输出 LLVM IR
    EmitAssembly    // 输出汇编
};

void printUsage(const char *ProgramName) {
    std::cerr << "Usage: " << ProgramName << " [options] [file]\n";
    std::cerr << "Options:\n";
    std::cerr << "  -c          Compile to object file\n";
    std::cerr << "  -S          Emit assembly code\n";
    std::cerr << "  -emit-llvm  Emit LLVM IR\n";
    std::cerr << "  -o <file>   Output file\n";
    std::cerr << "  -O<level>   Optimization level (0-3)\n";
}

int main(int argc, char *argv[]) {
    Mode mode = Mode::REPL;
    std::string OutputFile;
    std::string InputFile;
    int OptLevel = 2;
    
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string Arg = argv[i];
        
        if (Arg == "-c") {
            mode = Mode::Compile;
        } else if (Arg == "-S") {
            mode = Mode::EmitAssembly;
        } else if (Arg == "-emit-llvm") {
            mode = Mode::EmitLLVM;
        } else if (Arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "Error: -o requires an argument\n";
                return 1;
            }
            OutputFile = argv[++i];
        } else if (Arg.substr(0, 2) == "-O") {
            OptLevel = std::stoi(Arg.substr(2));
        } else if (Arg[0] == '-') {
            std::cerr << "Error: Unknown option: " << Arg << "\n";
            printUsage(argv[0]);
            return 1;
        } else {
            InputFile = Arg;
        }
    }
    
    if (mode != Mode::REPL && InputFile.empty()) {
        std::cerr << "Error: No input file specified\n";
        printUsage(argv[0]);
        return 1;
    }
    
    // 根据模式执行
    switch (mode) {
    case Mode::REPL:
        return runREPL();
    case Mode::Compile:
        return compileFile(InputFile, OutputFile, OptLevel);
    case Mode::EmitLLVM:
        return emitLLVM(InputFile, OutputFile);
    case Mode::EmitAssembly:
        return emitAssembly(InputFile, OutputFile);
    }
    
    return 0;
}
```

### 编译函数

```cpp
int compileFile(const std::string &InputFile, 
                const std::string &OutputFile,
                int OptLevel) {
    // 读取输入文件
    std::ifstream Input(InputFile);
    if (!Input) {
        std::cerr << "Error: Could not open input file: " << InputFile << "\n";
        return 1;
    }
    
    // 设置输入源
    setInput(Input);
    
    // 初始化代码生成器
    CodeGenerator CodeGen;
    Optimizer TheOptimizer(OptLevel);
    TargetGenerator TargetGen;
    
    if (!TargetGen.initializeTarget()) {
        return 1;
    }
    
    // 解析并生成代码
    getNextToken();
    
    std::unique_ptr<llvm::Module> TheModule;
    
    while (CurTok != tok_eof) {
        switch (CurTok) {
        case tok_def: {
            auto FnAST = ParseDefinition();
            if (FnAST) {
                if (auto *Fn = CodeGen.codegen(FnAST.get())) {
                    TheOptimizer.optimize(Fn);
                }
            }
            break;
        }
        case tok_extern: {
            auto ProtoAST = ParseExtern();
            if (ProtoAST) {
                CodeGen.codegen(ProtoAST.get());
            }
            break;
        }
        default: {
            auto FnAST = ParseTopLevelExpr();
            if (FnAST) {
                if (auto *Fn = CodeGen.codegen(FnAST.get())) {
                    TheOptimizer.optimize(Fn);
                }
            }
            break;
        }
        }
    }
    
    // 确定输出文件名
    std::string OutFile = OutputFile;
    if (OutFile.empty()) {
        // 从输入文件名推导
        OutFile = InputFile;
        // 替换扩展名为 .o
        size_t DotPos = OutFile.rfind('.');
        if (DotPos != std::string::npos) {
            OutFile = OutFile.substr(0, DotPos);
        }
        OutFile += ".o";
    }
    
    // 生成目标代码
    return TargetGen.emitObjectCode(CodeGen.getModule(), OutFile) ? 0 : 1;
}
```

---

## 链接

### 使用系统链接器

```cpp
int linkExecutable(const std::string &ObjectFile, 
                   const std::string &OutputFile) {
    // 构建链接命令
    std::string LinkCmd = "clang " + ObjectFile;
    
    if (!OutputFile.empty()) {
        LinkCmd += " -o " + OutputFile;
    }
    
    // 添加运行时库
    LinkCmd += " -lm";  // 数学库
    
    // 执行链接
    int Result = system(LinkCmd.c_str());
    
    if (Result != 0) {
        std::cerr << "Error: Linking failed\n";
        return 1;
    }
    
    std::cout << "Executable written to " 
              << (OutputFile.empty() ? "a.out" : OutputFile) << "\n";
    return 0;
}
```

### 完整编译示例

```bash
# 编译到目标代码
./kaleidoscope -c program.k -o program.o

# 链接
clang program.o -o program

# 或者一步完成
./kaleidoscope program.k -o program
```

---

## 示例输出

### 输入程序 (program.k)

```kaleidoscope
# 计算斐波那契数列
def fib(n)
  if n < 2 then
    n
  else
    fib(n - 1) + fib(n - 2);

# 主函数
def main()
  fib(10);
```

### 生成的汇编代码 (x86-64)

```asm
	.text
	.file	"program.k"
	.globl	fib
	.type	fib,@function
fib:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset %rbp, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register %rbp
	subq	$16, %rsp
	movsd	%xmm0, -8(%rbp)
	movsd	-8(%rbp), %xmm0
	movsd	.LC0(%rip), %xmm1
	ucomisd	%xmm1, %xmm0
	setae	%al
	testb	%al, %al
	je	.LBB0_2
	movsd	-8(%rbp), %xmm0
	movq	%xmm0, %xmm1
	jmp	.LBB0_5
.LBB0_2:
	movsd	-8(%rbp), %xmm0
	movsd	.LC1(%rip), %xmm1
	subsd	%xmm1, %xmm0
	callq	fib
	movsd	%xmm0, -16(%rbp)
	movsd	-8(%rbp), %xmm0
	movsd	.LC1(%rip), %xmm1
	subsd	%xmm1, %xmm0
	callq	fib
	movsd	-16(%rbp), %xmm1
	addsd	%xmm1, %xmm0
.LBB0_5:
	addq	$16, %rsp
	popq	%rbp
	.cfi_def_cfa %rsp, 8
	retq
.Lfunc_end0:
	.size	fib, .Lfunc_end0-fib
	.cfi_endproc

	.section	.rodata,"a",@progbits
.LC0:
	.quad	0x4000000000000000
.LC1:
	.quad	0x3ff0000000000000
```

---

## 交叉编译

### 支持不同目标

```cpp
// 编译到 ARM
TargetGen.initializeTarget("armv7-unknown-linux-gnueabihf");

// 编译到 AArch64
TargetGen.initializeTarget("aarch64-unknown-linux-gnu");

// 编译到 RISC-V
TargetGen.initializeTarget("riscv64-unknown-linux-gnu");

// 编译到 WebAssembly
TargetGen.initializeTarget("wasm32-unknown-unknown");
```

### 查看支持的目标

```cpp
void listTargets() {
    std::cout << "Available targets:\n";
    for (const auto &T : llvm::TargetRegistry::targets()) {
        std::cout << "  " << T.getName() 
                  << " - " << T.getShortDescription() << "\n";
    }
}
```

---

## 练习题

### 练习 1：添加静态库支持

生成静态库 (.a 文件)：

```bash
# 提示：使用 ar 命令
ar rcs libmylib.a mylib.o
```

### 练习 2：添加动态库支持

生成动态库 (.so 文件)：

```cpp
// 设置 Position Independent Code
TheTargetMachine->setRelocationModel(llvm::Reloc::PIC_);
```

### 练习 3：生成 LLVM 字节码模块

```cpp
bool emitBitcodeModule(llvm::Module *M, const std::string &OutputFile) {
    // 使用 llvm::WriteBitcodeToFile
}
```

---

## 常见问题

### Q: 为什么需要初始化所有目标？

**A**: LLVM 默认只启用宿主目标。要支持交叉编译，需要调用 `InitializeAllTargets()` 等函数。

### Q: 如何选择优化级别？

**A**: 
- `-O0`: 无优化，调试方便
- `-O1`: 基本优化
- `-O2`: 标准优化，推荐
- `-O3`: 激进优化，可能增加代码大小

### Q: 如何处理外部库依赖？

**A**: 在链接时指定库：
```bash
clang program.o -lm -lpthread -o program
```

### Q: 目标三元组是什么？

**A**: 目标三元组描述了目标平台：`<arch>-<vendor>-<sys>-<abi>`
- `x86_64-unknown-linux-gnu`: 64 位 Linux
- `aarch64-apple-darwin`: ARM64 macOS
- `wasm32-unknown-unknown`: WebAssembly

---

## 下一步

完成目标代码生成后，你的编译器已经可以生成独立的可执行文件了！

在 [下一章](09_debug_info.md) 中，我们将学习：
- 如何添加调试信息
- 如何支持源代码级调试
- 如何使用 GDB/LLDB 调试 Kaleidoscope 程序

---

## 验证清单

在进入下一章之前，确保：

- [ ] 能生成目标代码文件
- [ ] 能生成汇编代码文件
- [ ] 能生成 LLVM IR 文件
- [ ] 链接生成可执行文件成功
- [ ] 可执行程序运行正确
- [ ] 理解目标三元组的概念
