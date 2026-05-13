# EmmyLua Language Server (C++)

C++ 实现的 EmmyLua 语言服务器，替代原有的 Java 实现。

## 依赖

使用 CMake FetchContent 自动下载以下依赖（首次构建需要联网）：

- [nlohmann-json](https://github.com/nlohmann/json) v3.11.3 - JSON 解析
- [spdlog](https://github.com/gabime/spdlog) v1.14.1 - 日志
- [doctest](https://github.com/doctest/doctest) v2.4.11 - 单元测试

依赖库会自动下载到 `build/_deps/` 目录，无需手动管理。

## 构建

### Windows (Visual Studio)

```bash
cd emmylua-ls
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019" -A x64
cmake --build . --config Release
```

### Linux / macOS

```bash
cd emmylua-ls
mkdir build && cd build
cmake ..
make -j
```

### 使用 build.bat (Windows)

```bash
cd emmylua-ls
build.bat
```

或者使用根目录的 build.bat：

```bash
cd VSCode-EmmyLua
build.bat
```

## 测试

```bash
cd emmylua-ls/build
ctest -C Release
```

## 运行

```bash
# stdio 模式（默认，用于 VSCode 扩展）
./emmylua-ls --stdio

# TCP 模式（开发调试）
./emmylua-ls --tcp 5007
```

## 架构

```
src/
├── main.cpp              # 入口点
├── lsp/                  # LSP 协议层
│   ├── Transport.h/cpp   # JSON-RPC 2.0 stdio 传输
│   ├── Dispatcher.h/cpp  # 方法路由
│   └── handlers/         # LSP 请求处理器
├── parser/               # Lua 解析器
│   ├── Lexer.h/cpp       # 词法分析
│   ├── Parser.h/cpp      # 语法分析
│   ├── AstNode.h         # AST 节点
│   └── AnnotationParser  # EmmyLua 注解解析
├── index/                # 符号索引
│   ├── DocumentIndex     # 单文件索引
│   ├── SymbolIndex       # 工作区索引
│   └── WorkspaceManager  # 工作区管理
└── config/               # 配置管理
```
