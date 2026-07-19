#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"

#include <unordered_set>

#include "RefactorTool.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolCategory("refactor-tool options");

// Метод run вызывается для каждого совпадения с матчем.
// Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
void RefactorHandler::run(const MatchFinder::MatchResult &Result) {
    auto &Diag = Result.Context->getDiagnostics();
    auto &SM = *Result.SourceManager;  // Получаем SourceManager для проверки isInMainFile

    if (const auto *Dtor = Result.Nodes.getNodeAs<CXXDestructorDecl>("nonVirtualDtor")) {
        handle_nv_dtor(Dtor, Diag, SM);
    }

    if (const auto *Method = Result.Nodes.getNodeAs<CXXMethodDecl>("missingOverride");
        Method && Method->size_overridden_methods() > 0 && !Method->hasAttr<OverrideAttr>()) {
        handle_miss_override(Method, Diag, SM);
    }

    if (const auto *LoopVar = Result.Nodes.getNodeAs<VarDecl>("loopVar")) {
        handle_crange_for(LoopVar, Diag, SM);
    }
}

// обработка случая невиртуального деструктора
// с избеганием дублей (ограничиваемся одним SourceLocation)
void RefactorHandler::handle_nv_dtor(const CXXDestructorDecl *Dtor, DiagnosticsEngine &Diag, SourceManager &SM) {
    SourceLocation location = SM.getSpellingLoc(Dtor->getLocation());

    // проверяем, что совпадение находится в основном файле, а не в include
    if (!SM.isInMainFile(location)) {
        return;
    }

    // offset перед деструктором
    size_t offset = SM.getFileOffset(location);

    // проверяем на первую обработку этого деструктора
    auto [_, result] = virtualDtorLocations.insert(offset);
    if (!result) {
        return;
    }

    // добавляем virtual перед деструктором
    Rewrite.InsertText(location, "virtual ", true, true);

    size_t DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark,
                                         "Объявлен невиртуальный деструктор базового класса, исправлено");
    Diag.Report(location, DiagID);
}

// обработка случая отсутствие override
void RefactorHandler::handle_miss_override(const CXXMethodDecl *Method, DiagnosticsEngine &Diag, SourceManager &SM) {
    // проверяем, что совпадение находится в основном файле, а не в include
    SourceLocation location = SM.getSpellingLoc(Method->getLocation());
    if (!SM.isInMainFile(location)) {
        return;
    }

    // endpos указывает на конец имени метода, после которого идут скобки с параметрами
    auto endpos = Method->getNameInfo().getEndLoc();

    // ищем позицию закрывающей скобки
    auto ch = SM.getCharacterData(endpos);
    auto ch_end = SM.getCharacterData(SM.getLocForEndOfFile(SM.getFileID(endpos)));
    size_t offset = SM.getFileOffset(endpos);
    while (*ch != ')' && ch != ch_end) {
        ++ch;
        ++offset;
    }

    // обновляем endpos на позицию закрывающей скобки
    endpos = SM.getComposedLoc(SM.getFileID(endpos), offset);

    // добавляем override после описания входных параметров метода
    Rewrite.InsertTextAfterToken(endpos, " override");

    const unsigned DiagID =
        Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Объявлен виртуальный метод без override, исправлено");
    Diag.Report(Method->getLocation(), DiagID);
}

// обработка случая отсутствие & в range-for
void RefactorHandler::handle_crange_for(const VarDecl *LoopVar, DiagnosticsEngine &Diag, SourceManager &SM) {
    // проверяем, что совпадение находится в основном файле
    SourceLocation location = SM.getSpellingLoc(LoopVar->getLocation());
    if (!SM.isInMainFile(location)) {
        return;
    }

    // не меняем случаи, когда тип уже является ссылкой или фундаментальным типом
    auto varType = LoopVar->getType();
    if (varType->isReferenceType() || varType->isFundamentalType()) {
        return;
    }

    SourceLocation endpos = LoopVar->getTypeSourceInfo()->getTypeLoc().getEndLoc();
    Rewrite.InsertTextAfterToken(endpos, "&");

    const unsigned DiagID =
        Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Объявлена non-ref переменная в range-for, исправлено");
    Diag.Report(LoopVar->getLocation(), DiagID);
}

// матчер, который находит объявление деструктора базового класса без virtual, если у класса есть производные типы
auto NvDtorMatcher() {
    // clang-format off
    return cxxRecordDecl(
        isDefinition(),
        isDerivedFrom(
            cxxRecordDecl(
                has(
                    cxxDestructorDecl(
                    unless(isVirtual()), // без virtual
                        unless(isImplicit()), // без implicit
                        unless(isDeleted())   // без = delete
                    ).bind("nonVirtualDtor")
                )
            )
        )

    );
    // clang-format on
}

// матчер для метода, который действительно переопределяет базовый, но не имеет атрибута override
auto NoOverrideMatcher() {
    // clang-format off
    return cxxMethodDecl(
        isOverride(), // переопределяет
        isVirtual(), // виртуальный
        unless(isImplicit()), // без implicit
        unless(isDeleted()), // без = delete
        unless(cxxDestructorDecl()), // не деструктор
        unless(hasAttr(clang::attr::Override)) // без override
    ).bind("missingOverride");
    // clang-format on
}

// матчер для поиска range-for с переменной вида const T без &
auto NoRefConstVarInRangeLoopMatcher() {
    // clang-format off
    return cxxForRangeStmt(
        hasLoopVariable(
            varDecl(
                hasType(isConstQualified()), // const T
                unless(hasType(referenceType())), // без &
                unless(hasType(builtinType())), // без встроенных типов (int, char, bool, etc.)
                unless(hasType(autoType(hasDeducedType(builtinType())))) // без auto с выводом во встроенный тип
            ).bind("loopVar")
        )
    );
    // clang-format on
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

int main(int argc, const char **argv) {
    // Парсер опций: Обрабатывает флаги командной строки, компиляционные базы данных.
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();
    // Создаем ClangTool
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
    // Запускаем RefactorAction.
    return Tool.run(newFrontendActionFactory<CodeRefactorAction>().get());
}