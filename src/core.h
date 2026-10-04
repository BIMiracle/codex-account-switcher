#pragma once
#include <windows.h>
#include <objbase.h>
#include <filesystem>
#include <string>
#include <vector>
#include "../vendor/json.hpp"
using Json = nlohmann::ordered_json;
namespace fs = std::filesystem;
std::wstring Wide(const std::string& s);
std::string Utf8(const std::wstring& s);
std::string Read(const fs::path& p);
Json Parse(const std::string& s);
Json Convert(const Json& source);
void PrivateDirectory(const fs::path& p);
void AtomicWrite(const fs::path& p, const std::string& data);
void SaveEncrypted(const fs::path& p, const Json& j);
Json LoadEncrypted(const fs::path& p);
std::wstring NewId();
struct Profile { fs::path path; std::wstring label; std::string account; };
class Store {
public:
 fs::path root, auth;
 std::vector<Profile> profiles;
 size_t unreadable=0;
 Store(fs::path rootPath, fs::path authPath);
 void Reload();
 void Import(const fs::path& p);
 void Capture();
 void Activate(size_t index);
 void Restore();
private:
 void KeepCurrent(const std::string& selectedAccount="");
 void Put(const Json& j, const std::wstring& label);
};
int SelfTest();
