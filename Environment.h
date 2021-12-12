//
// Created by Critizero on 2021/10/22.
//

#ifndef ASSIGN1_ENVIRONMENT_H
#define ASSIGN1_ENVIRONMENT_H

//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//
#include <stdio.h>
#include <stdint.h>
#include <iostream>
#include <exception>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

class StackFrame {
    /// StackFrame maps Variable Declaration to Value
    /// Which are either integer or addresses (also represented using an Integer(64bits) value)
    std::map<Decl*, int64_t> mVars;
    std::map<Stmt*, int64_t> mExprs;
    /// The current stmt
    Stmt * mPC;
    /// Store return value
    int64_t mRetValue;
    /// To decide weather to return : for recursion
    bool mRet;
public:
    StackFrame() : mVars(), mExprs(), mPC(), mRet(false) {
    }

    void bindDecl(Decl* decl, int64_t val) {
        mVars[decl] = val;
    }
    int64_t getDeclVal(Decl * decl) {
        assert (mVars.find(decl) != mVars.end());
        return mVars.find(decl)->second;
    }
    void bindStmt(Stmt * stmt, int64_t val) {
        mExprs[stmt] = val;
    }
    int64_t getStmtVal(Stmt * stmt) {
        assert (mExprs.find(stmt) != mExprs.end());
        return mExprs[stmt];
    }
    void setPC(Stmt * stmt) {
        mPC = stmt;
    }
    bool exprExits(Stmt * stmt) {
        return mExprs.find(stmt) != mExprs.end();
    }
    void pushStmt(Stmt * stmt, int64_t val) {
        mExprs.insert(std::pair<Stmt *, int64_t>(stmt, val));
    }
    void setRetVal(int64_t val) {
        mRetValue = val;
    }
    int64_t getRetVal() {
        return mRetValue;
    }
    bool retTime() {
        return mRet;
    }
    void toReturn() {
        mRet =true;
    }
    Stmt * getPC() {
        return mPC;
    }
};

/// Heap maps address to a value

class Heap {
private:
    std::map<int64_t, int64_t> mMemory;
public:
    Heap() : mMemory() {
    }
    char * Malloc(int64_t size) {
        char * p = new char[size];
        int64_t ptr = (int64_t)p;
        mMemory[ptr] = size;
        return p;
    }
    void Free (int64_t addr) {
        char * p = (char *)addr;
        if (mMemory.find(addr) != mMemory.end()) {
            delete  p;
            mMemory.erase(addr);
        }
        else {
            llvm::errs()  << "[ERROR] Not A Valid Address";
            exit(0);
        }
    }
};


class Environment {
    std::vector<StackFrame> mStack;
    Heap mHeap;

    FunctionDecl * mFree;				/// Declartions to the built-in functions
    FunctionDecl * mMalloc;
    FunctionDecl * mInput;
    FunctionDecl * mOutput;

    FunctionDecl * mEntry;
public:
    /// Get the declartions to the built-in functions
    Environment() : mStack(), mHeap(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL), mEntry(NULL) {
    }

    /// Set return value
    void bindReturnValue(CallExpr * call) {
        int64_t ret = mStack.back().getRetVal();
        mStack.pop_back();
        mStack.back().bindStmt(call, ret);
    }

    /// Decide weather to return
    bool timeToReturn() {
        return mStack.back().retTime();
    }

    void returnStmt(ReturnStmt * returnstmt) {
//        llvm::errs() << "Into returnStmt\n";
        int64_t value = calculate(returnstmt->getRetValue());
        mStack.back().setRetVal(value);
        mStack.back().toReturn();
//        llvm::errs() << "Exit returnStmt\n";
    }

    /// Initialize the Environment
    void init(TranslationUnitDecl * unit) {
//        llvm::errs() << "Into init\n";

        /// This frame is used to store the global var.
        mStack.push_back(StackFrame());

        for (TranslationUnitDecl::decl_iterator i =unit->decls_begin(), e = unit->decls_end(); i != e; ++ i) {
//            i->dumpColor();
            if (FunctionDecl * fdecl = dyn_cast<FunctionDecl>(*i) ) {
                if (fdecl->getName().equals("FREE")) mFree = fdecl;
                else if (fdecl->getName().equals("MALLOC")) mMalloc = fdecl;
                else if (fdecl->getName().equals("GET")) mInput = fdecl;
                else if (fdecl->getName().equals("PRINT")) mOutput = fdecl;
                else if (fdecl->getName().equals("main")) mEntry = fdecl;
            }
            else if(VarDecl * vdecl = dyn_cast<VarDecl>(*i)){
                if (vdecl->getType().getTypePtr()->isIntegerType() ||
                    vdecl->getType().getTypePtr()->isCharType() ||
                    vdecl->getType().getTypePtr()->isPointerType() ||
                    vdecl->getType().getTypePtr()->isVoidType()){
                    if(vdecl->hasInit()){
                        mStack.back().bindDecl(vdecl, calculate(vdecl->getInit()));
                    }
                    else{
                        mStack.back().bindDecl(vdecl, 0);
                    }
                }
                /// !Todo : Supply for Arrays' initialization
            }
        }
//        llvm::errs() << "Exit init\n";
    }

    FunctionDecl * getEntry() {
//        llvm::errs() << "Into getEntry\n";
//        llvm::errs() << "Exit getEntry\n";
        return mEntry;
    }

    /// Binary operation
    void binop(BinaryOperator *bop) {
//        llvm::errs() << "Into binop\n";
        Expr * left = bop->getLHS();
        Expr * right = bop->getRHS();

        if (bop->isAssignmentOp()) {
            if (DeclRefExpr * declexpr = dyn_cast<DeclRefExpr>(left)) {
                int64_t val = calculate(right);
                Decl * decl = declexpr->getFoundDecl();
                mStack.back().bindDecl(decl, val);
                mStack.back().bindStmt(bop, val);
            }
            else if (auto array = dyn_cast<ArraySubscriptExpr>(left)) {
                if (DeclRefExpr * declexpr = dyn_cast<DeclRefExpr>(array->getLHS()->IgnoreImpCasts())) {
                    Decl * decl = declexpr->getFoundDecl();
                    int64_t val = calculate(right);
                    int64_t idx = calculate(array->getRHS());
                    if (VarDecl * vdecl = dyn_cast<VarDecl>(decl)) {
                        if (auto arr = dyn_cast<ConstantArrayType>(vdecl->getType().getTypePtr())) {
                            if (arr->getElementType().getTypePtr()->isIntegerType()) {
                                int64_t p = mStack.back().getDeclVal(vdecl);
                                *((int64_t *)p + idx) = (int64_t)val;
                            }
                            else if (arr->getElementType().getTypePtr()->isCharType()) {
                                int64_t p = mStack.back().getDeclVal(vdecl);
                                *((char *)p + idx) = (char)val;
                            }
                            else{
                                int64_t p = mStack.back().getDeclVal(vdecl);
                                *((int64_t *)p + idx) = (int64_t)val;
                            }
                        }
                    }
                }
            }
            else if (auto uaexpr = dyn_cast<UnaryOperator>(left)) {
                int64_t val = calculate(right);
                int64_t addr = calculate(uaexpr->getSubExpr());
                *((int64_t *)addr) = val;
            }
        }
        else {
            auto op = bop->getOpcode();
            int64_t vleft, vright, vresult;
            switch (op) {
                case BO_Add: {
                    if (left->getType().getTypePtr()->isPointerType()) {
                        int64_t base = calculate(left);
                        vresult = base + 8 * calculate(right);
                    }
                    else {
                        /// int64_t sss = expr(right);
                        vresult = calculate(left) + calculate(right);
                    }
                    break;
                }
                case BO_Sub: {
                    if (left->getType().getTypePtr()->isPointerType()) {
                        int64_t base = calculate(left);
                        vresult = base - 8 * calculate(right);
                    }
                    else {
                        vresult = calculate(left) - calculate(right);
                    }
                    break;
                }
                case BO_Mul: {
                    vresult = calculate(left) * calculate(right);
                    break;
                }
                case BO_Div: {
                    vright = calculate(right);
                    if (vright == 0){
                        llvm::errs()  << "[ERROR] Dived By Zero";
                        exit(0);
                    }
                    vleft = calculate(left);
                    vresult = vleft / vright;
                    break;
                }
                case BO_LT: {
                    vresult = calculate(left) < calculate(right);
                    break;
                }
                case BO_GT: {
                    vresult = calculate(left) > calculate(right);
                    break;
                }
                case BO_EQ: {
                    vresult = calculate(left) == calculate(right);
                    break;
                }
                default: {
                    llvm::errs()  << "[ERROR] Unknown Operator";
                    exit(0);
                    break;
                }
            }
            if (mStack.back().exprExits(bop)) {
                mStack.back().bindStmt(bop, vresult);
            }
            else {
                mStack.back().pushStmt(bop, vresult);
            }
        }
//        llvm::errs() << "Exit binop\n";
    }

    void unaryop(UnaryOperator * uop) {
//        llvm::errs() << "Into unaryop\n";
        auto op = uop->getOpcode();
        auto s_expr = uop->getSubExpr();
        switch(op) {
            case UO_Minus: {
                mStack.back().pushStmt(uop, -1 * calculate(s_expr));
                break;
            }
            case UO_Plus: {
                mStack.back().pushStmt(uop, calculate(s_expr));
                break;
            }
            case UO_Deref: {
                mStack.back().pushStmt(uop, *(int64_t *)calculate(s_expr));
                break;
            }
            default: {
                llvm::errs()  << "[ERROR] Unknown Operator";
                exit(0);
                break;
            }
        }
//        llvm::errs() << "Exit unaryop\n";
    }

    void decl(DeclStmt * decl_stmt) {
//        llvm::errs() << "Into decl\n";
        for (DeclStmt::decl_iterator it = decl_stmt->decl_begin(), ie = decl_stmt->decl_end();
             it != ie; ++ it) {
            Decl * decl = *it;
            if (VarDecl * var_decl = dyn_cast<VarDecl>(decl)) {
                if (var_decl->getType().getTypePtr()->isIntegerType() ||
                    var_decl->getType().getTypePtr()->isCharType() ||
                    var_decl->getType().getTypePtr()->isPointerType() ||
                    var_decl->getType().getTypePtr()->isVoidType()) {
                    if (var_decl->hasInit()) {
                        mStack.back().bindDecl(var_decl, calculate(var_decl->getInit()));
                    }
                    else {
                        mStack.back().bindDecl(var_decl, 0);
                    }
                }
                else {
                    if (auto array = dyn_cast<ConstantArrayType>(var_decl->getType().getTypePtr())) {
                        int64_t length = array->getSize().getSExtValue();
                        if (array->getElementType().getTypePtr()->isIntegerType()) {
                            int64_t *p = new int64_t[length];
                            for (int64_t i = 0; i < length; i++) {
                                p[i] = 0;
                            }
                            mStack.back().bindDecl(var_decl, (int64_t)p);
                        }
                        else if (array->getElementType().getTypePtr()->isCharType()) {
                            char *p = new char[length];
                            for (int64_t i = 0; i < length; i++) {
                                p[i] = 0;
                            }
                            mStack.back().bindDecl(var_decl, (int64_t)p);
                        }
                        else {
                            int64_t *p = new int64_t[length];
                            for (int64_t i = 0; i < length; i++) {
                                p[i] = 0;
                            }
                            mStack.back().bindDecl(var_decl, (int64_t)p);
                        }
                    }
                }
            }
        }
//        llvm::errs() << "Exit decl\n";
    }

    void declref(DeclRefExpr * decl_ref) {
//        llvm::errs() << "Into declref\n";
        mStack.back().setPC(decl_ref);
        if (decl_ref->getType()->isIntegerType()) {
            Decl* decl = decl_ref->getFoundDecl();
            int64_t val = mStack.back().getDeclVal(decl);
            mStack.back().bindStmt(decl_ref, val);
        }
        else if (decl_ref->getType()->isPointerType()) {
            Decl * decl = decl_ref->getFoundDecl();
            int64_t val = mStack.back().getDeclVal(decl);
            mStack.back().bindStmt(decl_ref, val);
        }
        else if (decl_ref->getType()->isArrayType()) {
            Decl * decl = decl_ref->getFoundDecl();
            int64_t val = mStack.back().getDeclVal(decl);
            mStack.back().bindStmt(decl_ref, val);
        }
//        llvm::errs() << "Exit declref\n";
    }

    void cast(CastExpr * cast_expr) {
//        llvm::errs() << "Into cast\n";
        Expr *  c_expr = cast_expr->IgnoreImpCasts();
        mStack.back().setPC(cast_expr);
        if (c_expr->getType()->isIntegerType()) {
            //expr(c_expr);
            //int64_t val = mStack.back().getStmtVal(c_expr);
            int64_t val = calculate(c_expr);
            //int64_t val = mStack.back().getStmtVal(c_expr);
            mStack.back().bindStmt(cast_expr, val);
        }
        else if (c_expr->getType()->isArrayType()) {
            int64_t val = calculate(c_expr->getExprStmt());
            mStack.back().bindStmt(cast_expr, val);
        }
        else if (c_expr->getType()->isFunctionType()) {
            DeclRefExpr * decl_ref_expr = dyn_cast<DeclRefExpr>(c_expr->getExprStmt());
            Decl * decl_expr = decl_ref_expr->getFoundDecl();
            int64_t val = (int64_t)decl_expr;
            mStack.back().bindStmt(cast_expr, val);
        }
//        llvm::errs() << "Exit cast\n";
    }

    /// Function Call
    void call(CallExpr * call_expr) {
//        llvm::errs() << "Into call\n";
        mStack.back().setPC(call_expr);
        int64_t val = 0;
        FunctionDecl *callee = call_expr->getDirectCallee();
        if (callee == mInput) {
            llvm::errs() << "Please Input an Integer Value : ";
            scanf("%ld", &val);

            mStack.back().bindStmt(call_expr, val);
        } else if (callee == mOutput) {
            Expr *decl = call_expr->getArg(0);
            val = mStack.back().getStmtVal(decl);
            llvm::errs() << val;
        } else if (callee == mMalloc) {
            Expr *decl = call_expr->getArg(0);
            int64_t addr = (int64_t) mHeap.Malloc(calculate(decl));
            mStack.back().bindStmt(call_expr, addr);
        } else if (callee == mFree) {
            Expr *decl = call_expr->getArg(0)->IgnoreImpCasts();
            if (auto sub_expr = dyn_cast<CStyleCastExpr>(decl)) {
                decl = sub_expr->getSubExpr()->IgnoreImpCasts();
            }
            mHeap.Free(mStack.back().getStmtVal(decl));
        } else {
            std::vector<int64_t> params;
            for (auto b = call_expr->arg_begin(), e = call_expr->arg_end(); b != e; b++) {
                params.push_back(calculate(*b));
            }
            mStack.emplace_back(StackFrame());
            int64_t idx = 0;
            for (auto i = callee->param_begin(), j = callee->param_end(); i != j; i++) {
                mStack.back().bindDecl(*i, params[idx++]);
            }
        }
//        llvm::errs() << "Exit call\n";
    }

    int64_t calculate(Expr * request) {
//        llvm::errs() << "Into expr\n";
        request = request->IgnoreImpCasts();
        if (auto exp = dyn_cast<IntegerLiteral>(request)) {
            return exp->getValue().getSExtValue();
        }
        else if (auto exp = dyn_cast<DeclRefExpr>(request)) {
            declref(exp);
            return mStack.back().getStmtVal(exp);
        }
        else if (auto exp = dyn_cast<BinaryOperator>(request)) {
            binop(exp);
            return mStack.back().getStmtVal(exp);
        }
        else if (auto exp = dyn_cast<UnaryOperator>(request)) {
            unaryop(exp);
            return mStack.back().getStmtVal(exp);
        }
        else if (auto exp = dyn_cast<CallExpr>(request)) {
            // Return Value
            return mStack.back().getStmtVal(exp);
        }
        else if (auto exp = dyn_cast<CStyleCastExpr>(request)) {
            return calculate(exp->getSubExpr());
        }
        else if (auto exp = dyn_cast<ParenExpr>(request)) {
            return calculate(exp->getSubExpr());
        }
        else if (auto exp = dyn_cast<UnaryExprOrTypeTraitExpr>(request)) {
            if (exp->getKind() == UETT_SizeOf) {
                if (exp->getArgumentType()->isIntegerType()) {
                    return sizeof(int64_t);
                }
                else if (exp->getArgumentType()->isPointerType()) {
                    return sizeof(int64_t *);
                }
                else if (exp->getArgumentType()->isCharType()) {
                    return sizeof(char);
                }
            }
        }
        else if (auto exp = dyn_cast<ArraySubscriptExpr>(request)) {
            if (DeclRefExpr * decl_ref = dyn_cast<DeclRefExpr>(exp->getLHS()->IgnoreImpCasts())) {
                Decl * decl = decl_ref->getFoundDecl();
                int64_t idx = calculate(exp->getRHS());
                if (VarDecl * v_decl = dyn_cast<VarDecl>(decl)) {
                    if(auto array = dyn_cast<ConstantArrayType>(v_decl->getType().getTypePtr())) {
                        if (array->getElementType().getTypePtr()->isIntegerType()) {
                            int64_t * p = (int64_t *)mStack.back().getDeclVal(v_decl);
                            int64_t val = p[idx];
                            return val;
                        }
                        else if (array->getElementType().getTypePtr()->isIntegerType()) {
                            char * p = (char *)mStack.back().getDeclVal(v_decl);
                            int64_t val = (int64_t)p[idx];
                            return val;
                        }
                        else {
                            int64_t * p = (int64_t *)mStack.back().getDeclVal(v_decl);
                            int64_t val = p[idx];
                            return val;
                        }
                    }
                }
            }
        }
        else {
            llvm::errs() << "[ERROR] Unknown Expr";
            exit(0);
        }
//        llvm::errs() << "Exit expr\n";
        return 0;
    }
};




#endif //ASSIGN1_ENVIRONMENT_H
