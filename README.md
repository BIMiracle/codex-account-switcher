# Codex Account Switcher

Windows 11 轻量账户切换 · 原生 C++ / Win32 桌面工具

中文 | [English](README_EN.md)

导入账户 JSON，在本机保存加密副本，一键切换 Codex 的文件认证。单个 EXE，无需安装 .NET、浏览器内核或 VC++ 运行库。

> 仅支持文件认证。切换后请在客户端确认账户；过期或撤销的 token 需要重新登录。重启 ChatGPT 不保证其独立网页会话随文件认证改变。

## 主要功能

- **导入与转换**：支持多选、拖入 JSON 和命令行文件路径；兼容扁平 token 字段及标准 `auth_mode: chatgpt` 格式。
- **账户切换**：保存当前凭据及已刷新的 token，再切换账户；可重启 ChatGPT / Codex，或仅写入认证文件。
- **保存与恢复**：保存当前账户、恢复上次认证、删除本地副本；同一账户重复导入更新已有副本。
- **本机加密**：副本使用 Windows DPAPI 当前用户加密，无联网、遥测或 token 日志。
- **托盘与自启**：内置应用图标，支持最小化到托盘、登录 Windows 时自启，重复启动恢复已有窗口。

## 构建与使用

运行环境：Windows 11 x64。构建需要 Visual Studio 的 **使用 C++ 的桌面开发**、Windows SDK 和 CMake。

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

脚本构建 Release 并运行隔离自测，输出 `release\CodexAccountSwitcher.exe` 和说明文件。将 EXE 放到固定位置后运行。

1. 点击 **导入 JSON…**，或将账户文件拖入窗口。导入不会立即覆盖当前认证。
2. 选择账户和重启目标，点击 **一键切换**；也可双击账户。
3. 按需使用 **保存当前账户**、**恢复上次认证** 或 **删除副本**。
4. 关闭窗口也会隐藏到托盘。单击托盘图标恢复窗口，右键菜单可显示窗口、切换自启；只有选择 **退出** 才真正结束程序。

| 设置 | 默认值 / 行为 |
| --- | --- |
| 切换后的操作 | 重启 ChatGPT；可选 Codex 或仅写入文件 |
| 最小化到托盘 | 开启，可取消并保存偏好 |
| 开机自启 | 关闭；开启后登录 Windows 时启动到托盘 |
| 关闭窗口 | 始终隐藏到托盘，不受最小化选项影响 |
| 托盘右键退出 | 真正结束程序；账户切换期间暂不能退出 |

重启客户端前请保存工作。程序先请求正常关闭，2.5 秒后仍未退出的相关进程会终止。未找到重启目标时不修改认证文件，可改选 **仅写入 auth.json**。

## 数据与隐私

| 项目 | 位置 |
| --- | --- |
| 当前认证 | `%USERPROFILE%\.codex\auth.json` |
| 加密账户副本 | `%LOCALAPPDATA%\CodexAccountSwitcher\profiles\*.dpapi` |
| 上次认证备份 | 同目录 `previous.dpapi` |
| 程序偏好 | 同目录 `settings.json` |
| 当前用户自启 | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\CodexAccountSwitcher` |

副本和备份通过 DPAPI 加密，目录与新建文件限制为当前用户及 SYSTEM，认证文件以临时文件原子替换。客户端使用的 `auth.json` 为明文；DPAPI 不能防止同一用户下的恶意进程或管理员读取。导入原文件保留原位置，请自行妥善保管。

副本不能直接迁移到其他 Windows 用户或机器，应重新导入原文件。移动 EXE 后关闭再开启自启以更新路径；删除程序前先关闭自启并退出。

## 兼容边界

- 目标固定为 `%USERPROFILE%\.codex\auth.json`，不跟随 `CODEX_HOME`。
- 只切换文件认证，不修改系统凭据库或 `config.toml`。使用文件认证时，可在 `.codex/config.toml` 中设置 `cli_auth_credentials_store = "file"`，参见 [Codex 认证文档](https://developers.openai.com/codex/auth/)。
- 导入逐值复制 `id_token`、`access_token`、`refresh_token`、`account_id` 和 `last_refresh`，不解码或修改 token。缺失必需字段、重复 JSON key、无效日期、禁用账户及超过 4 MB 的文件会被拒绝。
- ChatGPT / Codex 选项可能对应同一个桌面应用；独立网页会话不在切换范围内。实际登录结果由客户端认证方式及 token 有效性决定。
- 程序未签名。账户文件、token 和本机数据不应随源码或程序发布。

## 测试

```powershell
.\build.ps1
.\tests\window.ps1
```

构建自测仅使用临时目录和虚拟凭据。窗口测试覆盖图标、托盘、重复启动、偏好保存、自启注册项写入/删除与退出，使用独立临时目录和测试注册表，不改变真实账户或自启项。验证范围见 [VALIDATION.md](VALIDATION.md)。

只读诊断：`--check-file <path>` 校验 JSON，`--probe-apps` 检查桌面应用定位；均不切换账户。`--tray` 启动到托盘。

## 致谢

参考 [json-format-tool](https://github.com/BIMiracle/json-format-tool) 的转换流程和 [slack-off](https://github.com/BIMiracle/slack-off) 的原生桌面方案。JSON 解析使用 nlohmann/json 3.12.0（MIT），许可证见 [vendor/LICENSE-json.txt](vendor/LICENSE-json.txt)。
