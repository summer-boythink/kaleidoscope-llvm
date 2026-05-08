#include "codegen.hpp"
#include <iostream>

namespace kaleidoscope {

CodeGenerator::CodeGenerator() {
    TheContext = std::make_unique<llvm::LLVMContext>();
    TheModule = std::make_unique<llvm::Module>("Kaleidoscope", *TheContext);
    Builder = std::make_unique<llvm::IRBuilder<>>(*TheContext);
}

void CodeGenerator::InitializeModule() {
    // 重新初始化模块（用于 REPL 循环）
    TheModule = std::make_unique<llvm::Module>("Kaleidoscope", *TheContext);
    NamedValues.clear();
}

void CodeGenerator::printIR() {
    TheModule->print(llvm::outs(), nullptr);
}

llvm::Value* CodeGenerator::LogErrorV(const char* Str) {
    LastError = Str;
    std::cerr << "Error: " << Str << "\n";
    return nullptr;
}

llvm::Function* CodeGenerator::getFunction(const std::string& Name) {
    // 首先检查模块中是否已经有这个函数
    if (auto* F = TheModule->getFunction(Name)) {
        return F;
    }

    // 如果没有，检查是否已声明过原型
    auto FI = FunctionProtos.find(Name);
    if (FI != FunctionProtos.end()) {
        return codegen(FI->second.get());
    }

    return nullptr;
}

void CodeGenerator::addPrototype(std::unique_ptr<PrototypeAST> Proto) {
    FunctionProtos[Proto->getName()] = std::move(Proto);
}

/// codegenNumber - 数字表达式代码生成
llvm::Value* CodeGenerator::codegenNumber(const NumberExprAST* Expr) {
    return llvm::ConstantFP::get(*TheContext, llvm::APFloat(Expr->getValue()));
}

/// codegenVariable - 变量表达式代码生成
llvm::Value* CodeGenerator::codegenVariable(const VariableExprAST* Expr) {
    // 在符号表中查找变量
    llvm::Value* V = NamedValues[Expr->getName()];
    if (!V) {
        return LogErrorV(("Unknown variable name: " + Expr->getName()).c_str());
    }
    return V;
}

/// codegenBinary - 二元运算表达式代码生成
llvm::Value* CodeGenerator::codegenBinary(const BinaryExprAST* Expr) {
    // 递归生成左右操作数的代码
    llvm::Value* L = codegen(Expr->getLHS());
    llvm::Value* R = codegen(Expr->getRHS());

    if (!L || !R) {
        return nullptr;
    }

    // 根据运算符生成对应的指令
    switch (Expr->getOp()) {
    case '+':
        return Builder->CreateFAdd(L, R, "addtmp");
    case '-':
        return Builder->CreateFSub(L, R, "subtmp");
    case '*':
        return Builder->CreateFMul(L, R, "multmp");
    case '/':
        return Builder->CreateFDiv(L, R, "divtmp");
    case '<':
        // 比较：创建 fcmp 指令
        L = Builder->CreateFCmpULT(L, R, "cmptmp");
        // 将 i1 (bool) 转换为 double (0.0 或 1.0)
        return Builder->CreateUIToFP(L, llvm::Type::getDoubleTy(*TheContext), "booltmp");
    default:
        return LogErrorV(("Invalid binary operator: " + std::string(1, Expr->getOp())).c_str());
    }
}

/// codegenUnary - 一元运算表达式代码生成
llvm::Value* CodeGenerator::codegenUnary(const UnaryExprAST* Expr) {
    llvm::Value* OperandV = codegen(Expr->getOperand());
    if (!OperandV) {
        return nullptr;
    }

    switch (Expr->getOpcode()) {
    case '-':
        return Builder->CreateFNeg(OperandV, "negtmp");
    default:
        return LogErrorV(("Invalid unary operator: " + std::string(1, Expr->getOpcode())).c_str());
    }
}

/// codegenCall - 函数调用表达式代码生成
llvm::Value* CodeGenerator::codegenCall(const CallExprAST* Expr) {
    // 查找函数名
    llvm::Function* CalleeF = getFunction(Expr->getCallee());
    if (!CalleeF) {
        return LogErrorV(("Unknown function referenced: " + Expr->getCallee()).c_str());
    }

    // 检查参数数量
    if (CalleeF->arg_size() != Expr->getArgs().size()) {
        return LogErrorV("Incorrect number of arguments passed");
    }

    // 生成参数代码
    std::vector<llvm::Value*> ArgsV;
    for (const auto& Arg : Expr->getArgs()) {
        llvm::Value* ArgV = codegen(Arg.get());
        if (!ArgV) {
            return nullptr;
        }
        ArgsV.push_back(ArgV);
    }

    // 创建 call 指令
    return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

/// codegen - 表达式代码生成入口（使用动态类型分发）
llvm::Value* CodeGenerator::codegen(const ExprAST* Expr) {
    if (!Expr) {
        return nullptr;
    }

    if (auto* E = dynamic_cast<const NumberExprAST*>(Expr)) {
        return codegenNumber(E);
    } else if (auto* E = dynamic_cast<const VariableExprAST*>(Expr)) {
        return codegenVariable(E);
    } else if (auto* E = dynamic_cast<const BinaryExprAST*>(Expr)) {
        return codegenBinary(E);
    } else if (auto* E = dynamic_cast<const UnaryExprAST*>(Expr)) {
        return codegenUnary(E);
    } else if (auto* E = dynamic_cast<const CallExprAST*>(Expr)) {
        return codegenCall(E);
    }

    return LogErrorV("Unknown expression type");
}

/// codegen - 函数原型代码生成
llvm::Function* CodeGenerator::codegen(const PrototypeAST* Proto) {
    // 创建函数类型：double(double, double, ..., double)
    std::vector<llvm::Type*> Doubles(Proto->getArgs().size(),
                                      llvm::Type::getDoubleTy(*TheContext));

    llvm::FunctionType* FT = llvm::FunctionType::get(
        llvm::Type::getDoubleTy(*TheContext),  // 返回类型
        Doubles,                                // 参数类型
        false                                   // 不接受可变参数
    );

    // 创建函数
    llvm::Function* F = llvm::Function::Create(
        FT,
        llvm::Function::ExternalLinkage,       // 链接类型
        Proto->getName(),                       // 函数名
        TheModule.get()                         // 所属模块
    );

    // 设置参数名（用于调试）
    unsigned Idx = 0;
    for (auto& Arg : F->args()) {
        Arg.setName(Proto->getArgs()[Idx++]);
    }

    return F;
}

/// codegen - 函数定义代码生成
llvm::Function* CodeGenerator::codegen(const FunctionAST* Func) {
    // 首先获取函数原型
    const PrototypeAST* Proto = Func->getProto();

    // 将原型添加到表中（如果还没有）
    if (FunctionProtos.find(Proto->getName()) == FunctionProtos.end()) {
        FunctionProtos[Proto->getName()] = std::make_unique<PrototypeAST>(
            Proto->getName(),
            Proto->getArgs()
        );
    }

    // 生成函数原型
    llvm::Function* TheFunction = codegen(Proto);
    if (!TheFunction) {
        return nullptr;
    }

    // 如果函数已有定义（之前声明过 extern），需要检查
    if (!TheFunction->empty()) {
        return static_cast<llvm::Function*>(
            LogErrorV(("Function cannot be redefined: " + Proto->getName()).c_str())
        );
    }

    // 创建基本块
    llvm::BasicBlock* BB = llvm::BasicBlock::Create(*TheContext, "entry", TheFunction);
    Builder->SetInsertPoint(BB);

    // 将参数添加到符号表
    NamedValues.clear();
    for (auto& Arg : TheFunction->args()) {
        NamedValues[std::string(Arg.getName())] = &Arg;
    }

    // 生成函数体代码
    if (llvm::Value* RetVal = codegen(Func->getBody())) {
        // 创建返回指令
        Builder->CreateRet(RetVal);

        // 验证函数
        if (llvm::verifyFunction(*TheFunction, &llvm::errs())) {
            std::cerr << "Error: Function verification failed\n";
            TheFunction->eraseFromParent();
            return nullptr;
        }

        return TheFunction;
    }

    // 函数体生成失败，删除函数
    TheFunction->eraseFromParent();
    return nullptr;
}

} // namespace kaleidoscope
