#include "preparer.hpp"
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <initguid.h>
#include <oleacc.h>
#include <algorithm>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "oleacc.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
namespace preparer {
namespace {
constexpr int GameBrowse = 101, GameSelect = 102, Reload = 103, Close = 104;
constexpr int PathFirst = 200, FeatureFirst = 400, ActionFirst = 600;
constexpr UINT SelectPending = WM_APP + 1;
struct Positioned { HWND handle; int x, y, w, h; };
std::wstring Text(HWND h) {
    const int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, L'\0'); GetWindowTextW(h, s.data(), n + 1); s.resize(n); return s;
}
std::wstring Lines(std::wstring s) {
    for (size_t i = 0; i < s.size(); ++i) if (s[i] == L'\n' && (!i || s[i - 1] != L'\r')) s.insert(i++, 1, L'\r');
    return s;
}
fs::path Pick(HWND owner, const std::wstring& title) {
    IFileDialog* d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d))))
        throw std::runtime_error("Cannot open the folder picker");
    DWORD options = 0; d->GetOptions(&options);
    d->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    d->SetTitle(title.c_str()); fs::path result;
    if (SUCCEEDED(d->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(d->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { result = path; CoTaskMemFree(path); }
            item->Release();
        }
    }
    d->Release(); return result;
}
void Launch(const fs::path& exe, const fs::path& game) {
    if (exe.empty() || reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, game.c_str(), SW_SHOWNORMAL)) <= 32)
        throw std::runtime_error("Settings applied, but the game could not start");
}
struct App {
    Adapter& adapter; Product product; Session session;
    HWND window = nullptr, title = nullptr, intro = nullptr, game = nullptr, reload = nullptr;
    HWND note = nullptr, status = nullptr, details = nullptr, bar = nullptr, close = nullptr, browse = nullptr;
    HWND groups[2]{}; HFONT font = nullptr, heading = nullptr;
    IAccPropServices* accessibility = nullptr;
    std::vector<HWND> children, paths, features;
    std::vector<std::pair<Action, HWND>> actions;
    std::vector<fs::path> detected;
    fs::path pendingSelection;
    std::vector<Positioned> positions;
    bool updating = false; int dpi = 96, offset = 0, height = 0;
    uint64_t started = 0;
    App(Adapter& a) : adapter(a), product(a.Describe()), session(a, Launch) {
        CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&accessibility));
    }
    ~App() { if (accessibility) accessibility->Release(); if (font) DeleteObject(font); if (heading) DeleteObject(heading); }
    int Px(int n) const { return MulDiv(n, dpi, 96); }
    HWND Control(const wchar_t* kind, const std::wstring& label, DWORD style, int id = 0, const wchar_t* accessibleName = nullptr) {
        if (std::wstring(kind) == L"BUTTON") style |= BS_NOTIFY;
        auto h = CreateWindowExW(0, kind, label.c_str(), WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
        if (!h) throw std::runtime_error("Cannot create a preparer control");
        if (accessibility && accessibleName) accessibility->SetHwndPropStr(h, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, PROPID_ACC_NAME, accessibleName);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); children.push_back(h); return h;
    }
    void Fonts() {
        if (font) DeleteObject(font); if (heading) DeleteObject(heading);
        font = CreateFontW(-Px(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        heading = CreateFontW(-Px(23), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        for (auto h : children) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(h == title ? heading : font), TRUE);
    }
    void Error(const std::exception& e) {
        SetWindowTextW(status, L"Could not finish. See the details below.");
        SetWindowTextW(details, Lines(Wide(e.what())).c_str());
    }
    bool Discard() {
        if (!session.Dirty()) return true;
        TASKDIALOG_BUTTON buttons[] = {{1, L"Discard changes"}, {2, L"Stay"}};
        TASKDIALOGCONFIG config{}; config.cbSize = sizeof(config); config.hwndParent = window;
        config.pszWindowTitle = product.windowTitle.c_str(); config.pszMainInstruction = L"Discard unsaved changes?";
        config.pszContent = L"Your changes have not been saved. Choose Stay to keep editing or Discard changes to continue.";
        config.pButtons = buttons; config.cButtons = 2; config.nDefaultButton = 2;
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION; int answer = 0;
        return SUCCEEDED(TaskDialogIndirect(&config, &answer, nullptr, nullptr)) && answer == 1;
    }
    void Build() {
        updating = true;
        for (auto h : children) DestroyWindow(h);
        children.clear(); paths.clear(); features.clear(); actions.clear(); positions.clear();
        title = Control(L"STATIC", product.heading, 0);
        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(heading), TRUE);
        intro = Control(L"STATIC", product.introduction, 0);
        browse = Control(L"BUTTON", L"Game folder...", WS_TABSTOP, GameBrowse);
        if (product.multipleGames) {
            game = Control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL, GameSelect, L"Game folder");
            COMBOBOXINFO info{sizeof(info)};
            if (GetComboBoxInfo(game, &info)) {
                SendMessageW(info.hwndItem, EM_SETREADONLY, TRUE, 0);
                if (accessibility) accessibility->SetHwndPropStr(info.hwndItem, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, PROPID_ACC_NAME, L"Game folder");
            }
            int selected = -1;
            for (size_t i = 0; i < detected.size(); ++i) {
                SendMessageW(game, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(detected[i].c_str()));
                if (detected[i] == session.view.game) selected = int(i);
            }
            SendMessageW(game, CB_SETCURSEL, selected, 0);
        } else game = Control(L"EDIT", session.view.game.wstring(), WS_TABSTOP | WS_BORDER | ES_READONLY | ES_AUTOHSCROLL, GameSelect, L"Game folder");
        reload = Control(L"BUTTON", L"Reload settings", WS_TABSTOP, Reload);
        for (size_t i = 0; i < session.view.paths.size(); ++i) {
            const auto& p = session.view.paths[i];
            Control(L"BUTTON", p.label, WS_TABSTOP, PathFirst + int(i) * 2);
            paths.push_back(Control(L"EDIT", p.value.wstring(), WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL |
                (p.editable ? 0 : ES_READONLY), PathFirst + int(i) * 2 + 1, p.label.c_str()));
        }
        for (unsigned g = 0; g < 2; ++g) groups[g] = Control(L"STATIC", product.groups[g], 0);
        for (size_t i = 0; i < session.view.features.size(); ++i) {
            const auto& f = session.view.features[i];
            auto h = Control(L"BUTTON", f.label, WS_TABSTOP | BS_AUTOCHECKBOX, FeatureFirst + int(i));
            SendMessageW(h, BM_SETCHECK, f.value ? BST_CHECKED : BST_UNCHECKED, 0); features.push_back(h);
        }
        note = Control(L"STATIC", session.view.note, 0);
        auto capabilities = adapter.Actions(session.view);
        for (auto action : {Action::Install, Action::Prepare, Action::Play}) {
            actions.emplace_back(action, Control(L"BUTTON", Label(action), WS_TABSTOP |
                (action == Action::Install ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON), ActionFirst + int(action)));
        }
        close = Control(L"BUTTON", L"Close", WS_TABSTOP, Close);
        status = Control(L"STATIC", session.view.game.empty() ? L"Select your game folder to begin" : L"Ready", 0);
        bar = Control(PROGRESS_CLASSW, L"", 0, 0, L"Operation progress"); SendMessageW(bar, PBM_SETRANGE32, 0, 1000);
        details = Control(L"EDIT", L"Install / Update installs the patch and saves settings. Play applies settings and starts the game.",
            WS_TABSTOP | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 0, L"Operation details");
        for (auto action : {Action::Rollback, Action::Uninstall, Action::Recover, Action::Patch4GB, Action::RestoreExe})
            if (std::any_of(capabilities.begin(), capabilities.end(), [action](auto c) { return c.action == action; }))
                actions.emplace_back(action, Control(L"BUTTON", Label(action), WS_TABSTOP | BS_PUSHBUTTON, ActionFirst + int(action)));
        updating = false; offset = 0; Layout(); Enable();
    }
    void Put(HWND h, int x, int y, int w, int hgt) { positions.push_back({h, x, y, w, hgt}); }
    int TextHeight(HWND control, int width, int minimum) const {
        auto dc = GetDC(control);
        auto previous = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0)));
        RECT bounds{0, 0, Px(width), 0}; auto text = Text(control);
        DrawTextW(dc, text.c_str(), int(text.size()), &bounds, DT_CALCRECT | DT_WORDBREAK);
        SelectObject(dc, previous); ReleaseDC(control, dc);
        return std::max(minimum, (int(bounds.bottom) * 96 + dpi - 1) / dpi + 4);
    }
    void Layout() {
        RECT r{}; GetClientRect(window, &r); const int width = MulDiv(r.right, 96, dpi);
        const int usable = std::max(380, width - 44); const bool columns = width >= 740;
        positions.clear(); int y = 18;
        Put(title, 22, y, usable, 34); y += 36;
        const int introHeight = TextHeight(intro, usable, 44);
        Put(intro, 22, y, usable, introHeight); y += introHeight + 10;
        Put(browse, 22, y, 144, 30); Put(game, 178, y, usable - 300, product.multipleGames ? 300 : 30);
        Put(reload, 22 + usable - 132, y, 132, 30); y += 42;
        for (size_t i = 0; i < paths.size(); ++i) {
            Put(GetDlgItem(window, PathFirst + int(i) * 2), 22, y, 144, 30);
            Put(paths[i], 178, y, usable - 156, 30); y += 40;
        }
        int counts[2]{}; for (const auto& f : session.view.features) ++counts[f.group == 1 ? 1 : 0];
        const int colWidth = columns ? (usable - 18) / 2 : usable;
        const int origins[2] = {y, columns ? y : y + (counts[0] ? 30 + 30 * counts[0] : 0)};
        int rows[2]{};
        for (unsigned g = 0; g < 2; ++g) {
            ShowWindow(groups[g], counts[g] ? SW_SHOW : SW_HIDE);
            Put(groups[g], columns ? 22 + int(g) * (colWidth + 18) : 22, origins[g], colWidth, 26);
        }
        for (size_t i = 0; i < features.size(); ++i) {
            const unsigned g = session.view.features[i].group == 1 ? 1 : 0;
            Put(features[i], columns ? 22 + int(g) * (colWidth + 18) : 22,
                origins[g] + 30 + 30 * rows[g]++, colWidth, 28);
        }
        y += columns ? ((counts[0] || counts[1]) ? 30 + 30 * std::max(counts[0], counts[1]) : 0) :
            (counts[0] ? 30 + 30 * counts[0] : 0) + (counts[1] ? 30 + 30 * counts[1] : 0);
        y += 12; const int noteHeight = TextHeight(note, usable, 44);
        Put(note, 22, y, usable, noteHeight); y += noteHeight + 6;
        if (columns) {
            const int widths[] = {194, 194, 132}; int x = 22;
            for (auto& a : actions) if (a.first <= Action::Play) { Put(a.second, x, y, widths[int(a.first)], 36); x += widths[int(a.first)] + 10; }
            Put(close, 22 + usable - 112, y, 112, 36); y += 48;
        } else {
            int n = 0; const int w = (usable - 12) / 2;
            for (auto& a : actions) if (a.first <= Action::Play) { Put(a.second, 22 + (n % 2) * (w + 12), y + (n / 2) * 44, w, 36); ++n; }
            Put(close, 22 + w + 12, y + 44, w, 36); y += 92;
        }
        Put(status, 22, y, usable, 44); y += 48; Put(bar, 22, y, usable, 18); y += 28;
        Put(details, 22, y, usable, 80); y += 92;
        for (bool executableRow : {false, true}) {
            int count = 0;
            for (const auto& a : actions) if (a.first > Action::Play && (a.first >= Action::Patch4GB) == executableRow) ++count;
            if (!count) continue;
            const int perRow = std::min(count, columns ? 3 : 2);
            const int buttonWidth = (usable - 12 * (perRow - 1)) / perRow;
            int n = 0;
            for (auto& a : actions) if (a.first > Action::Play && (a.first >= Action::Patch4GB) == executableRow) {
                Put(a.second, 22 + (n % perRow) * (buttonWidth + 12), y + (n / perRow) * 40, buttonWidth, 32); ++n;
            }
            y += ((count + perRow - 1) / perRow) * 40;
        }
        height = y + 22;
        Scroll(offset);
    }
    void Scroll(int requested) {
        RECT r{}; GetClientRect(window, &r); const int page = MulDiv(r.bottom, 96, dpi);
        offset = std::clamp(requested, 0, std::max(0, height - page));
        SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS}; si.nMin = 0; si.nMax = height - 1; si.nPage = page; si.nPos = offset;
        SetScrollInfo(window, SB_VERT, &si, TRUE);
        for (const auto& p : positions) MoveWindow(p.handle, Px(p.x), Px(p.y - offset), Px(p.w), Px(p.h), FALSE);
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    void Focus(HWND target) {
        for (const auto& p : positions) if (p.handle == target) {
            RECT r{}; GetClientRect(window, &r); const int page = MulDiv(r.bottom, 96, dpi);
            const int actualHeight = product.multipleGames && target == game ? 30 : p.h;
            if (p.y < offset) Scroll(p.y);
            else if (p.y + actualHeight > offset + page) Scroll(p.y + actualHeight - page);
        }
    }
    void Enable() {
        const bool selected = !session.view.game.empty();
        for (auto h : children) EnableWindow(h, TRUE);
        EnableWindow(browse, !session.busy); EnableWindow(game, !session.busy);
        for (auto h : paths) EnableWindow(h, selected && !session.busy);
        for (auto h : features) EnableWindow(h, selected && !session.busy);
        for (size_t i = 0; i < paths.size(); ++i) EnableWindow(GetDlgItem(window, PathFirst + int(i) * 2), selected && !session.busy && session.view.paths[i].editable);
        EnableWindow(reload, selected && !session.busy);
        for (auto& a : actions) EnableWindow(a.second, session.Available(a.first));
        EnableWindow(close, TRUE);
        SetWindowTextW(close, session.busy && session.action == Action::Prepare ? L"Cancel" : L"Close");
    }
    void Select(const fs::path& path) {
        auto next = adapter.Open(path);
        if (std::find(detected.begin(), detected.end(), next.game) == detected.end()) detected.push_back(next.game);
        session.Select(std::move(next)); Build(); SetFocus(browse);
    }
    void SyncPaths() {
        updating = true;
        for (size_t i = 0; i < paths.size(); ++i) SetWindowTextW(paths[i], session.view.paths[i].value.c_str());
        updating = false;
    }
    void ConfirmSelection() {
        const auto path = std::move(pendingSelection); pendingSelection.clear();
        if (session.busy || path.empty()) return;
        if (Discard()) Select(path);
        else { auto found = std::find(detected.begin(), detected.end(), session.view.game); SendMessageW(game, CB_SETCURSEL, found - detected.begin(), 0); }
    }
    void Command(int id, int notification, HWND control) {
        if (notification == EN_SETFOCUS || notification == BN_SETFOCUS || notification == CBN_SETFOCUS) Focus(control);
        if (updating) return;
        if (id == Close && notification == BN_CLICKED) {
            if (session.busy) { session.RequestClose(false); SetWindowTextW(status, session.Cancelling() ? L"Stopping after the current step..." : L"Waiting for the operation to finish..."); }
            else if (Discard()) DestroyWindow(window);
            return;
        }
        if (session.busy) return;
        if (id == GameBrowse && notification == BN_CLICKED) {
            if (!Discard()) return;
            auto path = Pick(window, L"Select your installed game folder"); if (!path.empty()) Select(path);
        } else if (id == GameSelect && product.multipleGames && notification == CBN_SELCHANGE) {
            const int index = int(SendMessageW(game, CB_GETCURSEL, 0, 0));
            if (index >= 0 && index < int(detected.size())) {
                // Rebuilding here destroys the combo during its native notification.
                pendingSelection = detected[index]; PostMessageW(window, SelectPending, 0, 0);
            }
        } else if (id == Reload && notification == BN_CLICKED) {
            if (Discard()) { Select(session.view.game); SetWindowTextW(status, L"Settings reloaded from disk"); }
        } else if (id >= PathFirst && id < PathFirst + int(paths.size()) * 2) {
            const size_t i = size_t(id - PathFirst) / 2;
            if ((id - PathFirst) % 2 && notification == EN_CHANGE) {
                session.view.paths[i].value = Text(paths[i]); adapter.Changed(session.view, session.view.paths[i].id);
            } else if ((id - PathFirst) % 2 == 0 && notification == BN_CLICKED) {
                auto path = Pick(window, L"Select " + session.view.paths[i].label);
                if (!path.empty()) { session.view.paths[i].value = path; adapter.Changed(session.view, session.view.paths[i].id); SyncPaths(); }
            }
        } else if (id >= FeatureFirst && id < FeatureFirst + int(features.size()) && notification == BN_CLICKED) {
            const size_t i = size_t(id - FeatureFirst); session.view.features[i].value = SendMessageW(features[i], BM_GETCHECK, 0, 0) == BST_CHECKED;
            adapter.Changed(session.view, session.view.features[i].id); SyncPaths();
        } else if (id >= ActionFirst && id <= ActionFirst + int(Action::RestoreExe) && notification == BN_CLICKED) {
            const auto action = Action(id - ActionFirst);
            if (action > Action::Play) {
                if (!Discard()) return;
                session.DiscardChoices(); SyncPaths();
                for (size_t i = 0; i < features.size(); ++i) SendMessageW(features[i], BM_SETCHECK, session.view.features[i].value ? BST_CHECKED : BST_UNCHECKED, 0);
            }
            if (session.Begin(action)) { started = GetTickCount64(); Enable(); SendMessageW(bar, PBM_SETPOS, 0, 0); SetWindowTextW(status, L"Starting..."); }
        }
    }
    void Tick() {
        if (!session.busy) return;
        if (session.Ready()) {
            const auto result = session.Finish();
            if (result.state == State::Completed) Build(); else Enable();
            SetWindowTextW(status, result.summary.c_str()); SetWindowTextW(details, Lines(result.detail).c_str());
            SendMessageW(bar, PBM_SETPOS, result.state == State::Completed ? 1000 : 0, 0);
            if (session.closeAfter) DestroyWindow(window);
            return;
        }
        const auto p = session.Latest();
        if (!session.Cancelling() && !p.stage.empty()) {
            auto text = p.stage;
            if (p.total) text += L" " + std::to_wstring(p.completed) + L" / " + std::to_wstring(p.total) + L" " + p.unit;
            text += L" - " + std::to_wstring((GetTickCount64() - started) / 1000) + L" seconds elapsed";
            SetWindowTextW(status, text.c_str()); SetWindowTextW(details, Lines(p.detail).c_str());
            SendMessageW(bar, PBM_SETPOS, p.total ? WPARAM(1000.0L * std::min(p.completed, p.total) / p.total) : 0, 0);
        }
    }
};
LRESULT CALLBACK Proc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    auto* a = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (msg == WM_NCCREATE) { a = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); a->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a)); }
    if (!a) return DefWindowProcW(window, msg, w, l);
    try {
        switch (msg) {
        case WM_CREATE: a->Fonts(); a->Build(); SetTimer(window, 1, 100, nullptr); return 0;
        case WM_COMMAND: a->Command(LOWORD(w), HIWORD(w), reinterpret_cast<HWND>(l)); return 0;
        case SelectPending: a->ConfirmSelection(); return 0;
        case WM_SIZE: if (a->title) a->Layout(); return 0;
        case WM_DPICHANGED: {
            a->dpi = HIWORD(w); a->Fonts(); auto* r = reinterpret_cast<RECT*>(l);
            SetWindowPos(window, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE); a->Layout(); return 0;
        }
        case WM_GETMINMAXINFO: { auto* m = reinterpret_cast<MINMAXINFO*>(l); m->ptMinTrackSize.x = a->Px(460); m->ptMinTrackSize.y = a->Px(300); return 0; }
        case WM_VSCROLL: {
            SCROLLINFO si{sizeof(si), SIF_ALL}; GetScrollInfo(window, SB_VERT, &si); int next = a->offset;
            switch (LOWORD(w)) {
            case SB_LINEUP: next -= 30; break; case SB_LINEDOWN: next += 30; break;
            case SB_PAGEUP: next -= int(si.nPage); break; case SB_PAGEDOWN: next += int(si.nPage); break;
            case SB_THUMBTRACK: next = si.nTrackPos; break; case SB_TOP: next = 0; break; case SB_BOTTOM: next = si.nMax; break;
            } a->Scroll(next); return 0;
        }
        case WM_MOUSEWHEEL: a->Scroll(a->offset - GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 90); return 0;
        case WM_TIMER: a->Tick(); return 0;
        case WM_CLOSE:
            if (a->session.busy) { a->session.RequestClose(true); SetWindowTextW(a->status, L"Waiting for the operation to finish..."); }
            else if (a->Discard()) DestroyWindow(window); return 0;
        case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: SetBkColor(reinterpret_cast<HDC>(w), GetSysColor(COLOR_WINDOW)); return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        case WM_DESTROY: KillTimer(window, 1); PostQuitMessage(0); return 0;
        }
    } catch (const std::exception& e) { if (a->status) a->Error(e); else return -1; }
    return DefWindowProcW(window, msg, w, l);
}
} // namespace
int RunGui(Adapter& adapter, const View* initial, int show) {
    using Awareness = BOOL(WINAPI*)(HANDLE);
    auto awareness = reinterpret_cast<Awareness>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (awareness) awareness(reinterpret_cast<HANDLE>(INT_PTR(-4))); else SetProcessDPIAware();
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); if (FAILED(com)) return 1;
    int code = 1;
    try {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS}; InitCommonControlsEx(&controls);
        App a(adapter); auto dc = GetDC(nullptr); a.dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(nullptr, dc);
#ifdef PREPARER_UI_TEST
        // Fixture builds exercise layout scaling without changing desktop settings.
        wchar_t fixtureDpi[8]{};
        if (GetEnvironmentVariableW(L"PREPARER_TEST_DPI", fixtureDpi, 8)) {
            const int requested = _wtoi(fixtureDpi);
            if (requested == 96 || requested == 144 || requested == 192) a.dpi = requested;
        }
#endif
        try { a.detected = adapter.Detect(); } catch (...) { /* manual selection remains available */ }
        if (initial && !initial->game.empty()) a.session.Select(*initial);
        else if (!a.detected.empty()) a.session.Select(adapter.Open(a.detected.front()));
        if (!a.session.view.game.empty() && std::find(a.detected.begin(), a.detected.end(), a.session.view.game) == a.detected.end()) a.detected.push_back(a.session.view.game);
        WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = Proc;
        cls.lpszClassName = L"SharedNativeGamePreparer"; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hbrBackground = GetSysColorBrush(COLOR_WINDOW); RegisterClassW(&cls);
        const DWORD style = WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN;
        RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        size_t maintenance = 0; for (auto c : adapter.Actions(a.session.view)) if (c.action > Action::Play) ++maintenance;
        RECT bounds{0, 0, a.Px(780), std::min(a.Px(ContentHeight(a.session.view, maintenance, 780)), int(work.bottom - work.top) - a.Px(70))}; AdjustWindowRect(&bounds, style, FALSE);
        auto window = CreateWindowExW(0, cls.lpszClassName, a.product.windowTitle.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
            bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, cls.hInstance, &a);
        if (window) {
            ShowWindow(window, show); UpdateWindow(window); MSG msg{};
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) if (!IsDialogMessageW(window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            code = 0;
        }
    } catch (const std::exception& e) { MessageBoxW(nullptr, Wide(e.what()).c_str(), L"Prepare Game", MB_OK | MB_ICONERROR); }
    CoUninitialize(); return code;
}
} // namespace preparer
