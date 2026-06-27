#include "parser.hpp"
#include <cctype>
#include <iostream>
#include <sstream>

namespace kaleidoscope {

//===----------------------------------------------------------------------===//
// 辅助：运算符字符验证
//===----------------------------------------------------------------------===//

/// 内置运算符字符集合（不可被用户定义的运算符覆盖）
static constexpr const char* BuiltinBinaryOps = "+-*/<>";
static constexpr const char* BuiltinUnaryOps = "-";

/// isValidOperatorChar - 检查字符是否可以作为用户定义运算符
/// 规则：
///   1. 必须是 ASCII 字符
///   2. 不能是字母或数字
///   3. 不能是空白字符
///   4. 不能是语言语法关键字符：括号、逗号、分号、注释、引号等
///   5. 必须为可打印字符
static bool isValidOperatorChar(int c) {
    if (!isascii(c)) return false;
    if (isalnum(c)) return false;
    if (isspace(c)) return false;

    // 拒绝与语言语法冲突的字符
    switch (c) {
    case '(': case ')':   // 函数调用/分组
    case '[': case ']':   // 保留
    case '{': case '}':   // 保留
    case ',':              // 参数分隔符
    case ';':              // 顶层分隔符
    case '#':              // 注释
    case '\'': case '"':   // 字符串分隔符
    case '\\':             // 转义字符
    case '_':              // 标识符字符
    case '.':              // 数字中的小数点
        return false;
    default:
        break;
    }

    // 必须为可打印字符
    return isprint(c);
}

//===----------------------------------------------------------------------===//
// Parser 构造与运算符管理
//===----------------------------------------------------------------------===//

Parser::Parser(Lexer& lexer)
    : TheLexer(lexer), CurTok(tok_eof) {
    // 初始化内置运算符优先级（仅 ASCII 范围内的运算符字符）
    BinopPrecedence['<'] = 10;
    BinopPrecedence['>'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40;
    BinopPrecedence['/'] = 40;
}

/// addBinaryOperator - 添加用户定义的二元运算符
/// 安全检查：
///   1. 不允许覆盖内置运算符的优先级
///   2. 优先级必须在 1..100 范围内
/// 返回 true 表示成功添加，false 表示被拒绝
bool Parser::addBinaryOperator(char Op, unsigned Precedence) {
    // 检查是否是内置运算符
    for (const char* p = BuiltinBinaryOps; *p; ++p) {
        if (*p == Op) {
            std::cerr << "Warning: Cannot override built-in binary operator '"
                      << Op << "'. Use a different operator character.\n";
            return false;
        }
    }

    if (Precedence < 1 || Precedence > 100) {
        std::cerr << "Warning: Invalid precedence " << Precedence
                  << " for operator '" << Op
                  << "'. Must be 1..100. Using default 30.\n";
        Precedence = 30;
    }

    BinopPrecedence[Op] = static_cast<int>(Precedence);
    return true;
}

/// isUserDefinedUnary - 检查是否是用户定义的一元运算符
bool Parser::isUserDefinedUnary(char Op) const {
    return UserDefinedUnaryOps.find(Op) != std::string::npos;
}

/// addUnaryOperator - 添加用户定义的一元运算符
/// 安全检查：不允许覆盖内置一元运算符 '-'
/// 返回 true 表示成功添加，false 表示被拒绝
bool Parser::addUnaryOperator(char Op) {
    // 检查是否是内置一元运算符
    for (const char* p = BuiltinUnaryOps; *p; ++p) {
        if (*p == Op) {
            std::cerr << "Warning: Cannot override built-in unary operator '"
                      << Op << "'. Use a different operator character.\n";
            return false;
        }
    }

    if (UserDefinedUnaryOps.find(Op) == std::string::npos) {
        UserDefinedUnaryOps += Op;
    }
    return true;
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

/// GetTokPrecedence - 获取当前二元运算符的优先级
/// 返回 -1 表示当前 token 不是二元运算符
int Parser::GetTokPrecedence() {
    // 只有 ASCII 范围内的单字符 token 才可能是运算符
    // （tok_identifier, tok_number 等负值都会被 isascii 拒绝）
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

/// ParsePrimary - 解析基本表达式（不含一元运算符）
/// primary ::= identifierexpr | numberexpr | parenexpr | ifexpr | forexpr
/// 一元运算符在 ParseUnary 中处理
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
    default:
        // 到达这里说明遇到了无法识别的 token
        // 可能是真正无法识别的 token，也可能是 EOF
        if (CurTok == tok_eof) {
            return LogError("unexpected end of input when expecting an expression");
        }
        std::ostringstream oss;
        if (isascii(CurTok) && isprint(CurTok)) {
            oss << "unexpected character '" << static_cast<char>(CurTok)
                << "' when expecting an expression";
        } else {
            oss << "unknown token when expecting an expression: " << CurTok;
        }
        return LogError(oss.str().c_str());
    }
}

/// ParseUnary - 解析一元运算符表达式
/// unary ::= primary
///        ::= '-' unary
///        ::= user_defined_unary_op unary
///
/// 一元运算符是右递归的，例如:
///   !!x     → not(not(x))
///   -!x     → neg(not(x))
///   - - x   → neg(neg(x))
std::unique_ptr<ExprAST> Parser::ParseUnary() {
    // 检查是否是内置一元负号 '-'
    // 注意：'-' 既是二元减号也是一元负号，需要在 ParseUnary 中处理一元用法
    if (CurTok == '-') {
        getNextToken();  // 消费 '-'
        if (auto Operand = ParseUnary()) {
            return std::make_unique<UnaryExprAST>('-', std::move(Operand));
        }
        return nullptr;
    }

    // 检查是否是用户定义的一元运算符
    // 注意：必须是 ASCII 字符且不是内置一元运算符
    if (isascii(CurTok) && isUserDefinedUnary(static_cast<char>(CurTok))) {
        char OpChar = static_cast<char>(CurTok);
        getNextToken();  // 消费运算符

        if (auto Operand = ParseUnary()) {
            // 生成函数调用 "unary<op>"
            std::string FnName = "unary";
            FnName += OpChar;
            std::vector<std::unique_ptr<ExprAST>> Args;
            Args.push_back(std::move(Operand));
            return std::make_unique<CallExprAST>(FnName, std::move(Args));
        }
        return nullptr;
    }

    // 没有一元运算符，解析基本表达式
    return ParsePrimary();
}

/// ParseBinOpRHS - 解析二元运算符右侧
/// binoprhs ::= (binop unary)*
/// 右侧可以包含一元运算符: a + -!b
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

        // 解析运算符右侧的一元表达式（一元运算符可能紧接着出现，如 a + -b）
        auto RHS = ParseUnary();
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

        // 检查是否是内置二元运算符
        // 注意：即使有同名的用户定义运算符（如 binary+），内置运算符
        // 在表达式中的行为保持不变。这是为了防止递归陷阱：
        // 如果 binary+ 的实现中使用了 '+', 应当调用内置算术加法而非递归调用自身。
        if (BinOp == '+' || BinOp == '-' || BinOp == '*' ||
            BinOp == '/' || BinOp == '<' || BinOp == '>') {
            // 内置运算符：生成 BinaryExprAST（或 UnaryExprAST 的子类）
            LHS = std::make_unique<BinaryExprAST>(static_cast<char>(BinOp),
                                                   std::move(LHS),
                                                   std::move(RHS));
        } else {
            // 用户定义的运算符：生成函数调用 "binary<op>"
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
/// expression ::= unary binoprhs
std::unique_ptr<ExprAST> Parser::ParseExpression() {
    auto LHS = ParseUnary();
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
            return LogErrorP("Expected binary operator character");
        }
        if (!isValidOperatorChar(CurTok)) {
            std::ostringstream oss;
            oss << "Invalid binary operator character '"
                << static_cast<char>(CurTok) << "'";
            return LogErrorP(oss.str().c_str());
        }
        // 额外检查：不能是内置二元运算符字符（它们不可覆盖）
        for (const char* p = BuiltinBinaryOps; *p; ++p) {
            if (*p == CurTok) {
                std::cerr << "Warning: Defining binary operator '"
                          << static_cast<char>(CurTok)
                          << "' will not override the built-in operator in expressions.\n";
                break;
            }
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
            return LogErrorP("Expected unary operator character");
        }
        if (!isValidOperatorChar(CurTok)) {
            std::ostringstream oss;
            oss << "Invalid unary operator character '"
                << static_cast<char>(CurTok) << "'";
            return LogErrorP(oss.str().c_str());
        }
        // 额外检查：不能是内置一元运算符字符
        for (const char* p = BuiltinUnaryOps; *p; ++p) {
            if (*p == CurTok) {
                std::cerr << "Warning: Defining unary operator '"
                          << static_cast<char>(CurTok)
                          << "' will not override the built-in operator in expressions.\n";
                break;
            }
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
