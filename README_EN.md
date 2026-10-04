# Codex Account Switcher

Lightweight account switching for Windows 11 · Native C++ / Win32 desktop utility

[中文](README.md) | English

Import account JSON, keep encrypted copies locally, and switch Codex file authentication with one click. A single EXE with no .NET, browser engine, or separate VC++ runtime installation.

> File authentication only. Confirm the account in the client after switching; expired or revoked tokens require a new login. Restarting ChatGPT does not guarantee its separate web session follows the authentication file.

## Features

- **Import and convert**: select multiple files, drop JSON onto the window, or pass file paths as arguments. Accepts flat token fields and standard `auth_mode: chatgpt` JSON.
- **Switch accounts**: preserve current credentials and refreshed tokens before switching. Restart ChatGPT / Codex, or only write the authentication file.
- **Save and restore**: capture the current account, restore the previous authentication, and delete local copies. Reimporting an account updates its copy.
- **Local encryption**: protect copies with Windows DPAPI for the current user. No network requests, telemetry, or token logs.
- **Tray and startup**: embedded application icon, minimize to tray, optional startup at Windows sign-in, and existing-window restoration on repeated launches.

## Build and use

Runtime: Windows 11 x64. Building requires Visual Studio with **Desktop development with C++**, the Windows SDK, and CMake.

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The script builds Release, runs isolated self-tests, and places `CodexAccountSwitcher.exe` and documentation in `release\`. Keep the EXE in a fixed location and run it.

1. Click **导入 JSON…** (Import JSON), or drop account files onto the window. Importing does not replace active authentication.
2. Select an account and restart target, then click **一键切换** (Switch). Double-clicking an account also switches it.
3. Use **保存当前账户** (Save current account), **恢复上次认证** (Restore previous authentication), or **删除副本** (Delete copy) as needed.
4. Closing the window also hides it to the tray. Click the tray icon to restore the window. Its context menu offers Show window and Startup; only **Exit** ends the application.

The application interface is currently in Chinese.

| Setting | Default / behavior |
| --- | --- |
| Action after switching | Restart ChatGPT; Codex and file-only mode are available |
| Minimize to tray | Enabled; the preference can be changed and saved |
| Startup at sign-in | Disabled; enabling it starts directly in the tray |
| Close window | Always hide to tray, regardless of the minimize preference |
| Tray menu Exit | End the application; temporarily blocked during account switching |

Save your work before restarting a client. The utility requests a normal shutdown, then terminates related processes still running after 2.5 seconds. If the selected client cannot be found, authentication is left untouched; choose file-only mode instead.

## Data and privacy

| Item | Location |
| --- | --- |
| Active authentication | `%USERPROFILE%\.codex\auth.json` |
| Encrypted account copies | `%LOCALAPPDATA%\CodexAccountSwitcher\profiles\*.dpapi` |
| Previous authentication | `previous.dpapi` in the same directory |
| Preferences | `settings.json` in the same directory |
| Current-user startup | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\CodexAccountSwitcher` |

Copies and backups use DPAPI encryption. Directories and new files restrict access to the current user and SYSTEM; authentication updates use atomic file replacement. The client's `auth.json` is plaintext. DPAPI cannot protect against malicious processes running as the same user or administrators. Imported source files remain at their original location; keep them secure.

Encrypted copies cannot be transferred directly to another Windows user or machine; reimport the original files. After moving the EXE, disable and re-enable startup to update its path. Disable startup and exit before removing the application.

## Compatibility

- The target is always `%USERPROFILE%\.codex\auth.json`; `CODEX_HOME` is not followed.
- Only file authentication is switched. The utility does not modify the system credential store or `config.toml`. For file authentication, set `cli_auth_credentials_store = "file"` in `.codex/config.toml`; see the [Codex authentication documentation](https://developers.openai.com/codex/auth/).
- Imports copy `id_token`, `access_token`, `refresh_token`, `account_id`, and `last_refresh` unchanged. Tokens are not decoded or modified. Missing required fields, duplicate JSON keys, invalid dates, disabled accounts, and files larger than 4 MB are rejected.
- ChatGPT and Codex may resolve to the same desktop application. Separate web sessions are outside the scope of switching. Actual login depends on the client's authentication storage and token validity.
- The EXE is unsigned. Never distribute account files, tokens, or local application data with the source or executable.

## Tests

```powershell
.\build.ps1
.\tests\window.ps1
```

Build self-tests use temporary directories and synthetic credentials. Window tests cover the icon, tray, repeated launches, preference persistence, startup registration/removal, and exit using isolated directories and a test registry key. Real accounts and startup settings remain unchanged. See [VALIDATION.md](VALIDATION.md) for validation scope.

Read-only diagnostics: `--check-file <path>` validates JSON; `--probe-apps` checks desktop application discovery. Neither switches accounts. `--tray` starts in the tray.

## Acknowledgments

Inspired by the conversion workflow in [json-format-tool](https://github.com/BIMiracle/json-format-tool) and the native desktop approach in [slack-off](https://github.com/BIMiracle/slack-off). JSON parsing uses nlohmann/json 3.12.0 (MIT); see [vendor/LICENSE-json.txt](vendor/LICENSE-json.txt).
