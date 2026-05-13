#include <doctest/doctest.h>
#include "parser/Parser.h"
#include <fstream>
#include <sstream>
#include <chrono>

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace emmy;

#ifdef _WIN32
static std::string ansiToUtf8(const std::string& ansi, unsigned int codePage) {
    if (ansi.empty()) return ansi;
    int wideLen = MultiByteToWideChar(codePage, 0, ansi.c_str(), -1, nullptr, 0);
    if (wideLen <= 0) return ansi;
    std::wstring wide(wideLen, 0);
    MultiByteToWideChar(codePage, 0, ansi.c_str(), -1, &wide[0], wideLen);
    wide.pop_back();
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return ansi;
    std::string utf8(utf8Len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], utf8Len, nullptr, nullptr);
    return utf8;
}
#endif

TEST_CASE("Parse BaseLua.lua with encoding") {
    std::string path = "H:/sword3-products_Classic/trunk/client/interface/MY/MY_!Base/src/lib/BaseLua.lua";
    std::ifstream file(path, std::ios::binary);
    CHECK(file.is_open());

    std::stringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    CHECK(source.size() > 0);
    INFO("File size: " << source.size());

    // Convert GBK to UTF-8
#ifdef _WIN32
    std::string utf8Source = ansiToUtf8(source, 936);
#else
    std::string utf8Source = source;
#endif

    INFO("Converted size: " << utf8Source.size());
    CHECK(utf8Source.size() > 0);

    auto start = std::chrono::steady_clock::now();
    Parser parser(utf8Source);
    auto chunk = parser.parse();
    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    INFO("Parse time: " << ms << "ms");
    INFO("Parse errors: " << parser.errors().size());

    CHECK(ms < 5000);  // Should not take more than 5 seconds
}
