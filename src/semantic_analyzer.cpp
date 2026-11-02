#include "semantic_analyzer.hpp"

#include <unordered_set>

namespace ascend {

std::vector<Diagnostic> SemanticAnalyzer::analyze(const Program& program) {
    diagnostics_.clear();
    loopDepth_ = 0;
    functions_.clear();
    visitBlock(program.body);
    return diagnostics_;
}

void SemanticAnalyzer::checkType(const Expr* expr, const TypeSpec& type,
                                 const std::function<std::string(const CoerceFailure&)>& describe) {
    if (expr == nullptr || type.isAny()) return;
    CoerceFailure failure;
    Value value;
    switch (expr->kind) {
        case ExprKind::Literal: {
            Value converted;
            if (!coerceToType(expr->as<LiteralExpr>().value, type, converted, failure)) {
                report(expr->line, expr->column, describe(failure));
            }
            return;
        }
        case ExprKind::List: value = Value::fromList(std::make_shared<ListObj>()); break;
        case ExprKind::Map: value = Value::fromMap(std::make_shared<MapObj>()); break;
        case ExprKind::FString: value = Value::fromString(""); break;
        case ExprKind::Function: {
            if (type.kind == TypeKind::Function) return;
            failure.what = "a function";
            report(expr->line, expr->column, describe(failure));
            return;
        }
        default:
            return;  // only known when the program runs
    }
    Value converted;
    if (!coerceToType(value, type, converted, failure)) report(expr->line, expr->column, describe(failure));
}

void SemanticAnalyzer::report(int line, int column, std::string message) {
    diagnostics_.push_back({line, column, std::move(message)});
}

void SemanticAnalyzer::visitBlock(const Block& block) {
    for (const auto& stmt : block.statements) visitStmt(*stmt);
}

void SemanticAnalyzer::visitStmt(const Stmt& stmt) {
    switch (stmt.kind) {
        case StmtKind::Expression:
            visitExpr(stmt.as<ExpressionStmt>().expr.get());
            break;
        case StmtKind::Declare: {
            const auto& decl = stmt.as<DeclareStmt>();
            visitExpr(decl.value.get());
            if (decl.hasType) {
                if (!decl.value && decl.type.kind == TypeKind::Function && !decl.type.nullable) {
                    report(stmt.line, stmt.column,
                           "'" + decl.name + "' is declared as func, so it needs a value (or declare it as func?)");
                }
                checkType(decl.value.get(), decl.type, [&](const CoerceFailure& failure) {
                    return withHint("'" + decl.name + "' is declared as " + decl.type.name() + ", so it can't hold " +
                                        failure.what,
                                    failure);
                });
            }
            break;
        }
        case StmtKind::Assign: {
            const auto& assign = stmt.as<AssignStmt>();
            visitExpr(assign.target.get());
            visitExpr(assign.value.get());
            break;
        }
        case StmtKind::Block:
            visitBlock(stmt.as<BlockStmt>().body);
            break;
        case StmtKind::If: {
            const auto& branch = stmt.as<IfStmt>();
            visitExpr(branch.condition.get());
            visitBlock(branch.thenBody);
            if (branch.elseBranch) visitStmt(*branch.elseBranch);
            break;
        }
        case StmtKind::While: {
            const auto& loop = stmt.as<WhileStmt>();
            visitExpr(loop.condition.get());
            ++loopDepth_;
            visitBlock(loop.body);
            --loopDepth_;
            break;
        }
        case StmtKind::For: {
            const auto& loop = stmt.as<ForStmt>();
            if (!loop.second.empty() && loop.first == loop.second) {
                report(stmt.line, stmt.column, "both loop variables are named '" + loop.first + "'");
            }
            visitExpr(loop.iterable.get());
            ++loopDepth_;
            visitBlock(loop.body);
            --loopDepth_;
            break;
        }
        case StmtKind::Break:
            if (loopDepth_ == 0) report(stmt.line, stmt.column, "'break' can only be used inside a loop");
            break;
        case StmtKind::Continue:
            if (loopDepth_ == 0) report(stmt.line, stmt.column, "'continue' can only be used inside a loop");
            break;
        case StmtKind::Return: {
            const auto& ret = stmt.as<ReturnStmt>();
            visitExpr(ret.value.get());
            if (functions_.empty()) {
                report(stmt.line, stmt.column, "'return' can only be used inside a function");
                break;
            }
            const FunctionExpr& function = *functions_.back();
            const TypeSpec& type = function.returnType;
            std::string who = function.name.empty() ? "this function" : function.name + "()";
            if (type.kind == TypeKind::Void) {
                if (ret.value) report(stmt.line, stmt.column, who + " is declared '-> void', so it can't return a value");
            } else if (!ret.value && !type.isAny() && !type.nullable) {
                report(stmt.line, stmt.column, who + " must return " + type.name() + ", but this returns nothing");
            } else {
                checkType(ret.value.get(), type, [&](const CoerceFailure& failure) {
                    return withHint(who + " must return " + type.name() + ", but this returns " + failure.what, failure);
                });
            }
            break;
        }
        case StmtKind::Function:
            visitFunction(*stmt.as<FunctionStmt>().function);
            break;
        case StmtKind::Try: {
            const auto& tryStmt = stmt.as<TryStmt>();
            visitBlock(tryStmt.body);
            visitBlock(tryStmt.handler);
            break;
        }
        case StmtKind::Throw:
            visitExpr(stmt.as<ThrowStmt>().value.get());
            break;
        case StmtKind::Match: {
            const auto& match = stmt.as<MatchStmt>();
            visitExpr(match.subject.get());
            for (const auto& arm : match.arms) {
                for (const auto& pattern : arm.patterns) visitExpr(pattern.get());
                visitBlock(arm.body);
            }
            break;
        }
    }
}

void SemanticAnalyzer::visitExpr(const Expr* expr) {
    if (expr == nullptr) return;
    switch (expr->kind) {
        case ExprKind::Literal:
        case ExprKind::Variable:
            break;
        case ExprKind::Unary:
            visitExpr(expr->as<UnaryExpr>().operand.get());
            break;
        case ExprKind::Binary:
            visitExpr(expr->as<BinaryExpr>().left.get());
            visitExpr(expr->as<BinaryExpr>().right.get());
            break;
        case ExprKind::Logical:
            visitExpr(expr->as<LogicalExpr>().left.get());
            visitExpr(expr->as<LogicalExpr>().right.get());
            break;
        case ExprKind::Ternary: {
            const auto& ternary = expr->as<TernaryExpr>();
            visitExpr(ternary.condition.get());
            visitExpr(ternary.thenExpr.get());
            visitExpr(ternary.elseExpr.get());
            break;
        }
        case ExprKind::Call: {
            const auto& call = expr->as<CallExpr>();
            visitExpr(call.callee.get());
            for (const auto& arg : call.args) visitExpr(arg.get());
            break;
        }
        case ExprKind::Index:
            visitExpr(expr->as<IndexExpr>().object.get());
            visitExpr(expr->as<IndexExpr>().index.get());
            break;
        case ExprKind::Slice: {
            const auto& slice = expr->as<SliceExpr>();
            visitExpr(slice.object.get());
            visitExpr(slice.start.get());
            visitExpr(slice.end.get());
            break;
        }
        case ExprKind::Member:
            visitExpr(expr->as<MemberExpr>().object.get());
            break;
        case ExprKind::List:
            for (const auto& element : expr->as<ListExpr>().elements) visitExpr(element.get());
            break;
        case ExprKind::Map:
            for (const auto& entry : expr->as<MapExpr>().entries) {
                visitExpr(entry.first.get());
                visitExpr(entry.second.get());
            }
            break;
        case ExprKind::Function:
            visitFunction(expr->as<FunctionExpr>());
            break;
        case ExprKind::FString:
            for (const auto& part : expr->as<FStringExpr>().parts) visitExpr(part.expr.get());
            break;
    }
}

void SemanticAnalyzer::visitFunction(const FunctionExpr& function) {
    std::unordered_set<std::string> seen;
    for (const auto& param : function.params) {
        if (!seen.insert(param.name).second) {
            report(function.line, function.column, "parameter '" + param.name + "' is listed more than once");
        }
        visitExpr(param.defaultValue.get());
        checkType(param.defaultValue.get(), param.type, [&](const CoerceFailure& failure) {
            return withHint("parameter '" + param.name + "' is declared as " + param.type.name() +
                                ", so its default can't be " + failure.what,
                            failure);
        });
    }
    // A loop outside the function doesn't make `break` valid inside it.
    int savedLoopDepth = loopDepth_;
    loopDepth_ = 0;
    functions_.push_back(&function);
    visitBlock(function.body);
    functions_.pop_back();
    loopDepth_ = savedLoopDepth;
}

}  // namespace ascend
