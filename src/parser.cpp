#include "parser.hpp"
#include <cctype>
#include <iostream>
#include <sstream>

namespace kaleidoscope {

Parser::Parser(Lexer& lexer)
    : TheLexer(lexer), CurTok(tok_eof) {
    // 初始化内置运算符优先级
    BinopPrecedence['<'] = 10;
    BinopPrecedence['>'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40;
    BinopPrecedence['/'] = 40;
}

/// addBinaryOperator - 添加用户定义的二元运算符
void Parser::addBinaryOperator(char Op, unsigned Precedence) {
    BinopPrecedence[Op] = Precedence;
}

/// isUserDefinedUnary - 检查是否是用户定义的一元运算符
bool Parser::isUserDefinedUnary(char Op) const {
    return UserDefinedUnaryOps.find(Op) != std::string::npos;
}

/// addUnaryOperator - 添加用户定义的一元运算符
void Parser::addUnaryOperator(char Op) {
    if (UserDefinedUnaryOps.find(Op) == std::string::npos) {
        UserDefinedUnaryOps += Op;
    }
}

/// getNextToken - 从词法分析器获取下一个 Token
int Parser::getNextToken() {
    return CurTok = TheLexer.gettok();
}

/// LogError - 打印错误信息并返回 nullptr
std::unique_ptr<ExprAST> Parser::LogError(const char* Str) {
    LastError = Str;
    std::cerr << "Error: " << Str << "\n";
    return nullptr;
}

/// LogErrorP - 打印错误信息并返回 nullptr（用于原型）
std::unique_ptr<PrototypeAST> Parser::LogErrorP(const char* Str) {
    LogError(Str);
    return nullptr;
}

/// GetTokPrecedence - 获取当前运算符的优先级
int Parser::GetTokPrecedence() {
    if (!isascii(CurTok)) {
        return -1;
    }

    auto it = BinopPrecedence.find(static_cast<char>(CurTok));
    if (it == BinopPrecedence.end()) {
        return -1;
    }

    return it->second;
}

/// ParseNumberExpr - 解析数字字面量
/// numberexpr ::= number
std::unique_ptr<ExprAST> Parser::ParseNumberExpr() {
    auto Result = std::make_unique<NumberExprAST>(TheLexer.getNumVal());
    getNextToken();  // 消费数字
    return Result;
}

/// ParseParenExpr - 解析括号表达式
/// parenexpr ::= '(' expression ')'
std::unique_ptr<ExprAST> Parser::ParseParenExpr() {
    getNextToken();  // 消费 '('

    auto V = ParseExpression();  // 解析括号内的表达式
    if (!V) {
        return nullptr;
    }

    if (CurTok != ')') {
        return LogError("expected ')'");
    }

    getNextToken();  // 消费 ')'
    return V;
}

/// ParseIdentifierExpr - 解析标识符（变量或函数调用）
/// identifierexpr ::= identifier | identifier '(' expression* ')'
std::unique_ptr<ExprAST> Parser::ParseIdentifierExpr() {
    std::string IdName = TheLexer.getIdentifierStr();
    getNextToken();  // 消费标识符

    if (CurTok != '(') {
        // 简单变量引用，如 "x"
        return std::make_unique<VariableExprAST>(IdName);
    }

    // 函数调用，如 "foo(1, 2)"
    getNextToken();  // 消费 '('
    std::vector<std::unique_ptr<ExprAST>> Args;

    if (CurTok != ')') {
        while (true) {
            if (auto Arg = ParseExpression()) {
                Args.push_back(std::move(Arg));
            } else {
                return nullptr;
            }

            if (CurTok == ')') {
                break;
            }

            if (CurTok != ',') {
                return LogError("Expected ')' or ',' in argument list");
            }

            getNextToken();  // 消费 ','
        }
    }

    getNextToken();  // 消费 ')'

    return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

/// ParseIfExpr - 解析 if/then/else 表达式
/// ifexpr ::= 'if' expression 'then' expression ('else' expression)?
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

    // 可选的 else 分支
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

/// ParseForExpr - 解析 for 循环
/// forexpr ::= 'for' identifier '=' expr ',' expr (',' expr)? 'in' expression
std::unique_ptr<ExprAST> Parser::ParseForExpr() {
    getNextToken();  // 消费 'for'

    // 检查变量名
    if (CurTok != tok_identifier) {
        return LogError("expected identifier after 'for'");
    }

    std::string IdName = TheLexer.getIdentifierStr();
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

/// ParsePrimary - 解析基本表达式
/// primary ::= identifierexpr | numberexpr | parenexpr | ifexpr | forexpr | unaryexpr
std::unique_ptr<ExprAST> Parser::ParsePrimary() {
    switch (CurTok) {
    case tok_identifier:
        return ParseIdentifierExpr();
    case tok_number:
        return ParseNumberExpr();
    case '(':
        return ParseParenExpr();
    case tok_if:
        return ParseIfExpr();
    case tok_for:
        return ParseForExpr();
    case '-':
        // 内置一元负号
        getNextToken();  // 消费 '-'
        if (auto Operand = ParsePrimary()) {
            return std::make_unique<UnaryExprAST>('-', std::move(Operand));
        }
        return nullptr;
    default:
        // 检查是否是用户定义的一元运算符
        if (isascii(CurTok) && isUserDefinedUnary(static_cast<char>(CurTok))) {
            char OpChar = static_cast<char>(CurTok);
            getNextToken();  // 消费运算符
            if (auto Operand = ParsePrimary()) {
                // 生成函数调用 "unary<op>"
                std::string FnName = "unary";
                FnName += OpChar;
                std::vector<std::unique_ptr<ExprAST>> Args;
                Args.push_back(std::move(Operand));
                return std::make_unique<CallExprAST>(FnName, std::move(Args));
            }
            return nullptr;
        }

        std::ostringstream oss;
        oss << "unknown token when expecting an expression: " << CurTok;
        return LogError(oss.str().c_str());
    }
}

/// ParseBinOpRHS - 解析二元运算符右侧
/// binoprhs ::= ('+' primary)*
std::unique_ptr<ExprAST> Parser::ParseBinOpRHS(int ExprPrec,
                                                std::unique_ptr<ExprAST> LHS) {
    while (true) {
        int TokPrec = GetTokPrecedence();

        // 如果当前运算符优先级小于表达式优先级，返回
        if (TokPrec < ExprPrec) {
            return LHS;
        }

        // 否则，这个运算符属于这个表达式
        int BinOp = CurTok;
        getNextToken();  // 消费运算符

        // 解析运算符右侧的基本表达式
        auto RHS = ParsePrimary();
        if (!RHS) {
            return nullptr;
        }

        // 检查下一个运算符
        int NextPrec = GetTokPrecedence();

        // 如果下一个运算符优先级更高，先处理它
        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
            if (!RHS) {
                return nullptr;
            }
        }

        // 检查是否是内置运算符
        if (BinOp == '+' || BinOp == '-' || BinOp == '*' ||
            BinOp == '/' || BinOp == '<' || BinOp == '>') {
            // 内置运算符：生成 BinaryExprAST
            LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS),
                                                   std::move(RHS));
        } else {
            // 用户定义的运算符：生成函数调用
            std::string FnName = "binary";
            FnName += static_cast<char>(BinOp);

            std::vector<std::unique_ptr<ExprAST>> Args;
            Args.push_back(std::move(LHS));
            Args.push_back(std::move(RHS));

            LHS = std::make_unique<CallExprAST>(FnName, std::move(Args));
        }
    }
}

/// ParseExpression - 解析表达式
/// expression ::= primary binoprhs
std::unique_ptr<ExprAST> Parser::ParseExpression() {
    auto LHS = ParsePrimary();
    if (!LHS) {
        return nullptr;
    }

    return ParseBinOpRHS(0, std::move(LHS));
}

/// ParsePrototype - 解析函数原型
/// prototype ::= identifier '(' identifier* ')'
///            ::= 'binary' op number? '(' identifier identifier ')'
///            ::= 'unary' op '(' identifier ')'
std::unique_ptr<PrototypeAST> Parser::ParsePrototype() {
    std::string FnName;
    unsigned Kind = 0;  // 0 = 普通标识符, 1 = 一元运算符, 2 = 二元运算符
    unsigned BinaryPrecedence = 30;

    switch (CurTok) {
    default:
        return LogErrorP("Expected function name in prototype");
    case tok_identifier:
        FnName = TheLexer.getIdentifierStr();
        Kind = 0;
        getNextToken();
        break;
    case tok_binary:
        getNextToken();
        if (!isascii(CurTok)) {
            return LogErrorP("Expected binary operator");
        }
        FnName = "binary";
        FnName += static_cast<char>(CurTok);
        Kind = 2;
        getNextToken();

        // 读取可选的优先级
        if (CurTok == tok_number) {
            double Prec = TheLexer.getNumVal();
            if (Prec < 1 || Prec > 100) {
                return LogErrorP("Invalid precedence: must be 1..100");
            }
            BinaryPrecedence = static_cast<unsigned>(Prec);
            getNextToken();
        }
        break;
    case tok_unary:
        getNextToken();
        if (!isascii(CurTok)) {
            return LogErrorP("Expected unary operator");
        }
        FnName = "unary";
        FnName += static_cast<char>(CurTok);
        Kind = 1;
        getNextToken();
        break;
    }

    if (CurTok != '(') {
        return LogErrorP("Expected '(' in prototype");
    }

    // 解析参数列表
    std::vector<std::string> ArgNames;
    while (getNextToken() == tok_identifier) {
        ArgNames.push_back(TheLexer.getIdentifierStr());
    }

    if (CurTok != ')') {
        return LogErrorP("Expected ')' in prototype");
    }

    // 验证参数数量
    if (Kind == 1 && ArgNames.size() != 1) {
        return LogErrorP("Invalid number of operands for unary operator");
    }
    if (Kind == 2 && ArgNames.size() != 2) {
        return LogErrorP("Invalid number of operands for binary operator");
    }

    getNextToken();  // 消费 ')'

    return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames),
                                           Kind != 0, BinaryPrecedence);
}

/// ParseDefinition - 解析函数定义
/// definition ::= 'def' prototype expression
std::unique_ptr<FunctionAST> Parser::ParseDefinition() {
    getNextToken();  // 消费 'def'

    auto Proto = ParsePrototype();
    if (!Proto) {
        return nullptr;
    }

    if (auto E = ParseExpression()) {
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }

    return nullptr;
}

/// ParseExtern - 解析外部声明
/// external ::= 'extern' prototype
std::unique_ptr<PrototypeAST> Parser::ParseExtern() {
    getNextToken();  // 消费 'extern'
    return ParsePrototype();
}

/// ParseTopLevelExpr - 解析顶层表达式
/// toplevelexpr ::= expression
std::unique_ptr<FunctionAST> Parser::ParseTopLevelExpr() {
    if (auto E = ParseExpression()) {
        // 创建一个匿名的原型 "__anon_expr"
        auto Proto = std::make_unique<PrototypeAST>("__anon_expr",
                                                     std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

} // namespace kaleidoscope
