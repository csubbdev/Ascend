#include "semantic_analyzer.hpp"

#include <algorithm>
#include <unordered_set>

namespace ascend {

std::vector<Diagnostic> SemanticAnalyzer::analyze(const Program& program) {
    diagnostics_.clear();
    loopDepth_ = 0;
    switchDepth_ = 0;
    functions_.clear();
    typeUses_.clear();
    declared_.clear();
    importsEverything_ = false;
    hasMain_ = false;
    mentionsMain_ = false;
    for (const auto& stmt : program.body.statements) {
        if (stmt->kind == StmtKind::Function && stmt->as<FunctionStmt>().function->name == "main") hasMain_ = true;
    }
    visitBlock(program.body);

    // Struct types can be declared after they're used, or imported, so
    // unknown type names are only reported now.
    if (checkTypeNames && !importsEverything_) {
        for (const auto& use : typeUses_) {
            if (declared_.count(use.name) != 0) continue;
            report(use.line, use.column,
                   "unknown type '" + use.name +
                       "' (the types are: i8 i16 i32 i64 u8 u16 u32 u64 int byte short long f32 f64 float double bool "
                       "char string list map func any, and the structs you declare)");
        }
    }
    std::stable_sort(diagnostics_.begin(), diagnostics_.end(), [](const Diagnostic& a, const Diagnostic& b) {
        return a.line != b.line ? a.line < b.line : a.column < b.column;
    });
    return diagnostics_;
}

void SemanticAnalyzer::noteType(const TypeSpec& type, int line, int column) {
    if (type.kind == TypeKind::Struct) typeUses_.push_back({type.structName, line, column});
}

void SemanticAnalyzer::checkType(const Expr* expr, const TypeSpec& type,
                                 const std::function<std::string(const CoerceFailure&)>& describe) {
    // Struct values are only made at run time (and the type may be unknown).
    if (expr == nullptr || type.isAny() || type.kind == TypeKind::Struct) return;
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
            declared_.insert(decl.name);
            visitExpr(decl.value.get());
            noteType(decl.type, stmt.line, stmt.column);
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
            noteType(loop.firstType, stmt.line, stmt.column);
            noteType(loop.secondType, stmt.line, stmt.column);
            visitExpr(loop.iterable.get());
            ++loopDepth_;
            visitBlock(loop.body);
            --loopDepth_;
            break;
        }
        case StmtKind::CFor: {
            const auto& loop = stmt.as<CForStmt>();
            for (const auto& init : loop.init) visitStmt(*init);
            visitExpr(loop.condition.get());
            for (const auto& step : loop.step) visitStmt(*step);
            ++loopDepth_;
            visitBlock(loop.body);
            --loopDepth_;
            break;
        }
        case StmtKind::Switch: {
            const auto& choice = stmt.as<SwitchStmt>();
            visitExpr(choice.subject.get());
            for (const auto& entry : choice.cases) {
                for (const auto& value : entry.values) visitExpr(value.get());
            }
            ++switchDepth_;
            visitBlock(choice.body);
            --switchDepth_;
            break;
        }
        case StmtKind::Import: {
            const auto& import = stmt.as<ImportStmt>();
            if (import.all) importsEverything_ = true;
            for (const auto& name : import.names) declared_.insert(name.binding);
            break;
        }
        case StmtKind::Struct: {
            const auto& declaration = stmt.as<StructStmt>();
            declared_.insert(declaration.name);
            std::set<std::string> seen;
            for (const auto& field : declaration.fields) {
                if (!seen.insert(field.name).second) {
                    report(field.line, field.column,
                           "struct " + declaration.name + " has two fields named '" + field.name + "'");
                }
                noteType(field.type, field.line, field.column);
                visitExpr(field.defaultValue.get());
                if (!field.defaultValue && field.type.kind == TypeKind::Function && !field.type.nullable) {
                    report(field.line, field.column,
                           "field '" + field.name + "' is declared as func, so it needs a default value (or declare it "
                           "as func?)");
                }
                checkType(field.defaultValue.get(), field.type, [&](const CoerceFailure& failure) {
                    return withHint("field '" + field.name + "' is declared as " + field.type.name() +
                                        ", so its default can't be " + failure.what,
                                    failure);
                });
            }
            for (const auto& method : declaration.methods) {
                const FunctionExpr& function = *method.second;
                if (!seen.insert(method.first).second) {
                    report(function.line, function.column,
                           "struct " + declaration.name + " already has a field or method named '" + method.first + "'");
                }
                if (method.first == "init" && function.params.size() > 1) {
                    report(function.line, function.column,
                           "init() is called with no arguments when a " + declaration.name +
                               " is made, so it can't have parameters");
                }
                visitFunction(function);
            }
            break;
        }
        case StmtKind::Enum: {
            const auto& declaration = stmt.as<EnumStmt>();
            declared_.insert(declaration.name);
            std::set<std::string> seen;
            for (const auto& member : declaration.members) {
                if (!seen.insert(member.name).second) {
                    report(member.line, member.column,
                           "enum " + declaration.name + " lists '" + member.name + "' more than once");
                }
                visitExpr(member.value.get());
            }
            break;
        }
        case StmtKind::Group:
            for (const auto& inner : stmt.as<GroupStmt>().statements) visitStmt(*inner);
            break;
        case StmtKind::MultiAssign: {
            const auto& assign = stmt.as<MultiAssignStmt>();
            std::set<std::string> seen;
            for (const auto& target : assign.targets) {
                visitExpr(target.get());
                if (target->kind == ExprKind::Variable) {
                    const std::string& name = target->as<VariableExpr>().name;
                    if (assign.declare) declared_.insert(name);
                    if (!seen.insert(name).second) {
                        report(target->line, target->column, "'" + name + "' is listed twice");
                    }
                }
            }
            for (const auto& value : assign.values) visitExpr(value.get());
            break;
        }
        case StmtKind::Break:
            if (loopDepth_ == 0 && switchDepth_ == 0) {
                report(stmt.line, stmt.column, "'break' can only be used inside a loop or switch");
            }
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
                if (ret.value) report(stmt.line, stmt.column, who + " returns void, so it can't return a value");
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
            declared_.insert(stmt.as<FunctionStmt>().function->name);
            visitFunction(*stmt.as<FunctionStmt>().function);
            break;
        case StmtKind::Try: {
            const auto& tryStmt = stmt.as<TryStmt>();
            visitBlock(tryStmt.body);
            visitBlock(tryStmt.handler);
            visitBlock(tryStmt.finallyBody);
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
            break;
        case ExprKind::Variable:
            if (expr->as<VariableExpr>().name == "main") mentionsMain_ = true;
            break;
        case ExprKind::IncDec:
            visitExpr(expr->as<IncDecExpr>().target.get());
            break;
        case ExprKind::Comprehension: {
            const auto& comprehension = expr->as<ComprehensionExpr>();
            for (const auto& clause : comprehension.clauses) visitExpr(clause.expr.get());
            visitExpr(comprehension.key.get());
            visitExpr(comprehension.value.get());
            break;
        }
        case ExprKind::Range:
            visitExpr(expr->as<RangeExpr>().start.get());
            visitExpr(expr->as<RangeExpr>().end.get());
            break;
        case ExprKind::Spread:
            visitExpr(expr->as<SpreadExpr>().value.get());
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
    noteType(function.returnType, function.line, function.column);
    for (const auto& param : function.params) {
        if (!seen.insert(param.name).second) {
            report(function.line, function.column, "parameter '" + param.name + "' is listed more than once");
        }
        noteType(param.type, function.line, function.column);
        visitExpr(param.defaultValue.get());
        checkType(param.defaultValue.get(), param.type, [&](const CoerceFailure& failure) {
            return withHint("parameter '" + param.name + "' is declared as " + param.type.name() +
                                ", so its default can't be " + failure.what,
                            failure);
        });
    }
    // A loop outside the function doesn't make `break` valid inside it.
    int savedLoopDepth = loopDepth_;
    int savedSwitchDepth = switchDepth_;
    loopDepth_ = 0;
    switchDepth_ = 0;
    functions_.push_back(&function);
    visitBlock(function.body);
    functions_.pop_back();
    loopDepth_ = savedLoopDepth;
    switchDepth_ = savedSwitchDepth;
}

}  // namespace ascend
