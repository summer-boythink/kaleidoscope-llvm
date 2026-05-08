#include "lexer.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace kaleidoscope {

/// 关键字映射表初始化
const std::unordered_map<std::string, Token> Lexer::Keywords = {
    {"def", tok_def},
    {"extern", tok_extern},
    {"if", tok_if},
    {"then", tok_then},
    {"else", tok_else},
    {"for", tok_for},
    {"in", tok_in},
    {"binary", tok_binary},
    {"unary", tok_unary},
    {"var", tok_var},
};

/// 重置词法分析器状态
void Lexer::reset() {
    LastChar = ' ';
    InputStr.clear();
    InputPos = 0;
    IdentifierStr.clear();
    NumVal = 0.0;
}

/// 设置输入字符串
void Lexer::setInput(const std::string& input) {
    InputStr = input;
    InputPos = 0;
    LastChar = ' ';
}

/// 读取下一个字符
int Lexer::getchar() {
    if (InputPos >= InputStr.size()) {
        return EOF;
    }
    return static_cast<unsigned char>(InputStr[InputPos++]);
}

/// gettok - 返回下一个 Token
/// 这是词法分析器的核心函数
int Lexer::gettok() {
    // 1. 跳过空白字符（空格、制表符、换行符）
    while (isspace(LastChar)) {
        LastChar = getchar();
    }

    // 2. 处理标识符：[a-zA-Z_][a-zA-Z0-9_]*
    if (isalpha(LastChar) || LastChar == '_') {
        IdentifierStr = static_cast<char>(LastChar);
        LastChar = getchar();

        while (isalnum(LastChar) || LastChar == '_') {
            IdentifierStr += static_cast<char>(LastChar);
            LastChar = getchar();
        }

        // 检查是否是关键字
        auto it = Keywords.find(IdentifierStr);
        if (it != Keywords.end()) {
            return it->second;
        }
        return tok_identifier;
    }

    // 3. 处理数字：[0-9.]+
    if (isdigit(LastChar) || LastChar == '.') {
        std::string NumStr;
        bool hasDot = false;

        do {
            if (LastChar == '.') {
                if (hasDot) {
                    // 已经有小数点，停止读取
                    break;
                }
                hasDot = true;
            }
            NumStr += static_cast<char>(LastChar);
            LastChar = getchar();
        } while (isdigit(LastChar) || LastChar == '.');

        NumVal = strtod(NumStr.c_str(), nullptr);
        return tok_number;
    }

    // 4. 处理注释：# 开头到行尾
    if (LastChar == '#') {
        // 跳过注释直到行尾
        do {
            LastChar = getchar();
        } while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

        if (LastChar != EOF) {
            return gettok();  // 递归调用，处理下一行
        }
    }

    // 5. 检查文件结束
    if (LastChar == EOF) {
        return tok_eof;
    }

    // 6. 返回单个字符作为 Token
    // 这处理了 '+', '-', '*', '/', '(', ')', ',', ';', ':' 等
    int ThisChar = LastChar;
    LastChar = getchar();
    return ThisChar;
}

} // namespace kaleidoscope
