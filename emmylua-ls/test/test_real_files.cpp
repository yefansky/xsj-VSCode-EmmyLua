#include <doctest/doctest.h>
#include "parser/Lexer.h"
#include "parser/Parser.h"
#include <fstream>
#include <sstream>
#include <iostream>

using namespace emmy;

// Helper to parse a file and report errors
static void testParseFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        INFO("File not found: " << path);
        return;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    INFO("Parsing: " << path << " (" << source.size() << " bytes)");

    Parser parser(source);
    auto chunk = parser.parse();

    if (!parser.errors().empty()) {
        INFO("Errors: " << parser.errors().size());

        // Show first 5 errors with code context
        for (size_t i = 0; i < std::min(parser.errors().size(), size_t(5)); i++) {
            auto& err = parser.errors()[i];
            INFO("  Line " << err.position.line << ", Col " << err.position.column << ": " << err.message);

            // Show the problematic line
            std::istringstream stream(source);
            std::string line;
            int lineNum = 0;
            while (std::getline(stream, line)) {
                lineNum++;
                if (lineNum == err.position.line) {
                    INFO("    Code: " << line.substr(0, 120));
                    break;
                }
            }
        }
    }
}

TEST_CASE("Workspace - parse script files") {
    std::string base = "K:/Sword3-products/trunk/client/scripts";

    // Use actual files that exist
    std::vector<std::string> testFiles = {
        base + "/achievement/Achievement.lua",
        base + "/achievement/AchievementAward.lua",
    };

    // Parse each file
    for (auto& path : testFiles) {
        testParseFile(path);
    }
}

TEST_CASE("Parser - real Lua patterns") {
    // Test patterns commonly found in the game code

    SUBCASE("multi-line if condition with or") {
        std::string code = R"(
if a == 1 or
   b == 2 then
    return true
end
)";
        Parser parser(code);
        auto chunk = parser.parse();
        CHECK(parser.errors().empty());
    }

    SUBCASE("table with negative numbers") {
        std::string code = "local t = {-1, -2, -3}";
        Parser parser(code);
        auto chunk = parser.parse();
        CHECK(parser.errors().empty());
    }

    SUBCASE("table with expressions") {
        std::string code = "local t = {x + 1, y - 2, z * 3}";
        Parser parser(code);
        auto chunk = parser.parse();
        CHECK(parser.errors().empty());
    }

    SUBCASE("function call with table") {
        std::string code = "print({1, 2, 3})";
        Parser parser(code);
        auto chunk = parser.parse();
        CHECK(parser.errors().empty());
    }

    SUBCASE("complex if condition") {
        std::string code = R"(
if a == 1 or b == 2 or c == 3 then
    return true
end
)";
        Parser parser(code);
        auto chunk = parser.parse();
        CHECK(parser.errors().empty());
    }

    SUBCASE("nested tables") {
        std::string code = "local t = {{1, 2}, {3, 4}}";
        Parser parser(code);
        auto chunk = parser.parse();
        CHECK(parser.errors().empty());
    }

    SUBCASE("method call with string") {
        std::string code = "obj:method(\"hello\")";
        Parser parser(code);
        auto chunk = parser.parse();
        CHECK(parser.errors().empty());
    }
}
