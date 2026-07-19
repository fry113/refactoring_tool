#include <filesystem>
#include <gtest/gtest.h>

// #include "../include/RefactorTool.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

// через создание временного файла проверяем работу утилиты
// результат возвращаем через оператор ()
class FileRefactor {
public:
    FileRefactor(const std::string &checkCode) {
        // создаем временный файл в системной временной директории
        auto path = std::filesystem::temp_directory_path() / "tmp_testing_file.cpp";
        tempPath = path.string();
        // запускаем утилиту и получаем результат
        processedCode = processCode(checkCode);
    }

    // гарантия удаления временного файла при выходе из области видимости или при исключении
    ~FileRefactor() {
        if (std::filesystem::exists(tempPath)) {
            std::filesystem::remove(tempPath);
        }
    }

    // защита от копирования и перемещения
    FileRefactor(const FileRefactor &) = delete;
    FileRefactor(FileRefactor &&) = delete;
    FileRefactor &operator=(const FileRefactor &) = delete;
    FileRefactor &operator=(FileRefactor &&) = delete;

    // std::string getProcessedCode() const { return processedCode; }
    std::string operator()() const { return processedCode; }

private:
    std::string processCode(const std::string &checkCode) {
        std::ofstream out(tempPath);
        out << checkCode;
        out.close();

        // формируем команду с абсолютным путем
        std::string command = execPath + tempPath + params;

        // запускаем утилиту и проверяем код возврата
        int res = std::system(command.c_str());
        if (res != 0) {
            std::cerr << "Тестирование: ошибка при запуске утилиты (код " << res << "): " << command << std::endl;
            return "---";
        }

        // возвращаем код из файла
        std::ifstream in(tempPath);
        std::stringstream buffer;
        buffer << in.rdbuf();
        in.close();
        return buffer.str();
    }

private:
    std::string processedCode;
    std::string execPath = "./refactor_tool ";  // путь к исполняемому файлу утилиты
    std::string tempPath;                       // путь к временному файлу для тестирования
    std::string params = " -- -std=c++17";      // -Wno-everything ?
};

TEST(NonVirtualDtor, VirtualDestructorAdded) {
    FileRefactor check{"class Base {\n"
                       "public:\n"
                       "    ~Base() {} // требуется добавить\n"
                       "};\n"
                       "class Derived : public Base {};"};

    std::string control{"class Base {\n"
                        "public:\n"
                        "    virtual ~Base() {} // требуется добавить\n"
                        "};\n"
                        "class Derived : public Base {};"};

    EXPECT_EQ(check(), control);
}

TEST(NonVirtualDtor, VirtualDestructorNoNeedToAdd) {
    FileRefactor check{"class Base {\n"
                       "public:\n"
                       "    virtual ~Base() {} // не требуется добавлять\n"
                       "};\n"
                       "class Derived : public Base {};"};

    std::string control{"class Base {\n"
                        "public:\n"
                        "    virtual ~Base() {} // не требуется добавлять\n"
                        "};\n"
                        "class Derived : public Base {};"};

    EXPECT_EQ(check(), control);
}

TEST(NonVirtualDtor, VirtualDestructorNoHeritage) {
    FileRefactor check{"class Base {\n"
                       "public:\n"
                       "    ~Base() {} // не требуется добавлять\n"
                       "};"};

    std::string control{"class Base {\n"
                        "public:\n"
                        "    ~Base() {} // не требуется добавлять\n"
                        "};"};

    EXPECT_EQ(check(), control);
}

TEST(MissingOverride, OverrideOneAdded) {
    FileRefactor check{"class Base {\n"
                       "public:\n"
                       "    virtual void foo() {}\n"
                       "};\n"
                       "class Derived : public Base {\n"
                       "public:\n"
                       "    void foo() {} // необходимо добавить\n"
                       "};"};

    std::string control{"class Base {\n"
                        "public:\n"
                        "    virtual void foo() {}\n"
                        "};\n"
                        "class Derived : public Base {\n"
                        "public:\n"
                        "    void foo() override {} // необходимо добавить\n"
                        "};"};

    EXPECT_EQ(check(), control);
}

TEST(MissingOverride, OverrideMultipleAdded) {
    FileRefactor check{"class Base {\n"
                       "public:\n"
                       "    virtual void foo() {}\n"
                       "    virtual void bar() {}\n"
                       "    virtual void baz() {}\n"
                       "};\n"
                       "class Derived : public Base {\n"
                       "public:\n"
                       "    void foo() {} // необходимо добавить\n"
                       "    void bar() {} // необходимо добавить\n"
                       "    void baz() {} // необходимо добавить\n"
                       "};"};

    std::string control{"class Base {\n"
                        "public:\n"
                        "    virtual void foo() {}\n"
                        "    virtual void bar() {}\n"
                        "    virtual void baz() {}\n"
                        "};\n"
                        "class Derived : public Base {\n"
                        "public:\n"
                        "    void foo() override {} // необходимо добавить\n"
                        "    void bar() override {} // необходимо добавить\n"
                        "    void baz() override {} // необходимо добавить\n"
                        "};"};

    EXPECT_EQ(check(), control);
}

TEST(MissingOverride, OverrideNoNeedToAdd) {
    FileRefactor check{"class Base {\n"
                       "public:\n"
                       "    virtual void foo() {}\n"
                       "};\n"
                       "class Derived : public Base {\n"
                       "public:\n"
                       "    void foo() override {} // не требуется добавлять\n"
                       "};"};

    std::string control{"class Base {\n"
                        "public:\n"
                        "    virtual void foo() {}\n"
                        "};\n"
                        "class Derived : public Base {\n"
                        "public:\n"
                        "    void foo() override {} // не требуется добавлять\n"
                        "};"};

    EXPECT_EQ(check(), control);
}

TEST(LoopVar, ReferenceNoNeedToAddInt) {
    FileRefactor check{"#include <vector>\n"
                       "void foo() {\n"
                       "    std::vector<int> vec;\n"
                       "    for (const int x : vec) {} // не требуется добавлять\n"
                       "}"};

    std::string control{"#include <vector>\n"
                        "void foo() {\n"
                        "    std::vector<int> vec;\n"
                        "    for (const int x : vec) {} // не требуется добавлять\n"
                        "}"};

    EXPECT_EQ(check(), control);
}

TEST(LoopVar, ReferenceNoNeedToAddAutoInt) {
    FileRefactor check{"#include <vector>\n"
                       "void foo() {\n"
                       "    std::vector<int> vec;\n"
                       "    for (const auto x : vec) {} // не требуется добавлять\n"
                       "}"};

    std::string control{"#include <vector>\n"
                        "void foo() {\n"
                        "    std::vector<int> vec;\n"
                        "    for (const auto x : vec) {} // не требуется добавлять\n"
                        "}"};

    EXPECT_EQ(check(), control);
}

TEST(LoopVar, ReferenceAddedToString) {
    FileRefactor check{"#include <vector>\n"
                       "#include <string>\n"
                       "void foo() {\n"
                       "    std::vector<std::string> vec;\n"
                       "    for (const auto x : vec) {} // необходимо добавить\n"
                       "}"};

    std::string control{"#include <vector>\n"
                        "#include <string>\n"
                        "void foo() {\n"
                        "    std::vector<std::string> vec;\n"
                        "    for (const auto& x : vec) {} // необходимо добавить\n"
                        "}"};

    EXPECT_EQ(check(), control);
}

TEST(LoopVar, ReferenceAddedToStruct) {
    FileRefactor check{"#include <vector>\n"
                       "struct Base {};\n"
                       "void foo() {\n"
                       "    std::vector<Base> vec;\n"
                       "    for (const auto b : vec) {} // необходимо добавить\n"
                       "}"};

    std::string control{"#include <vector>\n"
                        "struct Base {};\n"
                        "void foo() {\n"
                        "    std::vector<Base> vec;\n"
                        "    for (const auto& b : vec) {} // необходимо добавить\n"
                        "}"};

    EXPECT_EQ(check(), control);
}
