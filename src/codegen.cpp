#include "codegen.hpp"
#include "llvm/Support/TargetSelect.h"
#include <iostream>

namespace kaleidoscope {

static bool InitializeLLVMTargets() {
    static bool Initialized = false;
    if (!Initialized) {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
        Initialized = true;
    }
    return Initialized;
}

CodeGenerator::CodeGenerator() {
    InitializeLLVMTargets();
    TheContext = std::make_unique<llvm::LLVMContext>();
    TheModule = std::make_unique<llvm::Module>("Kaleidoscope", *TheContext);
    Builder = std::make_unique<llvm::IRBuilder<>>(*TheContext);
    TheOptimizer = std::make_unique<Optimizer>(OptimizationLevel::O2);

    try {
        TheJIT = std::make_unique<KaleidoscopeJIT>();
        if (!TheJIT->isAvailable()) {
            std::cerr << "Running in IR-only mode.\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Warning: JIT creation failed: " << e.what() << "\n";
        std::cerr << "Running in IR-only mode.\n";
        TheJIT = nullptr;
    }
}

void CodeGenerator::InitializeModule() {
    // 重新创建上下文和模块（因为可能被移动到 JIT）
    TheContext = std::make_unique<llvm::LLVMContext>();
    TheModule = std::make_unique<llvm::Module>("Kaleidoscope", *TheContext);
    Builder = std::make_unique<llvm::IRBuilder<>>(*TheContext);
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
    if (auto* F = TheModule->getFunction(Name)) return F;
    auto FI = FunctionProtos.find(Name);
    if (FI != FunctionProtos.end()) return codegen(FI->second.get());
    return nullptr;
}

void CodeGenerator::addPrototype(std::unique_ptr<PrototypeAST> Proto) {
    FunctionProtos[Proto->getName()] = std::move(Proto);
}

llvm::Value* CodeGenerator::codegenNumber(const NumberExprAST* Expr) {
    return llvm::ConstantFP::get(*TheContext, llvm::APFloat(Expr->getValue()));
}

llvm::Value* CodeGenerator::codegenVariable(const VariableExprAST* Expr) {
    llvm::Value* V = NamedValues[Expr->getName()];
    if (!V) return LogErrorV(("Unknown variable name: " + Expr->getName()).c_str());
    return V;
}

llvm::Value* CodeGenerator::codegenBinary(const BinaryExprAST* Expr) {
    llvm::Value* L = codegen(Expr->getLHS());
    llvm::Value* R = codegen(Expr->getRHS());
    if (!L || !R) return nullptr;
    switch (Expr->getOp()) {
    case '+': return Builder->CreateFAdd(L, R, "addtmp");
    case '-': return Builder->CreateFSub(L, R, "subtmp");
    case '*': return Builder->CreateFMul(L, R, "multmp");
    case '/': return Builder->CreateFDiv(L, R, "divtmp");
    case '<':
        L = Builder->CreateFCmpULT(L, R, "cmptmp");
        return Builder->CreateUIToFP(L, llvm::Type::getDoubleTy(*TheContext), "booltmp");
    case '>':
        L = Builder->CreateFCmpUGT(L, R, "cmptmp");
        return Builder->CreateUIToFP(L, llvm::Type::getDoubleTy(*TheContext), "booltmp");
    default: return LogErrorV(("Invalid binary operator: " + std::string(1, Expr->getOp())).c_str());
    }
}

llvm::Value* CodeGenerator::codegenUnary(const UnaryExprAST* Expr) {
    llvm::Value* OperandV = codegen(Expr->getOperand());
    if (!OperandV) return nullptr;
    switch (Expr->getOpcode()) {
    case '-': return Builder->CreateFNeg(OperandV, "negtmp");
    default: return LogErrorV(("Invalid unary operator: " + std::string(1, Expr->getOpcode())).c_str());
    }
}

llvm::Value* CodeGenerator::codegenCall(const CallExprAST* Expr) {
    llvm::Function* CalleeF = getFunction(Expr->getCallee());
    if (!CalleeF) return LogErrorV(("Unknown function referenced: " + Expr->getCallee()).c_str());
    if (CalleeF->arg_size() != Expr->getArgs().size())
        return LogErrorV("Incorrect number of arguments passed");
    std::vector<llvm::Value*> ArgsV;
    for (const auto& Arg : Expr->getArgs()) {
        llvm::Value* ArgV = codegen(Arg.get());
        if (!ArgV) return nullptr;
        ArgsV.push_back(ArgV);
    }
    return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

llvm::Value* CodeGenerator::codegenIf(const IfExprAST* Expr) {
    // 1. 生成条件代码
    llvm::Value* CondV = codegen(Expr->getCond());
    if (!CondV) {
        return nullptr;
    }

    // 2. 将条件转换为布尔值（i1）：CondV != 0.0
    CondV = Builder->CreateFCmpONE(
        CondV,
        llvm::ConstantFP::get(*TheContext, llvm::APFloat(0.0)),
        "ifcond"
    );

    // 3. 获取当前函数
    llvm::Function* TheFunction = Builder->GetInsertBlock()->getParent();

    // 4. 创建基本块
    llvm::BasicBlock* ThenBB = llvm::BasicBlock::Create(*TheContext, "then", TheFunction);
    llvm::BasicBlock* ElseBB = llvm::BasicBlock::Create(*TheContext, "else");
    llvm::BasicBlock* MergeBB = llvm::BasicBlock::Create(*TheContext, "ifcont");

    // 5. 创建条件分支
    Builder->CreateCondBr(CondV, ThenBB, ElseBB);

    // 6. 生成 then 分支代码
    Builder->SetInsertPoint(ThenBB);
    llvm::Value* ThenV = codegen(Expr->getThen());
    if (!ThenV) {
        return nullptr;
    }
    Builder->CreateBr(MergeBB);
    ThenBB = Builder->GetInsertBlock();  // 代码生成可能添加了新块

    // 7. 生成 else 分支代码
    TheFunction->insert(TheFunction->end(), ElseBB);
    Builder->SetInsertPoint(ElseBB);

    llvm::Value* ElseV = nullptr;
    if (Expr->getElse()) {
        ElseV = codegen(Expr->getElse());
        if (!ElseV) {
            return nullptr;
        }
    } else {
        // 没有 else 分支，返回 0.0
        ElseV = llvm::ConstantFP::get(*TheContext, llvm::APFloat(0.0));
    }
    Builder->CreateBr(MergeBB);
    ElseBB = Builder->GetInsertBlock();  // 代码生成可能添加了新块

    // 8. 生成 merge 块
    TheFunction->insert(TheFunction->end(), MergeBB);
    Builder->SetInsertPoint(MergeBB);

    // 9. 创建 PHI 节点合并两个分支的结果
    llvm::PHINode* PN = Builder->CreatePHI(llvm::Type::getDoubleTy(*TheContext), 2, "iftmp");
    PN->addIncoming(ThenV, ThenBB);
    PN->addIncoming(ElseV, ElseBB);

    return PN;
}

llvm::Value* CodeGenerator::codegenFor(const ForExprAST* Expr) {
    // 1. 生成起始值
    llvm::Value* StartVal = codegen(Expr->getStart());
    if (!StartVal) {
        return nullptr;
    }

    // 2. 创建循环前的基本块
    llvm::Function* TheFunction = Builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* PreheaderBB = Builder->GetInsertBlock();

    // 3. 创建循环条件检查块
    llvm::BasicBlock* LoopBB = llvm::BasicBlock::Create(*TheContext, "loop", TheFunction);

    // 4. 跳转到循环块
    Builder->CreateBr(LoopBB);

    // 5. 开始生成循环块
    Builder->SetInsertPoint(LoopBB);

    // 6. 创建 PHI 节点接收初始值和迭代值
    llvm::PHINode* Variable = Builder->CreatePHI(
        llvm::Type::getDoubleTy(*TheContext), 2, Expr->getVarName()
    );
    Variable->addIncoming(StartVal, PreheaderBB);

    // 7. 在符号表中记录循环变量，保存旧值
    llvm::Value* OldVal = NamedValues[Expr->getVarName()];
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
    llvm::Value* StepVal = nullptr;
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
    llvm::Value* NextVar = Builder->CreateFAdd(Variable, StepVal, "nextvar");

    // 11. 生成结束条件
    llvm::Value* EndCond = codegen(Expr->getEnd());
    if (!EndCond) {
        return nullptr;
    }

    // 转换为布尔值：EndCond != 0.0
    EndCond = Builder->CreateFCmpONE(
        EndCond,
        llvm::ConstantFP::get(*TheContext, llvm::APFloat(0.0)),
        "loopcond"
    );

    // 12. 创建循环后块
    llvm::BasicBlock* LoopEndBB = Builder->GetInsertBlock();
    llvm::BasicBlock* AfterBB = llvm::BasicBlock::Create(*TheContext, "afterloop", TheFunction);

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

llvm::Value* CodeGenerator::codegen(const ExprAST* Expr) {
    if (!Expr) return nullptr;
    if (auto* E = dynamic_cast<const NumberExprAST*>(Expr)) return codegenNumber(E);
    if (auto* E = dynamic_cast<const VariableExprAST*>(Expr)) return codegenVariable(E);
    if (auto* E = dynamic_cast<const BinaryExprAST*>(Expr)) return codegenBinary(E);
    if (auto* E = dynamic_cast<const UnaryExprAST*>(Expr)) return codegenUnary(E);
    if (auto* E = dynamic_cast<const CallExprAST*>(Expr)) return codegenCall(E);
    if (auto* E = dynamic_cast<const IfExprAST*>(Expr)) return codegenIf(E);
    if (auto* E = dynamic_cast<const ForExprAST*>(Expr)) return codegenFor(E);
    return LogErrorV("Unknown expression type");
}

llvm::Function* CodeGenerator::codegen(const PrototypeAST* Proto) {
    std::vector<llvm::Type*> Doubles(Proto->getArgs().size(), llvm::Type::getDoubleTy(*TheContext));
    llvm::FunctionType* FT = llvm::FunctionType::get(llvm::Type::getDoubleTy(*TheContext), Doubles, false);
    llvm::Function* F = llvm::Function::Create(FT, llvm::Function::ExternalLinkage, Proto->getName(), TheModule.get());
    unsigned Idx = 0;
    for (auto& Arg : F->args()) Arg.setName(Proto->getArgs()[Idx++]);
    return F;
}

llvm::Function* CodeGenerator::codegen(const FunctionAST* Func) {
    const PrototypeAST* Proto = Func->getProto();
    if (FunctionProtos.find(Proto->getName()) == FunctionProtos.end())
        FunctionProtos[Proto->getName()] = std::make_unique<PrototypeAST>(Proto->getName(), Proto->getArgs());
    llvm::Function* TheFunction = codegen(Proto);
    if (!TheFunction) return nullptr;
    if (!TheFunction->empty())
        return static_cast<llvm::Function*>(LogErrorV(("Function cannot be redefined: " + Proto->getName()).c_str()));
    llvm::BasicBlock* BB = llvm::BasicBlock::Create(*TheContext, "entry", TheFunction);
    Builder->SetInsertPoint(BB);
    NamedValues.clear();
    for (auto& Arg : TheFunction->args()) NamedValues[std::string(Arg.getName())] = &Arg;
    if (llvm::Value* RetVal = codegen(Func->getBody())) {
        Builder->CreateRet(RetVal);
        if (llvm::verifyFunction(*TheFunction, &llvm::errs())) {
            std::cerr << "Error: Function verification failed\n";
            TheFunction->eraseFromParent();
            return nullptr;
        }
        return TheFunction;
    }
    TheFunction->eraseFromParent();
    return nullptr;
}

double CodeGenerator::executeTopLevelExpr(const FunctionAST* Func) {
    // 生成函数代码
    llvm::Function* F = codegen(Func);
    if (!F) {
        std::cerr << "Error: Failed to generate code for expression\n";
        return 0.0;
    }

    // 优化函数
    if (TheOptimizer) {
        TheOptimizer->optimize(F);
    }

    // 打印优化后的 IR
    std::cout << "Optimized IR:\n";
    F->print(llvm::outs());
    std::cout << "\n";

    // 检查 JIT 是否可用
    if (!TheJIT || !TheJIT->isAvailable()) {
        // JIT 不可用，只打印 IR
        InitializeModule();
        return 0.0;
    }

    // 验证函数
    if (llvm::verifyFunction(*F, &llvm::errs())) {
        std::cerr << "Error: Function verification failed after optimization\n";
        InitializeModule();
        return 0.0;
    }

    // 保存函数名
    std::string FuncName = F->getName().str();

    // 创建 ThreadSafeModule
    auto TSM = llvm::orc::ThreadSafeModule(
        std::move(TheModule),
        std::move(TheContext)
    );

    // 添加到 JIT (标记为匿名表达式)
    auto TSMPtr = std::make_unique<llvm::orc::ThreadSafeModule>(std::move(TSM));
    if (auto Err = TheJIT->addModule(std::move(TSMPtr), true)) {
        std::cerr << "Error: Failed to add module to JIT: "
                  << llvm::toString(std::move(Err)) << "\n";
        InitializeModule();
        return 0.0;
    }

    // 查找符号
    auto ExprSymbol = TheJIT->lookup(FuncName);
    if (!ExprSymbol) {
        std::cerr << "Error: Failed to lookup symbol: "
                  << llvm::toString(ExprSymbol.takeError()) << "\n";
        InitializeModule();
        return 0.0;
    }

    // 获取函数指针并调用
    double (*FP)() = reinterpret_cast<double(*)()>(*ExprSymbol);
    double Result = FP();

    // 重新初始化模块
    InitializeModule();

    return Result;
}

bool CodeGenerator::addFunctionToJIT(const FunctionAST* Func) {
    // 生成函数代码
    llvm::Function* F = codegen(Func);
    if (!F) {
        return false;
    }

    // 优化函数
    if (TheOptimizer) {
        TheOptimizer->optimize(F);
    }

    // 检查 JIT 是否可用
    if (!TheJIT || !TheJIT->isAvailable()) {
        // JIT 不可用，只保留在模块中
        return true;
    }

    // 验证函数
    if (llvm::verifyFunction(*F, &llvm::errs())) {
        std::cerr << "Error: Function verification failed\n";
        return false;
    }

    // 创建 ThreadSafeModule
    auto TSM = llvm::orc::ThreadSafeModule(
        std::move(TheModule),
        std::move(TheContext)
    );

    // 添加到 JIT (不是匿名表达式，所以会保留)
    auto TSMPtr = std::make_unique<llvm::orc::ThreadSafeModule>(std::move(TSM));
    if (auto Err = TheJIT->addModule(std::move(TSMPtr), false)) {
        std::cerr << "Error: Failed to add function to JIT: "
                  << llvm::toString(std::move(Err)) << "\n";
        InitializeModule();
        return false;
    }

    // 重新初始化模块
    InitializeModule();

    return true;
}

} // namespace kaleidoscope
