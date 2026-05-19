![logo](/res/logo.png)
# jx3-EmmyLua (C++ Native)

**高性能 C++ Lua 语言服务器**，专为 VS Code 超大工程优化。  
拥有全新的图形化调试界面、引用查找和定义跳转等增强功能。  
本项目基于 [EmmyLua](https://github.com/EmmyLua/IntelliJ-EmmyLua) 早期版本深度重构。

- 将语言解析由 Java 改为 C++ 实现，移除 Java 依赖
- 优化了索引建立效率，对超大 Lua 工程能够高效加载
- 实现了引用查找、定义跳转等原版缺失功能
- 优化了函数参数提示方式，去除多余的提示信息
- 优化了调试拉起流程，有对应 UI
- 调试时支持端口选择器，自动记录最近使用的端口
- 新增输出通道，方便排查问题

[更新日志](CHANGELOG.md)

[CHANGELOG](CHANGELOG_EN.md)

FAQ:

Q: 为什么附加调试没有作用？

A: 附加调试会试图获取进程内的lua符号，判断当前的lua版本，用于调试计算等。所以要求进程本身导出lua符号

Q: Emmy New Debug为什么连不上目标

A: 通常是由于插入代码require执行失败，或者`require("emmy_core")`返回true，这表明可执行文件没有导出lua符号

Q: 项目中有很多大文件并不想参与解析如何排除？

A: 在项目根目录创建`emmy.config.json`然后如下填写:
```json
{
    "source": [
        {
            "dir": "./",
            "exclude": [
                "csv/**.lua"
            ]
        }
    ]
}
```
## 许可证

本项目基于 MIT 许可证开源。详情请参阅 [LICENSE](LICENSE) 文件。

![logo](/res/logo.png)
# jx3-EmmyLua (C++ Native)

**A high-performance C++ Lua language server** optimized for large-scale VS Code projects.  
Features a new graphical debugging UI, reference search, go-to-definition, and more.  
This project is a deep re-architecture based on an early version of [EmmyLua](https://github.com/EmmyLua/IntelliJ-EmmyLua).

- Changed language parsing from Java to C++, removing Java dependency
- Optimized index building for efficient loading of very large Lua projects
- Implemented previously missing features such as reference search and go-to-definition
- Improved function parameter hints by removing redundant information
- Streamlined the debug launch process with a dedicated UI
- Added a port selector during debugging that remembers the most recently used port
- Added an output channel for easier troubleshooting

[Changelog](CHANGELOG.md)

[CHANGELOG (EN)](CHANGELOG_EN.md)

**FAQ**

**Q: Why doesn't attach debugging work?**  
A: Attach debugging tries to retrieve Lua symbols from the process to determine the Lua version for debugging calculations, etc. Therefore, the target process must export Lua symbols.

**Q: Why can't Emmy New Debug connect to the target?**  
A: Usually because the injected `require` call fails, or `require("emmy_core")` returns true, which means the executable does not export Lua symbols.

**Q: How can I exclude large files from being parsed in my project?**  
A: Create an `emmy.config.json` file in the project root with the following content:
```json
{
    "source": [
        {
            "dir": "./",
            "exclude": [
                "csv/**.lua"
            ]
        }
    ]
}
```

## Credits

This extension is a fork of the original [EmmyLua](https://github.com/EmmyLua/VSCode-EmmyLua) project by [@EmmyLua](https://github.com/EmmyLua). 
It has been significantly re-architected with a new C++ language server for improved performance and additional features.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
