# Changelog / 更新日志

## v0.1.0 — 2026-10-04

首次公开发布，适用于 Windows 11 x64。

- 导入、转换与切换账户 JSON，支持重启 ChatGPT / Codex 或仅写入认证文件。
- 使用 Windows DPAPI 加密本地账户副本，支持保存当前账户和恢复上次认证。
- 内置应用图标、最小化到托盘和可选开机自启。
- 关闭窗口隐藏到托盘，仅托盘菜单“退出”结束程序。
- 提供中英文说明、隔离账户自测及窗口行为测试。

Initial public release for Windows 11 x64.

- Import, convert, and switch account JSON; restart ChatGPT / Codex or update only the authentication file.
- Encrypt local copies with Windows DPAPI; capture current accounts and restore previous authentication.
- Embedded application icon, minimize to tray, and optional startup at Windows sign-in.
- Closing the window hides it to the tray; only the tray menu's Exit ends the application.
- Chinese and English documentation, isolated account self-tests, and window behavior tests.

此工具仅切换文件认证；实际登录结果需要在客户端确认。程序未签名。

This utility switches file authentication only; verify the resulting login in the client. The executable is unsigned.
