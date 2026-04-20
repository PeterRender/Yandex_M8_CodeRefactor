#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include <clang/Basic/AttrKinds.h>

#include <unordered_set>

#include "RefactorTool.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

// Метод run вызывается для каждого совпадения с матчем.
// Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
void RefactorHandler::run(const MatchFinder::MatchResult &Result) {
    auto &Diag = Result.Context->getDiagnostics();
    auto &SM = *Result.SourceManager;  // Получаем SourceManager для проверки isInMainFile

    // Обработка совпадения с матчем типа "невиртуальный деструктор"
    if (const auto *Dtor = Result.Nodes.getNodeAs<CXXDestructorDecl>("nonVirtualDtor")) {
        handle_nv_dtor(Dtor, Diag, SM);
    }

    // Обработка совпадения с матчем типа "метод без override"
    if (const auto *Method = Result.Nodes.getNodeAs<CXXMethodDecl>("missOverride");
        Method && Method->size_overridden_methods() > 0 && !Method->hasAttr<OverrideAttr>()) {
        handle_miss_override(Method, Diag, SM);
    }

    // Обработка совпадения с матчем типа "бессылочная переменная цикла range-for"
    if (const auto *LoopVar = Result.Nodes.getNodeAs<VarDecl>("loopVar")) {
        handle_crange_for(LoopVar, Diag, SM);
    }
}

// Обрабатывает матчер типа "невиртуальный деструктор"
void RefactorHandler::handle_nv_dtor(const CXXDestructorDecl *Dtor, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!Dtor)
        return;

    // Проверяем валидность сматченного деструктора (не в макросе и физически записан в основном файле)
    auto DtorLoc = Dtor->getLocation();  // позиция начала деструктора (тильды)
    if (DtorLoc.isInvalid() || DtorLoc.isMacroID() || !SM.isWrittenInMainFile(DtorLoc)) {
        return;
    }

    // Проверяем, что раньше не обрабатывали этот деструктор (может повторно матчиться)
    unsigned LocId = DtorLoc.getRawEncoding();  // уникальный id позиции начала деструктора
    if (virtualDtorLocations.count(LocId)) {
        return;  // деструктор уже обработан, выходим без изменений
    }

    // Вставляем "virtual " перед началом деструктора (false - успех, true - ошибка)
    if (Rewrite.InsertTextBefore(DtorLoc, "virtual "))
        return;  // вставка не удалась, выходим без изменений

    // Запоминаем, что уже обработали этот деструктор
    if (!virtualDtorLocations.insert(LocId).second)
        return;

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Объявлен деструктор");
    Diag.Report(Dtor->getLocation(), DiagID);
}

// Обрабатывает матчер типа "метод без override"
void RefactorHandler::handle_miss_override(const CXXMethodDecl *Method, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!Method)
        return;

    // Проверяем валидность сматченного метода (не в макросе и физически записан в основном файле)
    auto MethodLoc = Method->getLocation();  // позиция начала метода
    if (MethodLoc.isInvalid() || MethodLoc.isMacroID() || !SM.isWrittenInMainFile(MethodLoc)) {
        return;
    }

    // Получаем общую карту типа метода (тип = сигнатура - имя)
    TypeSourceInfo *TypeInfo = Method->getTypeSourceInfo();
    if (!TypeInfo)
        return;

    // Из общей карты извлекаем объект с детальной инфой о типе функции (координаты скобок, аргументов и спецификаторов)
    FunctionTypeLoc FuncType = TypeInfo->getTypeLoc().IgnoreParens().getAs<FunctionTypeLoc>();
    if (FuncType.isNull())
        return;  // извлечение не удалось, выходим без изменений

    // Проверяем валидность сигнатуры метода (наличие правой скобки)
    SourceLocation RParenLoc = FuncType.getRParenLoc();
    if (RParenLoc.isInvalid())
        return;

    // === Определяем точку вставки " override" ===
    // а) определяем начало последнего токена сигнатуры (у const это будет "c")
    auto InsertLoc = FuncType.getLocalRangeEnd();
    // б) получаем стандарт С++ из контекста проекта (для правильной трактовки токенов)
    const auto &LangOpts = Method->getASTContext().getLangOpts();
    // в) смещаем позицию за конец последнего токена сигнатуры (у const - после "t")
    InsertLoc = clang::Lexer::getLocForEndOfToken(InsertLoc, 0, SM, LangOpts);

    // Вставляем " override" в найденную позицию (false - успех, true - ошибка)
    if (Rewrite.InsertTextBefore(InsertLoc, " override")) {
        return;  // вставка не удалась, выходим без изменений
    }

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Объявлен метод");
    Diag.Report(Method->getLocation(), DiagID);
}

// Обрабатывает матчер типа "бессылочная переменная цикла range-for"
void RefactorHandler::handle_crange_for(const VarDecl *LoopVar, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!LoopVar)
        return;

    // Проверяем валидность сматченной переменной (не в макросе и физически записана в основном файле)
    auto VarLoc = LoopVar->getLocation();
    if (VarLoc.isInvalid() || VarLoc.isMacroID() || !SM.isWrittenInMainFile(VarLoc)) {
        return;
    }

    // Получаем общую карту типа переменной
    TypeSourceInfo *TypeInfo = LoopVar->getTypeSourceInfo();
    if (!TypeInfo)
        return;

    // === Определяем точку вставки "&" ===
    // а) определяем начало последнего токена в описании типа (у const auto это будет "a")
    auto InsertLoc = TypeInfo->getTypeLoc().getEndLoc();
    if (InsertLoc.isInvalid())
        return;
    // б) получаем стандарт С++ из контекста проекта (для правильной трактовки токенов)
    const auto &LangOpts = LoopVar->getASTContext().getLangOpts();
    // в) смещаем позицию за конец последнего токена типа (у const auto - после "o")
    InsertLoc = clang::Lexer::getLocForEndOfToken(InsertLoc, 0, SM, LangOpts);

    // Вставляем "&" в найденную позицию (false - успех, true - ошибка)
    if (Rewrite.InsertTextBefore(InsertLoc, "&")) {
        return;
    }

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Объявлена переменная");
    Diag.Report(LoopVar->getLocation(), DiagID);
}

// Narrowing матчер сущности, заданной явно и в текущем файле
auto explicitInFile = allOf(unless(isImplicit()), isExpansionInMainFile());

// Конструирует матчер для поиска невиртуальных деструкторов
auto NvDtorMatcher() {
    // Матчер класса, у которого есть невиртуальный деструктор (именуем его "nonVirtualDtor")
    auto classWithNvDtor =
        cxxRecordDecl(has(cxxDestructorDecl(explicitInFile, unless(isVirtual())).bind("nonVirtualDtor")));

    // Возвращаем матчер класса, производного от classWithNvDtor и имеющего определение
    return cxxRecordDecl(isDerivedFrom(classWithNvDtor), isDefinition());
}

// Конструирует матчер для поиска переопределенных методов без override
auto NoOverrideMatcher() {
    // Narrowing матчер спецификатора override или final
    auto virtSpec = anyOf(hasAttr(clang::attr::Override), hasAttr(clang::attr::Final));

    // Возвращаем матчер переопределенного метода без override/final (исключаем деструкторы)
    return cxxMethodDecl(explicitInFile, isOverride(), unless(virtSpec), unless(cxxDestructorDecl()))
        .bind("missOverride");  // именуем его "missOverride"
}

// Конструирует матчер для поиска бессылочных переменных циклов range-for
auto NoRefConstVarInRangeLoopMatcher() {
    // Narrowing матчер типа переменной цикла range-for
    // Тип должен быть const, не быть &, а поcле раскрытия алиасов не быть ни встроенным типом, ни указателем, ни enum
    auto varType = qualType(isConstQualified(), unless(referenceType()),
                            hasCanonicalType(unless(anyOf(builtinType(), pointerType(), enumType()))));

    // Возвращаем матчер цикла range-for с переменной, которая имеет тип varType (именуем ее "loopVar")
    return cxxForRangeStmt(hasLoopVariable(varDecl(explicitInFile, hasType(varType)).bind("loopVar")));
}

// Конструктор принимает Rewriter для изменения кода.
ComplexConsumer::ComplexConsumer(Rewriter &Rewrite) : Handler(Rewrite) {
    // Создаем MatchFinder и добавляем матчеры.
    Finder.addMatcher(NvDtorMatcher(), &Handler);
    Finder.addMatcher(NoOverrideMatcher(), &Handler);
    Finder.addMatcher(NoRefConstVarInRangeLoopMatcher(), &Handler);
}

// Метод HandleTranslationUnit вызывается для каждого файла.
void ComplexConsumer::HandleTranslationUnit(ASTContext &Context) { Finder.matchAST(Context); }

std::unique_ptr<ASTConsumer> CodeRefactorAction::CreateASTConsumer(CompilerInstance &CI, StringRef file) {
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<ComplexConsumer>(RewriterForCodeRefactor);
}

bool CodeRefactorAction::BeginSourceFileAction(CompilerInstance &CI) {
    // Инициализируем Rewriter для рефакторинга.
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return true;  // Возвращаем true, чтобы продолжить обработку файла.
}

void CodeRefactorAction::EndSourceFileAction() {
    // Применяем изменения в файле.
    if (RewriterForCodeRefactor.overwriteChangedFiles()) {
        llvm::errs() << "Error applying changes to files.\n";
    }
}