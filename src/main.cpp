#include "core.h"
#include "desktop.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <memory>
#include <thread>
static std::unique_ptr<Store> store;
static HWND window,list,status,combo,info;
static HFONT font;
static bool busy=false;
static HANDLE mutexHandle=nullptr;
static bool trayVisible=false, minimizeToTray=true;
static UINT taskbarCreated=0;
static std::wstring windowClass=L"CodexAccountSwitcher";
static std::wstring runKey=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static constexpr wchar_t RunValue[]=L"CodexAccountSwitcher";
static void Diagnostic(const std::string& text) {
 DWORD written=0;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),text.data(),(DWORD)text.size(),&written,nullptr);
}
enum { ImportId=101,SwitchId,DeleteId,CaptureId,RestoreId,ComboId,ListId,StartupId,TrayOptionId,TrayShowId,TrayExitId,Done=WM_APP+1,TrayCallback,ShowInstance };
struct Result { std::wstring message; };
static void ShowError(const std::exception& e) { MessageBoxW(window,Wide(e.what()).c_str(),L"操作未完成",MB_OK|MB_ICONERROR); }
static std::wstring StartupCommand() {
 wchar_t path[32768]{};DWORD length=GetModuleFileNameW(nullptr,path,32768);
 if(!length||length>=32768)throw std::runtime_error("无法读取程序路径。");
 return L"\""+std::wstring(path)+L"\" --tray";
}
static bool StartupEnabled() {
 wchar_t value[32768]{};DWORD bytes=sizeof(value);
 return RegGetValueW(HKEY_CURRENT_USER,runKey.c_str(),RunValue,RRF_RT_REG_SZ,nullptr,value,&bytes)==ERROR_SUCCESS && std::wstring(value)==StartupCommand();
}
static void SetStartup(bool enabled) {
 HKEY key=nullptr;LSTATUS code=RegCreateKeyExW(HKEY_CURRENT_USER,runKey.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
 if(code!=ERROR_SUCCESS)throw std::runtime_error("无法修改当前用户的开机自启设置。");
 if(enabled){auto command=StartupCommand();code=RegSetValueExW(key,RunValue,0,REG_SZ,(const BYTE*)command.c_str(),(DWORD)((command.size()+1)*sizeof(wchar_t)));}
 else code=RegDeleteValueW(key,RunValue);
 RegCloseKey(key);
 if(code!=ERROR_SUCCESS && !(code==ERROR_FILE_NOT_FOUND&&!enabled))throw std::runtime_error("开机自启设置保存失败。");
}
static void SaveSettings() {
 AtomicWrite(store->root/L"settings.json",Json{{"restart_mode",(int)SendMessageW(combo,CB_GETCURSEL,0,0)},{"minimize_to_tray",minimizeToTray}}.dump());
}
static NOTIFYICONDATAW TrayData() {
 NOTIFYICONDATAW data{};data.cbSize=sizeof(data);data.hWnd=window;data.uID=1;
 data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;data.uCallbackMessage=TrayCallback;
 data.hIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101));
 wcscpy_s(data.szTip,L"Codex 账户切换工具");return data;
}
static bool AddTray() {
 if(trayVisible)return true;
 auto data=TrayData();trayVisible=Shell_NotifyIconW(NIM_ADD,&data)!=FALSE;return trayVisible;
}
static void RemoveTray() {
 if(trayVisible){auto data=TrayData();Shell_NotifyIconW(NIM_DELETE,&data);trayVisible=false;}
}
static void ShowMain() { ShowWindow(window,SW_RESTORE);RemoveTray();SetForegroundWindow(window); }
static void HideToTray() {
 if(AddTray())ShowWindow(window,SW_HIDE);
 else {ShowMain();SetWindowTextW(status,L"无法创建托盘图标，窗口已恢复。");}
}
static void TrayMenu() {
 HMENU menu=CreatePopupMenu();if(!menu)return;
 AppendMenuW(menu,MF_STRING,TrayShowId,L"显示窗口");
 AppendMenuW(menu,MF_STRING|(StartupEnabled()?MF_CHECKED:0),StartupId,L"开机自启");
 AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
 AppendMenuW(menu,MF_STRING|(busy?MF_GRAYED:0),TrayExitId,L"退出");
 SetMenuDefaultItem(menu,TrayShowId,FALSE);POINT point{};GetCursorPos(&point);SetForegroundWindow(window);
 UINT selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);
 DestroyMenu(menu);PostMessageW(window,WM_NULL,0,0);
 if(selected)SendMessageW(window,WM_COMMAND,selected,0);
}
static void Refresh() {
 SendMessageW(list,LB_RESETCONTENT,0,0);
 std::string current;
 try{if(fs::exists(store->auth))current=Convert(Parse(Read(store->auth)))["tokens"]["account_id"].get<std::string>();}catch(...){}
 for(auto& p:store->profiles) {auto label=p.label+L" · "+Wide(p.account.substr(0,8))+(p.account==current?L"  [当前文件账户]":L"");SendMessageW(list,LB_ADDSTRING,0,(LPARAM)label.c_str());}
 std::wstring text=L"已保存 "+std::to_wstring(store->profiles.size())+L" 个账户 · 账户副本已加密";
 if(store->unreadable)text+=L" · "+std::to_wstring(store->unreadable)+L" 个损坏副本已忽略，请重新导入";
 SetWindowTextW(status,text.c_str());
}
static void ImportFile(const fs::path& path) { store->Import(path); Refresh(); SetWindowTextW(status,L"导入完成。选中账户后点击“一键切换”。"); }
static void ImportDialog() {
 wchar_t paths[32768]{}; OPENFILENAMEW of{sizeof(of)}; of.hwndOwner=window; of.lpstrFilter=L"JSON 账户文件\0*.json\0所有文件\0*.*\0";
 of.lpstrFile=paths; of.nMaxFile=32768; of.Flags=OFN_FILEMUSTEXIST|OFN_ALLOWMULTISELECT|OFN_EXPLORER|OFN_NOCHANGEDIR;
 if(!GetOpenFileNameW(&of)) return;
 wchar_t* next=paths+wcslen(paths)+1;
 if(!*next) ImportFile(paths);
 else {fs::path dir=paths; while(*next){ImportFile(dir/next); next+=wcslen(next)+1;}}
}
static void SetBusy(bool value) {
 busy=value; DragAcceptFiles(window,!value);
 for(int id:{ImportId,SwitchId,DeleteId,CaptureId,RestoreId,ComboId,ListId}) EnableWindow(GetDlgItem(window,id),!value);
}
static void Activate(bool restore) {
 LRESULT index=SendMessageW(list,LB_GETCURSEL,0,0); if(!restore && index==LB_ERR) throw std::runtime_error("请先导入并选择账户。");
 int mode=(int)SendMessageW(combo,CB_GETCURSEL,0,0);
 SetBusy(true); SetWindowTextW(status,L"正在保存当前凭据并切换，请稍候…");
 std::thread([index,mode,restore]{
  CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  auto result=new Result;
  try {
   DesktopApp app; bool restart=mode!=2;
   if(restart) { app=DiscoverApp(mode==1); if(!app.Available()) throw std::runtime_error("未找到所选桌面程序。请选择另一程序或“仅写入 auth.json”。"); StopApp(app); }
   try {if(restore)store->Restore();else store->Activate((size_t)index);}
   catch(...) {if(restart){try{StartApp(app);}catch(...){}} throw;}
   if(restart) StartApp(app);
   result->message=restart?L"认证文件已切换，已请求重启桌面程序。请在程序中确认当前账户。":L"认证文件已切换。下次启动使用文件认证的客户端时生效。";
  }catch(const Json::exception&){result->message=L"账户数据损坏，请重新导入。";}catch(const std::exception& e){result->message=Wide(e.what());}catch(...){result->message=L"操作失败，未输出凭据。";}
  CoUninitialize();
  PostMessageW(window,Done,0,(LPARAM)result);
 }).detach();
}
static HWND Control(const wchar_t* cls,const wchar_t* text,DWORD style,int id) {
 HWND c=CreateWindowExW(cls==std::wstring(L"LISTBOX")?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,window,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
 SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;
}
static void Layout() {
 RECT r{}; GetClientRect(window,&r); int w=r.right,h=r.bottom;
 MoveWindow(info,18,14,w-36,62,TRUE); MoveWindow(list,18,84,w-36,h-271,TRUE);
 int y=h-176; MoveWindow(GetDlgItem(window,ImportId),18,y,140,32,TRUE); MoveWindow(GetDlgItem(window,CaptureId),168,y,170,32,TRUE); MoveWindow(GetDlgItem(window,DeleteId),348,y,110,32,TRUE);
 MoveWindow(combo,18,y+44,210,160,TRUE); MoveWindow(GetDlgItem(window,SwitchId),240,y+44,150,34,TRUE); MoveWindow(GetDlgItem(window,RestoreId),402,y+44,150,34,TRUE); MoveWindow(status,18,h-42,w-36,30,TRUE);
 MoveWindow(GetDlgItem(window,StartupId),18,h-86,160,28,TRUE);MoveWindow(GetDlgItem(window,TrayOptionId),190,h-86,200,28,TRUE);
}
static LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
 try {
 if(taskbarCreated && msg==taskbarCreated){bool hidden=trayVisible;trayVisible=false;if(hidden)HideToTray();return 0;}
 switch(msg) {
 case WM_CREATE:
  window=hwnd; font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
  info=Control(L"STATIC",L"导入 JSON → 选择账户 → 一键切换\n写入 %USERPROFILE%\\.codex\\auth.json；支持文件认证的 Codex 客户端。",0,0);
  list=Control(L"LISTBOX",L"",LBS_NOTIFY|WS_VSCROLL|WS_TABSTOP,ListId);
  Control(L"BUTTON",L"导入 JSON…",WS_TABSTOP,ImportId);Control(L"BUTTON",L"保存当前账户",WS_TABSTOP,CaptureId);Control(L"BUTTON",L"删除副本",WS_TABSTOP,DeleteId);
  combo=Control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,ComboId);
  for(auto s:{L"切换后重启 ChatGPT",L"切换后重启 Codex",L"仅写入 auth.json"}) SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)s);
  SendMessageW(combo,CB_SETCURSEL,0,0);
  Control(L"BUTTON",L"一键切换",WS_TABSTOP|BS_DEFPUSHBUTTON,SwitchId);Control(L"BUTTON",L"恢复上次认证",WS_TABSTOP,RestoreId);
  Control(L"BUTTON",L"开机自启",WS_TABSTOP|BS_AUTOCHECKBOX,StartupId);Control(L"BUTTON",L"最小化到托盘",WS_TABSTOP|BS_AUTOCHECKBOX,TrayOptionId);
  SendMessageW(GetDlgItem(hwnd,StartupId),BM_SETCHECK,StartupEnabled()?BST_CHECKED:BST_UNCHECKED,0);
  SendMessageW(GetDlgItem(hwnd,TrayOptionId),BM_SETCHECK,BST_CHECKED,0);
  status=Control(L"STATIC",L"",0,0);DragAcceptFiles(hwnd,TRUE); Refresh(); return 0;
 case WM_SIZE:if(wp==SIZE_MINIMIZED && minimizeToTray)HideToTray();else Layout();return 0;
 case WM_GETMINMAXINFO: ((MINMAXINFO*)lp)->ptMinTrackSize={610,420};return 0;
 case WM_COMMAND:
  if(LOWORD(wp)==TrayShowId){ShowMain();return 0;}
  if(LOWORD(wp)==TrayExitId){SendMessageW(hwnd,WM_CLOSE,0,0);return 0;}
  if(LOWORD(wp)==StartupId){
   try{SetStartup(!StartupEnabled());}catch(...){SendMessageW(GetDlgItem(hwnd,StartupId),BM_SETCHECK,StartupEnabled()?BST_CHECKED:BST_UNCHECKED,0);throw;}
   SendMessageW(GetDlgItem(hwnd,StartupId),BM_SETCHECK,StartupEnabled()?BST_CHECKED:BST_UNCHECKED,0);return 0;
  }
  if(LOWORD(wp)==TrayOptionId){
   bool previous=minimizeToTray;minimizeToTray=SendMessageW(GetDlgItem(hwnd,TrayOptionId),BM_GETCHECK,0,0)==BST_CHECKED;
   try{SaveSettings();}catch(...){minimizeToTray=previous;SendMessageW(GetDlgItem(hwnd,TrayOptionId),BM_SETCHECK,previous?BST_CHECKED:BST_UNCHECKED,0);throw;}return 0;
  }
  if(busy) return 0;
  switch(LOWORD(wp)) {
  case ImportId:ImportDialog();break;
  case SwitchId:Activate(false);break;
  case RestoreId:Activate(true);break;
  case CaptureId:store->Capture();Refresh();break;
  case DeleteId:{auto i=SendMessageW(list,LB_GETCURSEL,0,0);if(i!=LB_ERR && MessageBoxW(hwnd,L"删除所选账户的加密副本？当前 auth.json 不受影响。",L"删除副本",MB_YESNO|MB_ICONQUESTION)==IDYES){fs::remove(store->profiles[(size_t)i].path);store->Reload();Refresh();}break;}
  case ComboId: if(HIWORD(wp)==CBN_SELCHANGE) {
   SaveSettings();
  }break;
  case ListId:if(HIWORD(wp)==LBN_DBLCLK) Activate(false);break;
  } return 0;
 case WM_DROPFILES:{HDROP drop=(HDROP)wp; UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0); try{for(UINT i=0;i<count;i++){wchar_t path[32768]{};DragQueryFileW(drop,i,path,32768);ImportFile(path);}}catch(...){DragFinish(drop);throw;}DragFinish(drop);return 0;}
 case Done:{std::unique_ptr<Result> result((Result*)lp);SetBusy(false);store->Reload();Refresh();SetWindowTextW(status,result->message.c_str());MessageBoxW(hwnd,result->message.c_str(),L"切换结果",MB_OK);return 0;}
 case WM_CLOSE:if(busy)return 0;DestroyWindow(hwnd);return 0;
 case TrayCallback:if(lp==WM_LBUTTONUP||lp==WM_LBUTTONDBLCLK)ShowMain();else if(lp==WM_RBUTTONUP||lp==WM_CONTEXTMENU)TrayMenu();return 0;
 case ShowInstance:ShowMain();return 0;
 case WM_DESTROY:RemoveTray();DeleteObject(font);PostQuitMessage(0);return 0;
 }
 }catch(const Json::exception&){MessageBoxW(hwnd,L"账户数据损坏，请重新导入。",L"操作未完成",MB_OK|MB_ICONERROR);}
 catch(const std::exception& e){ShowError(e);} catch(...){MessageBoxW(hwnd,L"操作失败。",L"错误",MB_OK|MB_ICONERROR);}
 return DefWindowProcW(hwnd,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 int argc=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
 if(argc==2 && std::wstring(argv[1])==L"--self-test") {LocalFree(argv);int code=SelfTest();CoUninitialize();return code;}
 if(argc==3 && std::wstring(argv[1])==L"--check-file") {
  int code=0;try{Convert(Parse(Read(argv[2])));Diagnostic("JSON mapping valid; no credentials saved.\n");}catch(...){Diagnostic("JSON mapping invalid.\n");code=1;}
  LocalFree(argv);CoUninitialize();return code;
 }
 if(argc==2 && std::wstring(argv[1])==L"--probe-apps") {
  for(bool codex:{false,true}){auto app=DiscoverApp(codex);Diagnostic(Utf8(app.name)+(app.Available()?": found":": not found")+", registered="+(!app.aumid.empty()?"yes":"no")+", running processes="+std::to_string(app.pids.size())+"\n");}
  LocalFree(argv);CoUninitialize();return 0;
 }
 // Single instance prevents two simultaneous writers from losing updated tokens.
 std::wstring mutexName=L"Local\\BIMiracle.CodexAccountSwitcher";
 // Isolated UI diagnostics can coexist with the user's running instance.
 bool uiTest=argc>=2 && std::wstring(argv[1])==L"--ui-test";
 if(uiTest){
  wchar_t local[32768]{},profile[32768]{},temp[32768]{};
  GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);GetEnvironmentVariableW(L"USERPROFILE",profile,32768);GetTempPathW(32768,temp);
  try{fs::path root(local);auto name=root.filename().wstring();
   if(name.find(L"CodexSwitcher-window-")!=0 || !fs::equivalent(root.parent_path(),fs::path(temp)) || !fs::equivalent(root,fs::path(profile)))throw std::runtime_error("invalid test directory");
   mutexName+=L"."+name;windowClass+=L"."+name;runKey=L"Software\\CodexAccountSwitcher\\UITests\\"+name+L"\\Run";
  }catch(...){Diagnostic("UI test requires matching temporary USERPROFILE/LOCALAPPDATA directories.\n");LocalFree(argv);CoUninitialize();return 1;}
 }
 mutexHandle=CreateMutexW(nullptr,FALSE,mutexName.c_str());
 if(!mutexHandle || GetLastError()==ERROR_ALREADY_EXISTS){HWND existing=FindWindowW(windowClass.c_str(),nullptr);if(existing)PostMessageW(existing,ShowInstance,0,0);else MessageBoxW(nullptr,L"账户切换工具已在运行。",L"提示",MB_OK);if(mutexHandle)CloseHandle(mutexHandle);LocalFree(argv);CoUninitialize();return 0;}
 try {
  wchar_t profile[32768]{},local[32768]{};GetEnvironmentVariableW(L"USERPROFILE",profile,32768);GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
  store=std::make_unique<Store>(fs::path(local)/L"CodexAccountSwitcher"/L"profiles",fs::path(profile)/L".codex"/L"auth.json");
  INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
  taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
  WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=instance;wc.lpszClassName=windowClass.c_str();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);
  HWND hwnd=CreateWindowW(wc.lpszClassName,L"Codex 账户切换工具",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,720,520,nullptr,nullptr,instance,nullptr);
  if(!hwnd) throw std::runtime_error("窗口创建失败。");
  if(fs::exists(store->root/L"settings.json")){try{auto j=Parse(Read(store->root/L"settings.json"));int mode=j.value("restart_mode",0);if(mode>=0&&mode<=2)SendMessageW(combo,CB_SETCURSEL,mode,0);minimizeToTray=j.value("minimize_to_tray",true);}catch(...){SetWindowTextW(status,L"设置文件损坏，已使用默认设置。");}}
  SendMessageW(GetDlgItem(hwnd,TrayOptionId),BM_SETCHECK,minimizeToTray?BST_CHECKED:BST_UNCHECKED,0);
  bool startInTray=false;for(int i=1;i<argc;i++)if(std::wstring(argv[i])==L"--tray")startInTray=true;
  if(startInTray)HideToTray();else ShowWindow(hwnd,show);UpdateWindow(hwnd);
  // Files may also be passed through Open With / a shortcut; credentials never appear in arguments.
  for(int i=1;i<argc;i++){if(std::wstring(argv[i])==L"--tray"||(uiTest&&i==1))continue;try{ImportFile(argv[i]);}catch(const std::exception& e){ShowError(e);}}
  LocalFree(argv);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
 }catch(const std::exception& e){MessageBoxW(nullptr,Wide(e.what()).c_str(),L"启动失败",MB_OK|MB_ICONERROR);return 1;}
 if(mutexHandle)CloseHandle(mutexHandle);CoUninitialize();return 0;
}
