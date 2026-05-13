# C++ EmmyLua Language Server 实现计划

## Context

当前 VSCode-EmmyLua 扩展依赖 Java 语言服务器（EmmyLua-LS-all.jar），用户每次安装都需要 JDK，且 Java 性能不理想。
目标是用 C++ 重新实现语言服务器，作为零依赖的独立可执行文件替换 Java JAR，无缝对接现有扩展。

## 架构概览

```
VSCode Extension (TypeScript) — 不改动核心逻辑
    │
    │ JSON-RPC 2.0 over stdio
    │
C++ Language Server (emmylua-ls.exe)
    ├── LSP Transport        — stdin/stdout 上的 Content-Length 帧
    ├── Request Dispatcher   — 方法路由（标准 LSP + emmy/* 自定义）
    ├── Lua Parser           — 手写递归下降（5.1/5.2/5.3）
    ├── Annotation Parser    — EmmyLua 注解解析（@class, @param 等）
    ├── Type System          — 类型表示与推断
    ├── Symbol Index         — 跨文件符号索引
    └── Workspace Manager    — 文件发现、监听、增量更新
```

---

## Part 1: C++ 语言服务器实现

### 1.1 项目结构

```
K:\Sword5\Source\Tools\VSCode-EmmyLua\emmylua-ls\
├── CMakeLists.txt
├── src/
│   ├── main.cpp                         # 入口，--stdio / --tcp 模式
│   ├── lsp/
│   │   ├── Transport.h/.cpp             # JSON-RPC 2.0 stdio 传输层
│   │   ├── Dispatcher.h/.cpp            # 方法路由
│   │   ├── LspTypes.h                   # LSP 数据结构（Position, Range, CompletionItem 等）
│   │   ├── ServerCapabilities.h         # 能力声明
│   │   └── handlers/
│   │       ├── InitializeHandler.cpp    # 初始化握手
│   │       ├── CompletionHandler.cpp    # 代码补全
│   │       ├── HoverHandler.cpp         # 悬停信息
│   │       ├── DefinitionHandler.cpp    # 跳转定义
│   │       ├── ReferencesHandler.cpp    # 查找引用
│   │       ├── DocumentSymbolHandler.cpp
│   │       ├── WorkspaceSymbolHandler.cpp
│   │       ├── CodeActionHandler.cpp
│   │       ├── RenameHandler.cpp
│   │       ├── SignatureHelpHandler.cpp
│   │       ├── DocumentHighlightHandler.cpp
│   │       ├── FoldingRangeHandler.cpp
│   │       ├── FormattingHandler.cpp
│   │       ├── CodeLensHandler.cpp
│   │       ├── DiagnosticsHandler.cpp   # publishDiagnostics 推送
│   │       └── EmmyCustomHandler.cpp    # emmy/* 自定义请求
│   ├── parser/
│   │   ├── Token.h                      # Token 枚举 + 源位置
│   │   ├── Lexer.h/.cpp                 # Lua 词法分析
│   │   ├── AstNode.h                    # AST 节点定义
│   │   ├── Parser.h/.cpp                # 递归下降语法分析（含错误恢复）
│   │   ├── AnnotationToken.h            # 注解 Token
│   │   ├── AnnotationLexer.h/.cpp       # 注解词法分析
│   │   └── AnnotationParser.h/.cpp      # 注解语法分析
│   ├── types/
│   │   ├── Type.h                       # 类型系统（Class, Union, Array, Table, Function, Literal, Generic）
│   │   ├── TypeResolver.h/.cpp          # 类型推断与解析
│   │   └── TypeChecker.h/.cpp           # 类型检查（受 emmylua.inspections.* 控制）
│   ├── index/
│   │   ├── SymbolIndex.h/.cpp           # 全局符号索引
│   │   ├── DocumentIndex.h/.cpp         # 单文件符号/AST 缓存
│   │   ├── WorkspaceManager.h/.cpp      # 工作区文件发现/监听/增量更新
│   │   └── CompletionEngine.h/.cpp      # 补全引擎
│   ├── config/
│   │   ├── Config.h/.cpp                # emmylua.* 设置
│   │   └── EmmyConfigParser.h/.cpp      # emmy.config.json 解析
│   └── util/
│       ├── Uri.h/.cpp                   # file:// URI ↔ 路径转换
│       ├── Json.h                       # nlohmann::json 便捷宏
│       ├── Logger.h/.cpp                # 日志（仅写 stderr）
│       └── Platform.h/.cpp              # 跨平台文件 I/O
├── test/
│   ├── test_parser.cpp
│   ├── test_annotation.cpp
│   ├── test_index.cpp
│   └── test_lsp_handlers.cpp
└── third_party/                         # CMake FetchContent 拉取
    ├── nlohmann_json/
    ├── spdlog/
    └── doctest/
```

### 1.2 构建系统

- **CMake 3.20+**, C++20
- **依赖**（全部轻量/头文件库）：
  - `nlohmann-json` — JSON-RPC 序列化（头文件库，零体积增加）
  - `spdlog` — 日志（~200KB，仅写 stderr，绝不能写 stdout）
  - `doctest` — 单元测试
- **不使用**现有的 C++ LSP 框架库（过时/不完整），LSP 传输层 ~300 行代码自己写更可控
- **不使用** tree-sitter（Lua 语法太简单，手写解析器更易维护且无额外依赖）

### 1.3 JSON-RPC 2.0 传输层

**关键约束**：
- Windows 上必须 `_setmode(_fileno(stdout), _O_BINARY)`，否则二进制模式问题会导致崩溃
- 消息格式：`Content-Length: N\r\n\r\n{N bytes UTF-8 JSON}`
- 独立 reader 线程阻塞读 stdin，解析后推入线程安全队列
- 主线程从队列消费并分发
- 所有写 stdout 操作用 mutex 保护

### 1.4 Lua 解析器

手写递归下降，特点：
- 支持 Lua 5.1/5.2/5.3 语法
- 每个 AST 节点携带源码位置（line/column/offset）供 LSP 使用
- **错误恢复**：遇到意外 token 时跳到下一个语句边界（`;`, `end`, `return` 等）继续解析，不崩溃
- AST 节点类型：Block, LocalStatement, AssignStatement, FunctionStatement, IfStatement, WhileStatement, RepeatStatement, ForStatement, ForInStatement, ReturnStatement, BreakStatement, CallExpression, IndexExpression, MemberExpression, TableConstructor, BinaryExpression, UnaryExpression, LiteralExpression, VarargExpression, AnonymousFunction 等

### 1.5 EmmyLua 注解解析器

独立于 Lua 解析器。处理 `---` 开头的注释块：

**支持的标签**：`@class`, `@field`, `@param`, `@return`, `@type`, `@overload`, `@generic`, `@vararg`, `@enum`, `@interface`, `@alias`, `@deprecated`, `@public`, `@protected`, `@private`, `@package`, `@see`

**类型表达式语法**：
```
type_expr    : union_type
union_type   : simple_type ('|' simple_type)*
simple_type  : literal | array_type | table_type | func_type | name | generic_app
literal      : '"' chars '"' | NUMBER | 'true' | 'false' | 'nil'
array_type   : name '[]'
table_type   : 'table' '<' type_expr ',' type_expr '>'
func_type    : 'fun' '(' params ')' (':' type_expr)?
generic_app  : name '<' type_expr (',' type_expr)* '>'
```

**复杂示例**（来自 `res/std/table.lua`）：
```lua
---@overload fun<V>(list:table<number, V> | V[]):V
---@generic V
---@param list table<number, V>
---@param comp fun(a:V, b:V):boolean
---@return number
function table.sort(list, comp) end
```

### 1.6 符号索引

两级索引：
- **DocumentIndex**（单文件）：缓存 AST + 注解类型，映射变量名→定义（含作用域：local/global/upvalue/param），追踪未使用变量
- **SymbolIndex**（全局）：映射类型名→ClassInfo（字段/方法/父类），映射符号名→定义列表，文件 URI→DocumentIndex

**增量更新策略**：文件变化时只重新解析该文件，更新全局索引中该文件的条目，不重扫整个工作区。

### 1.7 自定义 LSP 请求实现

| 方法 | 方向 | 实现位置 |
|------|------|----------|
| `emmy/annotator` | C→S | `EmmyCustomHandler.cpp` — 作用域分析返回 Param/Global/DocType/Upvalue/NotUse 着色 |
| `emmy/updateConfig` | C→S | `EmmyConfigParser.cpp` — 重新解析变更的 emmy.config.json |
| `emmy/reportAPI` | C→S | 存入全局符号索引 |
| `emmy/view_syntax_tree` | C→S | 序列化 AST 为 JSON |
| `emmy/view_psi_select` | C→S | 返回指定位置的 AST 节点信息 |
| `emmy/progressReport` | S→C | `WorkspaceManager.cpp` — 索引时发送 `{text, percent}` |

### 1.8 服务器能力声明

初始化时返回 `ServerCapabilities`：
```
completionProvider.triggerCharacters: [":", ".", "(", ",", "<"]
hoverProvider: true
definitionProvider: true
referencesProvider: true
documentSymbolProvider: true
workspaceSymbolProvider: true
codeActionProvider: true
renameProvider: true
signatureHelpProvider.triggerCharacters: ["(", ",", ")"]
documentHighlightProvider: true
foldingRangeProvider: true
documentFormattingProvider: true
documentRangeFormattingProvider: true
codeLensProvider: { resolveProvider: false }
```

### 1.9 CLI 参数

```
emmylua-ls --stdio           # 生产模式（默认）
emmylua-ls --tcp <port>      # 开发调试模式
```

### 1.10 二进制目标

- Windows: 静态链接 CRT（`/MT`），目标 < 10MB
- Linux: 静态链接 libstdc++/libgcc
- macOS: 支持 x64 + arm64

---

## Part 2: 扩展侧修改

### 2.1 修改文件清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/extension.ts` | 修改 | 替换 Java 启动命令为 C++ 二进制，删除 Java 验证逻辑 |
| `src/findJava.ts` | 删除 | 不再需要 Java 发现逻辑 |
| `build/config.js` | 修改 | 替换 Java JAR URL 为 C++ 二进制下载 URL |
| `build/prepare.js` | 修改 | 下载平台特定的 C++ 二进制而非 JAR |
| `package.json` | 修改 | 将 `emmylua.java.home` 改为 `emmylua.server.path`（可选自定义路径） |
| `package.nls.json` | 修改 | 更新对应描述 |
| `package.nls.zh-cn.json` | 修改 | 更新对应描述 |
| `.vscodeignore` | 修改 | 确保 `server/` 目录不被排除，使 C++ 二进制打包进 VSIX |

### 2.2 extension.ts 核心改动

**删除**（~40 行）：
- `findJava` import
- `javaExecutablePath` 变量
- `validateJava()` 函数（Java 版本检查）
- `onDidChangeConfiguration` 中的 Java 路径变更检测

**替换** `doStartServer()` 中的 serverOptions（第 218-223 行）：

```typescript
// 原来：
const cp = path.resolve(context.extensionPath, "server", "*");
const exePath = javaExecutablePath || "java";
serverOptions = {
    command: exePath,
    args: ["-cp", cp, "com.tang.vscode.MainKt", "-XX:+UseG1GC", "-XX:+UseStringDeduplication"]
};

// 替换为：
const serverBin = process.platform === 'win32'
    ? 'emmylua-ls.exe'
    : 'emmylua-ls';
const serverPath = path.resolve(context.extensionPath, "server", serverBin);
serverOptions = {
    command: serverPath,
    args: ["--stdio"]
};
```

### 2.3 build/prepare.js 改动

下载平台特定的 C++ 二进制替代 JAR：

```javascript
// 原来：
downloadTo(`${config.lanServerUrl}/${config.lanServerVersion}/EmmyLua-LS-all.jar`, 'temp/EmmyLua-LS-all.jar'),
// ...
await fc('temp/EmmyLua-LS-all.jar', 'server/EmmyLua-LS-all.jar', { mkdirp: true });

// 替换为（按平台下载）：
downloadTo(`${config.serverUrl}/${config.serverVersion}/emmylua-ls-win32-x64.zip`, 'temp/win32.zip'),
downloadTo(`${config.serverUrl}/${config.serverVersion}/emmylua-ls-linux-x64.zip`, 'temp/linux.zip'),
downloadTo(`${config.serverUrl}/${config.serverVersion}/emmylua-ls-darwin-x64.zip`, 'temp/darwin.zip'),
// ...
await decompress('temp/win32.zip', 'server/');
await decompress('temp/linux.zip', 'server/');
```

### 2.4 保留不变的部分

- **调试器完全不动** — `src/debugger/` 目录的所有文件保持原样，调试器通过 TCP socket 直接与 Lua 进程通信，不经过语言服务器
- 所有 LSP 标准请求的处理逻辑由 vscode-languageclient 自动处理
- PSI Viewer（`src/web/psiViewer.ts`）不需要改动

---

## 实施阶段

### Phase 1: 骨架 + 传输层（目标：可握手）
- CMake 项目搭建
- Transport（JSON-RPC 2.0 stdio）
- Dispatcher（方法路由）
- LspTypes（LSP 数据结构定义）
- InitializeHandler（最小实现，仅返回 ServerCapabilities）
- **里程碑**：服务器启动，完成 LSP 握手，扩展可连接

### Phase 2: Lua 解析器（目标：可解析）
- Lexer（完整 Lua 5.1/5.2/5.3 分词）
- AstNode（所有 AST 节点）
- Parser（递归下降 + 错误恢复）
- 用 `res/std/*.lua` 做测试
- **里程碑**：能解析任意 Lua 文件为 AST

### Phase 3: 注解解析器（目标：可提取类型）
- AnnotationLexer + AnnotationParser（所有标签 + 类型表达式）
- 注解关联到 AST 节点
- **里程碑**：`res/std/*.lua` 的类型信息完整提取

### Phase 4: 符号索引 + 工作区管理（目标：可索引）
- DocumentIndex + SymbolIndex
- WorkspaceManager（文件发现/监听/增量更新）
- Config + EmmyConfigParser
- `emmy/progressReport` 通知
- **里程碑**：索引整个工作区，响应文件变更

### Phase 5: 核心 LSP 功能（目标：可用）
- documentSymbol, workspaceSymbol, hover, definition, references
- completion, signatureHelp
- `emmy/annotator`（作用域分析着色）
- **里程碑**：核心 IDE 功能可用

### Phase 6: 扩展功能（目标：功能对等）
- codeAction, rename, documentHighlight, foldingRange, formatting, codeLens
- diagnostics（publishDiagnostics 推送）
- `emmy/view_syntax_tree`, `emmy/view_psi_select`
- **里程碑**：与 Java 服务器功能对等

### Phase 7: 集成发布（目标：可替换）
- 修改扩展 TypeScript 代码（`src/extension.ts` 等）
- 更新构建脚本（`build/prepare.js`, `build/config.js`）
- CI/CD 跨平台构建（GitHub Actions）
- 性能测试（用 JX3/Sword5 实际 Lua 代码库）
- **里程碑**：替代 JAR 的完整 VSIX 包

---

## 验证方式

1. **单元测试**：doctest 测试解析器、注解解析器、类型系统、索引
2. **集成测试**：用 `--tcp` 模式启动服务器，手动测试 VSCode 连接
3. **功能测试**：
   - 打开 `res/std/*.lua` 文件验证补全、悬停、跳转定义
   - 在工作区中打开游戏 Lua 文件，验证着色、诊断、引用查找
   - 修改文件验证增量更新
4. **性能测试**：索引 JX3 全部 Lua 文件，对比 Java 服务器的索引时间
5. **打包测试**：`vsce package` 生成 VSIX，安装到干净的 VSCode 验证零依赖启动
