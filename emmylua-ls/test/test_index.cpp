#include <doctest/doctest.h>
#include "index/SymbolIndex.h"
#include "index/DocumentIndex.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <chrono>

using namespace emmy;

TEST_CASE("DocumentIndex - basic indexing") {
    DocumentIndex index;
    std::string source = "local x = 1\nfunction foo(a, b)\n  return a + b\nend\n";

    index.update("file:///test.lua", source);

    auto symbols = index.allSymbols();
    CHECK(symbols.size() >= 3);  // x, foo, a, b

    // Check local variable
    bool foundX = false;
    for (auto& sym : symbols) {
        if (sym.name == "x") {
            foundX = true;
            CHECK(sym.scope == SymbolScope::Local);
        }
    }
    CHECK(foundX);

    // Check function
    bool foundFoo = false;
    for (auto& sym : symbols) {
        if (sym.name == "foo") {
            foundFoo = true;
            CHECK(sym.isFunction == true);
        }
    }
    CHECK(foundFoo);
}

TEST_CASE("SymbolIndex - cross-file search") {
    SymbolIndex index;

    // Add two documents
    index.updateDocument("file:///a.lua", "function add(x, y) return x + y end");
    index.updateDocument("file:///b.lua", "function sub(x, y) return x - y end");

    // Search for add
    auto defs = index.findDefinition("add", true);
    CHECK(defs.size() >= 1);
    CHECK(defs[0].name == "add");

    // Search for sub
    defs = index.findDefinition("sub", true);
    CHECK(defs.size() >= 1);
    CHECK(defs[0].name == "sub");

    // Case insensitive search
    index.setCaseSensitive(false);
    defs = index.findDefinition("ADD", false);
    CHECK(defs.size() >= 1);
}

TEST_CASE("SymbolIndex - workspace symbol search") {
    SymbolIndex index;

    index.updateDocument("file:///test.lua",
        "function CommonPQ_AddValue(a, b) return a + b end\n"
        "function CommonPQ_RemoveValue(a, b) return a - b end\n"
    );

    // Search for CommonPQ
    auto results = index.searchSymbols("CommonPQ", false);
    CHECK(results.size() == 2);

    // Search for Add
    results = index.searchSymbols("Add", false);
    CHECK(results.size() >= 1);
}

TEST_CASE("DocumentIndex - comments") {
    DocumentIndex index;
    std::string source =
        "-- English comment\n"
        "---@param a number\n"
        "---@return number\n"
        "function calculate(a)\n"
        "  return a * 2\n"
        "end\n";

    index.update("file:///test.lua", source);

    auto symbols = index.allSymbols();
    bool foundFunc = false;
    for (auto& sym : symbols) {
        if (sym.name == "calculate") {
            foundFunc = true;
            CHECK(sym.isFunction == true);
        }
    }
    CHECK(foundFunc);
}

TEST_CASE("DocumentIndex - position calculation") {
    DocumentIndex index;
    std::string source = "local x = 1\nlocal y = 2\n";

    index.update("file:///test.lua", source);

    auto symbols = index.allSymbols();

    // Find 'x' and check its position
    for (auto& sym : symbols) {
        if (sym.name == "x") {
            CHECK(sym.namePos.line == 1);
            CHECK(sym.namePos.column == 7);  // 1-based: after "local "
        }
        if (sym.name == "y") {
            CHECK(sym.namePos.line == 2);
            CHECK(sym.namePos.column == 7);
        }
    }
}

TEST_CASE("DocumentIndex - function parameters") {
    DocumentIndex index;
    std::string source = "function foo(a, b, c)\n  return a + b + c\nend\n";

    index.update("file:///test.lua", source);

    auto symbols = index.allSymbols();

    bool foundA = false, foundB = false, foundC = false;
    for (auto& sym : symbols) {
        if (sym.name == "a") { foundA = true; CHECK(sym.scope == SymbolScope::Parameter); }
        if (sym.name == "b") { foundB = true; CHECK(sym.scope == SymbolScope::Parameter); }
        if (sym.name == "c") { foundC = true; CHECK(sym.scope == SymbolScope::Parameter); }
    }
    CHECK(foundA);
    CHECK(foundB);
    CHECK(foundC);
}

TEST_CASE("DocumentIndex - member function parameters") {
    DocumentIndex index;
    std::string source =
        "function FBlist.CdGoShowBoss(dwMapID, dwBossIndex)\n"
        "    local x = dwMapID + dwBossIndex\n"
        "end\n";

    index.update("file:///test.lua", source);

    auto symbols = index.allSymbols();

    for (auto& sym : symbols) {
        if (sym.name == "dwMapID" || sym.name == "dwBossIndex") {
            CHECK(sym.scope == SymbolScope::Parameter);
        }
    }
}

TEST_CASE("Index - performance benchmark") {
    // Generate a large Lua file for benchmarking
    std::string largeSource;
    for (int i = 0; i < 1000; i++) {
        largeSource += "local var" + std::to_string(i) + " = " + std::to_string(i) + "\n";
    }
    largeSource += "function test(a, b, c)\n  return a + b + c\nend\n";

    SymbolIndex index;

    auto start = std::chrono::high_resolution_clock::now();

    // Index 100 files
    for (int i = 0; i < 100; i++) {
        std::string uri = "file:///test" + std::to_string(i) + ".lua";
        index.updateDocument(uri, largeSource);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    INFO("Indexed 100 files in " << duration << "ms");
    CHECK(duration < 10000);  // Should take less than 10 seconds

    // Test re-indexing (should be faster due to caching)
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 100; i++) {
        std::string uri = "file:///test" + std::to_string(i) + ".lua";
        index.updateDocument(uri, largeSource);
    }
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    INFO("Re-indexed 100 files in " << duration << "ms (cached)");
    CHECK(duration < 1000);  // Should be much faster with caching
}

TEST_CASE("DocumentIndex - continues after parse errors") {
    DocumentIndex index;

    // Source with intentional syntax error
    std::string source =
        "local x = 1\n"
        "local = \n"  // Invalid syntax
        "local y = 2\n"
        "function foo()\n"
        "  return x\n"
        "end\n";

    // Should not crash and should still find valid symbols
    index.update("file:///test.lua", source);

    auto symbols = index.allSymbols();

    // Should find at least x and y (the parser should recover from the error)
    bool foundX = false, foundY = false, foundFoo = false;
    for (auto& sym : symbols) {
        if (sym.name == "x") foundX = true;
        if (sym.name == "y") foundY = true;
        if (sym.name == "foo") foundFoo = true;
    }

    // At least some symbols should be found
    CHECK((foundX || foundY || foundFoo));
}

TEST_CASE("DocumentIndex - funcParams extraction") {
    DocumentIndex index;
    std::string source =
        "function foo(a, b)\n"
        "  return a + b\n"
        "end\n"
        "function bar(x, y, z)\n"
        "  return x\n"
        "end\n";

    index.update("file:///test.lua", source);

    auto fp = index.funcParams();
    CHECK(fp.count("foo") == 1);
    CHECK(fp["foo"].size() == 2);
    CHECK(fp["foo"][0] == "a");
    CHECK(fp["foo"][1] == "b");

    CHECK(fp.count("bar") == 1);
    CHECK(fp["bar"].size() == 3);
    CHECK(fp["bar"][0] == "x");
}

TEST_CASE("SymbolIndex - getFuncParams benchmark") {
    SymbolIndex index;

    // Generate test data: 1000 functions with 3 params each
    std::string source;
    for (int i = 0; i < 500; i++) {
        source += "function func" + std::to_string(i) + "(a, b, c)\n  return a\nend\n";
    }

    // Index 100 documents
    for (int i = 0; i < 100; i++) {
        index.updateDocument("file:///doc" + std::to_string(i) + ".lua", source);
    }

    // Benchmark getFuncParams (new approach)
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 100; i++) {
        auto fp = index.getFuncParams();
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto newMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // Benchmark old approach (iterating allUris + allSymbols)
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 100; i++) {
        std::unordered_map<std::string, std::vector<std::string>> funcParams;
        for (auto& searchUri : index.allUris()) {
            auto* searchDoc = index.getDocument(searchUri);
            if (!searchDoc) continue;
            for (auto& sym : searchDoc->allSymbols()) {
                if (sym.isFunction && funcParams.find(sym.name) == funcParams.end()) {
                    std::vector<std::string> pnames;
                    for (auto& ps : searchDoc->allSymbols()) {
                        if (ps.scope == SymbolScope::Parameter &&
                            ps.range.start.line >= sym.range.start.line &&
                            ps.range.start.line <= sym.range.start.line + 50) {
                            pnames.push_back(ps.name);
                        }
                    }
                    if (!pnames.empty()) funcParams[sym.name] = pnames;
                }
            }
        }
    }
    end = std::chrono::high_resolution_clock::now();
    auto oldMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    spdlog::info("getFuncParams: new={}ms old={}ms speedup={:.1f}x", newMs, oldMs, oldMs > 0 ? (double)oldMs / newMs : 0);
    CHECK(newMs < oldMs);  // New should be faster
}

TEST_CASE("SymbolIndex - re-index on edit after 1000 bytes") {
    SymbolIndex index;

    // Create a source with 2000+ chars, edit at byte 1500
    std::string padding(1500, 'x');
    std::string source1 = padding + "\nfunction foo(a)\n  return a\nend\n";
    std::string source2 = padding + "\nfunction bar(b)\n  return b\nend\n";

    index.updateDocument("file:///test.lua", source1);

    // Verify foo is indexed
    auto defs = index.findDefinition("foo", true);
    CHECK(defs.size() >= 1);

    // Edit (change foo to bar, past the 1000-byte boundary)
    index.updateDocument("file:///test.lua", source2);

    // Verify bar is now indexed and foo is gone
    defs = index.findDefinition("bar", true);
    CHECK(defs.size() >= 1);

    defs = index.findDefinition("foo", true);
    CHECK(defs.empty());  // foo should be gone after re-index
}

TEST_CASE("DocumentIndex - member function name position") {
    DocumentIndex index;
    std::string source = "function DataModel.UpdateNpcList(dwNpcID, bEnter)\n  return\nend\n";

    index.update("file:///test.lua", source);

    auto symbols = index.allSymbols();

    // Find UpdateNpcList
    bool found = false;
    for (auto& sym : symbols) {
        if (sym.name == "UpdateNpcList") {
            found = true;
            // "function DataModel.UpdateNpcList" -> U is at column 20 (1-based)
            CHECK(sym.namePos.column == 20);
            CHECK(sym.namePos.line == 1);
            CHECK(sym.isFunction == true);
        }
    }
    CHECK(found);

    // Parameters should have correct positions
    bool foundNpcID = false, foundEnter = false;
    for (auto& sym : symbols) {
        if (sym.name == "dwNpcID") {
            foundNpcID = true;
            CHECK(sym.scope == SymbolScope::Parameter);
        }
        if (sym.name == "bEnter") {
            foundEnter = true;
            CHECK(sym.scope == SymbolScope::Parameter);
        }
    }
    CHECK(foundNpcID);
    CHECK(foundEnter);
}

TEST_CASE("DocumentIndex - colon method name position") {
    DocumentIndex index;
    std::string source = "function MyClass:DoSomething(a, b)\n  return a\nend\n";

    index.update("file:///test.lua", source);

    auto symbols = index.allSymbols();

    // For a:b() syntax, sym.name should be "DoSomething"
    bool found = false;
    for (auto& sym : symbols) {
        if (sym.name == "DoSomething") {
            found = true;
            CHECK(sym.isFunction == true);
            // "function MyClass:DoSomething" -> D at column 18
            CHECK(sym.namePos.column == 18);
        }
    }
    CHECK(found);
}

TEST_CASE("SymbolIndex - URI normalization") {
    SymbolIndex index;

    // Index with one URI format
    index.updateDocument("file:///K:/test/foo.lua", "function bar() end");

    // Should find with percent-encoded URI
    auto* doc = index.getDocument("file:///k%3A/test/foo.lua");
    CHECK(doc != nullptr);

    // Should find with lowercase drive
    doc = index.getDocument("file:///k:/test/foo.lua");
    CHECK(doc != nullptr);

    // Should find with exact match
    doc = index.getDocument("file:///K:/test/foo.lua");
    CHECK(doc != nullptr);

    // Symbol lookup should work through any URI format
    auto defs = index.findDefinition("bar", true);
    CHECK(defs.size() >= 1);
}
