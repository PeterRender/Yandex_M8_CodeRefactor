#pragma once

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"

#include <unordered_set>

// Класс-обработчик матчей
// Получает найденные AST-узлы и правит код (вставляет virtual, override, &)
class RefactorHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    explicit RefactorHandler(clang::Rewriter &Rewrite) : Rewrite(Rewrite) {}
    // Метод run вызывается для каждого совпадения с матчем.
    // Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
    virtual void run(const clang::ast_matchers::MatchFinder::MatchResult &Result) override;

private:
    // 1. Невиртуальные деструкторы
    void handle_nv_dtor(const clang::CXXDestructorDecl *Dtor, clang::DiagnosticsEngine &Diag, clang::SourceManager &SM);

    // 2. Методы без override
    void handle_miss_override(const clang::CXXMethodDecl *Method, clang::DiagnosticsEngine &Diag,
                              clang::SourceManager &SM);

    // 3. range-for без &
    void handle_crange_for(const clang::VarDecl *LoopVar, clang::DiagnosticsEngine &Diag, clang::SourceManager &SM);

private:
    clang::Rewriter &Rewrite;
    std::unordered_set<unsigned>
        virtualDtorLocations;  // Для хранения позиций деструкторов, к которым уже добавлен virtual
};

// Класс-потребитель AST
// Регистрирует матчеры и запускает поиск проблемных узлов в коде
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

// Класс, управляющий рефакторингом
// Запускает процесс анализа для каждого файла в проекте,
// создает Rewriter и сохраняет изменения после обработки каждого файла
class CodeRefactorAction : public clang::ASTFrontendAction {
public:
    // Создает потребителя AST для каждого файла
    virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &CI,
                                                                  clang::StringRef file) override;

    // Вызывается в начале обработки файла (инициализирует Rewriter)
    virtual bool BeginSourceFileAction(clang::CompilerInstance &CI) override;

    // Вызывается в конце обработки файла (сохраняет изменения в файле)
    virtual void EndSourceFileAction() override;

private:
    clang::Rewriter RewriterForCodeRefactor;
};