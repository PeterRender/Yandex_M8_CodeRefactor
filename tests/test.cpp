#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

using namespace testing;  // для доступа к матчерам GMock

// Структура для хранения тестового случая
struct RefactorTestCase {
    std::string name;
    std::string input;
    std::string expected;
};

// === Тестовые данные ===

// Для случая "Невиртуальный деструктор"
std::vector<RefactorTestCase> dtorTestCases = {
    {"AddToBase", "class B { public: ~B() {} }; class D : public B {};", "virtual ~B()"},
    {"AddToRoot", "class G { public: ~G() {} }; class P : public G {}; class C : public P {};", "virtual ~G()"},
    {"AddFromMulti", "class B { public: ~B() {} }; class C : public B {}; class D : public B {};", "virtual ~B()"},
    {"SkipCorrect", "class B { public: virtual ~B() {} }; class D : public B {};", "public: virtual ~B()"},
    {"SkipStandalone", "class S { public: ~S() {} };", "public: ~S()"}};

// Для случая "Метод без override"
std::vector<RefactorTestCase> overrideTestCases = {
    {"AddToSimple", "class B { public: virtual void foo() {} }; class D : public B { public: void foo() {} };",
     "class D : public B { public: void foo() override {}"},
    {"AddToConst",
     "class B { public: virtual void foo() const {} }; class D : public B { public: void foo() const {} };",
     "class D : public B { public: void foo() const override {}"},
    {"AddToNoexcept",
     "class B { public: virtual void foo() noexcept {} }; class D : public B { public: void foo() noexcept {} };",
     "class D : public B { public: void foo() noexcept override {}"},
    {"AddToPure", "class B { public: virtual void foo() = 0; }; class D : public B { public: void foo() {} };",
     "class D : public B { public: void foo() override {} };"},
    {"SkipCorrect", "class B { public: virtual void foo() {} }; class D : public B { public: void foo() override {} };",
     "class D : public B { public: void foo() override {}"},
    {"SkipNonVirtual", "class B { public: void foo() {} }; class D : public B { public: void foo() {} };",
     "class D : public B { public: void foo() {}"},
    {"SkipStandalone", "class S { public: void foo() {} };", "class S { public: void foo() {} };"}};

// Для случая "Цикл range-for с переменной без &"
std::vector<RefactorTestCase> rangeForTestCases = {
    {"AddToConstCustom",
     "#include <vector>\nstruct Cust { int id[8]; };\nvoid foo() { std::vector<Cust> v; for (const Cust x : v) {} }",
     "for (const Cust& x : v)"},
    {"AddToConstAutoCustom",
     "#include <vector>\nstruct Cust { int id[8]; };\nvoid foo() { std::vector<Cust> v; for (const auto x : v) {} }",
     "for (const auto& x : v)"},
    {"AddToConstDecl",
     "#include <vector>\nstruct Cust { int id[8]; };\nvoid foo() { std::vector<Cust> v; for (const "
     "decltype(v)::value_type x : v) {} }",
     "for (const decltype(v)::value_type& x : v)"},
    {"SkipConstBuiltIn", "#include <vector>\nvoid foo() { std::vector<int> v; for (const int x : v) {} }",
     "for (const int x : v)"},
    {"SkipCorrectRef",
     "#include <vector>\nstruct Cust { int id[8]; };\nvoid foo() { std::vector<Cust> v; for (const auto& x : v) {} }",
     "for (const auto& x : v)"}};

// Базовая фикстура с общей логикой
class RefactorTestBase : public ::testing::Test {
protected:
    void SetUp() override {
        testDir = fs::temp_directory_path() / "refactor_test";
        fs::create_directories(testDir);
    }

    void TearDown() override { fs::remove_all(testDir); }

    // Запускает утилиту рефакторинга над тестовой строкой с cpp кодом
    void runRefactorOnCode(const std::string &inputCode, std::string &outputCode) {
        // Формируем input.cpp из тестовой входной строки и пишем его в тестовый каталог
        std::string inputFile = (testDir / "input.cpp").string();  // путь к input.cpp
        std::ofstream inputStream(inputFile);
        inputStream << inputCode;
        inputStream.close();

        // Запускаем утилиту рефакторинга над input.cpp (с подавлением вывода)
        std::string toolPath = "./refactor_tool";  // путь к утилите относительно каталога build
        std::string cmd = toolPath + " " + inputFile + " -- 2>/dev/null";
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            std::cerr << "Error: refactor_tool failed with exit code " << ret << std::endl;
            outputCode = "Error";
            return;
        }

        // Читаем отрефакторенный input.cpp в выходную строку
        std::ifstream resultStream(inputFile);
        outputCode = std::string(std::istreambuf_iterator<char>(resultStream), std::istreambuf_iterator<char>());
    }

    fs::path testDir;
};

// Параметрический тест и тест-сьют для случая "Невиртуальный деструктор"
class NonVirtualDtorTest : public RefactorTestBase, public ::testing::WithParamInterface<RefactorTestCase> {};

TEST_P(NonVirtualDtorTest, CheckRefactoring) {
    auto param = GetParam();
    std::string output;
    runRefactorOnCode(param.input, output);
    EXPECT_THAT(output, HasSubstr(param.expected)) << "Failed: " << param.name;
}
INSTANTIATE_TEST_SUITE_P(NonVirtualDtorVariants, NonVirtualDtorTest, ::testing::ValuesIn(dtorTestCases),
                         [](const ::testing::TestParamInfo<RefactorTestCase> &info) { return info.param.name; });

// Параметрический тест и тест-сьют для случая "Метод без override"
class OverrideTest : public RefactorTestBase, public ::testing::WithParamInterface<RefactorTestCase> {};

TEST_P(OverrideTest, CheckRefactoring) {
    auto param = GetParam();
    std::string output;
    runRefactorOnCode(param.input, output);
    EXPECT_THAT(output, HasSubstr(param.expected)) << "Failed: " << param.name;
}
INSTANTIATE_TEST_SUITE_P(OverrideVariants, OverrideTest, ::testing::ValuesIn(overrideTestCases),
                         [](const ::testing::TestParamInfo<RefactorTestCase> &info) { return info.param.name; });

// Параметрический тест и тест-сьют для случая "Цикл range-for с переменной без &"
class RangeForTest : public RefactorTestBase, public ::testing::WithParamInterface<RefactorTestCase> {};

TEST_P(RangeForTest, CheckRefactoring) {
    auto param = GetParam();
    std::string output;
    runRefactorOnCode(param.input, output);
    EXPECT_THAT(output, HasSubstr(param.expected)) << "Failed: " << param.name;
}
INSTANTIATE_TEST_SUITE_P(RangeForVariants, RangeForTest, ::testing::ValuesIn(rangeForTestCases),
                         [](const ::testing::TestParamInfo<RefactorTestCase> &info) { return info.param.name; });
