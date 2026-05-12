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

    // 用于调试输出的虚函数
    virtual std::string toString() const = 0;
};

/// NumberExprAST - 数字字面量表达式，如 "1.0"
class NumberExprAST : public ExprAST {
    double Val;

public:
    explicit NumberExprAST(double Val) : Val(Val) {}

    double getValue() const { return Val; }

    std::string toString() const override {
        return "Number(" + std::to_string(Val) + ")";
    }
};

/// VariableExprAST - 变量引用表达式，如 "x"
class VariableExprAST : public ExprAST {
    std::string Name;

public:
    explicit VariableExprAST(const std::string& Name) : Name(Name) {}

    const std::string& getName() const { return Name; }

    std::string toString() const override {
        return "Variable(" + Name + ")";
    }
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
    const ExprAST* getLHS() const { return LHS.get(); }
    const ExprAST* getRHS() const { return RHS.get(); }

    std::string toString() const override {
        return "Binary('" + std::string(1, Op) + "', " +
               (LHS ? LHS->toString() : "null") + ", " +
               (RHS ? RHS->toString() : "null") + ")";
    }
};

/// UnaryExprAST - 一元运算表达式，如 "-x"
class UnaryExprAST : public ExprAST {
    char Opcode;                         // 运算符，如 '-', '!'
    std::unique_ptr<ExprAST> Operand;    // 操作数

public:
    UnaryExprAST(char Opcode, std::unique_ptr<ExprAST> Operand)
        : Opcode(Opcode), Operand(std::move(Operand)) {}

    char getOpcode() const { return Opcode; }
    const ExprAST* getOperand() const { return Operand.get(); }

    std::string toString() const override {
        return "Unary('" + std::string(1, Opcode) + "', " +
               (Operand ? Operand->toString() : "null") + ")";
    }
};

/// CallExprAST - 函数调用表达式，如 "sin(1.0)"
class CallExprAST : public ExprAST {
    std::string Callee;                           // 被调用的函数名
    std::vector<std::unique_ptr<ExprAST>> Args;   // 参数列表

public:
    CallExprAST(const std::string& Callee,
                std::vector<std::unique_ptr<ExprAST>> Args)
        : Callee(Callee), Args(std::move(Args)) {}

    const std::string& getCallee() const { return Callee; }
    const std::vector<std::unique_ptr<ExprAST>>& getArgs() const { return Args; }

    std::string toString() const override {
        std::string result = "Call(" + Callee + ", [";
        for (size_t i = 0; i < Args.size(); ++i) {
            if (i > 0) result += ", ";
            result += Args[i] ? Args[i]->toString() : "null";
        }
        result += "])";
        return result;
    }
};

/// IfExprAST - if/then/else 条件表达式
/// 语法: if cond then then_expr else else_expr
class IfExprAST : public ExprAST {
    std::unique_ptr<ExprAST> Cond;   // 条件表达式
    std::unique_ptr<ExprAST> Then;   // then 分支
    std::unique_ptr<ExprAST> Else;   // else 分支（可为空）

public:
    IfExprAST(std::unique_ptr<ExprAST> Cond,
              std::unique_ptr<ExprAST> Then,
              std::unique_ptr<ExprAST> Else)
        : Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}

    const ExprAST* getCond() const { return Cond.get(); }
    const ExprAST* getThen() const { return Then.get(); }
    const ExprAST* getElse() const { return Else.get(); }

    std::string toString() const override {
        return "If(" + (Cond ? Cond->toString() : "null") + ", " +
               (Then ? Then->toString() : "null") + ", " +
               (Else ? Else->toString() : "null") + ")";
    }
};

/// ForExprAST - for 循环表达式
/// 语法: for var = start, cond, step in body
class ForExprAST : public ExprAST {
    std::string VarName;                    // 循环变量名
    std::unique_ptr<ExprAST> Start;         // 起始值
    std::unique_ptr<ExprAST> End;           // 结束条件
    std::unique_ptr<ExprAST> Step;          // 步长（可为空，默认 1.0）
    std::unique_ptr<ExprAST> Body;          // 循环体

public:
    ForExprAST(const std::string& VarName,
               std::unique_ptr<ExprAST> Start,
               std::unique_ptr<ExprAST> End,
               std::unique_ptr<ExprAST> Step,
               std::unique_ptr<ExprAST> Body)
        : VarName(VarName), Start(std::move(Start)), End(std::move(End)),
          Step(std::move(Step)), Body(std::move(Body)) {}

    const std::string& getVarName() const { return VarName; }
    const ExprAST* getStart() const { return Start.get(); }
    const ExprAST* getEnd() const { return End.get(); }
    const ExprAST* getStep() const { return Step.get(); }
    const ExprAST* getBody() const { return Body.get(); }

    std::string toString() const override {
        return "For(" + VarName + ", " +
               (Start ? Start->toString() : "null") + ", " +
               (End ? End->toString() : "null") + ", " +
               (Step ? Step->toString() : "null") + ", " +
               (Body ? Body->toString() : "null") + ")";
    }
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
    PrototypeAST(const std::string& Name, std::vector<std::string> Args)
        : Name(Name), Args(std::move(Args)) {}

    const std::string& getName() const { return Name; }
    const std::vector<std::string>& getArgs() const { return Args; }

    std::string toString() const {
        std::string result = "Prototype(" + Name + ", [";
        for (size_t i = 0; i < Args.size(); ++i) {
            if (i > 0) result += ", ";
            result += Args[i];
        }
        result += "])";
        return result;
    }
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

    const PrototypeAST* getProto() const { return Proto.get(); }
    const ExprAST* getBody() const { return Body.get(); }

    std::string toString() const {
        return "Function(" + (Proto ? Proto->toString() : "null") + ", " +
               (Body ? Body->toString() : "null") + ")";
    }
};

} // namespace kaleidoscope

#endif // KALEIDOSCOPE_AST_HPP
