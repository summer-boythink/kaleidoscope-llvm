#include "codegen.hpp"
#include "lexer.hpp"
#include "parser.hpp"

#include <iostream>
#include <string>

using namespace kaleidoscope;

//===----------------------------------------------------------------------===//
// 辅助函数
//===----------------------------------------------------------------------===//

/// Token 名称映射（用于调试）
std::string getTokenName(int tok, const Lexer& lexer) {
    switch (tok) {
    case tok_eof:
        return "EOF";
    case tok_def:
        return "def";
    case tok_extern:
        return "extern";
    case tok_identifier:
        return "identifier(" + lexer.getIdentifierStr() + ")";
    case tok_number:
        return "number(" + std::to_string(lexer.getNumVal()) + ")";
    case tok_if:
        return "if";
    case tok_then:
        return "then";
    case tok_else:
        return "else";
    case tok_for:
        return "for";
    case tok_in:
        return "in";
    default:
        if (tok > 0 && tok < 256) {
            return std::string("char('") + static_cast<char>(tok) + "')";
        }
        return "unknown(" + std::to_string(tok) + ")";
    }
}

//===----------------------------------------------------------------------===//
// 主程序
//===----------------------------------------------------------------------===//

int main(int argc, char* argv[]) {
    std::cout << "Kaleidoscope Compiler\n";
    std::cout << "Type 'def' to define a function, 'extern' to declare one.\n";
    std::cout << "Type any expression to generate LLVM IR.\n";
    std::cout << "Type 'quit' to exit.\n\n";

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
        if (line.empty()) {
            continue;
        }

        // 设置输入
        lexer.setInput(line + "\n");

        // 获取第一个 Token
        parser.getNextToken();

        // 根据当前 Token 类型处理
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
                if (auto* FnIR = codeGen.codegen(FnAST.get())) {
                    std::cout << "Read function definition:\n";
                    FnIR->print(llvm::outs());
                    std::cout << "\n";
                } else {
                    std::cerr << "Error: " << codeGen.getLastError() << "\n";
                }
            } else {
                std::cerr << "Error: " << parser.getLastError() << "\n";
                // 恢复：跳过到下一个分号或行尾
                while (parser.getCurrentToken() != ';' &&
                       parser.getCurrentToken() != tok_eof) {
                    parser.getNextToken();
                }
            }
            break;
        }

        case tok_extern: {
            auto ProtoAST = parser.ParseExtern();
            if (ProtoAST) {
                if (auto* FnIR = codeGen.codegen(ProtoAST.get())) {
                    std::cout << "Read extern:\n";
                    FnIR->print(llvm::outs());
                    std::cout << "\n";
                } else {
                    std::cerr << "Error: " << codeGen.getLastError() << "\n";
                }
            } else {
                std::cerr << "Error: " << parser.getLastError() << "\n";
            }
            break;
        }

        default: {
            // 顶层表达式
            auto FnAST = parser.ParseTopLevelExpr();
            if (FnAST) {
                if (auto* FnIR = codeGen.codegen(FnAST.get())) {
                    std::cout << "Read top-level expression:\n";
                    FnIR->print(llvm::outs());
                    std::cout << "\n";
                } else {
                    std::cerr << "Error: " << codeGen.getLastError() << "\n";
                }
            } else {
                std::cerr << "Error: " << parser.getLastError() << "\n";
                // 恢复
                while (parser.getCurrentToken() != ';' &&
                       parser.getCurrentToken() != tok_eof) {
                    parser.getNextToken();
                }
            }
            break;
        }
        }
    }

    std::cout << "\nGoodbye!\n";
    return 0;
}
