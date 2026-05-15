![logo](/res/logo.png)
# jx3-EmmyLua

基于 [EmmyLua](https://github.com/EmmyLua/IntelliJ-EmmyLua) VSCode 早期版本修改

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
