# 控制流的魔法：计划如何在代码中"活"起来

> 计划文件 `.plan/05_control_flow.md` 是一张蓝图，代码是按照蓝图盖起来的大楼。
> 让我们走进这座大楼，看看每个房间是怎么建造的。

---

## 全景：从文字到执行的四站旅程

```
用户输入: if x < 10 then x * 2 else x + 1

    │
    ▼
┌─────────────────────────────────────────────────────────────────┐
│  第一站：Lexer —— 把文字切成词元                                 │
│  [lexer.cpp:9-20] 关键字表已备好 if/then/else/for/in            │
│                                                                 │
│  "if x < 10 then x * 2 else x + 1"                             │
│          ↓                                                      │
│  [tok_if] [x] [<] [10] [tok_then] [x] [*] [2] [tok_else] ...   │
└─────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────┐
│  第二站：Parser —— 把词元搭成语法树                              │
│  [parser.cpp:118-152] ParseIfExpr() 搭建 IfExprAST             │
│                                                                 │
│               IfExprAST                                         │
│              /    |    \                                        │
│           Cond   Then   Else                                    │
│            │     │      │                                       │
│          x<10   x*2    x+1                                      │
└─────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────┐
│  第三站：CodeGen —— 把语法树翻译成 LLVM IR                       │
│  [codegen.cpp:118-179] codegenIf() 生成条件分支和 PHI 节点       │
│                                                                 │
│  entry:                                                         │
│    %ifcond = fcmp one double %x, 0.0                           │
│    br i1 %ifcond, label %then, label %else                     │
│  then:                                                          │
│    %multmp = fmul double %x, 2.0                               │
│    br label %ifcont                                            │
│  else:                                                          │
│    %addtmp = fadd double %x, 1.0                               │
│    br label %ifcont                                            │
│  ifcont:                                                        │
│    %iftmp = phi double [%multmp, %then], [%addtmp, %else]      │
└─────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────┐
│  第四站：JIT —— 把 IR 变成机器码并执行                           │
│  [jit.cpp] 机器码在内存中，直接调用                              │
│                                                                 │
│  CPU 执行: 比较 → 分支 → 计算 → 返回结果                        │
└─────────────────────────────────────────────────────────────────┘
```

---

## 第一站：Lexer —— 关键字的"门牌号"

计划文件说要支持 `if/then/else/for/in`，代码是怎么做到的？

### 门牌号系统（Token 枚举）

[lexer.hpp:11-35](../include/lexer.hpp#L11-L35) 定义了特殊的"门牌号"：

```cpp
enum Token {
    tok_eof = -1,
    tok_def = -2,
    tok_extern = -3,
    tok_identifier = -4,
    tok_number = -5,
    
    // 控制流关键词 —— 这就是计划要求的！
    tok_if = -6,      // "if" 的门牌号
    tok_then = -7,    // "then" 的门牌号
    tok_else = -8,    // "else" 的门牌号
    tok_for = -9,     // "for" 的门牌号
    tok_in = -10,     // "in" 的门牌号
    // ...
};
```

```
为什么用负数？

  ASCII 字符的范围是 0-255
  我们把 -1 到 -13 留给"特殊词汇"

  这样 Lexer.gettok() 返回一个 int：
    - 返回正数 → 这是一个普通字符（如 '+' 返回 43）
    - 返回负数 → 这是一个关键词（如 'if' 返回 -6）

  一眼就能区分：正数是单字符，负数是多字符词。
```

### 关键字查表（Keyword Map）

[lexer.cpp:9-20](../src/lexer.cpp#L9-L20) 建立了"名字→门牌号"的映射：

```cpp
const std::unordered_map<std::string, Token> Lexer::Keywords = {
    {"def", tok_def},
    {"extern", tok_extern},
    
    // 控制流关键词 —— 计划要求的五个！
    {"if", tok_if},
    {"then", tok_then},
    {"else", tok_else},
    {"for", tok_for},
    {"in", tok_in},
    // ...
};
```

```
工作流程：

  输入: "if"

  1. Lexer 读到 'i'，识别为标识符开始
  2. 继续读 'f'，拼成 "if"
  3. 查 Keywords 表 → 找到了！
  4. 返回 tok_if (-6)

  输入: "iffy"

  1. Lexer 读到 'i'，识别为标识符开始
  2. 继续读 'f' 'f' 'y'，拼成 "iffy"
  3. 查 Keywords 表 → 找不到
  4. 返回 tok_identifier，标识符值为 "iffy"

区别在于：先读完整个词，再查表。
不会看到 'i' 'f' 就急着认作 if。
```

---

## 第二站：Parser —— 搭建语法树的"建筑师"

### if/then/else 的搭建过程

[parser.cpp:118-152](../src/parser.cpp#L118-L152) 的 `ParseIfExpr()`：

```
输入 Token 流: [tok_if] [x] [<] [10] [tok_then] [x] [*] [2] [tok_else] [x] [+] [1]

步骤1: 看到 tok_if
       → 确认这是 if 表达式
       → getNextToken() 消费 'if'，当前 Token 变成 [x]

步骤2: ParseExpression() 解析条件
       → 这会递归调用 ParsePrimary() → ParseBinOpRHS()
       → 最终返回 BinaryExprAST('<', x, 10)
       → 当前 Token 变成 [tok_then]

步骤3: 检查 tok_then
       → 必须有！没有 'then' 语法就错了
       → getNextToken() 消费 'then'，当前 Token 变成 [x]

步骤4: ParseExpression() 解析 then 分支
       → 返回 BinaryExprAST('*', x, 2)
       → 当前 Token 变成 [tok_else]

步骤5: 检查 tok_else（可选）
       → 有！继续解析
       → getNextToken() 消费 'else'
       → ParseExpression() 返回 BinaryExprAST('+', x, 1)

步骤6: 组装 IfExprAST
       ┌─────────────────────────────┐
       │        IfExprAST            │
       │    ┌─────┼─────┐           │
       │   Cond Then  Else          │
       │    │    │     │            │
       │   x<10 x*2   x+1           │
       └─────────────────────────────┘
```

### for 循环的搭建过程

[parser.cpp:156-216](../src/parser.cpp#L156-L216) 的 `ParseForExpr()`：

```
语法: for var = start, cond, step in body

输入: for i = 1, i < n, 1.0 in i * 2

步骤1: 看到 tok_for，消费
步骤2: 看到标识符 "i"，记为循环变量
步骤3: 看到 '='，消费
步骤4: ParseExpression() → NumberExprAST(1) 作为起始值
步骤5: 看到 ','，消费
步骤6: ParseExpression() → BinaryExprAST('<', i, n) 作为条件
步骤7: 看到 ','（可选），消费
       ParseExpression() → NumberExprAST(1.0) 作为步长
步骤8: 看到 tok_in，消费
步骤9: ParseExpression() → BinaryExprAST('*', i, 2) 作为循环体

最终 AST:
       ┌─────────────────────────────┐
       │        ForExprAST           │
       │  VarName = "i"              │
       │    ┌───┬───┬───┬───┐       │
       │  Start End Step Body        │
       │    │    │    │    │        │
       │    1   i<n  1.0  i*2        │
       └─────────────────────────────┘
```

### 路由中心：ParsePrimary()

[parser.cpp:220-234](../src/parser.cpp#L220-L234)：

```cpp
std::unique_ptr<ExprAST> Parser::ParsePrimary() {
    switch (CurTok) {
    case tok_identifier:
        return ParseIdentifierExpr();
    case tok_number:
        return ParseNumberExpr();
    case '(':
        return ParseParenExpr();
        
    // 控制流入口！
    case tok_if:
        return ParseIfExpr();      // 看到 if → 走 if 通道
    case tok_for:
        return ParseForExpr();     // 看到 for → 走 for 通道
    // ...
    }
}
```

```
ParsePrimary() 就像一个路由中心：

  Token 流进入 → 查表 → 分发到对应的解析函数

  tok_number    ──→  ParseNumberExpr()
  tok_identifier───→  ParseIdentifierExpr()
  tok_if        ──→  ParseIfExpr()        ← 新增！
  tok_for       ──→  ParseForExpr()       ← 新增！
  '('           ──→  ParseParenExpr()

计划说"添加 if/then/else"，实际就是：
  1. 在 Token 枚举里加门牌号
  2. 在 Keywords 表里加映射
  3. 在 ParsePrimary 里加 case
  4. 写 ParseIfExpr() 函数
```

---

## 第三站：CodeGen —— 语法树的"翻译官"

这是计划文件最核心的部分，也是 LLVM 最神奇的地方。

### if 表达式的翻译过程

[codegen.cpp:118-179](../src/codegen.cpp#L118-L179)：

```
输入 AST: IfExprAST(Cond: x<10, Then: x*2, Else: x+1)

══════════════════════════════════════════════════════════════
步骤1: 生成条件代码
══════════════════════════════════════════════════════════════

  CondV = codegen(x < 10)
  → %cmptmp = fcmp ult double %x, 10.0

  然后 CreateFCmpONE 把结果转成布尔：
  → %ifcond = fcmp one double %cmptmp, 0.0

  为什么要转？因为 Kaleidoscope 万物皆 double，
  但 LLVM 的 br 指令需要 i1（布尔）。

══════════════════════════════════════════════════════════════
步骤2-4: 创建三个基本块（"三个房间"）
══════════════════════════════════════════════════════════════

  ThenBB  = BasicBlock::Create("then")   // then 分支房间
  ElseBB  = BasicBlock::Create("else")   // else 分支房间
  MergeBB = BasicBlock::Create("ifcont") // 汇合房间

  房间布局:
  
       [entry]
          │
          │  条件判断
          ▼
    ┌─────┴─────┐
    ▼           ▼
 [then]      [else]
    │           │
    └─────┬─────┘
          ▼
       [ifcont]

══════════════════════════════════════════════════════════════
步骤5: 创建条件分支门
══════════════════════════════════════════════════════════════

  Builder->CreateCondBr(CondV, ThenBB, ElseBB)
  
  生成: br i1 %ifcond, label %then, label %else

  这是一扇"智能门"：
    - ifcond 为真 → 走 then 房间
    - ifcond 为假 → 走 else 房间

══════════════════════════════════════════════════════════════
步骤6: 生成 then 房间的代码
══════════════════════════════════════════════════════════════

  Builder->SetInsertPoint(ThenBB)  // 进入 then 房间
  ThenV = codegen(x * 2)           // 生成代码
  → %multmp = fmul double %x, 2.0
  
  Builder->CreateBr(MergeBB)       // 跳到汇合房间
  → br label %ifcont

══════════════════════════════════════════════════════════════
步骤7: 生成 else 房间的代码
══════════════════════════════════════════════════════════════

  Builder->SetInsertPoint(ElseBB)  // 进入 else 房间
  ElseV = codegen(x + 1)           // 生成代码
  → %addtmp = fadd double %x, 1.0
  
  Builder->CreateBr(MergeBB)       // 跳到汇合房间
  → br label %ifcont

══════════════════════════════════════════════════════════════
步骤8-9: 创建 PHI 节点（关键！）
══════════════════════════════════════════════════════════════

  Builder->SetInsertPoint(MergeBB)  // 进入汇合房间
  
  PHI 节点的魔法：
  
    %iftmp = phi double [%multmp, %then], [%addtmp, %else]
    
    意思是：
      - 如果从 then 房间来 → 取 %multmp 的值
      - 如果从 else 房间来 → 取 %addtmp 的值
    
    这就像一个"智能收银台"：
      - 你从 A 口出来 → 收银台显示 A 的价格
      - 你从 B 口出来 → 收银台显示 B 的价格

最终生成的 IR:

  define double @foo(double %x) {
  entry:
    %cmptmp = fcmp ult double %x, 1.000000e+01
    %ifcond = fcmp one i1 %cmptmp, false
    br i1 %ifcond, label %then, label %else

  then:
    %multmp = fmul double %x, 2.000000e+00
    br label %ifcont

  else:
    %addtmp = fadd double %x, 1.000000e+00
    br label %ifcont

  ifcont:
    %iftmp = phi double [%multmp, %then], [%addtmp, %else]
    ret double %iftmp
  }
```

### PHI 节点：SSA 的"时光机"

计划文件花了大量篇幅解释 PHI 节点，为什么它这么重要？

```
问题：SSA 要求每个变量只赋值一次

传统代码:
  if (cond) {
    x = 1;
  } else {
    x = 2;   // 错！x 被赋值两次
  }
  return x; // x 是 1 还是 2？

SSA 代码:
  if (cond) {
    x1 = 1;   // 不同的名字
  } else {
    x2 = 2;   // 不同的名字
  }
  x = phi [x1, then], [x2, else]  // PHI 节点"合并"
  return x;

PHI 节点的语义:
  "我根据你从哪里来，决定我是什么值"

这就像是：
  - 你从北京来 → 你的"籍贯"显示"北京"
  - 你从上海来 → 你的"籍贯"显示"上海"

PHI 节点在汇合点，根据控制流的来源选择对应的值。
```

### for 循环的翻译过程

[codegen.cpp:181-268](../src/codegen.cpp#L181-L268)：

```
输入 AST: ForExprAST(VarName="i", Start=1, End=i<n, Step=1.0, Body=i*2)

══════════════════════════════════════════════════════════════
步骤1: 生成起始值
══════════════════════════════════════════════════════════════

  StartVal = codegen(1)
  → 常量 1.0

══════════════════════════════════════════════════════════════
步骤3-4: 创建循环块，跳转过去
══════════════════════════════════════════════════════════════

  LoopBB = BasicBlock::Create("loop")
  Builder->CreateBr(LoopBB)

  房间布局:
  
  [entry] ──→ [loop] ←──┐
                  │     │
                  │     │
                  └─────┘  循环！

══════════════════════════════════════════════════════════════
步骤6: 创建 PHI 节点（循环的"时光机"）
══════════════════════════════════════════════════════════════

  Variable = Builder->CreatePHI(double, 2, "i")
  Variable->addIncoming(StartVal, PreheaderBB)

  PHI 节点有两个入口：
    - 第一次进入循环：从 entry 来，值是 StartVal (1.0)
    - 后续迭代：从 loop 自己来，值是 NextVar（下次迭代的 i）

  这就是循环的"时间机器"：
    - 第一次：i = 1
    - 第二次：i = 2
    - 第三次：i = 3
    - ...

  但 SSA 只允许一个定义！PHI 节点解决了这个矛盾：
    %i = phi [1.0, %entry], [%nextvar, %loop]

══════════════════════════════════════════════════════════════
步骤7: 符号表的作用域管理
══════════════════════════════════════════════════════════════

  OldVal = NamedValues["i"]       // 保存外层的 i（如果有）
  NamedValues["i"] = Variable     // 循环变量 i 指向 PHI 节点

  这意味着：
    - 循环体内引用 "i" → 找到的是 PHI 节点
    - 循环结束后 → 恢复 OldVal（如果有）

  这就是"变量作用域"的底层实现！

══════════════════════════════════════════════════════════════
步骤8: 生成循环体
══════════════════════════════════════════════════════════════

  codegen(Body)  // 这里可以用 NamedValues["i"]

══════════════════════════════════════════════════════════════
步骤9-10: 生成步长和下一次迭代的值
══════════════════════════════════════════════════════════════

  StepVal = codegen(Step) 或默认 1.0
  NextVar = CreateFAdd(Variable, StepVal)
  
  → %nextvar = fadd double %i, 1.0

  注意：这里 Variable 是 PHI 节点，也就是"当前的 i"
        NextVar 是"下一次的 i"

══════════════════════════════════════════════════════════════
步骤11-13: 生成循环条件和分支
══════════════════════════════════════════════════════════════

  EndCond = codegen(i < n)
  → %cmptmp = fcmp ult double %i, %n
  → %loopcond = fcmp one i1 %cmptmp, false

  AfterBB = BasicBlock::Create("afterloop")
  Builder->CreateCondBr(EndCond, LoopBB, AfterBB)
  
  → br i1 %loopcond, label %loop, label %afterloop

  这意味着：
    - 条件为真 → 继续循环（跳回 LoopBB）
    - 条件为假 → 退出循环（跳到 AfterBB）

══════════════════════════════════════════════════════════════
步骤14: 完善 PHI 节点
══════════════════════════════════════════════════════════════

  Variable->addIncoming(NextVar, LoopEndBB)

  现在 PHI 节点完整了：
    %i = phi [1.0, %entry], [%nextvar, %loop]

  第一次从 entry 进入 → i = 1.0
  后续从 loop 自己进入 → i = nextvar（上次 i + 1）

最终生成的 IR:

  define double @loop(double %n) {
  entry:
    br label %loop

  loop:
    %i = phi double [1.000000e+00, %entry], [%nextvar, %loop]
    %multmp = fmul double %i, 2.000000e+00    ; 循环体 i*2
    %nextvar = fadd double %i, 1.000000e+00   ; i++
    %cmptmp = fcmp ult double %i, %n          ; i < n?
    %loopcond = fcmp one i1 %cmptmp, false
    br i1 %loopcond, label %loop, label %afterloop

  afterloop:
    ret double 0.000000e+00
  }
```

### 为什么 for 循环返回 0.0？

[codegen.cpp:268](../src/codegen.cpp#L268):

```
Kaleidoscope 的哲学：万物皆表达式，每个表达式都有值。

if 表达式的值 = PHI 节点合并的 then/else 结果
for 表达式的值 = ？

这里选择返回 0.0，因为：
  1. for 循环的"目的"通常是副作用（打印、累加...）
  2. 返回值不重要，但必须有（SSA 要求）

你也可以改成返回循环变量最后的值：
  return Variable;  // 返回 i 的最终值
```

---

## 第四站：JIT —— 代码的"瞬间实化"

计划文件没有详细讲 JIT，但它是让控制流"活起来"的关键。

```
LLVM IR                      JIT 做的事
────────                     ──────────
br i1 %cond, label %then...  → 翻译成 x86 的 jne/je 指令
phi [a, %then], [b, %else]   → 编译器自动处理，运行时根本不需要 phi！
loop: br label %loop         → 变成 jmp 指令跳回循环开头

PHI 节点的秘密：
  - PHI 节点只存在于编译期（LLVM IR）
  - 到了机器码阶段，PHI 消失了！
  - 编译器自动选择正确的寄存器或栈位置

这就像：
  - PHI 是编译期的"概念"
  - 机器码是运行期的"现实"
  - 编译器负责把概念"具象化"
```

---

## 计划与代码的对照表

| 计划文件章节 | 代码位置 | 做了什么 |
|-------------|---------|---------|
| AST 节点定义 | [ast.hpp:116-171](../include/ast.hpp#L116-L171) | IfExprAST 和 ForExprAST 两个类 |
| Lexer 关键字 | [lexer.hpp:23-27](../include/lexer.hpp#L23-L27) | tok_if/than/else/for/in 枚举 |
| Lexer 映射表 | [lexer.cpp:9-20](../src/lexer.cpp#L9-L20) | 关键字字符串→Token 映射 |
| Parser if | [parser.cpp:118-152](../src/parser.cpp#L118-L152) | ParseIfExpr() 函数 |
| Parser for | [parser.cpp:156-216](../src/parser.cpp#L156-L216) | ParseForExpr() 函数 |
| Parser 路由 | [parser.cpp:228-231](../src/parser.cpp#L228-L231) | ParsePrimary() 加 case |
| CodeGen if | [codegen.cpp:118-179](../src/codegen.cpp#L118-L179) | codegenIf() 生成条件分支和 PHI |
| CodeGen for | [codegen.cpp:181-268](../src/codegen.cpp#L181-L268) | codegenFor() 生成循环和 PHI |
| CodeGen 路由 | [codegen.cpp:281-282](../src/codegen.cpp#L281-L282) | codegen() 加 dynamic_cast |

---

## 总结：计划的"生命力"

```
计划文件（.md）     ────  蓝图，描述"要做什么"
     │
     ▼
代码文件（.cpp/.hpp）───  大楼，描述"怎么做"
     │
     ▼
运行结果（IR/机器码） ───  使用，真正"活起来"

控制流的实现链条：

  Lexer 看到 "if" → 返回 tok_if
  Parser 看到 tok_if → 调用 ParseIfExpr() → 返回 IfExprAST
  CodeGen 看到 IfExprAST → 调用 codegenIf() → 生成 br + phi
  JIT 看到 IR → 翻译成机器码 → CPU 执行

每一个环节都像齿轮一样咬合，
计划文件描述了齿轮的形状，
代码实现了齿轮的转动。
```
