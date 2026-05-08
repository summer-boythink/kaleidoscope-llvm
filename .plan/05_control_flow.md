---
title: 第五章 - 扩展语言：控制流
chapter: 5
prerequisites: chapter 4
estimated_time: 3-4 hours
---

# 第五章：扩展语言 - 控制流

## 学习目标

完成本章后，你将能够：

1. 实现 if/then/else 条件表达式
2. 实现 for 循环
3. 理解 SSA 形式中的控制流
4. 使用 phi 节点合并控制流
5. 处理作用域和变量生命周期

---

## 控制流概述

### Kaleidoscope 控制流语法

**if/then/else**：
```kaleidoscope
def foo(x) 
  if x < 10 then
    x * 2
  else
    x + 1;
```

**for 循环**：
```kaleidoscope
# 计算 1 到 n 的和
def sum(n)
  var result = 0, i = 1 in
  for i = 1, i < n, 1.0 in
    result = result + i
  in result;
```

### 控制流图 (CFG)

控制流图表示程序的执行路径：

```
if/then/else 的 CFG:

       [entry]
          |
          v
     [condition]
      /      \
   [then]   [else]
      \      /
       v    v
       [merge]
```

---

## if/then/else 实现

### 1. AST 节点

```cpp
// 在 ast.hpp 中添加

/// IfExprAST - if/then/else 表达式
class IfExprAST : public ExprAST {
    std::unique_ptr<ExprAST> Cond;   // 条件表达式
    std::unique_ptr<ExprAST> Then;   // then 分支
    std::unique_ptr<ExprAST> Else;   // else 分支（可为空）

public:
    IfExprAST(std::unique_ptr<ExprAST> Cond, 
              std::unique_ptr<ExprAST> Then,
              std::unique_ptr<ExprAST> Else)
        : Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}
    
    const ExprAST *getCond() const { return Cond.get(); }
    const ExprAST *getThen() const { return Then.get(); }
    const ExprAST *getElse() const { return Else.get(); }
};
```

### 2. 词法分析器

```cpp
// 在 Token 枚举中已定义：
// tok_if = -6
// tok_then = -7
// tok_else = -8

// 关键字映射表中已添加：
// {"if", tok_if},
// {"then", tok_then},
// {"else", tok_else}
```

### 3. 语法分析器

```cpp
// 在 parser.cpp 中添加

/// ParseIfExpr - 解析 if/then/else 表达式
/// ifexpr ::= 'if' expression 'then' expression 'else' expression
std::unique_ptr<ExprAST> Parser::ParseIfExpr() {
    getNextToken();  // 消费 'if'
    
    // 解析条件
    auto Cond = ParseExpression();
    if (!Cond) {
        return nullptr;
    }
    
    // 检查 'then'
    if (CurTok != tok_then) {
        return LogError("expected 'then'");
    }
    getNextToken();  // 消费 'then'
    
    // 解析 then 分支
    auto Then = ParseExpression();
    if (!Then) {
        return nullptr;
    }
    
    // 检查 'else'
    std::unique_ptr<ExprAST> Else = nullptr;
    if (CurTok == tok_else) {
        getNextToken();  // 消费 'else'
        Else = ParseExpression();
        if (!Else) {
            return nullptr;
        }
    }
    
    return std::make_unique<IfExprAST>(std::move(Cond), 
                                        std::move(Then), 
                                        std::move(Else));
}

// 在 ParsePrimary 中添加：
std::unique_ptr<ExprAST> Parser::ParsePrimary() {
    switch (CurTok) {
    // ... 其他 case ...
    case tok_if:
        return ParseIfExpr();
    // ...
    }
}
```

### 4. 代码生成

```cpp
// 在 codegen.cpp 中添加

llvm::Value *CodeGenerator::codegen(const IfExprAST *Expr) {
    // 1. 生成条件代码
    llvm::Value *CondV = codegen(Expr->getCond());
    if (!CondV) {
        return nullptr;
    }
    
    // 2. 将条件转换为布尔值（i1）
    // 比较：CondV != 0.0
    CondV = Builder->CreateFCmpONE(
        CondV, 
        llvm::ConstantFP::get(*TheContext, llvm::APFloat(0.0)), 
        "ifcond"
    );
    
    // 3. 获取当前函数
    llvm::Function *TheFunction = Builder->GetInsertBlock()->getParent();
    
    // 4. 创建基本块
    // then 块：条件为真时执行
    llvm::BasicBlock *ThenBB = llvm::BasicBlock::Create(*TheContext, "then", TheFunction);
    // else 块：条件为假时执行
    llvm::BasicBlock *ElseBB = llvm::BasicBlock::Create(*TheContext, "else");
    // merge 块：两个分支汇合点
    llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(*TheContext, "ifcont");
    
    // 5. 创建条件分支
    Builder->CreateCondBr(CondV, ThenBB, ElseBB);
    
    // 6. 生成 then 分支代码
    Builder->SetInsertPoint(ThenBB);
    llvm::Value *ThenV = codegen(Expr->getThen());
    if (!ThenV) {
        return nullptr;
    }
    
    // 跳转到 merge 块
    Builder->CreateBr(MergeBB);
    // 更新 ThenBB（因为代码生成可能添加了新块）
    ThenBB = Builder->GetInsertBlock();
    
    // 7. 生成 else 分支代码
    TheFunction->insert(TheFunction->end(), ElseBB);
    Builder->SetInsertPoint(ElseBB);
    
    llvm::Value *ElseV = nullptr;
    if (Expr->getElse()) {
        ElseV = codegen(Expr->getElse());
        if (!ElseV) {
            return nullptr;
        }
    } else {
        // 没有 else 分支，返回 0.0
        ElseV = llvm::ConstantFP::get(*TheContext, llvm::APFloat(0.0));
    }
    
    // 跳转到 merge 块
    Builder->CreateBr(MergeBB);
    // 更新 ElseBB
    ElseBB = Builder->GetInsertBlock();
    
    // 8. 生成 merge 块
    TheFunction->insert(TheFunction->end(), MergeBB);
    Builder->SetInsertPoint(MergeBB);
    
    // 9. 创建 PHI 节点合并两个分支的结果
    llvm::PHINode *PN = Builder->CreatePHI(llvm::Type::getDoubleTy(*TheContext), 2, "iftmp");
    PN->addIncoming(ThenV, ThenBB);
    PN->addIncoming(ElseV, ElseBB);
    
    return PN;
}
```

### 生成 IR 示例

对于 `def foo(x) if x < 10 then x * 2 else x + 1`：

```llvm
define double @foo(double %x) {
entry:
    %cmptmp = fcmp ult double %x, 1.000000e+01
    %ifcond = fcmp one i1 %cmptmp, false
    br i1 %ifcond, label %then, label %else

then:                                             ; preds = %entry
    %multmp = fmul double %x, 2.000000e+00
    br label %ifcont

else:                                             ; preds = %entry
    %addtmp = fadd double %x, 1.000000e+00
    br label %ifcont

ifcont:                                           ; preds = %else, %then
    %iftmp = phi double [%multmp, %then], [%addtmp, %else]
    ret double %iftmp
}
```

---

## PHI 节点详解

### 什么是 PHI 节点？

PHI 节点是 SSA 形式中用于合并不同控制流路径值的特殊指令。

**语法**：
```llvm
%result = phi type [value1, label1], [value2, label2], ...
```

**语义**：
- 如果控制流从 `label1` 跳转而来，`%result = value1`
- 如果控制流从 `label2` 跳转而来，`%result = value2`

### 为什么需要 PHI？

**问题**：SSA 要求每个变量只赋值一次，但控制流可能导致同一变量在不同路径有不同的值。

**解决方案**：PHI 节点在控制流汇合处合并这些值。

### 示例

```llvm
; 传统形式（非 SSA）
entry:
    br i1 %cond, label %then, label %else
then:
    %x = 1
    br label %merge
else:
    %x = 2          ; 错误！x 被重新赋值
    br label %merge
merge:
    use(%x)         ; x 是 1 还是 2？

; SSA 形式（使用 PHI）
entry:
    br i1 %cond, label %then, label %else
then:
    %x1 = 1         ; 第一个定义
    br label %merge
else:
    %x2 = 2         ; 第二个定义（不同的变量名）
    br label %merge
merge:
    %x = phi i32 [%x1, %then], [%x2, %else]  ; PHI 节点合并
    use(%x)
```

---

## for 循环实现

### 1. AST 节点

```cpp
/// ForExprAST - for 循环表达式
/// 语法: for var = start, cond, step in body
class ForExprAST : public ExprAST {
    std::string VarName;                    // 循环变量名
    std::unique_ptr<ExprAST> Start;         // 起始值
    std::unique_ptr<ExprAST> End;           // 结束条件
    std::unique_ptr<ExprAST> Step;          // 步长（可为空，默认 1.0）
    std::unique_ptr<ExprAST> Body;          // 循环体

public:
    ForExprAST(const std::string &VarName,
               std::unique_ptr<ExprAST> Start,
               std::unique_ptr<ExprAST> End,
               std::unique_ptr<ExprAST> Step,
               std::unique_ptr<ExprAST> Body)
        : VarName(VarName), Start(std::move(Start)), End(std::move(End)),
          Step(std::move(Step)), Body(std::move(Body)) {}
    
    const std::string &getVarName() const { return VarName; }
    const ExprAST *getStart() const { return Start.get(); }
    const ExprAST *getEnd() const { return End.get(); }
    const ExprAST *getStep() const { return Step ? Step.get() : nullptr; }
    const ExprAST *getBody() const { return Body.get(); }
};
```

### 2. 语法分析器

```cpp
/// ParseForExpr - 解析 for 循环
/// forexpr ::= 'for' identifier '=' expr ',' expr (',' expr)? 'in' expression
std::unique_ptr<ExprAST> Parser::ParseForExpr() {
    getNextToken();  // 消费 'for'
    
    // 检查变量名
    if (CurTok != tok_identifier) {
        return LogError("expected identifier after 'for'");
    }
    
    std::string IdName = IdentifierStr;
    getNextToken();  // 消费标识符
    
    // 检查 '='
    if (CurTok != '=') {
        return LogError("expected '=' after 'for'");
    }
    getNextToken();  // 消费 '='
    
    // 解析起始值
    auto Start = ParseExpression();
    if (!Start) {
        return nullptr;
    }
    
    // 检查 ','
    if (CurTok != ',') {
        return LogError("expected ',' after start value");
    }
    getNextToken();  // 消费 ','
    
    // 解析结束条件
    auto End = ParseExpression();
    if (!End) {
        return nullptr;
    }
    
    // 可选的步长
    std::unique_ptr<ExprAST> Step;
    if (CurTok == ',') {
        getNextToken();  // 消费 ','
        Step = ParseExpression();
        if (!Step) {
            return nullptr;
        }
    }
    
    // 检查 'in'
    if (CurTok != tok_in) {
        return LogError("expected 'in' after for");
    }
    getNextToken();  // 消费 'in'
    
    // 解析循环体
    auto Body = ParseExpression();
    if (!Body) {
        return nullptr;
    }
    
    return std::make_unique<ForExprAST>(IdName, std::move(Start),
                                         std::move(End), std::move(Step),
                                         std::move(Body));
}

// 在 ParsePrimary 中添加：
case tok_for:
    return ParseForExpr();
```

### 3. 代码生成

```cpp
llvm::Value *CodeGenerator::codegen(const ForExprAST *Expr) {
    // 1. 生成起始值
    llvm::Value *StartVal = codegen(Expr->getStart());
    if (!StartVal) {
        return nullptr;
    }
    
    // 2. 创建循环前的基本块
    llvm::Function *TheFunction = Builder->GetInsertBlock()->getParent();
    llvm::BasicBlock *PreheaderBB = Builder->GetInsertBlock();
    
    // 3. 创建循环条件检查块
    llvm::BasicBlock *LoopBB = llvm::BasicBlock::Create(*TheContext, "loop", TheFunction);
    
    // 4. 跳转到循环块
    Builder->CreateBr(LoopBB);
    
    // 5. 开始生成循环块
    Builder->SetInsertPoint(LoopBB);
    
    // 6. 创建 PHI 节点接收初始值和迭代值
    llvm::PHINode *Variable = Builder->CreatePHI(
        llvm::Type::getDoubleTy(*TheContext), 2, Expr->getVarName()
    );
    Variable->addIncoming(StartVal, PreheaderBB);
    
    // 7. 在符号表中记录循环变量
    // 保存旧值（如果有同名变量）
    llvm::Value *OldVal = NamedValues[Expr->getVarName()];
    NamedValues[Expr->getVarName()] = Variable;
    
    // 8. 生成循环体代码
    if (!codegen(Expr->getBody())) {
        // 恢复符号表
        if (OldVal) {
            NamedValues[Expr->getVarName()] = OldVal;
        } else {
            NamedValues.erase(Expr->getVarName());
        }
        return nullptr;
    }
    
    // 9. 生成步长
    llvm::Value *StepVal = nullptr;
    if (Expr->getStep()) {
        StepVal = codegen(Expr->getStep());
        if (!StepVal) {
            return nullptr;
        }
    } else {
        // 默认步长 1.0
        StepVal = llvm::ConstantFP::get(*TheContext, llvm::APFloat(1.0));
    }
    
    // 10. 计算下一次迭代的值
    llvm::Value *NextVar = Builder->CreateFAdd(Variable, StepVal, "nextvar");
    
    // 11. 生成结束条件
    llvm::Value *EndCond = codegen(Expr->getEnd());
    if (!EndCond) {
        return nullptr;
    }
    
    // 转换为布尔值
    EndCond = Builder->CreateFCmpONE(
        EndCond, 
        llvm::ConstantFP::get(*TheContext, llvm::APFloat(0.0)), 
        "loopcond"
    );
    
    // 12. 创建循环后块
    llvm::BasicBlock *LoopEndBB = Builder->GetInsertBlock();
    llvm::BasicBlock *AfterBB = llvm::BasicBlock::Create(*TheContext, "afterloop", TheFunction);
    
    // 13. 创建条件分支
    Builder->CreateCondBr(EndCond, LoopBB, AfterBB);
    
    // 14. 为 PHI 添加后续迭代值
    Variable->addIncoming(NextVar, LoopEndBB);
    
    // 15. 恢复符号表
    if (OldVal) {
        NamedValues[Expr->getVarName()] = OldVal;
    } else {
        NamedValues.erase(Expr->getVarName());
    }
    
    // 16. 继续生成循环后代码
    Builder->SetInsertPoint(AfterBB);
    
    // for 循环返回 0.0
    return llvm::ConstantFP::get(*TheContext, llvm::APFloat(0.0));
}
```

### 生成 IR 示例

对于 `for i = 1, i < n, 1.0 in body`：

```llvm
define double @loop(double %n) {
entry:
    br label %loop

loop:                                             ; preds = %loop, %entry
    %i = phi double [1.000000e+00, %entry], [%nextvar, %loop]
    ; ... 循环体代码 ...
    %nextvar = fadd double %i, 1.000000e+00
    %cmptmp = fcmp ult double %i, %n
    %loopcond = fcmp one i1 %cmptmp, false
    br i1 %loopcond, label %loop, label %afterloop

afterloop:                                        ; preds = %loop
    ret double 0.000000e+00
}
```

---

## 控制流图可视化

### 使用 Graphviz 可视化

```cpp
// 在代码生成器中添加
void CodeGenerator::viewCFG(llvm::Function *F) {
    F->viewCFG();  // 自动生成并显示 CFG
}
```

### 手动生成 DOT 格式

```bash
# 使用 opt 生成 DOT 文件
opt -passes=dot-cfg input.ll -disable-output

# 使用 Graphviz 查看
dot -Tpng cfg.foo.dot -o cfg.png
```

---

## 示例：斐波那契数列

### 递归版本

```kaleidoscope
def fib(x)
  if x < 3 then
    1
  else
    fib(x-1) + fib(x-2);
```

### 迭代版本（使用 for 循环）

```kaleidoscope
def fibi(n)
  var a = 1, b = 1 in
  (for i = 3, i < n, 1.0 in
     var t = a in
     (a = b, b = t + b)
  in a) + b;
```

注：完整的迭代版本需要第七章的变量支持。

---

## 练习题

### 练习 1：添加 while 循环

实现 `while cond do body` 循环：

1. 添加 AST 节点 `WhileExprAST`
2. 添加词法分析和语法分析
3. 生成代码

提示：
```cpp
class WhileExprAST : public ExprAST {
    std::unique_ptr<ExprAST> Cond;
    std::unique_ptr<ExprAST> Body;
    // ...
};
```

### 练习 2：添加 break 和 continue

为 for 循环添加 `break` 和 `continue` 支持：

1. 添加 Token `tok_break` 和 `tok_continue`
2. 在代码生成时保存 loop 的基本块引用
3. `break` 跳转到 afterloop 块
4. `continue` 跳转到 loop 块

### 练习 3：嵌套控制流

测试嵌套的 if 和 for：

```kaleidoscope
def test(x n)
  for i = 1, i < n, 1 in
    if i < x then
      i * 2
    else
      i + 1;
```

---

## 常见问题

### Q: PHI 节点的顺序重要吗？

**A**: PHI 节点必须在基本块的开头，并且所有 PHI 节点必须连续排列。LLVM IRBuilder 会自动处理这一点。

### Q: 为什么 for 循环返回 0.0？

**A**: Kaleidoscope 的每个表达式都必须有值。for 循环作为一个表达式，我们简单地返回 0.0。你可以修改它返回最后一个迭代的值。

### Q: 如何处理空 else 分支？

**A**: 如果没有 else 分支，我们生成一个默认值（0.0）作为 else 分支的结果。

### Q: 循环变量作用域是什么？

**A**: 循环变量在循环体内可见，循环结束后恢复之前的值（如果存在同名变量）。这就是为什么我们需要保存 `OldVal` 并在循环后恢复。

---

## 下一步

完成控制流后，你的语言已经可以进行复杂的计算了！

在 [下一章](06_user_operators.md) 中，我们将学习：
- 如何支持用户定义的二元运算符
- 如何支持用户定义的一元运算符
- 如何设置运算符优先级

---

## 验证清单

在进入下一章之前，确保：

- [ ] 你理解了控制流图的概念
- [ ] if/then/else 能正确生成代码
- [ ] 理解了 PHI 节点的作用
- [ ] for 循环能正确生成代码
- [ ] 循环变量作用域正确处理
- [ ] 能处理嵌套控制流
- [ ] 测试程序运行正常
