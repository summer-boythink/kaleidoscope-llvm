#ifndef KALEIDOSCOPE_CODEGEN_HPP
#define KALEIDOSCOPE_CODEGEN_HPP

#include "ast.hpp"
#include "lexer.hpp"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace kaleidoscope {

/// CodeGenerator 类：代码生成器
class CodeGenerator {
public:
    CodeGenerator();

    /// 初始化模块
    void InitializeModule();

    /// 代码生成入口
    llvm::Value* codegen(const ExprAST* Expr);
    llvm::Function* codegen(const PrototypeAST* Proto);
    llvm::Function* codegen(const FunctionAST* Func);

    /// 获取模块
    llvm::Module* getModule() const { return TheModule.get(); }

    /// 获取上下文
    llvm::LLVMContext* getContext() const { return TheContext.get(); }

    /// 打印 IR
    void printIR();

    /// 获取函数原型
    llvm::Function* getFunction(const std::string& Name);

    /// 添加函数原型到表中
    void addPrototype(std::unique_ptr<PrototypeAST> Proto);

    /// 获取错误信息
    const std::string& getLastError() const { return LastError; }

private:
    /// LLVM 核心对象
    std::unique_ptr<llvm::LLVMContext> TheContext;
    std::unique_ptr<llvm::Module> TheModule;
    std::unique_ptr<llvm::IRBuilder<>> Builder;

    /// 符号表：记录当前可见的变量名和对应的 LLVM Value
    std::unordered_map<std::string, llvm::Value*> NamedValues;

    /// 函数原型表：记录已声明的函数原型
    std::unordered_map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;

    /// 错误信息
    std::string LastError;

    /// 各类型 AST 节点的代码生成
    llvm::Value* codegenNumber(const NumberExprAST* Expr);
    llvm::Value* codegenVariable(const VariableExprAST* Expr);
    llvm::Value* codegenBinary(const BinaryExprAST* Expr);
    llvm::Value* codegenUnary(const UnaryExprAST* Expr);
    llvm::Value* codegenCall(const CallExprAST* Expr);

    /// 错误处理
    llvm::Value* LogErrorV(const char* Str);
};

} // namespace kaleidoscope

#endif // KALEIDOSCOPE_CODEGEN_HPP
