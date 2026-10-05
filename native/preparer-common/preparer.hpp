#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace preparer {
namespace fs = std::filesystem;
enum class Action { Install, Prepare, Play, Rollback, Uninstall, Recover, Patch4GB, RestoreExe };
enum class State { Completed, Cancelled, Failed };
struct Cancelled : std::runtime_error { using std::runtime_error::runtime_error; };
struct PathField { std::string id; std::wstring label; fs::path value; bool editable = true; };
struct Feature { std::string id; std::wstring label; unsigned group = 0; bool value = false; };
struct View {
    fs::path game, executable;
    std::wstring gameName, note;
    std::vector<PathField> paths;
    std::vector<Feature> features;
    // Immutable backend settings/identity snapshot. The shell never interprets it.
    std::shared_ptr<const void> snapshot;
};
struct Product {
    std::wstring windowTitle, heading, introduction;
    std::wstring groups[2] = {L"Loading improvements", L"Optional gameplay changes"};
    bool multipleGames = false;
};
struct Progress { std::wstring stage, detail, unit; uint64_t completed = 0, total = 0; };
struct Outcome { State state = State::Completed; std::wstring summary, detail; };
struct Capability { Action action; bool enabled = true; };
using Report = std::function<void(const Progress&)>;
using Cancel = std::function<bool()>;
using Launcher = std::function<void(const fs::path&, const fs::path&)>;
class Adapter {
public:
    virtual ~Adapter() = default;
    virtual Product Describe() const = 0;
    virtual std::vector<fs::path> Detect() = 0;
    virtual View Open(const fs::path&) = 0;
    virtual std::vector<Capability> Actions(const View&) const = 0;
    virtual void Changed(View&, const std::string&) {}
    virtual Outcome Execute(Action, const View&, const Report&, const Cancel&) = 0;
};
std::wstring Wide(const std::string&);
const wchar_t* Label(Action);
bool SameChoices(const View&, const View&);
// Owns operation lifetime; UI calls Finish only after Ready. No detached threads.
class Session {
    Adapter& adapter;
    Launcher launcher;
    View saved;
    std::thread worker;
    std::atomic<bool> cancel{false}, ready{false};
    mutable std::mutex mutex;
    Progress progress;
    Outcome outcome;
public:
    View view;
    bool busy = false, closeAfter = false;
    Action action = Action::Install;
    Session(Adapter& a, Launcher l) : adapter(a), launcher(std::move(l)) {}
    ~Session();
    void Select(View);
    void DiscardChoices();
    bool Dirty() const { return !SameChoices(view, saved); }
    bool Available(Action) const;
    bool Begin(Action);
    void RequestClose(bool windowClose);
    bool Ready() const { return ready.load(); }
    bool Cancelling() const { return cancel.load(); }
    Progress Latest() const;
    Outcome Finish();
};
// Computes logical layout height; used by the Win32 UI and fixture tests.
int ContentHeight(const View&, size_t maintenanceActions, int logicalWidth);
int RunGui(Adapter&, const View* initial = nullptr, int show = 1);
} // namespace preparer
