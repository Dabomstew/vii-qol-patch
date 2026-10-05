#include "prepare_install_internal.hpp"

namespace vii::prepare {
using namespace install;
namespace {
void State(Transaction& transaction, bool active, const fs::path& config,
           const std::wstring& previousBackup = L"") {
    auto state = transaction.Stage(2);
    NewIni(state);
    Set(state, L"Install", L"Version", L"2");
    Set(state, L"Install", L"Active", active ? L"1" : L"0");
    Set(state, L"Install", L"GameSHA256", Wide(Baseline));
    Set(state, L"Install", L"Cache", Ini(config, L"MipCache", L"DiskDirectory", L"vii-speedrun-patch\\cache"));
    Set(state, L"Install", L"Backup", previousBackup);
    Flush(state);
}
}
std::wstring RollbackGame(const fs::path& selected) {
    Session session(selected);
    const auto& game = session.game;
    auto state = game / Names[2];
    Need(Ini(state, L"Install", L"Version") == L"2" && Ini(state, L"Install", L"Active") == L"1",
         "No verified update is recorded; legacy backups require manual review");
    const auto backup = Ini(state, L"Install", L"Backup");
    Need(!backup.empty(), "No verified update backup is recorded");
    auto previous = ReadSnapshot(game, backup, true);
    Need(previous.operation == L"Install", "Backup does not describe a patch installation");
    VerifySnapshotFiles(previous);
    Need(!session.observed[0].empty() && session.observed[0] == previous.after[0],
         "Installed DLL changed since this update; rollback has left it untouched");
    const bool restoreSettings = !previous.before[1].empty() && session.observed[1] == previous.after[1];

    // Preserve only verified earlier rollback history. Legacy state never grants ownership.
    std::wstring priorBackup;
    if (!previous.before[0].empty() && !previous.before[2].empty()) {
        auto priorState = Before(previous, 2);
        if (Ini(priorState, L"Install", L"Version") == L"2") {
            auto candidate = Ini(priorState, L"Install", L"Backup");
            if (!candidate.empty()) try {
                auto prior = ReadSnapshot(game, candidate, true);
                VerifySnapshotFiles(prior);
                if (prior.operation == L"Install" && prior.after[0] == previous.before[0]) priorBackup = candidate;
            } catch (const std::exception&) { /* Latest rollback remains valid without older history. */ }
        }
    }
    Transaction transaction(session, L"Rollback");
    if (!previous.before[0].empty()) Copy(Before(previous, 0), transaction.Stage(0));
    if (restoreSettings) Copy(Before(previous, 1), transaction.Stage(1));
    else transaction.Keep(1);
    State(transaction, !previous.before[0].empty(), transaction.Stage(1), priorBackup);
    transaction.Commit();
    return restoreSettings ? L"Patch update rolled back; previous settings restored."
                           : L"Patch update rolled back; current settings retained.";
}
void UninstallGame(const fs::path& selected) {
    Session session(selected);
    Need(!session.observed[0].empty(), "No VII patch DLL is installed");
    Transaction transaction(session, L"Uninstall");
    // An absent staged DLL requests deletion of exactly the recognized current bytes.
    transaction.Keep(1);
    State(transaction, false, transaction.Stage(1));
    transaction.Commit();
}
}
