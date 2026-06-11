#include "codegen.hpp"
#include "lexer.hpp"
#include "parser.hpp"

#include <iostream>
#include <string>

using namespace kaleidoscope;

//===----------------------------------------------------------------------===//
// 辅助函数
//===----------------------------------------------------------------------===//

/// 打印欢迎信息
static void PrintWelcome() {
    std::cout << "Kaleidoscope JIT Interpreter\n";
    std::cout << "================================\n";
    std::cout << "Type expressions to evaluate them.\n";
    std::cout << "Type 'def name(args) expr' to define a function.\n";
    std::cout << "Type 'def binary op prec (args) expr' to define a binary operator.\n";
    std::cout << "Type 'def unary op (args) expr' to define a unary operator.\n";
    std::cout << "Type 'extern name(args)' to declare an external function.\n";
    std::cout << "Type 'quit' to exit.\n\n";
}

/// 处理函数原型，安装运算符优先级
static void HandleOperatorPrototype(Parser& parser, const PrototypeAST* Proto) {
    if (!Proto || !Proto->isOperator()) {
        return;
    }

    // 检查是否是二元运算符
    std::string BinOp = Proto->getBinaryOperator();
    if (!BinOp.empty() && BinOp.size() == 1) {
        parser.addBinaryOperator(BinOp[0], Proto->getPrecedence());
        std::cout << "Installed binary operator '" << BinOp
                  << "' with precedence " << Proto->getPrecedence() << "\n";
        return;
    }

    // 检查是否是一元运算符
    std::string UnOp = Proto->getUnaryOperator();
    if (!UnOp.empty() && UnOp.size() == 1) {
        parser.addUnaryOperator(UnOp[0]);
        std::cout << "Installed unary operator '" << UnOp << "'\n";
        return;
    }
}

//===----------------------------------------------------------------------===//
// 主程序
//===----------------------------------------------------------------------===//

int main(int argc, char* argv[]) {
    // 打印欢迎信息
    PrintWelcome();

    // 创建组件
    Lexer lexer;
    Parser parser(lexer);
    CodeGenerator codeGen;

    // 主循环
    while (true) {
        std::cout << "ready> ";
        std::string line;

        if (!std::getline(std::cin, line)) {
            break;
        }

        // 检查退出命令
        if (line == "quit" || line == "exit") {
            break;
        }

        // 跳过空行
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }

        // 设置输入
        lexer.setInput(line + "\n");

        // 获取第一个 Token
        parser.getNextToken();

        // 根据 Token 类型处理
        switch (parser.getCurrentToken()) {
        case tok_eof:
            break;

        case ';':
            // 忽略顶层分号
            parser.getNextToken();
            break;

        case tok_def: {
            auto FnAST = parser.ParseDefinition();
            if (FnAST) {
                // 如果是运算符，安装优先级
                HandleOperatorPrototype(parser, FnAST->getProto());

                if (codeGen.addFunctionToJIT(FnAST.get())) {
                    std::cout << "Defined function: " << FnAST->getProto()->getName() << "\n";
                } else {
                    std::cerr << "Error: " << codeGen.getLastError() << "\n";
                }
            } else {
                std::cerr << "Error: " << parser.getLastError() << "\n";
            }
            break;
        }

        case tok_extern: {
            auto ProtoAST = parser.ParseExtern();
            if (ProtoAST) {
                // 如果是运算符，安装优先级
                HandleOperatorPrototype(parser, ProtoAST.get());

                // 添加到函数原型表，以便后续查找
                codeGen.addPrototype(std::make_unique<PrototypeAST>(
                    ProtoAST->getName(), ProtoAST->getArgs(),
                    ProtoAST->isOperator(), ProtoAST->getPrecedence()));
                std::cout << "Declared extern: " << ProtoAST->getName() << "\n";
            } else {
                std::cerr << "Error: " << parser.getLastError() << "\n";
            }
            break;
        }

        default: {
            // 顶层表达式 - JIT 执行
            auto FnAST = parser.ParseTopLevelExpr();
            if (FnAST) {
                double Result = codeGen.executeTopLevelExpr(FnAST.get());
                std::cout << "= " << Result << "\n";
            } else {
                std::cerr << "Error: " << parser.getLastError() << "\n";
            }
            break;
        }
        }
    }

    std::cout << "\nGoodbye!\n";
    return 0;
}
