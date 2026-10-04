#include "desktop.h"
#include <shlobj.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
#include <appmodel.h>
namespace fs=std::filesystem;
DesktopApp DiscoverApp(bool codex) {
 DesktopApp result; result.name=codex?L"Codex":L"ChatGPT";
 // Enumerate registered desktop apps (Store AUMID and ordinary shortcuts), without PowerShell.
 IShellItem* folder=nullptr;
 if(SUCCEEDED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder,0,nullptr,IID_PPV_ARGS(&folder)))) {
  IEnumShellItems* items=nullptr;
  if(SUCCEEDED(folder->BindToHandler(nullptr,BHID_EnumItems,IID_PPV_ARGS(&items)))) {
   IShellItem* item=nullptr;
   while(items->Next(1,&item,nullptr)==S_OK) {
    PWSTR name=nullptr,id=nullptr;
    item->GetDisplayName(SIGDN_NORMALDISPLAY,&name);
    if(SUCCEEDED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&id))) {
     std::wstring parsing=id; auto pos=parsing.find_last_of(L'\\'); auto appId=pos==std::wstring::npos?parsing:parsing.substr(pos+1);
     bool match=name && _wcsicmp(name,result.name.c_str())==0;
     // Some releases register OpenAI.Codex but display and run as ChatGPT.
     if(codex && appId.find(L"OpenAI.Codex_")!=std::wstring::npos && appId.find(L"!App")!=std::wstring::npos) match=true;
     if(match) result.aumid=appId;
    }
    if(name) CoTaskMemFree(name); if(id) CoTaskMemFree(id); item->Release();
   }
   items->Release();
  }
  folder->Release();
 }
 HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
 if(snap!=INVALID_HANDLE_VALUE) {
  PROCESSENTRY32W entry{sizeof(entry)};
  if(Process32FirstW(snap,&entry)) do {
   bool correctName=_wcsicmp(entry.szExeFile,(result.name+L".exe").c_str())==0;
   if(codex && _wcsicmp(entry.szExeFile,L"ChatGPT.exe")==0) correctName=true;
   if(!correctName) continue;
   HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID); if(!p) continue;
   wchar_t path[32768]{}; DWORD n=32768;
   if(QueryFullProcessImageNameW(p,0,path,&n)) {
    std::wstring full=path, lower=full; for(auto& c:lower)c=(wchar_t)towlower(c);
    // Codex CLI also uses codex.exe. Only the desktop app belongs in this restart flow.
    bool desktop=!codex || lower.find(L"\\windowsapps\\openai.codex_")!=std::wstring::npos || lower.find(L"\\codex\\app\\")!=std::wstring::npos;
    if(desktop) {
     result.pids.push_back(entry.th32ProcessID); result.exe=full;
     result.paths.emplace_back(entry.th32ProcessID,full);
     UINT32 length=0;
     if(GetApplicationUserModelId(p,&length,nullptr)==ERROR_INSUFFICIENT_BUFFER && length>0) {
      std::wstring model(length,L'\0');
      if(GetApplicationUserModelId(p,&length,model.data())==ERROR_SUCCESS) {model.resize(wcslen(model.c_str()));result.aumid=model;}
     }
    }
   }
   CloseHandle(p);
  }while(Process32NextW(snap,&entry));
  CloseHandle(snap);
 }
 if(!result.Available()) {
  wchar_t local[32768]{}; GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
  for(auto suffix:{L"\\Programs\\",L"\\"}) {
   fs::path p=std::wstring(local)+suffix+result.name+L"\\"+result.name+L".exe";
   if(fs::exists(p)) {result.exe=p.wstring();break;}
  }
 }
 return result;
}
static bool SameProcess(HANDLE handle,DWORD pid,const DesktopApp& app) {
 auto it=std::find_if(app.paths.begin(),app.paths.end(),[&](const auto& entry){return entry.first==pid;});
 if(it==app.paths.end()) return false;
 wchar_t path[32768]{};DWORD n=32768;
 return QueryFullProcessImageNameW(handle,0,path,&n) && _wcsicmp(path,it->second.c_str())==0;
}
static BOOL CALLBACK CloseWindow(HWND w,LPARAM p) {
 DWORD pid=0;GetWindowThreadProcessId(w,&pid);
 auto& app=*(const DesktopApp*)p;
 if(std::find(app.pids.begin(),app.pids.end(),pid)!=app.pids.end() && GetWindow(w,GW_OWNER)==nullptr) {
  HANDLE handle=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
  if(handle){if(SameProcess(handle,pid,app))PostMessageW(w,WM_CLOSE,0,0);CloseHandle(handle);}
 }
 return TRUE;
}
void StopApp(const DesktopApp& app) {
 EnumWindows(CloseWindow,(LPARAM)&app);
 ULONGLONG deadline=GetTickCount64()+2500;
 for(DWORD id:app.pids) {
  HANDLE p=OpenProcess(SYNCHRONIZE|PROCESS_TERMINATE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,id);
  if(!p) {
   HANDLE probe=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,id);
   if(probe){CloseHandle(probe);throw std::runtime_error("无法关闭桌面程序，请手动退出后再切换。");}
   continue;
  }
  if(!SameProcess(p,id,app)){CloseHandle(p);continue;}
  DWORD wait=(DWORD)(GetTickCount64()<deadline?deadline-GetTickCount64():0);
  if(WaitForSingleObject(p,wait)!=WAIT_OBJECT_0) {
   if(!TerminateProcess(p,0)||WaitForSingleObject(p,3000)!=WAIT_OBJECT_0){CloseHandle(p);throw std::runtime_error("桌面程序仍在运行，未修改认证文件。");}
  }
  CloseHandle(p);
 }
}
void StartApp(const DesktopApp& app) {
 if(!app.aumid.empty()) {
  std::wstring target=L"shell:AppsFolder\\"+app.aumid;
  if((INT_PTR)ShellExecuteW(nullptr,L"open",target.c_str(),nullptr,nullptr,SW_SHOWNORMAL)>32) return;
 }
 if(!app.exe.empty() && (INT_PTR)ShellExecuteW(nullptr,L"open",app.exe.c_str(),nullptr,fs::path(app.exe).parent_path().c_str(),SW_SHOWNORMAL)>32) return;
 throw std::runtime_error("认证文件已写入，但桌面程序启动失败，请手动打开。");
}
