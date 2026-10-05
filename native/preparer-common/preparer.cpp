#include "preparer.hpp"
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif
namespace preparer {
std::wstring Wide(const std::string& s) {
#ifdef _WIN32
    const auto count = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring result(count, L' ');
    if (count) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), result.data(), count);
    return result;
#else
    return {s.begin(), s.end()};
#endif
}
const wchar_t* Label(Action action) {
    switch (action) {
    case Action::Install: return L"Install / Update";
    case Action::Prepare: return L"Prepare / Resume";
    case Action::Play: return L"Play";
    case Action::Rollback: return L"Rollback last update";
    case Action::Uninstall: return L"Uninstall patch";
    case Action::Recover: return L"Recover interrupted update";
    case Action::Patch4GB: return L"Apply 4GB patch";
    case Action::RestoreExe: return L"Restore original EXE";
    }
    return L"";
}
bool SameChoices(const View& a, const View& b) {
    if (a.game != b.game || a.paths.size() != b.paths.size() || a.features.size() != b.features.size()) return false;
    for (size_t i = 0; i < a.paths.size(); ++i)
        if (a.paths[i].id != b.paths[i].id || a.paths[i].value != b.paths[i].value) return false;
    for (size_t i = 0; i < a.features.size(); ++i)
        if (a.features[i].id != b.features[i].id || a.features[i].value != b.features[i].value) return false;
    return true;
}
Session::~Session() { cancel = true; if (worker.joinable()) worker.join(); }
void Session::Select(View next) {
    if (busy) throw std::logic_error("Cannot change games during an operation");
    view = std::move(next); saved = view;
}
void Session::DiscardChoices() {
    if (busy) throw std::logic_error("Cannot discard choices during an operation");
    view = saved;
}
bool Session::Available(Action requested) const {
    if (view.game.empty() || busy) return false;
    for (const auto& c : adapter.Actions(view)) if (c.action == requested) return c.enabled;
    return false;
}
bool Session::Begin(Action requested) {
    if (!Available(requested)) return false;
    if (worker.joinable()) worker.join();
    const auto chosen = view;
    action = requested; cancel = false; ready = false; closeAfter = false;
    { std::lock_guard<std::mutex> lock(mutex); progress = {}; outcome = {}; }
    busy = true;
    try {
        worker = std::thread([this, chosen, requested] {
            Outcome result;
            try {
                result = adapter.Execute(requested == Action::Play ? Action::Install : requested, chosen,
                    [this](const Progress& p) { std::lock_guard<std::mutex> lock(mutex); progress = p; },
                    [this] { return cancel.load(); });
            } catch (const Cancelled& e) { result = {State::Cancelled, L"Cancelled. Completed work can be resumed.", Wide(e.what())}; }
              catch (const std::exception& e) { result = {State::Failed, L"Could not finish. See the details below.", Wide(e.what())}; }
              catch (...) { result = {State::Failed, L"Could not finish", L"An unexpected error occurred."}; }
            { std::lock_guard<std::mutex> lock(mutex); outcome = std::move(result); }
            ready = true;
        });
    } catch (...) { busy = false; throw; }
    return true;
}
void Session::RequestClose(bool windowClose) {
    if (!busy) return;
    if (action == Action::Prepare) cancel = true;
    if (windowClose || action != Action::Prepare) closeAfter = true;
}
Progress Session::Latest() const { std::lock_guard<std::mutex> lock(mutex); return progress; }
Outcome Session::Finish() {
    if (!busy || !ready) throw std::logic_error("Operation is not ready to finish");
    worker.join(); busy = false;
    Outcome result;
    { std::lock_guard<std::mutex> lock(mutex); result = outcome; }
    if (result.state == State::Completed) {
        try {
            Select(adapter.Open(view.game));
            if (action == Action::Play && !closeAfter) {
                launcher(view.executable, view.game);
                result.summary = L"Patch applied; game started.";
            }
        } catch (const std::exception& e) { result = {State::Failed, L"Finished with an error. See the details below.", Wide(e.what())}; }
    }
    return result;
}
int ContentHeight(const View& view, size_t maintenance, int width) {
    int counts[2]{};
    for (const auto& f : view.features) ++counts[f.group == 1 ? 1 : 0];
    const int featureHeight = width >= 740 ? ((counts[0] || counts[1]) ? 30 + 30 * std::max(counts[0], counts[1]) : 0) :
        (counts[0] ? 30 + 30 * counts[0] : 0) + (counts[1] ? 30 + 30 * counts[1] : 0);
    return 450 + (width < 740 ? 44 : 0) + int(view.paths.size()) * 40 + featureHeight +
        int((maintenance + (width >= 740 ? 2 : 1)) / (width >= 740 ? 3 : 2)) * 40;
}
} // namespace preparer
