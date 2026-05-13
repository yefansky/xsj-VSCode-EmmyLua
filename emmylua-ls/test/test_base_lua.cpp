#include <doctest/doctest.h>
#include "parser/Parser.h"
#include "parser/Lexer.h"
#include <fstream>
#include <sstream>

using namespace emmy;

TEST_CASE("Parse BaseLua.lua") {
    std::string path = "H:/sword3-products_Classic/trunk/client/interface/MY/MY_!Base/src/lib/BaseLua.lua";
    std::ifstream file(path);
    if (!file.is_open()) {
        INFO("File not found: " << path);
        return;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    INFO("File size: " << source.size() << " bytes");

    Parser parser(source);
    auto chunk = parser.parse();

    INFO("Parse errors: " << parser.errors().size());
    for (auto& err : parser.errors()) {
        INFO("  Line " << err.position.line << ": " << err.message);
    }
}
