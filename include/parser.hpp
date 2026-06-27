#ifndef KALEIDOSCOPE_PARSER_HPP
#define KALEIDOSCOPE_PARSER_HPP

#include "ast.hpp"
#include "lexer.hpp"
#include <memory>
#include <map>
#include <string>

namespace kaleidoscope {

/// Parser 类：语法分析器
class Parser {
public:
    explicit Parser(Lexer& lexer);

    /// 获取当前 Token
    int getCurrentToken() const { return CurTok; }

    /// 获取下一个 Token
    int getNextToken();

    /// 解析顶层表达式
    std::unique_ptr<FunctionAST> ParseTopLevelExpr();

    /// 解析函数定义
    std::unique_ptr<FunctionAST> ParseDefinition();

    /// 解析外部声明
    std::unique_ptr<PrototypeAST> ParseExtern();

    /// 获取错误信息
    const std::string& getLastError() const { return LastError; }

    /// 添加用户定义的二元运算符优先级
    /// 返回 true 表示成功添加，false 表示被拒绝（如内置运算符）
    bool addBinaryOperator(char Op, unsigned Precedence);

    /// 检查是否是用户定义的一元运算符
    bool isUserDefinedUnary(char Op) const;

    /// 添加用户定义的一元运算符
    /// 返回 true 表示成功添加，false 表示被拒绝（如内置运算符）
    bool addUnaryOperator(char Op);

private:
    /// 错误处理辅助函数
    std::unique_ptr<ExprAST> LogError(const char* Str);
    std::unique_ptr<PrototypeAST> LogErrorP(const char* Str);

    /// 表达式解析
    std::unique_ptr<ExprAST> ParseExpression();
    std::unique_ptr<ExprAST> ParsePrimary();
    std::unique_ptr<ExprAST> ParseUnary();
    std::unique_ptr<ExprAST> ParseNumberExpr();
    std::unique_ptr<ExprAST> ParseParenExpr();
    std::unique_ptr<ExprAST> ParseIdentifierExpr();
    std::unique_ptr<ExprAST> ParseIfExpr();
    std::unique_ptr<ExprAST> ParseForExpr();

    /// 运算符优先级解析
    std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                           std::unique_ptr<ExprAST> LHS);
    int GetTokPrecedence();

    /// 函数解析
    std::unique_ptr<PrototypeAST> ParsePrototype();

    /// 词法分析器引用
    Lexer& TheLexer;

    /// 当前 Token
    int CurTok;

    /// 错误信息
    std::string LastError;

    /// 运算符优先级表 (包含内置和用户定义的)
    std::map<char, int> BinopPrecedence;

    /// 用户定义的一元运算符集合
    std::string UserDefinedUnaryOps;
};

} // namespace kaleidoscope

#endif // KALEIDOSCOPE_PARSER_HPP
