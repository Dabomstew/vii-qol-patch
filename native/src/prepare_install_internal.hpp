#pragma once
#include "prepare_game.hpp"
#include "patch.hpp"
#include <array>
#include <set>
#include <string>
#include <vector>

namespace vii::prepare::install {
namespace fs = std::filesystem;
inline constexpr char Baseline[] = "7ff2aad55e965add3b6fc45bd0ad2158769de014d6f619dc564a50c3e0fa42d9";
inline constexpr std::array<const wchar_t*, 3> Names = {
    L"dinput8.dll", L"vii-patches.ini", L"vii-prepare-state.ini"};
void Need(bool value, const std::string& message);
std::string Hex(const Digest& digest);
std::string HashPath(const fs::path& path); // Empty means absent, never an unreadable file.
std::wstring Wide(const std::string& text);
std::string Utf8(const std::wstring& text);
std::wstring Ini(const fs::path&, const wchar_t* section, const wchar_t* key,
                 const wchar_t* fallback = L"");
void Set(const fs::path&, const wchar_t* section, const wchar_t* key, const std::wstring& value);
void NewIni(const fs::path&);
void Write(const fs::path&, const void* bytes, size_t size);
void Copy(const fs::path& from, const fs::path& to);
void Flush(const fs::path&);
std::wstring Stamp();
bool Same(const fs::path&, const fs::path&);
fs::path Native(const fs::path&); // Extended absolute Windows path for filesystem APIs.
fs::path Normal(const fs::path&); // Absolute display/comparison path without extended prefix.
fs::path SafeBelow(const fs::path& root, const fs::path& path);
void CheckOutput(const fs::path& game, const fs::path& output);
void CheckGame(const fs::path& game);
std::vector<unsigned char> Resource(int number);
std::set<std::string> KnownProxies();
void ValidateProxy(const fs::path& path);
void Failpoint(const char* point);
void ValidateSettings(const fs::path& game, const Settings& settings);
void StageSettings(const fs::path& game, const Settings& settings, const fs::path& staged);
std::wstring CacheText(const fs::path& config);
class SettingsFile {
    std::string ownedHash_;
public:
    const fs::path path;
    explicit SettingsFile(const fs::path& game);
    void Seal();
    ~SettingsFile();
    SettingsFile(const SettingsFile&) = delete;
    SettingsFile& operator=(const SettingsFile&) = delete;
};

class Session {
    HANDLE mutex_ = nullptr;
    bool owned_ = false;
public:
    const fs::path game;
    std::array<std::string, 3> observed;
    explicit Session(const fs::path&, bool allowPending = false);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    void CheckUnchanged() const;
};

struct Snapshot {
    fs::path directory;
    std::wstring operation;
    std::array<std::string, 3> before, after;
};
Snapshot ReadSnapshot(const fs::path& game, const fs::path& directory, bool completed = false);
fs::path Before(const Snapshot&, size_t index);
fs::path After(const Snapshot&, size_t index);
void VerifySnapshotFiles(const Snapshot&);
void Recover(Session&);

class Transaction {
    Session& session_;
public:
    Snapshot snapshot;
    Transaction(Session&, const wchar_t* operation);
    fs::path Stage(size_t index) const { return After(snapshot, index); }
    void Keep(size_t index);
    void Commit();
};
}
