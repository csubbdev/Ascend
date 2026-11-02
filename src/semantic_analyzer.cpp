#include "semantic_analyzer.hpp"

#include <unordered_set>

namespace ascend {

std::vector<Diagnostic> SemanticAnalyzer::analyze(const Program& program) {
    diagnostics_.clear();
    loopDepth_ = 0;
    functionDepth_ = 0;
    visitBlock(program.body);
    return diagnostics_;
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
        case StmtKind::Declare:
            visitExpr(stmt.as<DeclareStmt>().value.get());
            break;
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
        case StmtKind::Return:
            if (functionDepth_ == 0) report(stmt.line, stmt.column, "'return' can only be used inside a function");
            visitExpr(stmt.as<ReturnStmt>().value.get());
            break;
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
    }
}

void SemanticAnalyzer::visitFunction(const FunctionExpr& function) {
    std::unordered_set<std::string> seen;
    for (const auto& param : function.params) {
        if (!seen.insert(param.name).second) {
            report(function.line, function.column, "parameter '" + param.name + "' is listed more than once");
        }
        visitExpr(param.defaultValue.get());
    }
    // A loop outside the function doesn't make `break` valid inside it.
    int savedLoopDepth = loopDepth_;
    loopDepth_ = 0;
    ++functionDepth_;
    visitBlock(function.body);
    --functionDepth_;
    loopDepth_ = savedLoopDepth;
}

}  // namespace ascend
