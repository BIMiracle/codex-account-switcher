#include "core.h"
#include <wincrypt.h>
#include <sddl.h>
#include <fstream>
#include <set>
#include <stdexcept>
static void Fail(const char* s) { throw std::runtime_error(s); }
std::wstring Wide(const std::string& s) {
 if(s.empty()) return {};
 int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);
 if(!n) Fail("文本不是有效 UTF-8。");
 std::wstring r(n,L'\0'); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),r.data(),n); return r;
}
std::string Utf8(const std::wstring& s) {
 if(s.empty()) return {};
 int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
 if(!n) Fail("文本编码错误。");
 std::string r(n,'\0'); WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),(int)s.size(),r.data(),n,nullptr,nullptr); return r;
}
std::string Read(const fs::path& p) {
 std::ifstream f(p,std::ios::binary); if(!f) Fail("无法读取文件。");
 f.seekg(0,std::ios::end); auto size=f.tellg();
 if(size<0 || size>4*1024*1024) Fail("文件超过 4 MB 限制。");
 std::string s((size_t)size,'\0'); f.seekg(0); f.read(s.data(),size); if(!f) Fail("读取文件失败。"); return s;
}
Json Parse(const std::string& s) {
 try {
  std::vector<std::set<std::string>> keys;
  auto cb=[&](int depth,Json::parse_event_t event,Json& value) {
   if(depth>64) Fail("JSON 嵌套过深。");
   if(event==Json::parse_event_t::object_start) keys.emplace_back();
   if(event==Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second) Fail("JSON 有重复字段。");
   if(event==Json::parse_event_t::object_end) keys.pop_back();
   return true;
  };
  return Json::parse(s,cb);
 } catch(...) { Fail("JSON 格式无效、含重复字段或嵌套过深。"); }
 return {};
}
static std::string Required(const Json& j,const char* key) {
 if(!j.contains(key)||!j[key].is_string()||j[key].get_ref<const std::string&>().empty())
  Fail("缺少必需字段或字段不是非空字符串（id_token/access_token/refresh_token/account_id/last_refresh）。");
 return j[key].get<std::string>();
}
static bool Timestamp(const std::string& s) {
 if(s.size()<20 || s[4]!='-' || s[7]!='-' || s[10]!='T' || s[13]!=':' || s[16]!=':') return false;
 auto num=[&](size_t p,size_t n) { int v=0; for(size_t i=p;i<p+n;i++) { if(s[i]<'0'||s[i]>'9') return -1; v=v*10+s[i]-'0'; } return v; };
 SYSTEMTIME t{}; t.wYear=(WORD)num(0,4); t.wMonth=(WORD)num(5,2); t.wDay=(WORD)num(8,2);
 t.wHour=(WORD)num(11,2); t.wMinute=(WORD)num(14,2); t.wSecond=(WORD)num(17,2); FILETIME ft{};
 if(!SystemTimeToFileTime(&t,&ft)) return false;
 size_t pos=19; if(s[pos]=='.') { ++pos; size_t begin=pos; while(pos<s.size() && isdigit((unsigned char)s[pos])) ++pos; if(pos==begin) return false; }
 if(pos>=s.size()) return false;
 if(s[pos]=='Z') return pos+1==s.size();
 return (s[pos]=='+'||s[pos]=='-') && pos+6==s.size() && s[pos+3]==':' && num(pos+1,2)>=0 && num(pos+1,2)<=23 && num(pos+4,2)>=0 && num(pos+4,2)<=59;
}
Json Convert(const Json& source) {
 if(!source.is_object()) Fail("JSON 顶层必须是对象。");
 if(source.contains("disabled") && (!source["disabled"].is_boolean() || source["disabled"].get<bool>())) Fail("该账户被标记为禁用，不能导入。");
 if(source.contains("type") && source["type"]!="codex") Fail("仅支持 type 为 codex 的账户文件。");
 const Json* tokens=&source;
 if(source.contains("tokens")) {
  if(!source["tokens"].is_object() || !source.contains("auth_mode") || source["auth_mode"]!="chatgpt") Fail("仅支持 ChatGPT token 认证格式。");
  tokens=&source["tokens"];
 }
 Json out; out["auth_mode"]="chatgpt"; out["OPENAI_API_KEY"]=nullptr;
 for(auto key:{"id_token","access_token","refresh_token","account_id"}) out["tokens"][key]=Required(*tokens,key);
 auto stamp=Required(source,"last_refresh"); if(!Timestamp(stamp)) Fail("last_refresh 必须是带时区的 ISO 8601 时间。");
 out["last_refresh"]=stamp; return out;
}
struct Security {
 PSECURITY_DESCRIPTOR sd=nullptr;
 SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES),nullptr,FALSE};
 Security() {
  HANDLE token=nullptr; if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) Fail("无法获取 Windows 用户身份。");
  DWORD n=0; GetTokenInformation(token,TokenUser,nullptr,0,&n); std::vector<BYTE> data(n);
  BOOL ok=GetTokenInformation(token,TokenUser,data.data(),n,&n); CloseHandle(token); if(!ok) Fail("无法获取用户 SID。");
  LPWSTR sid=nullptr; if(!ConvertSidToStringSidW(((TOKEN_USER*)data.data())->User.Sid,&sid)) Fail("无法获取用户 SID。");
  std::wstring desc=L"D:P(A;;FA;;;SY)(A;;FA;;;"+std::wstring(sid)+L")"; LocalFree(sid);
  if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(desc.c_str(),SDDL_REVISION_1,&sd,nullptr)) Fail("无法创建私有文件权限。"); sa.lpSecurityDescriptor=sd;
 }
 ~Security(){if(sd) LocalFree(sd);}
};
static void NoLink(const fs::path& p) {
 auto q=fs::absolute(p);
 for(auto current=q; !current.empty(); current=current.parent_path()) {
  DWORD a=GetFileAttributesW(current.c_str()); if(a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_REPARSE_POINT)) Fail("为保护凭据，目标路径不能包含符号链接或目录联接。");
  if(current==current.parent_path()) break;
 }
}
void PrivateDirectory(const fs::path& p) {
 NoLink(p); fs::create_directories(p); Security sec;
 if(!SetFileSecurityW(p.c_str(),DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,sec.sd)) Fail("无法设置私有目录权限。");
}
std::wstring NewId() { GUID id{}; if(FAILED(CoCreateGuid(&id))) Fail("无法生成文件标识。"); wchar_t b[40]{}; StringFromGUID2(id,b,40); return b; }
void AtomicWrite(const fs::path& p,const std::string& data) {
 NoLink(p); fs::create_directories(p.parent_path()); Security sec;
 fs::path temp=p.parent_path()/(NewId()+L".tmp");
 HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,&sec.sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(f==INVALID_HANDLE_VALUE) Fail("无法创建私有临时文件。");
 DWORD written=0; bool ok=WriteFile(f,data.data(),(DWORD)data.size(),&written,nullptr) && written==data.size() && FlushFileBuffers(f); CloseHandle(f);
 if(!ok || !MoveFileExW(temp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) { DeleteFileW(temp.c_str()); Fail("写入失败；原文件未被替换。"); }
}
void SaveEncrypted(const fs::path& p,const Json& j) {
 auto plain=j.dump(); DATA_BLOB input{(DWORD)plain.size(),(BYTE*)plain.data()},out{};
 if(!CryptProtectData(&input,L"Codex Account Switcher",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)) { SecureZeroMemory(plain.data(),plain.size()); Fail("DPAPI 加密失败。"); }
 SecureZeroMemory(plain.data(),plain.size());
 std::string cipher((char*)out.pbData,out.cbData); LocalFree(out.pbData); AtomicWrite(p,cipher);
}
Json LoadEncrypted(const fs::path& p) {
 auto cipher=Read(p); DATA_BLOB input{(DWORD)cipher.size(),(BYTE*)cipher.data()},out{};
 if(!CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)) Fail("无法解密账户；请使用原 Windows 用户重新导入。");
 std::string plain((char*)out.pbData,out.cbData); SecureZeroMemory(out.pbData,out.cbData); LocalFree(out.pbData);
 try { auto j=Parse(plain); SecureZeroMemory(plain.data(),plain.size()); return j; }
 catch(...) { SecureZeroMemory(plain.data(),plain.size()); throw; }
}
Store::Store(fs::path r,fs::path a):root(std::move(r)),auth(std::move(a)) { PrivateDirectory(root); Reload(); }
void Store::Reload() {
 profiles.clear(); unreadable=0;
 for(auto& e:fs::directory_iterator(root)) if(e.path().extension()==L".dpapi" && e.path().filename()!=L"previous.dpapi") {
  try {
  auto j=LoadEncrypted(e.path()); auto a=Convert(j.at("auth"));
  profiles.push_back({e.path(),Wide(j.at("label").get<std::string>()),a["tokens"]["account_id"].get<std::string>()});
  }catch(...){++unreadable;}
 }
 std::sort(profiles.begin(),profiles.end(),[](auto& a,auto& b){return a.label<b.label;});
}
void Store::Put(const Json& j,const std::wstring& label) {
 std::string id=j["tokens"]["account_id"]; fs::path p=root/(NewId()+L".dpapi");
 for(auto& profile:profiles) if(profile.account==id) { p=profile.path; break; }
 SaveEncrypted(p,Json{{"label",Utf8(label)},{"auth",j}}); Reload();
}
void Store::Import(const fs::path& p) {
 auto source=Parse(Read(p)); auto j=Convert(source);
 std::wstring label=L"账户 "+Wide(j["tokens"]["account_id"].get<std::string>());
 if(source.contains("email")&&source["email"].is_string()) label=Wide(source["email"].get<std::string>());
 Put(j,label);
}
void Store::Capture() { auto j=Convert(Parse(Read(auth))); Put(j,L"当前账户 "+Wide(j["tokens"]["account_id"].get<std::string>())); }
void Store::KeepCurrent(const std::string& selectedAccount) {
 if(!fs::exists(auth)) return;
 auto raw=Read(auth); auto original=Parse(raw); SaveEncrypted(root/L"previous.dpapi",Json{{"raw",raw}});
 // Preserve refreshed tokens before leaving the account; also import an unlisted ChatGPT account.
 if(original.contains("tokens") && original.contains("auth_mode") && original["auth_mode"]=="chatgpt") {
  auto j=Convert(original); auto id=j["tokens"]["account_id"].get<std::string>();
  auto label=L"已有账户 "+Wide(id); for(auto& profile:profiles) if(profile.account==id) {label=profile.label;break;}
  if(id!=selectedAccount) Put(j,label);
 }
}
void Store::Activate(size_t index) {
 if(index>=profiles.size()) Fail("请先选择账户。");
 fs::path selected=profiles[index].path;
 auto j=Convert(LoadEncrypted(selected).at("auth")); KeepCurrent(j["tokens"]["account_id"].get<std::string>()); auto plain=j.dump(2)+"\n";
 try { AtomicWrite(auth,plain); } catch(...) { SecureZeroMemory(plain.data(),plain.size()); throw; }
 SecureZeroMemory(plain.data(),plain.size());
}
void Store::Restore() {
 auto saved=LoadEncrypted(root/L"previous.dpapi"); auto raw=saved.at("raw").get<std::string>(); Parse(raw);
 KeepCurrent(); AtomicWrite(auth,raw); SecureZeroMemory(raw.data(),raw.size());
}
int SelfTest() {
 fs::path root;
 try {
  wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH,temp); root=fs::path(temp)/(L"CodexSwitcher-test-"+NewId());
  Store store(root/L"vault",root/L"codex"/L"auth.json");
  Json flat={{"id_token","synthetic-id"},{"access_token","synthetic-access"},{"refresh_token","synthetic-refresh"},{"account_id","synthetic-A"},{"last_refresh","2026-09-26T15:02:47+08:00"},{"email","test@example.invalid"},{"type","codex"}};
  auto converted=Convert(flat); if(converted.size()!=4||converted["tokens"].size()!=4||converted["tokens"]["access_token"]!=flat["access_token"]||!converted["OPENAI_API_KEY"].is_null()) Fail("mapping");
  if(Convert(converted)!=converted) Fail("roundtrip");
  int rejected=0;
  for(int i=0;i<7;i++) { auto bad=flat; if(i==0) bad.erase("access_token"); if(i==1) bad["account_id"]=42; if(i==2) bad["disabled"]=true; if(i==3) bad["type"]="other"; if(i==4) bad["last_refresh"]="2026-02-30T25:00:00Z"; if(i==5) bad["id_token"]=""; if(i==6)bad["last_refresh"]="2026-02-30T15:00:00Z"; try{Convert(bad);}catch(...){++rejected;} }
  try{Parse("{\"a\":1,\"a\":2}");}catch(...){++rejected;}
  if(rejected!=8) Fail("validation");
  AtomicWrite(root/L"input.json",flat.dump()); store.Import(root/L"input.json"); store.Import(root/L"input.json"); if(store.profiles.size()!=1) Fail("dedup");
  if(Read(store.profiles[0].path).find("synthetic-access")!=std::string::npos) Fail("encryption");
  store.Activate(0); if(Parse(Read(store.auth))!=converted) Fail("activation");
  flat["access_token"]="synthetic-reimport"; AtomicWrite(root/L"input.json",flat.dump()); store.Import(root/L"input.json"); store.Activate(0);
  if(Parse(Read(store.auth))["tokens"]["access_token"]!="synthetic-reimport") Fail("same-account import");
  converted["tokens"]["access_token"]="synthetic-refreshed"; AtomicWrite(store.auth,converted.dump());
  flat["account_id"]="synthetic-B"; AtomicWrite(root/L"input.json",flat.dump()); store.Import(root/L"input.json");
  size_t b=0; for(size_t i=0;i<store.profiles.size();++i) if(store.profiles[i].account=="synthetic-B") b=i;
  store.Activate(b); if(Parse(Read(store.auth))["tokens"]["account_id"]!="synthetic-B") Fail("switch");
  store.Restore(); if(Parse(Read(store.auth))["tokens"]["access_token"]!="synthetic-refreshed") Fail("restore");
  bool failed=false; try{AtomicWrite(root/L"vault", "bad");}catch(...){failed=true;} if(!failed || Parse(Read(store.auth))!=converted) Fail("atomic failure");
  AtomicWrite(root/L"vault"/L"corrupt.dpapi","invalid"); store.Reload();if(store.unreadable!=1||store.profiles.size()!=2)Fail("corrupt profile isolation");
  fs::remove_all(root); return 0;
 }catch(...) { if(!root.empty()) { std::error_code ec; fs::remove_all(root,ec); } return 1; }
}
