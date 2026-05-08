#ifndef KALEIDOSCOPE_LEXER_HPP
#define KALEIDOSCOPE_LEXER_HPP

#include <string>
#include <unordered_map>

namespace kaleidoscope {

/// Token 枚举：每个 Token 都有一个对应的数字
/// 0-255 范围留给 ASCII 字符（如 '+', '-', '*' 等）
enum Token {
    tok_eof = -1,           // 文件结束

    // 命令
    tok_def = -2,           // def 关键字
    tok_extern = -3,        // extern 关键字

    // 标识符和数字
    tok_identifier = -4,    // 标识符（变量名、函数名）
    tok_number = -5,        // 数字字面量

    // 控制流（第五章使用）
    tok_if = -6,            // if 关键字
    tok_then = -7,          // then 关键字
    tok_else = -8,          // else 关键字
    tok_for = -9,           // for 关键字
    tok_in = -10,           // in 关键字

    // 运算符（第六章使用）
    tok_binary = -11,       // binary 关键字
    tok_unary = -12,        // unary 关键字

    // 变量定义（第七章使用）
    tok_var = -13,          // var 关键字
};

/// Lexer 类：词法分析器
class Lexer {
public:
    /// 获取当前标识符文本
    const std::string& getIdentifierStr() const { return IdentifierStr; }

    /// 获取当前数字值
    double getNumVal() const { return NumVal; }

    /// 获取下一个 Token
    int gettok();

    /// 重置词法分析器状态
    void reset();

    /// 设置输入字符串
    void setInput(const std::string& input);

private:
    /// 读取下一个字符
    int getchar();

    /// 当前标识符文本
    std::string IdentifierStr;

    /// 当前数字值
    double NumVal = 0.0;

    /// 上一个读取的字符（用于 gettok）
    int LastChar = ' ';

    /// 输入字符串
    std::string InputStr;

    /// 输入字符串当前位置
    size_t InputPos = 0;

    /// 关键字映射表
    static const std::unordered_map<std::string, Token> Keywords;
};

} // namespace kaleidoscope

#endif // KALEIDOSCOPE_LEXER_HPP
