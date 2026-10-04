#pragma once
#include <windows.h>
#include <string>
#include <vector>
struct DesktopApp {
 std::wstring name, aumid, exe;
 std::vector<DWORD> pids;
 std::vector<std::pair<DWORD,std::wstring>> paths;
 bool Available() const {return !aumid.empty() || !exe.empty();}
};
DesktopApp DiscoverApp(bool codex);
void StopApp(const DesktopApp& app);
void StartApp(const DesktopApp& app);
