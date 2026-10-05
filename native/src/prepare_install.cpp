#include "prepare_install_internal.hpp"
#include "proxy_registry.hpp"
#include <tlhelp32.h>
#include <algorithm>
#include <exception>
#include <objbase.h>
#include <sstream>
#pragma comment(lib, "ole32.lib")

namespace vii::prepare::install {
namespace {
struct Handle {
    HANDLE value;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
void CheckNode(const fs::path& path) {
    auto attributes = GetFileAttributesW(Native(path).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        auto error = GetLastError();
        Need(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND,
             "Cannot inspect path: " + Utf8(path.wstring()));
    } else Need(!(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
                "Linked installer path refused: " + Utf8(path.wstring()));
}
bool Below(const fs::path& root, const fs::path& path) {
    auto r = Normal(root).wstring();
    auto p = Normal(path).wstring();
    if (Same(root, path)) return true;
    if (r.back() != L'\\') r += L'\\';
    return p.size() > r.size() && CompareStringOrdinal(r.c_str(), int(r.size()),
        p.c_str(), int(r.size()), TRUE) == CSTR_EQUAL;
}
void Publish(const fs::path& from, const fs::path& to) {
    Need(MoveFileExW(Native(from).c_str(), Native(to).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
         "Cannot publish file: " + Utf8(to.wstring()));
}
void Remove(const fs::path& path, const std::string& hash) {
    Need(HashPath(path) == hash, "File changed before removal: " + Utf8(path.wstring()));
    if (!hash.empty()) Need(DeleteFileW(Native(path).c_str()) != 0, "Cannot remove file: " + Utf8(path.wstring()));
}
bool DigestText(const std::string& value) {
    return value.empty() || (value.size() == 64 && std::all_of(value.begin(), value.end(),
        [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }));
}
fs::path Pending(const fs::path& game) { return SafeBelow(game, game / L"vii-prepare-pending.ini"); }
std::wstring Section(size_t index) { return L"File" + std::to_wstring(index); }
void Restore(const fs::path& game, const Snapshot& snapshot, size_t index) {
    const auto target = SafeBelow(game, game / Names[index]);
    auto actual = HashPath(target);
    if (actual == snapshot.before[index]) return; // Idempotent after interrupted recovery.
    Need(actual == snapshot.after[index], "Changed file prevents recovery: " + Utf8(target.wstring()));
    if (snapshot.before[index].empty()) Remove(target, actual);
    else {
        const auto source = SafeBelow(game, Before(snapshot, index));
        Need(HashPath(source) == snapshot.before[index], "Recovery backup changed: " + Utf8(source.wstring()));
        auto staged = SafeBelow(game, snapshot.directory / (L"restore-" + Stamp() + L".stage"));
        Copy(source, staged);
        Need(HashPath(target) == actual, "File changed during recovery");
        Publish(staged, target);
    }
    Need(HashPath(target) == snapshot.before[index], "Restored file hash mismatch");
}
std::string RestoreAll(const fs::path& game, const Snapshot& snapshot) {
    std::string errors;
    for (size_t n = Names.size(); n != 0; --n) {
        try {
            Failpoint(("recover-" + std::to_string(n - 1)).c_str());
            CheckGame(game);
            Restore(game, snapshot, n - 1);
        } catch (const std::exception& e) { errors += std::string("\n") + e.what(); }
    }
    return errors;
}
std::string FileText(const fs::path& file) {
    Handle h(CreateFileW(Native(file).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    Need(bool(h), "Cannot read transaction completion record");
    char data[65]{}; DWORD read = 0;
    Need(ReadFile(h.value, data, sizeof(data), &read, nullptr) && read == 64,
         "Invalid transaction completion record");
    return {data, read};
}
}
void Need(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
std::string Hex(const Digest& digest) {
    const char* digits = "0123456789abcdef";
    std::string result;
    for (auto b : digest) { result += digits[b >> 4]; result += digits[b & 15]; }
    return result;
}
std::string Utf8(const std::wstring& text) {
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    Need(n != 0 || text.empty(), "Invalid path encoding");
    std::string out(size_t(n), ' ');
    if (n) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::wstring Wide(const std::string& text) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    Need(n != 0 || text.empty(), "Invalid text encoding");
    std::wstring out(size_t(n), L' ');
    if (n) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), out.data(), n);
    return out;
}
std::string HashPath(const fs::path& path) {
    CheckNode(path);
    if (!fs::exists(Native(path))) return {};
    Need(fs::is_regular_file(Native(path)), "Expected a file: " + Utf8(path.wstring()));
    return Hex(HashFile(Native(path).wstring()));
}
std::wstring Ini(const fs::path& file, const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
    wchar_t text[32768]{};
    auto size = GetPrivateProfileStringW(section, key, fallback, text, 32768, Native(file).c_str());
    Need(size < 32767, "INI value exceeds supported length");
    return text;
}
void Set(const fs::path& file, const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    Need(WritePrivateProfileStringW(section, key, value.c_str(), Native(file).c_str()) != 0, "Cannot update preparation INI");
}
void Write(const fs::path& path, const void* bytes, size_t size) {
    Need(size <= MAXDWORD, "Preparation file exceeds size limit");
    CheckNode(path);
    Handle h(CreateFileW(Native(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    Need(bool(h), "Cannot create preparation file: " + Utf8(path.wstring()));
    DWORD written = 0;
    Need(WriteFile(h.value, bytes, DWORD(size), &written, nullptr) && written == size && FlushFileBuffers(h.value),
         "Cannot write preparation file: " + Utf8(path.wstring()));
}
void NewIni(const fs::path& path) { const unsigned char bom[] = {0xff, 0xfe}; Write(path, bom, sizeof(bom)); }
void Flush(const fs::path& path) {
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, Native(path).c_str());
    Handle h(CreateFileW(Native(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    Need(bool(h) && FlushFileBuffers(h.value), "Cannot flush preparation file");
}
void Copy(const fs::path& from, const fs::path& to) {
    CheckNode(from); CheckNode(to);
    auto expected = HashPath(from);
    Need(!expected.empty(), "Copy source missing");
    if (!CopyFileW(Native(from).c_str(), Native(to).c_str(), TRUE)) {
        const auto error = GetLastError();
        throw std::runtime_error("Cannot copy preparation file (Windows error " + std::to_string(error) + "): " + Utf8(to.wstring()));
    }
    Flush(to);
    Need(HashPath(to) == expected && HashPath(from) == expected, "Copied file hash mismatch");
}
std::wstring Stamp() {
    GUID id{}; Need(SUCCEEDED(CoCreateGuid(&id)), "Cannot create transaction identifier");
    wchar_t text[40]{}; StringFromGUID2(id, text, 40);
    return std::wstring(text + 1, 36);
}
bool Same(const fs::path& a, const fs::path& b) {
    auto x = Normal(a).wstring(), y = Normal(b).wstring();
    while (x.size() > 3 && x.back() == L'\\') x.pop_back();
    while (y.size() > 3 && y.back() == L'\\') y.pop_back();
    return CompareStringOrdinal(x.c_str(), -1, y.c_str(), -1, TRUE) == CSTR_EQUAL;
}
fs::path Normal(const fs::path& path) {
    auto text = path.wstring();
    if (text.rfind(L"\\\\?\\UNC\\", 0) == 0) text = L"\\\\" + text.substr(8);
    else if (text.rfind(L"\\\\?\\", 0) == 0) text = text.substr(4);
    return fs::absolute(text).lexically_normal();
}
fs::path Native(const fs::path& path) {
    auto text = Normal(path).wstring();
    if (text.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + text.substr(2);
    return L"\\\\?\\" + text;
}
fs::path SafeBelow(const fs::path& root, const fs::path& path) {
    auto boundary = Normal(root), result = Normal(path);
    Need(Below(boundary, result), "Installer path escapes its selected folder");
    for (auto p = result;; p = p.parent_path()) {
        CheckNode(p);
        if (Same(p, boundary)) break;
        Need(p.has_parent_path() && p != p.parent_path(), "Invalid installer path boundary");
    }
    return result;
}
void CheckOutput(const fs::path& game, const fs::path& output) {
    auto absolute = fs::absolute(output).lexically_normal();
    if (Below(game, absolute)) SafeBelow(game, absolute);
    else CheckNode(absolute); // Linked library parents above the selected root are supported.
    const auto physical = Normal(fs::weakly_canonical(Native(absolute)));
    Need(!Same(physical, game) && !Below(physical, game) &&
         !Below(game / L"CONTENTS", physical) && !Below(game / L"DLC", physical) &&
         !Below(game / L"vii-prepare-backups", physical), "Output overlaps game source or installer files");
    Need(!fs::exists(Native(absolute)) || fs::is_directory(Native(absolute)), "Output path is not a directory");
}
void CheckGame(const fs::path& game) {
    auto exe = SafeBelow(game, game / L"NeptuniaVII.exe");
    Need(HashPath(exe) == Baseline, "This game executable is not the supported Neptunia VII build");
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    Need(bool(snapshot), "Cannot check running game processes");
    PROCESSENTRY32W entry{sizeof(entry)};
    if (!Process32FirstW(snapshot.value, &entry)) {
        Need(GetLastError() == ERROR_NO_MORE_FILES, "Cannot enumerate running game processes");
        return;
    }
    do {
        if (_wcsicmp(entry.szExeFile, L"NeptuniaVII.exe")) continue;
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
        Need(bool(process), "Cannot verify a running game's path; close it before continuing");
        wchar_t path[32768]{}; DWORD size = 32768;
        Need(QueryFullProcessImageNameW(process.value, 0, path, &size) != 0, "Cannot verify a running game's path");
        std::error_code error;
        bool same = fs::equivalent(exe, path, error);
        Need(!error, "Cannot identify a running game executable");
        Need(!same, "Close the selected Neptunia VII installation before changing it");
    } while (Process32NextW(snapshot.value, &entry));
    Need(GetLastError() == ERROR_NO_MORE_FILES, "Cannot finish enumerating running games");
}
std::vector<unsigned char> Resource(int number) {
    auto module = GetModuleHandleW(nullptr);
    auto resource = FindResourceW(module, MAKEINTRESOURCEW(number), RT_RCDATA);
    Need(resource != nullptr, "Preparer resource missing");
    auto loaded = LoadResource(module, resource);
    auto data = loaded ? static_cast<const unsigned char*>(LockResource(loaded)) : nullptr;
    auto size = SizeofResource(module, resource);
    Need(data && size, "Cannot read preparer resource");
    return {data, data + size};
}
std::set<std::string> KnownProxies() {
    auto bytes = Resource(102);
    return ParseProxyRegistry(std::string(bytes.begin(), bytes.end()));
}
void ValidateProxy(const fs::path& path) {
    const auto actual = HashPath(path);
    Need(actual.empty() || KnownProxies().count(actual),
         "Unrecognized dinput8.dll; it has been left untouched. If it belongs to a newer VII patch, use that release's preparer. Otherwise, resolve the mod conflict before installing.");
}
void Failpoint(const char* point) {
#ifdef VII_PREPARE_TEST
    char text[128]{};
    auto n = GetEnvironmentVariableA("VII_PREPARE_FAILPOINT", text, sizeof(text));
    const std::string value = n && n < sizeof(text) ? text : "";
    if (value == std::string("crash:") + point) TerminateProcess(GetCurrentProcess(), 73);
    if (value == std::string("pause:") + point) {
        const char message[] = "test-paused\n"; DWORD written = 0;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, sizeof(message) - 1, &written, nullptr);
        Sleep(4000);
    }
    if (value == point) throw std::runtime_error(std::string("Injected failure: ") + point);
    // A separate switch allows a publication failure plus a recovery failure.
    n = GetEnvironmentVariableA("VII_PREPARE_RECOVERY_FAILPOINT", text, sizeof(text));
    if (n && n < sizeof(text) && std::string(text) == point)
        throw std::runtime_error(std::string("Injected recovery failure: ") + point);
#else
    (void)point;
#endif
}
Session::Session(const fs::path& selected, bool allowPending) : game(GameFolder(selected)) {
    CheckGame(game);
    SafeBelow(game, game / L"CONTENTS"); SafeBelow(game, game / L"DLC");
    Need(game.filename().wstring().find(L';') == std::wstring::npos,
         "A semicolon in the game folder name is not supported by this VII installer; select a game folder without one");
    Handle exe(CreateFileW(Native(game / L"NeptuniaVII.exe").c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, 0, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    Need(bool(exe) && GetFileInformationByHandle(exe.value, &info), "Cannot identify selected installation");
    auto name = L"Local\\VII_Prepare_" + std::to_wstring(info.dwVolumeSerialNumber) + L"_" +
        std::to_wstring(info.nFileIndexHigh) + L"_" + std::to_wstring(info.nFileIndexLow);
    mutex_ = CreateMutexW(nullptr, FALSE, name.c_str());
    try {
        Need(mutex_ != nullptr, "Cannot lock selected installation");
        auto waited = WaitForSingleObject(mutex_, 0);
        owned_ = waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED;
        Need(owned_, "Another preparer is already working on this installation");
        SafeBelow(game, game / L"vii-prepare-backups");
        const auto pending = Pending(game);
        Need(allowPending || HashPath(pending).empty(),
             "An interrupted update needs recovery. Use Recover interrupted update or --recover. Keep backups in " + Utf8((game / L"vii-prepare-backups").wstring()));
        auto bytes = Resource(101);
        Need(KnownProxies().count(Hex(Hash(bytes.data(), bytes.size()))) != 0,
             "Bundled DLL is missing from the known-proxy registry");
        ValidateProxy(SafeBelow(game, game / Names[0]));
        for (size_t i = 0; i < Names.size(); ++i) observed[i] = HashPath(SafeBelow(game, game / Names[i]));
        Failpoint("after-lock");
    } catch (...) {
        if (owned_) ReleaseMutex(mutex_);
        if (mutex_) CloseHandle(mutex_);
        mutex_ = nullptr; owned_ = false;
        throw;
    }
}
Session::~Session() { if (owned_) ReleaseMutex(mutex_); if (mutex_) CloseHandle(mutex_); }
void Session::CheckUnchanged() const {
    CheckGame(game);
    for (size_t i = 0; i < Names.size(); ++i)
        Need(HashPath(SafeBelow(game, game / Names[i])) == observed[i], "Installed files changed during preparation; retry with the new settings");
}
fs::path Before(const Snapshot& s, size_t i) { return s.directory / (L"before-" + std::wstring(Names.at(i))); }
fs::path After(const Snapshot& s, size_t i) { return s.directory / (L"after-" + std::wstring(Names.at(i))); }
Snapshot ReadSnapshot(const fs::path& game, const fs::path& directory, bool completed) {
    Snapshot result; result.directory = SafeBelow(game, directory);
    Need(Same(directory.parent_path(), game / L"vii-prepare-backups") && fs::is_directory(Native(directory)),
         "Backup is not a direct transaction folder in this installation");
    auto manifest = SafeBelow(game, directory / L"snapshot.ini");
    Need(!HashPath(manifest).empty(), "Snapshot manifest missing");
    if (completed) {
        auto marker = SafeBelow(game, directory / L"committed.txt");
        Need(!HashPath(marker).empty() && FileText(marker) == HashPath(manifest),
             "Snapshot is not a verified completed transaction");
    }
    Need(Ini(manifest, L"Transaction", L"Version") == L"1" &&
         Same(Ini(manifest, L"Transaction", L"GameDirectory"), game), "Snapshot targets a different installation or format");
    result.operation = Ini(manifest, L"Transaction", L"Operation");
    Need(result.operation == L"Install" || result.operation == L"Rollback" || result.operation == L"Uninstall", "Unknown transaction operation");
    for (size_t i = 0; i < Names.size(); ++i) {
        auto section = Section(i);
        Need(Same(Ini(manifest, section.c_str(), L"Target"), game / Names[i]), "Snapshot file target mismatch");
        result.before[i] = Utf8(Ini(manifest, section.c_str(), L"BeforeSHA256"));
        result.after[i] = Utf8(Ini(manifest, section.c_str(), L"AfterSHA256"));
        Need(DigestText(result.before[i]) && DigestText(result.after[i]), "Malformed snapshot hash");
        SafeBelow(game, Before(result, i)); SafeBelow(game, After(result, i));
    }
    const auto known = KnownProxies();
    Need((result.before[0].empty() || known.count(result.before[0])) &&
         (result.after[0].empty() || known.count(result.after[0])), "Snapshot contains an unrecognized proxy");
    return result;
}
void VerifySnapshotFiles(const Snapshot& s) {
    for (size_t i = 0; i < Names.size(); ++i) {
        Need(HashPath(Before(s, i)) == s.before[i], "Prior transaction backup changed or is missing");
        Need(HashPath(After(s, i)) == s.after[i], "Staged transaction backup changed or is missing");
    }
}
void Recover(Session& session) {
    const auto pending = Pending(session.game);
    const auto pendingHash = HashPath(pending);
    Need(!pendingHash.empty(), "No interrupted update is recorded");
    const auto leaf = Ini(pending, L"Transaction", L"Backup");
    Need(!leaf.empty() && fs::path(leaf).filename() == fs::path(leaf) && leaf != L"." && leaf != L"..",
         "Invalid recovery backup reference");
    auto snapshot = ReadSnapshot(session.game, session.game / L"vii-prepare-backups" / leaf);
    auto expectedManifest = Utf8(Ini(pending, L"Transaction", L"ManifestSHA256"));
    Need(expectedManifest.size() == 64 && HashPath(snapshot.directory / L"snapshot.ini") == expectedManifest,
         "Recovery manifest changed");
    VerifySnapshotFiles(snapshot);
    session.CheckUnchanged();
    const auto committed = SafeBelow(session.game, snapshot.directory / L"committed.txt");
    if (!HashPath(committed).empty()) {
        Need(FileText(committed) == expectedManifest, "Transaction completion record changed");
        for (size_t i = 0; i < Names.size(); ++i)
            Need(HashPath(session.game / Names[i]) == snapshot.after[i], "Completed update files changed; retain its recovery evidence");
    } else {
        const auto errors = RestoreAll(session.game, snapshot);
        Need(errors.empty(), "Recovery requires attention. Backups: " + Utf8(snapshot.directory.wstring()) + errors);
    }
    Remove(pending, pendingHash);
}
Transaction::Transaction(Session& session, const wchar_t* operation) : session_(session) {
    session.CheckUnchanged();
    snapshot.operation = operation;
    snapshot.before = session.observed;
    auto root = SafeBelow(session.game, session.game / L"vii-prepare-backups");
    fs::create_directories(Native(root));
    snapshot.directory = SafeBelow(session.game, root / Stamp());
    Need(fs::create_directory(Native(snapshot.directory)), "Transaction backup already exists");
    for (size_t i = 0; i < Names.size(); ++i)
        if (!snapshot.before[i].empty()) {
            Copy(SafeBelow(session.game, session.game / Names[i]), Before(snapshot, i));
            Need(HashPath(Before(snapshot, i)) == snapshot.before[i], "Installed file changed while backing up");
        }
}
void Transaction::Keep(size_t i) { if (!snapshot.before.at(i).empty()) Copy(Before(snapshot, i), Stage(i)); }
void Transaction::Commit() {
    const auto game = session_.game;
    auto manifest = SafeBelow(game, snapshot.directory / L"snapshot.ini");
    NewIni(manifest);
    Set(manifest, L"Transaction", L"Version", L"1");
    Set(manifest, L"Transaction", L"Operation", snapshot.operation);
    Set(manifest, L"Transaction", L"GameDirectory", game.wstring());
    for (size_t i = 0; i < Names.size(); ++i) {
        snapshot.after[i] = HashPath(SafeBelow(game, Stage(i)));
        const auto section = Section(i);
        Set(manifest, section.c_str(), L"Target", (game / Names[i]).wstring());
        Set(manifest, section.c_str(), L"BeforeSHA256", Wide(snapshot.before[i]));
        Set(manifest, section.c_str(), L"AfterSHA256", Wide(snapshot.after[i]));
    }
    Flush(manifest);
    const auto manifestHash = HashPath(manifest);
    VerifySnapshotFiles(ReadSnapshot(game, snapshot.directory));
    session_.CheckUnchanged();
    auto pending = Pending(game), stagedPending = SafeBelow(game, snapshot.directory / L"pending.stage");
    Need(HashPath(pending).empty(), "An interrupted update already needs recovery");
    NewIni(stagedPending);
    Set(stagedPending, L"Transaction", L"Backup", snapshot.directory.filename().wstring());
    Set(stagedPending, L"Transaction", L"ManifestSHA256", Wide(manifestHash));
    Flush(stagedPending);
    Publish(stagedPending, pending);
    const auto pendingHash = HashPath(pending);
    bool committedRecorded = false;
    try {
        Failpoint("before-publish");
        for (size_t i = 0; i < Names.size(); ++i) {
            CheckGame(game);
            const auto target = SafeBelow(game, game / Names[i]);
            Need(HashPath(target) == snapshot.before[i], "Installed file changed before publication");
            if (snapshot.after[i] != snapshot.before[i]) {
                if (snapshot.after[i].empty()) Remove(target, snapshot.before[i]);
                else {
                    auto staged = SafeBelow(game, snapshot.directory / (L"publish-" + std::to_wstring(i) + L".stage"));
                    Copy(Stage(i), staged);
                    Need(HashPath(staged) == snapshot.after[i], "Staged publication hash mismatch");
                    Need(HashPath(target) == snapshot.before[i], "Installed file changed during staging");
                    Publish(staged, target);
                }
            }
            Need(HashPath(target) == snapshot.after[i], "Published file hash mismatch");
            Failpoint(i == 0 ? "after-proxy" : i == 1 ? "after-config" : "after-state");
        }
        Write(SafeBelow(game, snapshot.directory / L"committed.txt"), manifestHash.data(), manifestHash.size());
        committedRecorded = true;
        Failpoint("after-commit");
        Remove(pending, pendingHash);
    } catch (...) {
        auto original = std::current_exception();
        std::string reason;
        try { std::rethrow_exception(original); } catch (const std::exception& e) { reason = e.what(); }
        if (committedRecorded)
            throw std::runtime_error("Update committed; cleanup needs attention: " + reason +
                ". Use --recover to finish. Backups: " + Utf8(snapshot.directory.wstring()));
        auto committed = SafeBelow(game, snapshot.directory / L"committed.txt");
        std::string errors;
        try { Remove(committed, HashPath(committed)); }
        catch (const std::exception& e) { errors += std::string("\n") + e.what(); }
        errors += RestoreAll(game, snapshot);
        if (errors.empty()) {
            try { Remove(pending, pendingHash); }
            catch (const std::exception& e) { errors += std::string("\n") + e.what(); }
        }
        if (!errors.empty()) {
            throw std::runtime_error("Update failed: " + reason + ". Recovery requires attention. Backups: " +
                Utf8(snapshot.directory.wstring()) + errors);
        }
        std::rethrow_exception(original);
    }
}
}
