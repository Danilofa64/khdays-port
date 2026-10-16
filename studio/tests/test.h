// The tool's tests: synthetic data only, made by the
// tests in a scratch folder; nothing of the game.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace studio_test {

struct Case {
    const char* name;
    void (*run)();
};

std::vector<Case>& cases();

struct Register {
    Register(const char* name, void (*run)()) {
        cases().push_back({name, run});
    }
};

void fail(const char* file, int line, const char* what);

// A fresh empty folder for one test, removed when the tests end.
std::filesystem::path scratch(const std::string& name);

// Writes bytes to a file (folders made).
void write(const std::filesystem::path& file, const std::string& bytes);

}  // namespace studio_test

#define TEST(name)                                                                                                     \
    static void name();                                                                                                \
    static const studio_test::Register register_##name(#name, name);                                                   \
    static void name()

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition))                                                                                              \
            studio_test::fail(__FILE__, __LINE__, #condition);                                                         \
    } while (0)
