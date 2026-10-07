# 来源与第三方说明

本工程从本机既有 astral-forge / aamod 源码及 Git 历史继续维护。上游许可证为 MIT，署名为 `Copyright (c) 2026 aamod contributors`，完整原文保留在 LICENSE。初始基线提交为 bc509e76a75f8790f3df53f1c6d24f85cdb96c0f。尚未确认上游作者实名或对应公开仓库，不虚构作者身份或网址。

本项目的新代码、示例与工具按仓库 MIT 许可证提供。native_image_sample/assets/replacement.png 和 image_sample 的测试 PNG 是程序生成的自有测试图，不含游戏图片。

research、引擎源码快照和 astral-ascent-re 是原工程中的参考资料，不属于本 MIT 工程，不进入源码/运行交付包，不链接进 DLL。Astral Ascent 的程序、Assets.dat、游戏素材、存档、Steam 身份数据及 BD Mod DLL 均不分发。

代理 DLL 的导出名称和跳转表由本机 Windows 系统 DLL 的导出表生成；交付不包含微软系统 DLL。安装器只在当前机器从 System32 复制对应实现，作为本机安装步骤。MSVC、Windows SDK、Visual C++ 运行库及 GitHub Actions 各自按供应商条款提供，不纳入项目 MIT 许可，不在预览包中再分发。
# JSON for Modern C++

原生目录和检查点 JSON 处理使用 nlohmann/json v3.12.0（MIT），仅作为核心内部实现，不向公共 ABI 暴露其类型。来源：[官方版本](https://github.com/nlohmann/json/releases/tag/v3.12.0)。固定头文件 SHA-256：`aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`，与上游公布值核对一致。

头文件与完整许可保存在 `src/vendor/nlohmann/json.hpp`、`src/vendor/nlohmann/LICENSE.MIT`。游戏目录、文字与 JSON 内容仍由运行时读取用户已安装副本，不加入分发包。
