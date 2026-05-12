# 深入剖析：每一步的内部机理与 LLVM 的魔力

> 前文用了比喻串起了流水线，这篇拆开每一台机器的盖子，看齿轮如何咬合。
> 并回答一个核心问题：**用 LLVM 和自己手写，到底差在哪里？**

---

## 总览：LLVM 提供了什么，你省下了什么

```
编译器阶段     自己手写要做什么                     LLVM 替你做了什么
─────────────────────────────────────────────────────────────────────────
词法分析       ✦ 完全自己写，LLVM 不管               ❌ LLVM 不提供
语法分析       ✦ 完全自己写，LLVM 不管               ❌ LLVM 不提供
AST 定义       ✦ 完全自己写，LLVM 不管               ❌ LLVM 不提供
代码生成       ✦ 手写 x86/ARM 汇编选指令、分配寄存器  ✅ 生成平台无关的 IR
              ✦ 处理调用约定、栈帧布局                ✅ IRBuilder 一键建指令
              ✦ 为每个目标平台写不同后端              ✅ 一个 IR → N 个平台
优化           ✦ 自己实现每一条优化规则               ✅ 几十种工业级 Pass
              ✦ 证明优化的正确性                      ✅ 经过20年生产验证
              ✦ 设计 Pass 之间的交互顺序              ✅ PassManager 自动调度
JIT 执行       ✦ 手写机器码加载器、链接器、重定位器    ✅ LLJIT 全部搞定
              ✦ 手写符号解析、内存管理                ✅ ResourceTracker 自动回收
              ✦ 调用宿主进程的外部函数                ✅ DynamicLibrarySearchGenerator
```

**一句话总结**：LLVM 的边界从"代码生成"开始。在这之前（Lexer/Parser/AST），你和用 LLVM 之前一样得自己动手；在这之后，LLVM 是一台超级机器，你喂它 IR，它吐出优化过的机器码。

---

## 第一步：词法分析（Lexer）—— LLVM 不插手的领域

### 内部机理

[lexer.cpp](../src/lexer.cpp) 的 `gettok()` 是一个**有限状态机**，只不过它没有显式地画状态转移表，而是用 if-else 链隐式表达的：

```
状态机：

  ┌──────────────────────────────────────────┐
  │  状态：读取首字符                          │
  │                                          │
  │  是空白? ──→ 循环跳过，继续读             │
  │  是字母? ──→ 进入"标识符/关键字"模式      │
  │  是数字? ──→ 进入"数字字面量"模式         │
  │  是 '#'? ──→ 进入"注释"模式              │
  │  是 EOF? ──→ 返回 tok_eof               │
  │  其他?   ──→ 直接返回该字符的 ASCII 值    │
  └──────────────────────────────────────────┘
```

关键细节：

1. **超前读取（Lookahead）**：`LastChar` 成员变量始终保存"多读一个"的字符。当 `gettok()` 返回时，下一个字符已经被读进 `LastChar` 了，下次调用不用重新读。这就像你读书时眼睛总是比嘴巴快一个字。

2. **关键字的"延迟判定"**：先读完整标识符（如 `def`），再去关键字表查。不是看到 `d` 就猜 `def`——万一用户写了 `define` 呢？

3. **数字的 `strtod()`**：不用自己解析浮点数，直接交给 C 标准库。[lexer.cpp:89](../src/lexer.cpp#L89) `NumVal = strtod(NumStr.c_str(), nullptr);`

4. **注释的递归处理**：[lexer.cpp:101](../src/lexer.cpp#L101) 注释结束后 `return gettok()`，跳过注释后继续读下一个 token。这是一种简洁但需要注意栈深度的写法——如果输入有10000行连续注释，可能栈溢出。

### LLVM vs 手写：词法分析

```
LLVM 不提供词法分析器。这是有意为之的设计决策：
  - 词法规则和语言强耦合，无法通用化
  - 用 flex/ANTLR 等工具更专业
  - LLVM 的哲学：只做"后端"的事

如果你不用 LLVM，词法分析这步完全一样——
  没有任何区别，因为 LLVM 根本不参与。
```

---

## 第二步：语法分析（Parser）—— LLVM 不插手，但 Pratt 算法很妙

### 内部机理：Pratt 解析的逐帧慢放

[parser.cpp](../src/parser.cpp) 的核心是 `ParseBinOpRHS()`。让我们用 `1 + 2 * 3` 逐帧回放：

```
输入 Token 流: [1] [+] [2] [*] [3]

帧1: ParseExpression() 调用 ParsePrimary()
     → 读到 [1]，返回 NumberExprAST(1) 作为 LHS
     → 调用 ParseBinOpRHS(0, LHS=1)

帧2: ParseBinOpRHS(0, LHS=1)
     → TokPrec = GetTokPrecedence('+') = 20
     → 20 >= 0? 是！继续
     → BinOp = '+'，消费 [+]
     → RHS = ParsePrimary() → 读到 [2]，返回 NumberExprAST(2)
     → NextPrec = GetTokPrecedence('*') = 40
     → 20 < 40? 是！右侧优先级更高，先处理右侧
     → 递归调用 ParseBinOpRHS(21, RHS=2)  ← 注意：ExprPrec = TokPrec + 1

帧3: ParseBinOpRHS(21, LHS=2)
     → TokPrec = GetTokPrecedence('*') = 40
     → 40 >= 21? 是！继续
     → BinOp = '*'，消费 [*]
     → RHS = ParsePrimary() → 读到 [3]，返回 NumberExprAST(3)
     → NextPrec = GetTokPrecedence(下一个token) = -1（没有运算符了）
     → 40 < -1? 否！不递归
     → 合并: BinaryExprAST('*', 2, 3)
     → 返回 BinaryExpr('*', 2, 3)

帧2（续）: RHS = BinaryExpr('*', 2, 3)
     → 合并: BinaryExprAST('+', 1, BinaryExpr('*', 2, 3))
     → 返回

最终 AST:
        (+)
       /   \
      1    (*)
          /   \
         2     3
```

**为什么 ExprPrec 传的是 `TokPrec + 1` 而不是 `TokPrec`？**

这是 Pratt 解析的精髓。如果传 `TokPrec`，那么对于 `1 + 2 + 3`：

```
传 TokPrec:     1 + (2 + 3)  → 右结合
传 TokPrec + 1: (1 + 2) + 3  → 左结合

+1 让"同级运算符从左边开始绑定"，实现了左结合。
这是 Pratt 解析的"默认行为"——简单、优雅、正确。
```

### 运算符优先级表的可扩展性

[parser.cpp:10-14](../src/parser.cpp#L10-L14) 优先级用 `std::map<char, int>` 存储：

```cpp
BinopPrecedence['<'] = 10;
BinopPrecedence['+'] = 20;
BinopPrecedence['-'] = 20;
BinopPrecedence['*'] = 40;
BinopPrecedence['/'] = 40;
```

这意味着：
- 运行时可以动态添加新运算符（第六章 `binary` 关键字的实现基础）
- 可以修改现有运算符的优先级
- 不用修改解析器代码，只需修改这张表

### LLVM vs 手写：语法分析

```
同样，LLVM 不提供语法分析器。

但有个微妙区别：如果你不用 LLVM，你的 AST 可以直接包含
代码生成需要的信息（比如直接嵌入目标平台的寄存器分配提示）。

用了 LLVM，你的 AST 只需要表达语义——因为后续会翻译成
平台无关的 LLVM IR，由 LLVM 负责平台适配。

这改变了 AST 的设计哲学：
  手写后端：AST 可能需要知道 "这段代码在 x86 上用什么指令"
  LLVM 后端：AST 只表达 "这段代码做什么"，不管 "怎么做"
```

---

## 第三步：代码生成（CodeGen）—— LLVM 魔力真正开始的地方

### 内部机理：LLVM 的核心对象模型

[codegen.hpp:73-75](../include/codegen.hpp#L73-L75) 定义了三个核心对象：

```cpp
std::unique_ptr<llvm::LLVMContext> TheContext;   // 类型系统
std::unique_ptr<llvm::Module> TheModule;         // 编译单元
std::unique_ptr<llvm::IRBuilder<>> Builder;      // 指令工厂
```

它们的关系就像一座工厂：

```
LLVMContext  ───  "类型模具库"
  │                管理所有类型对象，确保同一类型只有一个实例
  │                Type::getDoubleTy() 每次返回同一个指针
  │                这是一个"实习"（interning）机制，省内存、快比较
  │
  ├── Module   ───  "车间"
  │     包含所有函数、全局变量
  │     有名字（"Kaleidoscope"），有数据布局（DataLayout）
  │     最终整个 Module 被喂给 JIT 或写出到 .o 文件
  │
  └── IRBuilder ───  "装配工"
        知道"当前插在哪个基本块的哪条指令后面"
        提供 CreateFAdd / CreateRet 等便捷方法
        自动维护指令插入位置
```

### 逐条指令的生成过程

以 `def add(x y) x + y` 为例，完整追踪 [codegen.cpp](../src/codegen.cpp) 的执行：

```
步骤1: codegen(PrototypeAST("add", ["x", "y"]))
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  [codegen.cpp:126-132]

  1. 构造参数类型列表: [double, double]
     std::vector<llvm::Type*> Doubles(2, Type::getDoubleTy(*TheContext));

  2. 构造函数类型: double(double, double)
     FunctionType::get(DoubleTy, Doubles, false)
                          ↑返回类型  ↑参数类型  ↑不接受可变参数

  3. 创建函数对象:
     Function::Create(FT, ExternalLinkage, "add", TheModule.get())
                       ↑类型  ↑链接方式      ↑名字  ↑放入哪个模块

  4. 给参数命名:
     arg[0].setName("x"), arg[1].setName("y")
     这是可选的，只影响 IR 可读性，不影响语义

  产生的 IR:
    declare double @add(double %x, double %y)


步骤2: codegen(FunctionAST)
━━━━━━━━━━━━━━━━━━━━━━━━━━━
  [codegen.cpp:134-157]

  1. 创建入口基本块:
     BasicBlock::Create(*TheContext, "entry", TheFunction)
     → 函数的第一个基本块，指令从这里开始

  2. 设置 IRBuilder 插入点:
     Builder->SetInsertPoint(BB)
     → 告诉 Builder："接下来生成的指令，都插到 entry 块的末尾"

  3. 建立符号表:
     NamedValues["x"] = &arg[0]
     NamedValues["y"] = &arg[1]
     → 后续遇到变量 "x" 时，查表就能找到对应的 LLVM Value

  4. 递归生成函数体:
     codegen(BinaryExprAST('+', VariableExprAST("x"), VariableExprAST("y")))

     4a. codegen(VariableExprAST("x"))
         → NamedValues["x"] → %x

     4b. codegen(VariableExprAST("y"))
         → NamedValues["y"] → %y

     4c. Builder->CreateFAdd(%x, %y, "addtmp")
         → %addtmp = fadd double %x, %y

  5. 生成返回指令:
     Builder->CreateRet(%addtmp)
     → ret double %addtmp

  6. 验证函数正确性:
     verifyFunction(*TheFunction, &llvm::errs())
     → LLVM 内置的 IR 验证器，检查类型错误、结构错误等

  最终产生的 IR:
    define double @add(double %x, double %y) {
    entry:
      %addtmp = fadd double %x, %y
      ret double %addtmp
    }
```

### LLVM IR 是什么？为什么不直接生成 x86？

```
你的代码          LLVM IR              x86-64 汇编
─────────        ────────             ────────────
x + y            fadd double %x, %y   addsd %xmm0, %xmm1
                                      vadd.f64 d0, d0, d1  (ARM)
                                      fadd.d %f0, %f0, %f1  (RISC-V)

LLVM IR 是一个"抽象计算机"的汇编语言：
  - 它有无穷多个虚拟寄存器（%0, %1, %2, ...）
  - 它有 SSA 形式（每个值只赋值一次）
  - 它有类型系统（double, i32, i1, pointer...）
  - 它有基本块和控制流

你只需要学一种"语言"（LLVM IR），就能生成 N 种平台的机器码。
这就是 LLVM 的核心价值：**一次生成，到处运行**（不是 Java 那种意思，
而是编译器后端只需要写一次 IR 生成逻辑）。
```

### LLVM vs 手写：代码生成

```
┌────────────────────────────────────────────────────────────────────┐
│  自己手写代码生成，你需要：                                         │
│                                                                    │
│  1. 寄存器分配                                                     │
│     - x86 只有 16 个 XMM 寄存器，溢出时需要 spill 到栈上           │
│     - 要写图着色算法或线性扫描算法                                  │
│     - 要处理调用约定（caller-saved vs callee-saved）               │
│     → LLVM 的寄存器分配器经过20年优化，产出的代码接近手写汇编       │
│                                                                    │
│  2. 指令选择                                                       │
│     - x86 的加法是 addsd 还是 vaddsd？（标量 vs AVX 向量）         │
│     - 乘加融合：a*b+c 能不能变成 vfmadd?                           │
│     - 特殊指令：位运算代替取模、LEA 代替加法                       │
│     → LLVM 的指令选择器基于 DAG，自动选择最优指令模式               │
│                                                                    │
│  3. 调用约定                                                       │
│     - x86-64: 浮点参数走 XMM0-XMM7，整数走 RDI/RSI/RDX...        │
│     - ARM64: 参数走 d0-d7 / x0-x7                                 │
│     - Windows vs Linux 的调用约定还不同！                           │
│     → LLVM 的 CallingConv 机制自动处理                             │
│                                                                    │
│  4. 栈帧布局                                                       │
│     - 局部变量怎么排列？对齐要求？红区？                           │
│     - 调试信息怎么和栈偏移关联？                                   │
│     → LLVM 自动计算栈帧，保证对齐                                  │
│                                                                    │
│  5. 多平台支持                                                     │
│     - 要支持 x86、ARM、RISC-V、WebAssembly...                     │
│     - 每个平台一套代码？还是用抽象层？                             │
│     → LLVM 支持 30+ 个目标平台，你生成 IR 就行                     │
│                                                                    │
│  6. ELF/Mach-O/COFF 目标文件格式                                   │
│     - 重定位、符号表、段布局...                                    │
│     → LLVM 的 MC 层自动处理                                       │
│                                                                    │
│  粗略估计：手写一个像样的 x86 后端，至少 10000 行代码。            │
│  用 LLVM：codegen.cpp 总共 274 行，就能生成所有平台的代码。         │
└────────────────────────────────────────────────────────────────────┘
```

### IRBuilder 的巧妙设计

```cpp
// [codegen.cpp:81]
Builder->CreateFAdd(L, R, "addtmp");
```

这一行代码背后的 IRBuilder 做了什么：

```
1. 检查 L 和 R 的类型是否匹配（都是 double？）
2. 创建一条 fadd 指令
3. 将指令插入到当前 BasicBlock 的末尾
4. 给指令一个名字 "addtmp"（方便调试，可选）
5. 返回新创建的 Value（可以传给下一条指令）

如果你手写，你需要：
1. 手动分配一个虚拟寄存器编号
2. 手动维护 SSA 的 φ 节点
3. 手动管理指令链表
4. 手动做类型检查

IRBuilder 把这些机械操作全部自动化了。
```

### `verifyFunction` —— LLVM 内置的"质检员"

[codegen.cpp:148](../src/codegen.cpp#L148):

```cpp
if (llvm::verifyFunction(*TheFunction, &llvm::errs())) {
    // 验证失败！函数有结构错误
    TheFunction->eraseFromParent();
    return nullptr;
}
```

`verifyFunction` 会检查：
- 所有指令都正确终止了基本块（ret/br）
- 所有 Value 的使用都有对应的定义
- 类型系统一致性（不会把 i32 当 double 用）
- SSA 规则（每个 Value 只定义一次）

**这是手写后端不可能拥有的能力**——你不会在生成 x86 汇编后再写一个 x86 验证器。但 LLVM 的 IR 有严格的形式化规范，验证器能捕获你99%的错误。

---

## 第四步：优化（Optimizer）—— LLVM 最核心的竞争力

### 内部机理：New Pass Manager 的架构

[optimizer.hpp:30-35](../include/optimizer.hpp#L30-L35) 定义了四层分析管理器：

```
llvm::PassBuilder PB;                    // Pass 构建器
llvm::LoopAnalysisManager LAM;           // 循环级分析
llvm::FunctionAnalysisManager FAM;       // 函数级分析
llvm::CGSCCAnalysisManager CGAM;         // 调用图SCC级分析
llvm::ModuleAnalysisManager MAM;         // 模块级分析
```

```
层次结构：

  Module（模块）
    └── CGSCC（调用图强连通分量）
          └── Function（函数）
                └── Loop（循环）

每个层次有自己的分析管理器，负责：
  1. 按需计算分析结果（惰性求值）
  2. 缓存分析结果，避免重复计算
  3. 当 IR 被修改时，自动失效相关缓存

这就是 "crossRegisterProxies" 的作用：
  [optimizer.cpp:20]
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
  
  让函数级 Pass 能查询模块级分析结果，
  让循环级 Pass 能查询函数级分析结果。
  就像公司各部门之间的"联络员"。
```

### 各个 Pass 的深层原理

#### 1. PromotePass（mem2reg）—— "栈变量飞升为 SSA"

```c
// 优化前的 IR（变量在栈上）：
  %x.addr = alloca double           ; 在栈上分配空间
  store double 1.0, double* %x.addr ; 写入
  %0 = load double, double* %x.addr ; 读出
  store double %0, double* %x.addr  ; 又写入
  %1 = load double, double* %x.addr ; 又读出
  ret double %1

// 优化后（变量晋升为 SSA 寄存器）：
  ret double 1.0                    ; 直接用值，零内存访问
```

**为什么需要 mem2reg？**

因为 CodeGen 生成的函数参数直接用 Value（已经是 SSA），但如果将来加入可变变量（`var x = 1; x = 2`），就需要 alloca/store/load。mem2reg 负责把这些"低效的内存操作"变回"高效的 SSA 值"。

**手写的话**：你需要自己实现 SSA 构造算法（插入 φ 节点、重命名变量），这是编译器课程中最复杂的算法之一。LLVM 的 PromotePass 帮你搞定了。

#### 2. InstCombinePass —— "窥孔优化之王"

```
它做的是局部优化：看一条或几条相邻指令，找到可以简化的模式。

模式库（节选）：
  x + 0 → x                    零加消除
  x * 1 → x                    一乘消除
  x * 0 → 0                    零乘消除（注意 NaN）
  double(double) → double      恒等转换消除
  (x < y) ? 1.0 : 0.0 → fcmp  选择指令→比较指令
  x * 2 → x + x                乘法变加法（更快）
  x / 2 → x * 0.5              除法变乘法（除法比乘法慢5-20倍）

实际项目中的效果：
  输入: 1 + 2 * 3
  CodeGen 生成:
    %multmp = fmul double 2.0, 3.0
    %addtmp = fadd double 1.0, %multmp
  InstCombine 发现 2.0 * 3.0 是常量，直接算出 6.0：
    %addtmp = fadd double 1.0, 6.0
  再发现 1.0 + 6.0 也是常量：
    直接 ret double 7.0
```

**手写的话**：你需要自己写模式匹配引擎，自己维护"代数简化规则表"。InstCombine 有数百条规则，每条都经过形式化验证——你自己写很难做到这个完整度。

#### 3. ReassociatePass —— "代数重结合"

```
目的：重新排列表达式，使后续优化更容易发现化简机会。

例子：
  a = x + 1
  b = a + 2

  正常理解: b = (x + 1) + 2

  Reassociate 重排: b = x + (1 + 2) = x + 3

  然后 InstCombine 把 1 + 2 常量折叠成 3。

如果不重结合：
  - (x + 1) + 2：InstCombine 看不出能化简
  - x + 3：一目了然

核心算法：
  1. 将表达式拆成"原子"和"系数"的线性组合
  2. 按某个排序标准（如操作数出现频率）重新排列
  3. 常量自然聚在一起，被 InstCombine 折叠
```

#### 4. GVNPass —— "全局值编号"

```
核心思想：给每个计算出的值一个"编号"，相同计算 → 相同编号 → 复用。

例子：
  %a = fadd double %x, %y    ; 编号 #42
  ... (中间可能有很多其他代码)
  %b = fadd double %x, %y    ; 也是编号 #42（相同的操作数和运算符）

  GVN 发现 #42 已经算过了：
  %a = fadd double %x, %y
  %b = %a                     ; 直接复用

更复杂的例子（需要考虑控制流）：
  if (cond) {
    %a = %x + %y
  } else {
    %b = %x + %y
  }
  // 无论走哪个分支，结果都是 %x + %y
  // GVN + 后续优化可以消除冗余
```

**手写的话**：GVN 是编译优化中较复杂的算法之一。你需要实现值编号（hash 操作数+运算符）、处理控制流中的 φ 节点、处理指针别名分析。LLVM 的 GVNPass 是工业级实现，正确性和性能都有保证。

#### 5. SimplifyCFGPass —— "控制流图简化"

```
消除无意义的控制流结构：

模式1：空基本块跳转
  entry → bb1 → bb2
  bb1 里只有一条 br bb2（啥也没干）
  简化：entry → bb2

模式2：不可达代码
  entry → bb1
  bb2 没有任何前驱（永远到不了）
  简化：删除 bb2

模式3：单前驱合并
  bb1 只有 bb2 一个前驱
  简化：合并 bb1 和 bb2 为一个基本块

模式4：常量条件跳转
  br i1 true, label %bb1, label %bb2
  简化：无条件跳转 br label %bb1，删除 bb2 的边
```

#### 6. DCEPass —— "死代码消除"

```
删除计算了但从未使用的值：

  %x = fadd double %a, %b    ; %x 从未被使用
  ret double %a               ; 只返回了 %a

  → 删除 %x 那条指令

注意：DCE 和 InstCombine 配合使用效果最好。
  InstCombine 可能产生新的死代码（比如把 x + 0 变成 x，
  那 %x = 0 就死了），DCE 再清扫。
  这就是 O2 里 InstCombine 出现两次的原因：
  [optimizer.cpp:40,43]
  FPM.addPass(llvm::InstCombinePass());  // 第一次
  ...
  FPM.addPass(llvm::InstCombinePass());  // 第二次
```

### O2 Pass 的顺序为什么是这样？

[optimizer.cpp:36-44](../src/optimizer.cpp#L36-L44):

```
PromotePass         ← 先把栈变量变 SSA，后续 Pass 才好处理
  ↓
InstCombinePass     ← 常量折叠、代数简化（第一轮）
  ↓
ReassociatePass     ← 重排表达式，让常量聚在一起
  ↓
GVNPass             ← 消除重复计算
  ↓
SimplifyCFGPass     ← 清理控制流
  ↓
InstCombinePass     ← 再次简化（前面的 Pass 可能暴露新的机会）
  ↓
DCEPass             ← 最终清扫死代码
```

**这个顺序不是随便排的**，而是编译器领域几十年的经验总结：
- mem2reg 必须最先，因为其他 Pass 假设代码是 SSA 形式
- Reassociate 在 GVN 前面，因为重排后 GVN 更容易发现重复
- InstCombine 出现两次，因为前后其他 Pass 都可能产生新的化简机会
- DCE 放最后，确保所有死代码都被清扫干净

**手写的话**：你需要自己设计 Pass 顺序，理解每个 Pass 的前置条件和后置效果。LLVM 社区花了大量时间调优这个顺序。

### LLVM vs 手写：优化

```
┌────────────────────────────────────────────────────────────────────┐
│  LLVM 优化 vs 手写优化的根本区别：                                  │
│                                                                    │
│  1. 规模                                                           │
│     LLVM 有 100+ 个优化 Pass                                      │
│     手写：你可能实现 5-10 个就不错了                                │
│                                                                    │
│  2. 正确性                                                         │
│     每个 Pass 都经过 fuzz testing + 形式化验证                     │
│     手写：你的常量折叠能正确处理 NaN 和 ±0 吗？                    │
│           (0.0 == -0.0 但 1/0.0 != 1/-0.0)                        │
│                                                                    │
│  3. 交互效应                                                       │
│     Pass 之间会互相暴露优化机会（"迭代收敛"）                      │
│     PassManager 自动处理分析结果的缓存和失效                        │
│     手写：你需要自己管理"优化到不动点"的迭代逻辑                    │
│                                                                    │
│  4. 可组合性                                                       │
│     新 Pass 可以复用已有分析结果（支配树、循环信息...）             │
│     手写：每个优化都是孤立的，无法复用分析                          │
│                                                                    │
│  5. 实际效果                                                       │
│     对这个项目：1+2*3 直接编译成 ret double 7.0                    │
│     如果手写后端：你可能根本不会实现常量折叠                        │
│     那运行时就要真的算 2*3=6, 1+6=7（虽然也快，但浪费了 CPU）      │
└────────────────────────────────────────────────────────────────────┘
```

---

## 第五步：JIT 执行 —— LLVM 最"魔法"的部分

### 内部机理：从 IR 到执行的完整链路

```
LLVM IR                     机器码
   │                          ▲
   │  ThreadSafeModule        │
   │  (把 Module + Context     │
   │   打包成线程安全对象)      │
   │                          │
   ▼                          │
addModule() ──────▶ LLJIT ──────▶ 机器码在内存中
                        │
                        │  内部流水线:
                        │
                        │  IR → IRCompileLayer → ObjectLayer → 内存
                        │         │
                        │         ▼
                        │    TargetMachine::addPassesToEmitFile
                        │    (这里才真正选目标平台、选指令)
                        │         │
                        │         ▼
                        │    机器码 → RTDyldObjectLinkingLayer
                        │    (运行时动态链接器：重定位、符号解析)
                        │         │
                        │         ▼
                        │    可执行的机器码在内存中
                        │
   lookup() ──────────▶ 找到函数的内存地址
                        │
                        ▼
                    函数指针 → 直接调用！
```

### ThreadSafeModule —— 为什么要把 Context 和 Module 一起移动？

[codegen.cpp:195-198](../src/codegen.cpp#L195-L198):

```cpp
auto TSM = llvm::orc::ThreadSafeModule(
    std::move(TheModule),
    std::move(TheContext)
);
```

```
LLVM 的设计：Module 依赖于 Context（类型信息在 Context 里）。
如果只移动 Module 不移动 Context，Module 里的类型指针就悬空了。

ThreadSafeModule 把两者绑定在一起，还加了线程安全保护：
  - 同一个 Context 不能被两个线程同时访问
  - ThreadSafeModule 用 mutex 保护
  - 确保编译和执行不会数据竞争

之后必须调用 InitializeModule() 创建新的 Context + Module：
  [codegen.cpp:223]
  InitializeModule();
  因为旧的已经被 move 走了！
```

### ResourceTracker —— 匿名表达式的"垃圾回收"

[jit.cpp:58-63](../src/jit.cpp#L58-L63):

```cpp
if (IsAnonExpr && AnonExprRT) {
    if (auto Err = AnonExprRT->remove()) {
        return Err;
    }
    AnonExprRT = nullptr;
}
```

```
问题：每次执行顶层表达式，都会编译成机器码占内存。
  1+2 → __anon_expr() 占 64 字节
  3*4 → __anon_expr() 占 64 字节（新的）
  5+6 → __anon_expr() 占 64 字节（又一个）
  
  如果不清理，内存会无限增长！

解决：ResourceTracker 跟踪"这批机器码是哪个模块的"
  执行新的匿名表达式之前：
    1. 用旧 ResourceTracker 删除旧的机器码
    2. 创建新 ResourceTracker 跟踪新的机器码
  
  用户定义的函数（def foo...）不用删除，
  因为 IsAnonExpr = false，不保存 ResourceTracker。
  
  这就是 JIT 的"垃圾回收"——比 GC 简单，但很有效。
```

### DynamicLibrarySearchGenerator —— 调用 C 标准库的"任意门"

[jit.cpp:24-25](../src/jit.cpp#L24-L25):

```cpp
auto DLSG = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
    TheJIT->getDataLayout().getGlobalPrefix()
);
```

```
这行代码做了什么？

  1. 扫描当前进程的所有动态链接库（libc, libm, libstdc++...）
  2. 建立一个"符号查找器"
  3. 当 JIT 编译的代码调用 "sin" 时：
     - JIT 先在自己编译的模块里找
     - 找不到？问 DynamicLibrarySearchGenerator
     - 它查 libm.so，找到 sin 的地址
     - JIT 把这个地址填进 call 指令
     - 运行时直接调用 C 库的 sin！

  所以 extern sin(x) 声明后，真的能调用 C 的 sin 函数。
  
  这就像给 JIT 开了一个"任意门"，通向整个 C 标准库。
```

### LLVM vs 手写：JIT 执行

```
┌────────────────────────────────────────────────────────────────────┐
│  手写一个 JIT 编译器，你需要：                                      │
│                                                                    │
│  1. 机器码发射器                                                   │
│     - 手动编码每条 x86 指令的字节序列                               │
│     - 比如 fadd %xmm0, %xmm1 = {0x62, 0xf1, 0xfd, 0x08, ...}    │
│     - 每个指令集不同，每条指令的编码规则不同                         │
│     → LLVM 的 TargetMachine 自动选指令、自动编码                    │
│                                                                    │
│  2. 重定位器                                                       │
│     - call 指令的目标地址在编译时不知道                             │
│     - 需要运行时计算相对偏移并回填                                  │
│     - x86 和 ARM 的重定位格式完全不同                               │
│     → LLVM 的 RuntimeDyld 自动处理重定位                           │
│                                                                    │
│  3. 内存管理                                                       │
│     - 机器码需要放在可执行内存页上                                   │
│     - 需要 mmap(VPROT_EXEC) 或 VirtualProtect                     │
│     - 需要处理代码缓存的 I-Cache 一致性                            │
│     → LLVM 的 SectionMemoryManager 自动处理                        │
│                                                                    │
│  4. 符号解析                                                       │
│     - 函数 A 调用函数 B，需要找到 B 的地址                          │
│     - 可能 B 还没被编译（延迟编译）                                 │
│     - 需要编译回调桩（reentry stub）                                │
│     → LLVM 的 LLJIT 自动管理符号表和延迟编译                       │
│                                                                    │
│  5. 线程安全                                                       │
│     - 编译和执行可能并发                                           │
│     - Context 不能被两个线程同时修改                                │
│     → LLVM 的 ThreadSafeModule 和 LLJIT 处理了这些                 │
│                                                                    │
│  粗略估计：手写一个基本的 JIT，至少 5000 行。                       │
│  用 LLVM：jit.cpp 总共 95 行。                                     │
│                                                                    │
│  而且你的 JIT 很难支持 ARM64、RISC-V、WebAssembly...               │
│  LLVM 的 JIT 天然支持所有 LLVM 后端支持的平台。                    │
└────────────────────────────────────────────────────────────────────┘
```

---

## 跨步骤的关键机制

### 1. 符号表的两级架构

```
CodeGen 维护两张表：

NamedValues (局部符号表)          FunctionProtos (全局原型表)
┌─────────────────┐              ┌─────────────────────────┐
│ "x" → %x        │              │ "add" → Prototype("add",│
│ "y" → %y        │              │          ["x","y"])      │
│ (每个函数清空)   │              │ "sin" → Prototype("sin",│
└─────────────────┘              │          ["x"])          │
                                 │ (全局持久，跨函数存在)   │
                                 └─────────────────────────┘

[codegen.cpp:144]  NamedValues.clear();  // 每个新函数清空
[codegen.cpp:145]  for (auto& Arg : TheFunction->args())
    NamedValues[std::string(Arg.getName())] = &Arg;

[codegen.cpp:63]  FunctionProtos[Proto->getName()] = std::move(Proto);
    // extern 声明的函数持久保存在原型表中
```

### 2. getFunction() 的"延迟实例化"

[codegen.cpp:55-60](../src/codegen.cpp#L55-L60):

```cpp
llvm::Function* CodeGenerator::getFunction(const std::string& Name) {
    if (auto* F = TheModule->getFunction(Name)) return F;
    auto FI = FunctionProtos.find(Name);
    if (FI != FunctionProtos.end()) return codegen(FI->second.get());
    return nullptr;
}
```

```
为什么需要两步查找？

  第一次调用 sin(1.0) 时：
    1. 在当前 Module 里找 "sin" → 找不到
    2. 在 FunctionProtos 里找 → 找到了！
    3. 用 PrototypeAST 生成 sin 的 declare 声明
    4. 返回新创建的 Function*

  之后 Module 里就有了 sin，下次直接命中第一步。

  这就是"延迟实例化"——用到才生成，不用就不生成。
  类似于懒加载（Lazy Loading）的设计模式。
```

### 3. Module 的生命周期管理

```
Module 的"出生-死亡"循环：

  InitializeModule()  →  codegen()  →  optimize()  →  JIT addModule()
       ↑                                                            │
       │                    move(TheModule)                         │
       └────────── InitializeModule() ◀────────────────────────────┘

  每次 JIT 编译后，Module 和 Context 被 move 进 ThreadSafeModule，
  被 JIT 持有了。代码生成器必须重新创建新的 Module + Context，
  否则后续代码生成就会操作一个"已经被 move 走"的空壳对象。

  这就是 [codegen.cpp:223] InitializeModule() 的意义。
  它不是"重新初始化"——它是"重生"。
```

---

## 全局对比：用 LLVM vs 纯手写编译器

```
                    纯手写编译器              使用 LLVM
────────────────────────────────────────────────────────
代码量               10000+ 行               ~700 行
支持平台             1个（你写的那个）         30+个
优化能力             基础（常量折叠？）        工业级（100+ Pass）
正确性保证           靠测试                    形式化验证 + fuzz
JIT 支持             极难实现                  95 行搞定
调试信息             需要手写 DWARF           自动生成
维护成本             极高（x86 变了要改）       LLVM 社区维护

学习曲线             低（但做出来的差）         中（需要学 LLVM API）
灵活性               高（完全控制）             中（受 IR 表达力限制）
编译速度             快（没有中间层）           较慢（IR→机器码有开销）
依赖                  无                        需要安装 LLVM
```

### LLVM 的"代价"

```
1. 编译时间：LLVM 是个庞大的库（~5000万行代码），链接它需要时间
2. 内存占用：LLVMContext、Module、PassManager 都有内存开销
3. 学习曲线：LLVM API 不是最直观的，错误信息有时晦涩
4. IR 表达力限制：某些语言特性（如 continuation、effect system）
   很难直接映射到 LLVM IR
5. 启动延迟：JIT 首次编译有初始化开销

对于这个教学项目，这些代价完全可以接受。
对于生产级语言实现（Rust、Swift），这些是工程权衡的一部分。
```

---

## 附录：每个文件用到的关键 LLVM API 速查

| 文件 | LLVM API | 作用 |
|------|----------|------|
| codegen.cpp | `LLVMContext` | 类型系统和常量的上下文 |
| codegen.cpp | `Module` | 编译单元容器 |
| codegen.cpp | `IRBuilder<>` | 指令构建的便捷接口 |
| codegen.cpp | `ConstantFP::get()` | 创建浮点常量 |
| codegen.cpp | `Type::getDoubleTy()` | 获取 double 类型 |
| codegen.cpp | `FunctionType::get()` | 创建函数签名类型 |
| codegen.cpp | `Function::Create()` | 创建函数 |
| codegen.cpp | `BasicBlock::Create()` | 创建基本块 |
| codegen.cpp | `Builder->CreateFAdd/FSub/FMul/FDiv` | 创建算术指令 |
| codegen.cpp | `Builder->CreateFCmpULT` | 创建比较指令 |
| codegen.cpp | `Builder->CreateUIToFP` | 整数→浮点转换 |
| codegen.cpp | `Builder->CreateCall` | 创建函数调用指令 |
| codegen.cpp | `Builder->CreateRet` | 创建返回指令 |
| codegen.cpp | `verifyFunction()` | 验证 IR 正确性 |
| codegen.cpp | `InitializeNativeTarget()` | 初始化目标平台 |
| optimizer.cpp | `PassBuilder` | 构建 Pass 管道 |
| optimizer.cpp | `FunctionPassManager` | 管理函数级 Pass |
| optimizer.cpp | `*AnalysisManager` | 管理分析结果缓存 |
| optimizer.cpp | `PromotePass` | mem2reg 优化 |
| optimizer.cpp | `InstCombinePass` | 指令合并优化 |
| optimizer.cpp | `ReassociatePass` | 表达式重结合 |
| optimizer.cpp | `GVNPass` | 全局值编号 |
| optimizer.cpp | `SimplifyCFGPass` | 控制流简化 |
| optimizer.cpp | `DCEPass` | 死代码消除 |
| jit.cpp | `LLJITBuilder` | 创建 JIT 实例 |
| jit.cpp | `ThreadSafeModule` | 线程安全的模块包装 |
| jit.cpp | `ResourceTracker` | 追踪和回收 JIT 资源 |
| jit.cpp | `DynamicLibrarySearchGenerator` | 查找进程外部符号 |
| jit.cpp | `LLJIT::lookup()` | 查找符号地址 |
