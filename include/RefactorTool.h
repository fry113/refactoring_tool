#pragma once
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"

#include <unordered_set>

using clang::CXXDestructorDecl;
using clang::CXXMethodDecl;
using clang::DiagnosticsEngine;
using clang::VarDecl;
using clang::ast_matchers::MatchFinder;

class RefactorHandler : public MatchFinder::MatchCallback {
public:
    explicit RefactorHandler(clang::Rewriter &Rewrite) : Rewrite(Rewrite) {}
    // Метод run вызывается для каждого совпадения с матчем.
    // Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
    virtual void run(const MatchFinder::MatchResult &Result) override;

private:
    // 1. Невиртуальные деструкторы
    void handle_nv_dtor(const CXXDestructorDecl *Dtor, DiagnosticsEngine &Diag, clang::SourceManager &SM);

    // 2. Методы без override
    void handle_miss_override(const CXXMethodDecl *Method, DiagnosticsEngine &Diag, clang::SourceManager &SM);

    // 3. range-for без &
    void handle_crange_for(const VarDecl *LoopVar, DiagnosticsEngine &Diag, clang::SourceManager &SM);

private:
    clang::Rewriter &Rewrite;
    std::unordered_set<unsigned>
        virtualDtorLocations;  // Для хранения позиций деструкторов, к которым уже добавлен virtual
};

class ComplexConsumer : public clang::ASTConsumer {
public:
    // Конструктор принимает Rewriter для изменения кода.
    explicit ComplexConsumer(clang::Rewriter &Rewrite);
    // Метод HandleTranslationUnit вызывается для каждого файла.
    void HandleTranslationUnit(clang::ASTContext &Context) override;

private:
    RefactorHandler Handler;                  // Обработчик матчеров.
    clang::ast_matchers::MatchFinder Finder;  // MatchFinder для поиска узлов AST.
};

class CodeRefactorAction : public clang::ASTFrontendAction {
public:
    // Returns our ASTConsumer per translation unit.
    virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &CI,
                                                                  clang::StringRef file) override;
    virtual bool BeginSourceFileAction(clang::CompilerInstance &CI) override;
    virtual void EndSourceFileAction() override;

private:
    clang::Rewriter RewriterForCodeRefactor;
};