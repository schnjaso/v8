// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/ast-expression-visitor.h"

#include "src/ast.h"
#include "src/codegen.h"
#include "src/scopes.h"

namespace v8 {
namespace internal {


#define RECURSE(call)               \
  do {                              \
    DCHECK(!HasStackOverflow());    \
    call;                           \
    if (HasStackOverflow()) return; \
  } while (false)


#define RECURSE_EXPRESSION(call)    \
  do {                              \
    DCHECK(!HasStackOverflow());    \
    ++depth_;                       \
    call;                           \
    --depth_;                       \
    if (HasStackOverflow()) return; \
  } while (false)


AstExpressionVisitor::AstExpressionVisitor(CompilationInfo* info)
    : compilation_info_(info), depth_(0) {
  InitializeAstVisitor(info->isolate(), info->zone());
}


void AstExpressionVisitor::Run() {
  RECURSE(VisitFunctionLiteral(compilation_info_->literal()));
}


void AstExpressionVisitor::VisitVariableDeclaration(VariableDeclaration* decl) {
}


void AstExpressionVisitor::VisitFunctionDeclaration(FunctionDeclaration* decl) {
  RECURSE(Visit(decl->fun()));
}


void AstExpressionVisitor::VisitImportDeclaration(ImportDeclaration* decl) {}


void AstExpressionVisitor::VisitExportDeclaration(ExportDeclaration* decl) {}


void AstExpressionVisitor::VisitStatements(ZoneList<Statement*>* stmts) {
  for (int i = 0; i < stmts->length(); ++i) {
    Statement* stmt = stmts->at(i);
    RECURSE(Visit(stmt));
    if (stmt->IsJump()) break;
  }
}


void AstExpressionVisitor::VisitBlock(Block* stmt) {
  RECURSE(VisitStatements(stmt->statements()));
}


void AstExpressionVisitor::VisitExpressionStatement(ExpressionStatement* stmt) {
  RECURSE(Visit(stmt->expression()));
}


void AstExpressionVisitor::VisitEmptyStatement(EmptyStatement* stmt) {}


void AstExpressionVisitor::VisitIfStatement(IfStatement* stmt) {
  RECURSE(Visit(stmt->condition()));
  RECURSE(Visit(stmt->then_statement()));
  RECURSE(Visit(stmt->else_statement()));
}


void AstExpressionVisitor::VisitContinueStatement(ContinueStatement* stmt) {}


void AstExpressionVisitor::VisitBreakStatement(BreakStatement* stmt) {}


void AstExpressionVisitor::VisitReturnStatement(ReturnStatement* stmt) {
  RECURSE(Visit(stmt->expression()));
}


void AstExpressionVisitor::VisitWithStatement(WithStatement* stmt) {
  RECURSE(stmt->expression());
  RECURSE(stmt->statement());
}


void AstExpressionVisitor::VisitSwitchStatement(SwitchStatement* stmt) {
  RECURSE(Visit(stmt->tag()));

  ZoneList<CaseClause*>* clauses = stmt->cases();

  for (int i = 0; i < clauses->length(); ++i) {
    CaseClause* clause = clauses->at(i);
    if (!clause->is_default()) {
      Expression* label = clause->label();
      RECURSE(Visit(label));
    }
    ZoneList<Statement*>* stmts = clause->statements();
    RECURSE(VisitStatements(stmts));
  }
}


void AstExpressionVisitor::VisitCaseClause(CaseClause* clause) {
  UNREACHABLE();
}


void AstExpressionVisitor::VisitDoWhileStatement(DoWhileStatement* stmt) {
  RECURSE(Visit(stmt->body()));
  RECURSE(Visit(stmt->cond()));
}


void AstExpressionVisitor::VisitWhileStatement(WhileStatement* stmt) {
  RECURSE(Visit(stmt->cond()));
  RECURSE(Visit(stmt->body()));
}


void AstExpressionVisitor::VisitForStatement(ForStatement* stmt) {
  if (stmt->init() != NULL) {
    RECURSE(Visit(stmt->init()));
  }
  if (stmt->cond() != NULL) {
    RECURSE(Visit(stmt->cond()));
  }
  if (stmt->next() != NULL) {
    RECURSE(Visit(stmt->next()));
  }
  RECURSE(Visit(stmt->body()));
}


void AstExpressionVisitor::VisitForInStatement(ForInStatement* stmt) {
  RECURSE(Visit(stmt->enumerable()));
  RECURSE(Visit(stmt->body()));
}


void AstExpressionVisitor::VisitForOfStatement(ForOfStatement* stmt) {
  RECURSE(Visit(stmt->iterable()));
  RECURSE(Visit(stmt->body()));
}


void AstExpressionVisitor::VisitTryCatchStatement(TryCatchStatement* stmt) {
  RECURSE(Visit(stmt->try_block()));
  RECURSE(Visit(stmt->catch_block()));
}


void AstExpressionVisitor::VisitTryFinallyStatement(TryFinallyStatement* stmt) {
  RECURSE(Visit(stmt->try_block()));
  RECURSE(Visit(stmt->finally_block()));
}


void AstExpressionVisitor::VisitDebuggerStatement(DebuggerStatement* stmt) {}


void AstExpressionVisitor::VisitFunctionLiteral(FunctionLiteral* expr) {
  Scope* scope = expr->scope();
  VisitExpression(expr);
  RECURSE_EXPRESSION(VisitDeclarations(scope->declarations()));
  RECURSE_EXPRESSION(VisitStatements(expr->body()));
}


void AstExpressionVisitor::VisitNativeFunctionLiteral(
    NativeFunctionLiteral* expr) {}


void AstExpressionVisitor::VisitConditional(Conditional* expr) {
  RECURSE(Visit(expr->condition()));
  RECURSE(Visit(expr->then_expression()));
  RECURSE(Visit(expr->else_expression()));
}


void AstExpressionVisitor::VisitVariableProxy(VariableProxy* expr) {
  VisitExpression(expr);
}


void AstExpressionVisitor::VisitLiteral(Literal* expr) {
  VisitExpression(expr);
}


void AstExpressionVisitor::VisitRegExpLiteral(RegExpLiteral* expr) {
  VisitExpression(expr);
}


void AstExpressionVisitor::VisitObjectLiteral(ObjectLiteral* expr) {
  VisitExpression(expr);
  ZoneList<ObjectLiteralProperty*>* props = expr->properties();
  for (int i = 0; i < props->length(); ++i) {
    ObjectLiteralProperty* prop = props->at(i);
    RECURSE_EXPRESSION(Visit(prop->value()));
  }
}


void AstExpressionVisitor::VisitArrayLiteral(ArrayLiteral* expr) {
  VisitExpression(expr);
  ZoneList<Expression*>* values = expr->values();
  for (int i = 0; i < values->length(); ++i) {
    Expression* value = values->at(i);
    RECURSE_EXPRESSION(Visit(value));
  }
}


void AstExpressionVisitor::VisitAssignment(Assignment* expr) {
  VisitExpression(expr);
  RECURSE_EXPRESSION(Visit(expr->target()));
  RECURSE_EXPRESSION(Visit(expr->value()));
}


void AstExpressionVisitor::VisitYield(Yield* expr) {
  VisitExpression(expr);
  RECURSE_EXPRESSION(Visit(expr->generator_object()));
  RECURSE_EXPRESSION(Visit(expr->expression()));
}


void AstExpressionVisitor::VisitThrow(Throw* expr) {
  VisitExpression(expr);
  RECURSE_EXPRESSION(Visit(expr->exception()));
}


void AstExpressionVisitor::VisitProperty(Property* expr) {
  VisitExpression(expr);
  RECURSE_EXPRESSION(Visit(expr->obj()));
  RECURSE_EXPRESSION(Visit(expr->key()));
}


void AstExpressionVisitor::VisitCall(Call* expr) {
  VisitExpression(expr);
  RECURSE_EXPRESSION(Visit(expr->expression()));
  ZoneList<Expression*>* args = expr->arguments();
  for (int i = 0; i < args->length(); ++i) {
    Expression* arg = args->at(i);
    RECURSE_EXPRESSION(Visit(arg));
  }
}


void AstExpressionVisitor::VisitCallNew(CallNew* expr) {
  VisitExpression(expr);
  RECURSE_EXPRESSION(Visit(expr->expression()));
  ZoneList<Expression*>* args = expr->arguments();
  for (int i = 0; i < args->length(); ++i) {
    Expression* arg = args->at(i);
    RECURSE_EXPRESSION(Visit(arg));
  }
}


void AstExpressionVisitor::VisitCallRuntime(CallRuntime* expr) {
  VisitExpression(expr);
  ZoneList<Expression*>* args = expr->arguments();
  for (int i = 0; i < args->length(); ++i) {
    Expression* arg = args->at(i);
    RECURSE_EXPRESSION(Visit(arg));
  }
}


void AstExpressionVisitor::VisitUnaryOperation(UnaryOperation* expr) {
  VisitExpression(expr);
  RECURSE_EXPRESSION(Visit(expr->expression()));
}


void AstExpressionVisitor::VisitCountOperation(CountOperation* expr) {
  VisitExpression(expr);
  RECURSE_EXPRESSION(Visit(expr->expression()));
}


void AstExpressionVisitor::VisitBinaryOperation(BinaryOperation* expr) {
  VisitExpression(expr);
  RECURSE_EXPRESSION(Visit(expr->left()));
  RECURSE_EXPRESSION(Visit(expr->right()));
}


void AstExpressionVisitor::VisitCompareOperation(CompareOperation* expr) {
  VisitExpression(expr);
  RECURSE_EXPRESSION(Visit(expr->left()));
  RECURSE_EXPRESSION(Visit(expr->right()));
}


void AstExpressionVisitor::VisitThisFunction(ThisFunction* expr) {
  VisitExpression(expr);
}


void AstExpressionVisitor::VisitDeclarations(ZoneList<Declaration*>* decls) {
  for (int i = 0; i < decls->length(); ++i) {
    Declaration* decl = decls->at(i);
    RECURSE(Visit(decl));
  }
}


void AstExpressionVisitor::VisitClassLiteral(ClassLiteral* expr) {}


void AstExpressionVisitor::VisitSpread(Spread* expr) {}


void AstExpressionVisitor::VisitEmptyParentheses(EmptyParentheses* expr) {}


void AstExpressionVisitor::VisitSuperPropertyReference(
    SuperPropertyReference* expr) {}


void AstExpressionVisitor::VisitSuperCallReference(SuperCallReference* expr) {}
}


}  // namespace v8::internal
