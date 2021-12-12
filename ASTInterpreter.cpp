//
// Created by Critizero on 2021/10/22.
//
//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"
#include <fstream>

using namespace clang;

#include "Environment.h"

class InterpreterVisitor :
        public EvaluatedExprVisitor<InterpreterVisitor> {
public:
    explicit InterpreterVisitor(const ASTContext &context, Environment * env)
            : EvaluatedExprVisitor(context), mEnv(env) {}
    virtual ~InterpreterVisitor() = default;

    virtual void VisitBinaryOperator (BinaryOperator * bop) {
        if (mEnv->timeToReturn()) {
            /// For recursion
            return;
        }
        VisitStmt(bop);
        mEnv->binop(bop);
    }
    virtual void VisitDeclRefExpr(DeclRefExpr * expr) {
        if (mEnv->timeToReturn()) {
            return;
        }
        VisitStmt(expr);
        mEnv->declref(expr);
    }
    virtual void VisitCastExpr(CastExpr * expr) {
        if (mEnv->timeToReturn()) {
            return;
        }
        VisitStmt(expr);
        mEnv->cast(expr);
    }
    virtual void VisitCallExpr(CallExpr * expr) {
        if (mEnv->timeToReturn()) {
            return;
        }
        VisitStmt(expr);
        mEnv->call(expr);
        // For self-define function
        FunctionDecl *callee = expr->getDirectCallee();
        if (!callee->getName().equals("FREE") &&
            !callee->getName().equals("MALLOC") &&
            !callee->getName().equals("INPUT") &&
            !callee->getName().equals("PRINT")) {
            if (callee->getBody()) {
                Visit(callee->getBody());
            }
            mEnv->bindReturnValue(expr);
        }
    }
    virtual void VisitDeclStmt(DeclStmt * decl_stmt) {
        if (mEnv->timeToReturn()) {
            return;
        }
        mEnv->decl(decl_stmt);
    }
    virtual void VisitIfStmt(IfStmt * if_stmt) {
        if (mEnv->timeToReturn()) {
            return;
        }
        Expr * cond = if_stmt->getCond();
        if (mEnv->calculate(cond)) {
            Stmt * then_stmt = if_stmt->getThen();
            Visit(then_stmt);
        }
        else {
            if (if_stmt->getElse()) {
                Stmt * else_stmt = if_stmt->getElse();
                Visit(else_stmt);
            }
        }
    }
    virtual void VisitReturnStmt(ReturnStmt * ret_stmt) {
        if (mEnv->timeToReturn()) {
            return;
        }
        else {
            Visit(ret_stmt->getRetValue());
            mEnv->returnStmt(ret_stmt);
        }
    }
    virtual void VisitWhileStmt(WhileStmt * while_stmt) {
        if (mEnv->timeToReturn()) {
            return;
        }
        Expr * cond = while_stmt->getCond();
        while(mEnv->calculate(cond)) {
            Visit(while_stmt->getBody());
        }
    }
    virtual void VisitForStmt(ForStmt * for_stmt) {
        if (mEnv->timeToReturn()) {
            return;
        }
        if (Stmt * init = for_stmt->getInit()) {
            VisitStmt(init);
        }
        Expr * cond = for_stmt->getCond();
        for(;mEnv->calculate(cond);mEnv->calculate(for_stmt->getInc())) {
            Visit(for_stmt->getBody());
        }
    }
private:
    Environment * mEnv;
};

class InterpreterConsumer : public ASTConsumer {
public:
    explicit InterpreterConsumer(const ASTContext& context) : mEnv(),
                                                              mVisitor(context, &mEnv) {
    }
    ~InterpreterConsumer() override = default;

    void HandleTranslationUnit(clang::ASTContext &Context) override {
        TranslationUnitDecl * decl = Context.getTranslationUnitDecl();
        mEnv.init(decl);

        FunctionDecl * entry = mEnv.getEntry();
        mVisitor.VisitStmt(entry->getBody());
    }
private:
    Environment mEnv;
    InterpreterVisitor mVisitor;
};

class InterpreterClassAction : public ASTFrontendAction {
public:
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
            clang::CompilerInstance &Compiler, llvm::StringRef InFile) override {
        return std::unique_ptr<clang::ASTConsumer>(
                new InterpreterConsumer(Compiler.getASTContext()));
    }
};

int main (int argc, char ** argv) {
    if (argc > 1) {
        std::ifstream code_file(argv[1]);
        if (code_file.is_open()) {
            std::string code((std::istreambuf_iterator<char>(code_file)), std::istreambuf_iterator<char>());
            clang::tooling::runToolOnCode(std::unique_ptr<clang::FrontendAction>(new InterpreterClassAction), code);
        }
        else {
            clang::tooling::runToolOnCode(std::unique_ptr<clang::FrontendAction>(new InterpreterClassAction), argv[1]);
        }
    }
    return 0;
}

