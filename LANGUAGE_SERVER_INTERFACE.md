# EmmyLua Language Server 对接接口文档

本文档详细描述 VSCode-EmmyLua 扩展（TypeScript）与 EmmyLua Language Server（Java）之间的所有接口，
用于指导用其他语言（C++/Python/TypeScript）重新实现语言服务器。

---

## 1. 整体架构

```
VSCode Extension (TypeScript)
    |
    |--- LSP (Language Server Protocol) ---→ Language Server (Java, EmmyLua-LS-all.jar)
    |       标准 LSP + 自定义请求/通知              |
    |                                              |
    |--- Debug Adapter Protocol ---→ Debug Adapter (TypeScript) --- TCP Socket ---→ Lua 进程
```

语言服务器是一个**独立进程**，通过 LSP 协议与 VSCode 扩展通信。
调试器是完全独立的模块，通过 TCP socket 直接与 Lua 进程中的 `emmy_core` 库通信，**不经过语言服务器**。

---

## 2. 语言服务器启动方式

### 2.1 生产模式（stdio）

**源码位置**: `src/extension.ts` 第 217-224 行

```typescript
const cp = path.resolve(context.extensionPath, "server", "*");
const exePath = javaExecutablePath || "java";
serverOptions = {
    command: exePath,
    args: ["-cp", cp, "com.tang.vscode.MainKt", "-XX:+UseG1GC", "-XX:+UseStringDeduplication"]
};
```

- 启动命令: `java -cp server/* com.tang.vscode.MainKt`
- 通信方式: **stdio**（标准输入输出）
- 入口类: `com.tang.vscode.MainKt`（Kotlin 编写）
- JVM 参数: `-XX:+UseG1GC -XX:+UseStringDeduplication`

### 2.2 开发模式（TCP）

**源码位置**: `src/extension.ts` 第 200-216 行

```typescript
const connectionInfo = { port: 5007 };
serverOptions = () => {
    let socket = net.connect(connectionInfo);
    let result: StreamInfo = {
        writer: socket,
        reader: socket as NodeJS.ReadableStream
    };
    return Promise.resolve(result);
};
```

- 触发条件: 环境变量 `EMMY_DEV=true`
- 通信方式: **TCP socket** 连接到 `localhost:5007`
- 用途: 在 IDE 中调试语言服务器本身

### 2.3 替换语言服务器的接入点

新语言服务器只需满足以下条件之一即可接入：
- **stdio 模式**: 从 stdin 读取 LSP 请求，向 stdout 写入 LSP 响应
- **TCP 模式**: 监听某个端口，接受 TCP 连接，通过 socket 交换 LSP 消息

修改 `src/extension.ts:doStartServer()` 中的 `serverOptions` 即可：

```typescript
// 示例: 替换为 Python 实现
serverOptions = {
    command: "python3",
    args: ["-m", "my_lua_lsp_server"]
};
// 或者替换为 C++ 实现
serverOptions = {
    command: "/path/to/lua-lsp-server",
    args: ["--stdio"]
};
```

---

## 3. LSP 初始化参数（InitializationOptions）

**源码位置**: `src/extension.ts` 第 191-196 行

扩展在 LSP `initialize` 请求中附带以下参数：

```typescript
initializationOptions: {
    stdFolder: "file:///path/to/extension/res/std",  // Lua 标准库类型定义目录
    apiFolders: [],                                    // API 文档文件夹（当前为空）
    client: 'vsc',                                     // 客户端标识
    configFiles: [                                     // emmy.config.json 文件列表
        {
            uri: "file:///path/to/emmy.config.json",
            workspace: "file:///path/to/workspace"
        }
    ]
}
```

### 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `stdFolder` | string (URI) | 指向 `res/std/` 目录，包含 Lua 标准库的类型定义文件 |
| `apiFolders` | string[] (URI) | 外部 API 文件夹，当前未使用 |
| `client` | string | 固定为 `'vsc'`，标识客户端类型 |
| `configFiles` | IEmmyConfigSource[] | 工作区中的 `emmy.config.json` 文件 |

**`res/std/` 目录内容**（语言服务器需要解析这些文件来获取标准库的类型信息）：
- `builtin.lua` - 内置类型（nil, boolean, number, string, table, function, any, void, self）
- `string.lua` - string 库
- `table.lua` - table 库
- `math.lua` - math 库
- `io.lua` - io 库
- `os.lua` - os 库
- `debug.lua` - debug 库
- `coroutine.lua` - coroutine 库
- `package.lua` - package 库
- `utf8.lua` - utf8 库

这些文件使用 EmmyLua 注解语法（`---@class`, `---@param`, `---@return` 等），
语言服务器需要能解析这些注解。

---

## 4. 同步配置（Configuration Synchronization）

**源码位置**: `src/extension.ts` 第 183-190 行

```typescript
synchronize: {
    configurationSection: ["emmylua", "files.associations"],
    fileEvents: [
        vscode.workspace.createFileSystemWatcher("**/*.lua")
    ]
}
```

### 4.1 同步的 VSCode 设置（emmylua.*）

语言服务器会收到 `workspace/didChangeConfiguration` 通知，包含以下设置：

**源码位置**: `package.json` 第 281-444 行

| 设置键 | 类型 | 默认值 | 用途 |
|--------|------|--------|------|
| `emmylua.source.roots` | string[] | `[]` | 额外的源码索引目录 |
| `emmylua.completion.caseSensitive` | boolean | `false` | 补全是否区分大小写 |
| `emmylua.codeLens` | boolean | `false` | 是否启用 CodeLens |
| `emmylua.constructorNames` | string | `"new;get"` | 构造函数名（分号分隔） |
| `emmylua.requireLikeFunctions` | string | `"require"` | 类似 require 的函数名 |
| `emmylua.typecheck.anyTypeCanAssignToAnyDefineType` | boolean | `true` | any 类型可赋值给任意类型 |
| `emmylua.typecheck.defineAnyTypeCanBeAssignedByAnyVariable` | boolean | `true` | 定义为 any 的类型可接收任意变量 |
| `emmylua.typecheck.defineTypeCanReceiveNilType` | boolean | `false` | 定义类型可接收 nil |
| `emmylua.inspections.undeclaredVariable` | enum | `"None"` | 未声明变量检查级别 |
| `emmylua.inspections.fieldValidation` | enum | `"None"` | 字段校验级别 |
| `emmylua.inspections.parameterValidation` | enum | `"None"` | 参数校验级别 |
| `emmylua.inspections.assignValidation` | enum | `"None"` | 赋值校验级别 |
| `emmylua.inspections.deprecated` | boolean | `false` | 废弃项检查 |
| `emmylua.hint.paramHint` | boolean | `true` | 参数提示 |
| `emmylua.hint.localHint` | boolean | `false` | 局部变量提示 |
| `emmylua.hint.varargHint` | boolean | `true` | 可变参数提示 |
| `emmylua.hint.overrideHint` | boolean | `false` | 覆写提示 |
| `files.associations` | object | `{}` | 文件关联（如 `*.lua.txt` → lua） |

### 4.2 文件监听

语言服务器会收到所有 `**/*.lua` 文件的创建/修改/删除事件（`workspace/didChangeWatchedFiles`）。

---

## 5. 自定义 LSP 请求（Requests）

以下是扩展向语言服务器发送的**非标准 LSP 请求**（以 `emmy/` 为前缀）。

### 5.1 `emmy/annotator` - 代码着色请求

**源码位置**: `src/annotator.ts` 第 72 行

```typescript
// 扩展 → 语言服务器
client.sendRequest<IAnnotator[]>("emmy/annotator", { uri: "file:///path/to/file.lua" });
```

**请求参数**:
```typescript
interface AnnotatorParams {
    uri: string;  // 文档 URI
}
```

**响应**: `IAnnotator[]`

```typescript
enum AnnotatorType {
    Param = 0,      // 参数着色
    Global = 1,     // 全局变量着色
    DocType = 2,    // 文档类型着色
    Upvalue = 3,    // 上值着色
    NotUse = 4,     // 未使用变量着色
    ParamHint = 5,  // 参数内联提示
    LocalHint = 6,  // 局部变量内联提示
    OverrideHint = 7 // 覆写内联提示
}

interface RenderRange {
    range: vscode.Range;  // { start: {line, character}, end: {line, character} }
    hint: string;          // 提示文本（用于 Hint 类型）
}

interface IAnnotator {
    uri: string;
    ranges: RenderRange[];
    type: AnnotatorType;
}
```

**触发时机**: 每次文档内容变化（防抖 150ms）或切换编辑器标签页。

### 5.2 `emmy/updateConfig` - 配置文件更新通知

**源码位置**: `src/extension.ts` 第 265-270 行

```typescript
client.sendRequest('emmy/updateConfig', {
    type: UpdateType.Created | UpdateType.Changed | UpdateType.Deleted,
    source: {
        uri: "file:///path/to/emmy.config.json",
        workspace: "file:///path/to/workspace"
    }
});
```

**触发时机**: 工作区中 `emmy.config.json` 文件被创建/修改/删除时。

### 5.3 `emmy/reportAPI` - API 文档上报

**源码位置**: `src/extension.ts` 第 48-51 行

```typescript
// 返回值暴露给外部调用
return {
    reportAPIDoc: (classDoc: any) => {
        luaContext?.client?.sendRequest("emmy/reportAPI", classDoc);
    }
}
```

这是一个供外部工具调用的接口，用于向语言服务器上报 API 类型文档。
`classDoc` 的具体结构取决于外部工具，语言服务器需要能接收任意 JSON 对象。

### 5.4 `emmy/view_syntax_tree` - PSI 语法树查看

**源码位置**: `src/web/psiViewer.ts` 第 133 行

```typescript
client?.sendRequest<{ data: any }>("emmy/view_syntax_tree", { uri: "file:///path/to/file.lua" });
```

**请求参数**: `{ uri: string }`

**响应**: `{ data: any }` — 语法树的 JSON 表示，发送到 PSI Viewer webview 进行可视化展示。

### 5.5 `emmy/view_psi_select` - PSI 节点选择

**源码位置**: `src/web/psiViewer.ts` 第 146 行

```typescript
client?.sendRequest<{ data: any }>("emmy/view_psi_select", {
    uri: "file:///path/to/file.lua",
    position: { line: number, character: number }
});
```

**请求参数**: `{ uri: string, position: Position }`

**响应**: `{ data: any }` — 指定位置的 PSI 节点信息。

---

## 6. 自定义 LSP 通知（Notifications）

### 6.1 `emmy/progressReport` - 索引进度通知

**源码位置**: `src/extension.ts` 第 229-237 行

```typescript
// 语言服务器 → 扩展
luaContext.client?.onNotification("emmy/progressReport", (d: IProgressReport) => {
    progressBar.text = d.text;
    if (d.percent >= 1) {
        setTimeout(() => progressBar.hide(), 3000);
    }
});
```

**通知参数**:
```typescript
interface IProgressReport {
    text: string;    // 进度文本，如 "Indexing: 45/100"
    percent: number; // 进度百分比，0.0 ~ 1.0，>=1 时进度条自动隐藏
}
```

语言服务器在索引工作区文件时应发送此通知，用于在 VSCode 状态栏显示进度。

---

## 7. 标准 LSP 功能要求

语言服务器需要实现以下标准 LSP 功能（具体能力取决于 `ServerCapabilities`）：

| LSP 功能 | 说明 |
|----------|------|
| `textDocument/completion` | 代码补全 |
| `textDocument/hover` | 悬停信息 |
| `textDocument/definition` | 跳转定义 |
| `textDocument/references` | 查找引用 |
| `textDocument/documentSymbol` | 文档符号 |
| `textDocument/workspaceSymbol` | 工作区符号 |
| `textDocument/codeAction` | 代码操作 |
| `textDocument/rename` | 重命名 |
| `textDocument/signatureHelp` | 函数签名帮助 |
| `textDocument/documentHighlight` | 文档高亮 |
| `textDocument/foldingRange` | 折叠范围 |
| `textDocument/color` / `textDocument/colorPresentation` | 颜色 |
| `textDocument/formatting` / `textDocument/rangeFormatting` | 格式化 |
| `textDocument/inlayHint` | 内联提示（由 `emmy/annotator` 补充） |
| `textDocument/codeLens` | CodeLens（受 `emmylua.codeLens` 设置控制） |
| `textDocument/diagnostics` | 诊断信息（publishDiagnostics） |

### EmmyLua 注解语法

语言服务器必须解析 EmmyLua 注解语法，这是类型系统的核心：

```lua
---@class MyClass
---@field name string
---@field age number

---@param x number
---@param y number
---@return number
function add(x, y) return x + y end

---@type MyClass
local obj = {}

---@overload fun(x: number): void
```

常用注解标签：`@class`, `@field`, `@param`, `@return`, `@type`, `@alias`, `@enum`,
`@overload`, `@deprecated`, `@generic`, `@see`, `@package`, `@protected`, `@public` 等。

---

## 8. emmy.config.json 配置文件格式

**Schema 位置**: `syntaxes/emmy.config.schema.json`

```json
{
    "lua.version": "lua5.1 | lua5.2 | lua5.3",
    "source": [
        {
            "dir": "src",
            "exclude": ["build", "third_party"]
        }
    ],
    "editor": {
        "completionCaseSensitive": false
    }
}
```

语言服务器需要监听此文件的变化（通过 `emmy/updateConfig` 请求），并据此调整：
- Lua 版本的语法和标准库行为
- 源码索引目录和排除规则
- 编辑器行为（如补全大小写敏感性）

---

## 9. 调试器（独立于语言服务器）

调试器**不经过语言服务器**，是完全独立的模块。TypeScript 扩展中的调试适配器通过 TCP socket 直接与 Lua 进程中的 `emmy_core` 动态库通信。

如果要重新实现语言服务器，**调试器部分不需要改动**。

如需了解调试协议细节，参见：
- `src/debugger/EmmyDebugProto.ts` - 调试协议定义
- `src/debugger/EmmyDebugSession.ts` - 调试会话实现
- `debugger/emmy/emmyHelper.lua` - Lua 端辅助代码

---

## 10. 替换语言服务器的完整步骤

### 方案 A：仅修改启动命令（最简方案）

修改 `src/extension.ts:doStartServer()` 中的 `serverOptions`：

```typescript
// 替换为新实现
serverOptions = {
    command: "your-server-executable",
    args: ["--stdio"]
};
```

新语言服务器只需：
1. 从 stdin 读取 JSON-RPC 消息（LSP 格式）
2. 向 stdout 写入 JSON-RPC 响应
3. 实现上述所有自定义请求（`emmy/*`）
4. 发送 `emmy/progressReport` 通知

### 方案 B：完全替换服务器目录

1. 删除 `server/EmmyLua-LS-all.jar`
2. 将新语言服务器的可执行文件放入 `server/` 目录
3. 修改启动命令
4. 如果不需要 Java，删除 `findJava.ts` 中的 Java 查找逻辑，简化 `validateJava()`

### 方案 C：TypeScript 实现的语言服务器

如果用 TypeScript 实现 LSP Server：
- 可以直接使用 `vscode-languageserver` npm 包
- 可以与扩展打包在一起，无需独立进程
- 但仍需实现所有 `emmy/*` 自定义请求

---

## 11. 关键源码文件索引

| 文件 | 职责 |
|------|------|
| `src/extension.ts` | 扩展入口，语言服务器启动/停止，注册所有请求 |
| `src/annotator.ts` | 发送 `emmy/annotator` 请求，处理着色响应 |
| `src/notifications.ts` | 定义 `AnnotatorType`, `IAnnotator`, `IProgressReport` 等接口 |
| `src/emmyConfigWatcher.ts` | 监视 `emmy.config.json` 变化，发送 `emmy/updateConfig` |
| `src/findJava.ts` | 查找 Java 可执行文件路径 |
| `src/luaContext.ts` | 扩展上下文容器 |
| `src/web/psiViewer.ts` | PSI 查看器，发送 `emmy/view_syntax_tree` 和 `emmy/view_psi_select` |
| `src/languageConfiguration.ts` | Lua 语言配置（括号匹配、注释、折叠） |
| `res/std/*.lua` | Lua 标准库类型定义（需要语言服务器解析） |
| `syntaxes/emmy.config.schema.json` | `emmy.config.json` 的 JSON Schema |
| `package.json` | 扩展清单，包含所有设置定义和命令注册 |
