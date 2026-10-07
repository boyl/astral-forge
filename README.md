# Astral Forge · 星界战士 Mod 开发基座

基于原 aamod 的 MIT 源码和历史继续维护，为《星界战士》（Astral Ascent）的 C/C++ Mod 开发者提供加载器、版本化 C ABI 和游戏适配接口。

当前 **0.1.9-dev：开发者试用预发布**。按可服务更多开发者的方向设计，当前以少量开发者试用标准交付。

## 目录

- [下载与兼容](#下载与兼容)
- [关键功能](#关键功能)
- [安装与卸载](#安装与卸载)
- [开始开发](#开始开发)
- [示例与文档](#示例与文档)
- [验证与限制](#验证与限制)
- [反馈与许可证](#反馈与许可证)

## 下载与兼容

前往 [GitHub Releases](https://github.com/boyl/astral-forge/releases)：

- `aamod-0.1.9-dev-runtime.zip`：加载器、预编译示例、SDK、安装器和文档。
- `aamod-0.1.9-dev-source.zip`：可独立构建的源码。
- `aamod-0.1.9-dev-sha256.json`：归档与逐文件 SHA-256 清单。

已验证 **Steam Windows x64 · Astral Ascent 2.6.4 · 单人**。游戏更新后先检查适配状态；精确 EXE 指纹与覆盖范围见 [试用验收矩阵](docs/试用验收矩阵.md)。

## 关键功能

| 能力 | 可以构建的内容 |
| --- | --- |
| 加载与 SDK | C/C++ 原生插件、日志、配置、锚点与 hook；ABI 1，API 328 字节，保留旧前缀 |
| 状态与原生事件 | 场景、玩家、血量、法力；响应原生伤害、水晶拾取、开局和结束 |
| 构筑接口 | 光环/技能目录、五个光环槽与四个技能槽，按契约受控修改 |
| 受控命令 | 回血、法力补充、装备修改、原生保存派发和请求结果 |
| 菜单与持久数据 | 共享搜索选配菜单、键鼠交互、插件自有预设和跨进程恢复 |
| 图片服务 | 插件 PNG 服务、原生图片替换/恢复；任意新资源注册尚未开放 |

`build_selector` 演示选配与预设；`event_responder` 演示伤害回血和水晶拾取补充法力。玩法修改示例默认关闭，需要明确启用。

## 安装与卸载

需要 PowerShell 7。解压运行包，关闭游戏，在解压目录执行：

```powershell
./install.ps1 -GameDir '你的 Astral Ascent 游戏目录' -WhatIf
./install.ps1 -GameDir '你的 Astral Ascent 游戏目录'
./tools/launch-game.ps1 -GameDir '你的 Astral Ascent 游戏目录'
```

默认使用 **winmm 入口**，保留 BD Mod 的 `version.dll`；未知入口和文件漂移拒绝覆盖。安装、更新与卸载保留用户插件、配置和日志；游戏目录需要写权限。

受控命令用启动脚本 `-Commands` 显式启用。原生事件与插件启用方式见 [内容开发者快速开始](docs/内容开发者快速开始.md)。渲染回调默认关闭，需要 `-Render` 显式开启；当前推荐默认 winmm 路径。

```powershell
./tools/launch-game.ps1 -GameDir '你的 Astral Ascent 游戏目录' -Commands
./install.ps1 -GameDir '你的 Astral Ascent 游戏目录' -Uninstall
```

更换入口前先卸载。完整安装和排错见 [试用版交付流程](docs/试用版交付流程.md) 与 [SUPPORT](SUPPORT.md)。

## 开始开发

构建需要 Windows x64、PowerShell 7、Visual Studio C++ 构建工具和 Python 3.11 以上。源码包或仓库均可独立构建，命令在 PowerShell 7 中执行：

```powershell
./build.ps1 -Test -PythonPath '你的 Python 可执行文件路径'
./tests/run-contracts.ps1
./tests/run-installer.ps1
./tools/build-mod.ps1 -ModDirectory ./mods/template
```

从 `mods/template` 开始，按结构大小与能力检查访问 API。先在独立宿主验证，再部署到匹配版本游戏。插件是原生 DLL，没有恶意代码沙箱；仅加载可信插件。

## 示例与文档

运行包包含 14 个示例及其源码，公开示例只依赖 SDK。

- [内容开发者快速开始](docs/内容开发者快速开始.md)：两个完整示例、启用步骤与开发闭环。
- [开发者起步](docs/开发者起步.md) · [SDK 稳定规则](docs/SDK稳定规则.md)。
- [只读状态](docs/只读状态接口.md) · [状态事件](docs/状态事件接口.md) · [原生玩法事件](docs/原生玩法事件接口.md)。
- [受控修改](docs/受控修改接口.md) · [PNG 服务](docs/PNG资源接口.md) · [原生图片替换](docs/原生图片替换接口.md)。
- [内容开发闭环](docs/内容开发闭环.md) · [路线图](docs/接管与路线图.md) · [CHANGELOG](CHANGELOG.md)。

## 验证与限制

试用候选通过独立解压构建、14 示例编译、旧 ABI/宿主契约、安装/诊断/卸载与包审计。技能名称、图标、施法、升级尾部、换房和重启有隔离实机证据；双示例同进程组合及组合预设新进程恢复通过。部分历史结束事件证据对应此前构建，以验收矩阵为准。

- 仅认证上述精确版本与单人；多人、控制器、其他 DPI 和其他版本未认证。
- 技能替换清除四个 gambit ID，保留升级及实例身份尾部；不提供任意等级或冷却写入。
- `SAVE_DISPATCHED` 只表示原生保存入口已派发，不代表持久化成功。
- Lua、通用房间事件、其他拾取类型、任意新资源注册与热重载尚未开放。
- 曾发生 D3D 设备失效，后续台前启动未复现，根因未确定；后台独立桌面路线不作为支持能力。图形设备重建未认证。

## 反馈与许可证

问题提交到 [Issues](https://github.com/boyl/astral-forge/issues)，附游戏/框架版本、日志与复现步骤，先删除个人信息。贡献见 [CONTRIBUTING](CONTRIBUTING.md)。

保留原 aamod 的 MIT 许可与贡献者声明，见 [LICENSE](LICENSE)；依赖见 [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES.md)。社区项目，与游戏官方没有隶属关系，不分发游戏资源、个人存档或 BD Mod。
