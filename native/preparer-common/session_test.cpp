#include "preparer.hpp"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <iostream>
using namespace preparer;
struct Fake final : Adapter {
    bool fail = false, wait = false, release = false; int installed = 0;
    std::mutex mutex; std::condition_variable condition;
    Product Describe() const override { return {L"Fictional Preparer", L"Fictional Patch", L"Fixture"}; }
    std::vector<fs::path> Detect() override { return {L"fiction-a", L"fiction-b"}; }
    View Open(const fs::path& game) override {
        View v; v.game = game; v.executable = game / L"fiction.exe";
        v.paths = {{"assets", L"Assets", game / L"assets"}};
        v.features = {{"load", L"Prepared loading", 0, false}};
        if (game == L"fiction-b") v.features.push_back({"skip", L"Skip", 1, true});
        return v;
    }
    std::vector<Capability> Actions(const View&) const override {
        return {{Action::Install}, {Action::Prepare}, {Action::Play}, {Action::Rollback, false}};
    }
    Outcome Execute(Action a, const View&, const Report& report, const Cancel& cancel) override {
        if (fail) throw std::runtime_error("Fixture failure");
        report({L"Preparing", L"Fictional data", L"files", 2, 5});
        if (a == Action::Prepare) {
            std::unique_lock<std::mutex> lock(mutex);
            while (wait && !release && !cancel()) condition.wait_for(lock, std::chrono::milliseconds(5));
            if (cancel()) throw Cancelled("Fixture cancelled at safe boundary");
        } else {
            std::unique_lock<std::mutex> lock(mutex);
            while (wait && !release) condition.wait_for(lock, std::chrono::milliseconds(5));
            ++installed;
        }
        return {State::Completed, L"Fixture complete", L"Synthetic only"};
    }
};
void Wait(Session& s) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!s.Ready()) { assert(std::chrono::steady_clock::now() < deadline); std::this_thread::yield(); }
}
int main() {
    Fake f; int launches = 0;
    Session s(f, [&](const fs::path& exe, const fs::path& game) {
        assert(f.installed > 0); assert(exe == game / L"fiction.exe"); ++launches;
    });
    assert(!s.Begin(Action::Install)); s.Select(f.Open(L"fiction-a")); assert(!s.Dirty());
    s.view.features[0].value = true; assert(s.Dirty());
    s.DiscardChoices(); assert(!s.Dirty() && f.installed == 0);
    s.view.features[0].value = true;
    assert(!s.Begin(Action::Rollback)); assert(s.Begin(Action::Play)); assert(!s.Begin(Action::Install));
    Wait(s); assert(s.Latest().completed == 2); assert(s.Finish().state == State::Completed);
    assert(launches == 1 && !s.Dirty());
    f.fail = true; s.view.features[0].value = true; assert(s.Begin(Action::Play)); Wait(s);
    assert(s.Finish().state == State::Failed && launches == 1 && s.Dirty()); f.fail = false;
    f.wait = true; assert(s.Begin(Action::Prepare)); s.RequestClose(false); Wait(s);
    assert(s.Finish().state == State::Cancelled && !s.closeAfter && s.Dirty());
    assert(s.Begin(Action::Prepare)); s.RequestClose(true); Wait(s);
    assert(s.Finish().state == State::Cancelled && s.closeAfter);
    assert(s.Begin(Action::Play)); s.RequestClose(true); assert(!s.Cancelling());
    { std::lock_guard<std::mutex> lock(f.mutex); f.release = true; } f.condition.notify_all(); Wait(s);
    assert(s.Finish().state == State::Completed && launches == 1 && s.closeAfter);
    Session badLaunch(f, [](const fs::path&, const fs::path&) { throw std::runtime_error("Launch failed"); });
    badLaunch.Select(f.Open(L"fiction-b")); assert(badLaunch.Begin(Action::Play)); Wait(badLaunch);
    assert(badLaunch.Finish().state == State::Failed);
    View layout = f.Open(L"fiction-b");
    assert(ContentHeight(layout, 5, 500) > ContentHeight(layout, 5, 780));
    // Destruction requests preparation cancellation and joins a live worker.
    f.release = false;
    { Session owned(f, [](const fs::path&, const fs::path&) {}); owned.Select(f.Open(L"fiction-a")); assert(owned.Begin(Action::Prepare)); }
    std::cout << "PASS: routing, dirty choices, capabilities, progress, failures, cancellation, safe closure, Play and layouts\n";
}
