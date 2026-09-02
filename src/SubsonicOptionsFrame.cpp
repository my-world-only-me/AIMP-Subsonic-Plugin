#include "SubsonicOptionsFrame.h"

#include <algorithm>
#include <vector>

#include "AimpPlugin.h"
#include "Diagnostics.h"
#include "SubsonicClient.h"
#include "Version.h"
#include "i18n.h"

extern HMODULE g_moduleHandle;

namespace {

const wchar_t* kFrameClassName = L"AIMPSubsonicOptionsFrame";

constexpr int kIdServerUrl = 1001;
constexpr int kIdUsername = 1002;
constexpr int kIdPassword = 1003;
constexpr int kIdStreamFormat = 1004;
constexpr int kIdMaxBitRate = 1005;
constexpr int kIdDebugLogging = 1008;
constexpr int kIdLibraryPageSize = 1010;
constexpr int kIdIgnoreTlsCertificateErrors = 1011;
constexpr int kIdShowPassword = 1012;
constexpr int kIdTestConnection = 2001;

enum class UiAction {
    Modified,
    TestConnection,
    TogglePassword,
};

void EnsureFrameClassRegistered() {
    static bool registered = false;
    if (registered) {
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SubsonicOptionsFrame::WindowProc;
    wc.hInstance = g_moduleHandle;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kFrameClassName;
    RegisterClassExW(&wc);
    registered = true;
}

void ApplyDefaultFont(HWND hwnd) {
    if (hwnd) {
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
}

TAIMPUIControlPlacement Placement(int x, int y, int width, int height) {
    TAIMPUIControlPlacement placement{};
    placement.Alignment = ualNone;
    placement.Bounds = RECT{ x, y, x + width, y + height };
    placement.Anchors = RECT{ 1, 1, 0, 0 };
    return placement;
}

TAIMPUIControlPlacement ClientPlacement(int margin = 0) {
    TAIMPUIControlPlacement placement{};
    placement.Alignment = ualClient;
    placement.AlignmentMargins = RECT{ margin, margin, margin, margin };
    return placement;
}

void MoveControl(HWND hwnd, int x, int y, int width, int height) {
    if (hwnd) {
        MoveWindow(hwnd, x, y, width, height, TRUE);
    }
}

bool IsEditChange(WPARAM wParam) {
    const WORD code = HIWORD(wParam);
    return code == EN_CHANGE;
}

bool IsTrackedEdit(WORD id) {
    switch (id) {
    case kIdServerUrl:
    case kIdUsername:
    case kIdPassword:
    case kIdStreamFormat:
    case kIdMaxBitRate:
    case kIdLibraryPageSize:
        return true;
    default:
        return false;
    }
}

int ParseIntOr(const std::wstring& value, int fallback) {
    if (value.empty()) {
        return fallback;
    }
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(value.c_str(), &end, 10);
    return parsed > 0 ? static_cast<int>(parsed) : fallback;
}

void NormalizeUiConfig(SubsonicConfig& config) {
    while (!config.serverUrl.empty() && config.serverUrl.back() == L'/') {
        config.serverUrl.pop_back();
    }
    if (config.streamFormat.empty()) {
        config.streamFormat = L"mp3";
    }
    if (config.maxBitRate <= 0) {
        config.maxBitRate = 320;
    }
    if (config.libraryPageSize <= 0) {
        config.libraryPageSize = 500;
    }
    if (config.libraryPageSize > 500) {
        config.libraryPageSize = 500;
    }
}

bool SameOptionsConfig(const SubsonicConfig& left, const SubsonicConfig& right) {
    SubsonicConfig a = left;
    SubsonicConfig b = right;
    NormalizeUiConfig(a);
    NormalizeUiConfig(b);
    return a.serverUrl == b.serverUrl &&
        a.username == b.username &&
        a.password == b.password &&
        a.streamFormat == b.streamFormat &&
        a.maxBitRate == b.maxBitRate &&
        a.libraryPageSize == b.libraryPageSize &&
        a.debugLogging == b.debugLogging &&
        a.ignoreTlsCertificateErrors == b.ignoreTlsCertificateErrors;
}

using SetWindowThemeProc = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);

void DisableVisualTheme(HWND hwnd) {
    static SetWindowThemeProc setWindowTheme = []() -> SetWindowThemeProc {
        HMODULE module = LoadLibraryW(L"uxtheme.dll");
        if (!module) {
            return nullptr;
        }
        return reinterpret_cast<SetWindowThemeProc>(GetProcAddress(module, "SetWindowTheme"));
    }();
    if (hwnd && setWindowTheme) {
        setWindowTheme(hwnd, L"", L"");
    }
}

using ShouldAppsUseDarkModeProc = bool(WINAPI*)();

bool IsSystemDarkMode() {
    // Windows 10 1809+ exposes the apps dark-mode preference through uxtheme.dll.
    HMODULE module = LoadLibraryW(L"uxtheme.dll");
    if (!module) {
        return false;
    }
    const auto shouldAppsUseDarkMode = reinterpret_cast<ShouldAppsUseDarkModeProc>(
        GetProcAddress(module, "ShouldAppsUseDarkMode"));
    FreeLibrary(module);
    return shouldAppsUseDarkMode && shouldAppsUseDarkMode();
}

} // namespace

class UiChangeHandler final : public IAIMPUIChangeEvents, public ComBase {
public:
    UiChangeHandler(SubsonicOptionsFrame* frame, UiAction action) : frame_(frame), action_(action) {}

    HRESULT WINAPI QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) {
            return E_POINTER;
        }
        *ppvObject = nullptr;
        if (riid == IID_IUnknown || riid == IID_IAIMPUIChangeEvents) {
            *ppvObject = static_cast<IAIMPUIChangeEvents*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG WINAPI AddRef() override { return AddRefImpl(); }
    ULONG WINAPI Release() override { return ReleaseImpl(); }
    void WINAPI OnChanged(IUnknown*) override;

private:
    SubsonicOptionsFrame* frame_;
    UiAction action_;
};

void WINAPI UiChangeHandler::OnChanged(IUnknown*) {
    if (!frame_) {
        return;
    }
    switch (action_) {
    case UiAction::TestConnection:
        frame_->TestConnection();
        break;
    case UiAction::TogglePassword: {
        int state = AIMPUI_CHECKSTATE_UNCHECKED;
        if (frame_->uiShowPasswordCheck_) {
            frame_->uiShowPasswordCheck_->GetValueAsInt32(AIMPUI_CHECKBOX_PROPID_STATE, &state);
        }
        if (frame_->uiPasswordEdit_) {
            frame_->uiPasswordEdit_->SetValueAsInt32(AIMPUI_EDIT_PROPID_PASSWORDCHAR,
                state == AIMPUI_CHECKSTATE_CHECKED ? 0 : L'*');
        }
        break;
    }
    case UiAction::Modified:
    default:
        frame_->MarkModified();
        break;
    }
}

SubsonicOptionsFrame::SubsonicOptionsFrame(IAIMPCore* core, AimpSubsonicPlugin* plugin)
    : core_(core), plugin_(plugin) {
}

SubsonicOptionsFrame::~SubsonicOptionsFrame() {
    DestroyFrame();
}

HRESULT WINAPI SubsonicOptionsFrame::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) {
        return E_POINTER;
    }
    *ppvObject = nullptr;
    if (riid == IID_IUnknown || riid == IID_IAIMPOptionsDialogFrame) {
        *ppvObject = static_cast<IAIMPOptionsDialogFrame*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

HRESULT WINAPI SubsonicOptionsFrame::GetName(IAIMPString** S) {
    if (!S) {
        return E_POINTER;
    }
    *S = MakeAimpString(core_.get(), L"Subsonic");
    return *S ? S_OK : E_FAIL;
}

HWND WINAPI SubsonicOptionsFrame::CreateFrame(HWND ParentWnd) {
    DestroyFrame();
    LogInfo(L"Subsonic options frame uses stable Win32 layout.");
    EnsureFrameClassRegistered();
    host_ = CreateWindowExW(WS_EX_CONTROLPARENT, kFrameClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 640, 420, ParentWnd, nullptr, g_moduleHandle, this);
    if (!host_) {
        LogInfo(FormatLastWindowsError(L"Subsonic options frame CreateWindowEx failed"));
        return nullptr;
    }
    UpdateTheme();
    CreateControls();
    LayoutControls();
    LoadIntoControls(LoadPluginConfig(core_.get()).value_or(DefaultSubsonicConfig()));
    RedrawFrame();
    return host_;
}

void WINAPI SubsonicOptionsFrame::DestroyFrame() {
    const bool hadUiForm = uiForm_ != nullptr;
    if (uiForm_) {
        uiForm_->Release(FALSE);
    }
    uiForm_ = nullptr;
    if (hadUiForm) {
        host_ = nullptr;
    }
    SafeRelease(uiConnectionGroup_);
    SafeRelease(uiPlaybackGroup_);
    SafeRelease(uiLibraryGroup_);
    SafeRelease(uiDiagnosticsGroup_);
    SafeRelease(uiServerUrlEdit_);
    SafeRelease(uiUsernameEdit_);
    SafeRelease(uiPasswordEdit_);
    SafeRelease(uiStreamFormatEdit_);
    SafeRelease(uiMaxBitRateEdit_);
    SafeRelease(uiLibraryPageSizeEdit_);
    SafeRelease(uiShowPasswordCheck_);
    SafeRelease(uiIgnoreTlsCertificateErrorsCheck_);
    SafeRelease(uiDebugLoggingCheck_);
    SafeRelease(uiTestButton_);
    SafeRelease(uiStatusLabel_);
    for (auto* label : uiLabels_) {
        SafeRelease(label);
    }
    uiLabels_.clear();
    for (auto* hint : uiHints_) {
        SafeRelease(hint);
    }
    uiHints_.clear();
    for (auto* handler : uiEventHandlers_) {
        SafeRelease(handler);
    }
    uiEventHandlers_.clear();
    SafeRelease(uiService_);

    if (host_) {
        DestroyWindow(host_);
        host_ = nullptr;
    }
    DestroyThemeBrushes();
    serverUrlEdit_ = nullptr;
    usernameEdit_ = nullptr;
    passwordEdit_ = nullptr;
    showPasswordCheck_ = nullptr;
    streamFormatEdit_ = nullptr;
    maxBitRateEdit_ = nullptr;
    libraryPageSizeEdit_ = nullptr;
    ignoreTlsCertificateErrorsCheck_ = nullptr;
    debugLoggingCheck_ = nullptr;
    testButton_ = nullptr;
    statusLabel_ = nullptr;
    connectionGroup_ = nullptr;
    playbackGroup_ = nullptr;
    libraryGroup_ = nullptr;
    diagnosticsGroup_ = nullptr;
    labels_.clear();
    hints_.clear();
}

void WINAPI SubsonicOptionsFrame::Notification(int ID) {
    switch (ID) {
    case AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_LOAD:
        LoadIntoControls(LoadPluginConfig(core_.get()).value_or(DefaultSubsonicConfig()));
        RedrawFrame();
        break;
    case AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_SAVE:
        SaveFromControls();
        break;
    case AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_RESET:
        LoadIntoControls(DefaultSubsonicConfig());
        MarkModified();
        RedrawFrame();
        break;
    default:
        break;
    }
}

LRESULT CALLBACK SubsonicOptionsFrame::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SubsonicOptionsFrame* frame = reinterpret_cast<SubsonicOptionsFrame*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        frame = reinterpret_cast<SubsonicOptionsFrame*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(frame));
        if (frame) {
            frame->host_ = hwnd;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    if (frame) {
        return frame->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT SubsonicOptionsFrame::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_ERASEBKGND: {
        RECT rect{};
        GetClientRect(host_, &rect);
        FillRect(reinterpret_cast<HDC>(wParam), &rect, backgroundBrush_ ? backgroundBrush_ : GetSysColorBrush(COLOR_WINDOW));
        return 1;
    }
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wParam), editTextColor_);
        SetBkColor(reinterpret_cast<HDC>(wParam), editBackgroundColor_);
        return reinterpret_cast<LRESULT>(editBackgroundBrush_ ? editBackgroundBrush_ : GetSysColorBrush(COLOR_WINDOW));
    case WM_CTLCOLORSTATIC: {
        const HWND child = reinterpret_cast<HWND>(lParam);
        const bool isHint = std::find(hints_.begin(), hints_.end(), child) != hints_.end();
        SetTextColor(reinterpret_cast<HDC>(wParam), isHint ? hintTextColor_ : textColor_);
        SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(backgroundBrush_ ? backgroundBrush_ : GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_CTLCOLORBTN:
        SetTextColor(reinterpret_cast<HDC>(wParam), textColor_);
        SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(backgroundBrush_ ? backgroundBrush_ : GetSysColorBrush(COLOR_WINDOW));
    case WM_SIZE:
        LayoutControls();
        RedrawFrame();
        return 0;
    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
        UpdateTheme();
        RedrawFrame();
        return 0;
    case WM_DPICHANGED:
        LayoutControls();
        RedrawFrame();
        return 0;
    case WM_COMMAND: {
        const WORD id = LOWORD(wParam);
        if (id == kIdTestConnection && HIWORD(wParam) == BN_CLICKED) {
            TestConnection();
            return 0;
        }
        if (id == kIdDebugLogging && HIWORD(wParam) == BN_CLICKED) {
            MarkModified();
            return 0;
        }
        if (id == kIdIgnoreTlsCertificateErrors && HIWORD(wParam) == BN_CLICKED) {
            MarkModified();
            return 0;
        }
        if (id == kIdShowPassword && HIWORD(wParam) == BN_CLICKED) {
            const bool show = SendMessageW(showPasswordCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            SendMessageW(passwordEdit_, EM_SETPASSWORDCHAR, show ? 0 : L'*', 0);
            InvalidateRect(passwordEdit_, nullptr, TRUE);
            return 0;
        }
        if (IsTrackedEdit(id) && IsEditChange(wParam)) {
            MarkModified();
            return 0;
        }
        break;
    }
    case WM_DESTROY:
        if (host_) {
            SetWindowLongPtrW(host_, GWLP_USERDATA, 0);
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(host_, message, wParam, lParam);
}

void SubsonicOptionsFrame::CreateControls() {
    connectionGroup_ = CreateGroupBox(L10n::Text(L"连接", L"Connection"));
    CreateLabel(L10n::Text(L"服务器地址", L"Server URL"));
    serverUrlEdit_ = CreateEdit(kIdServerUrl);
    CreateLabel(L10n::Text(L"用户名", L"Username"));
    usernameEdit_ = CreateEdit(kIdUsername);
    CreateLabel(L10n::Text(L"密码", L"Password"));
    passwordEdit_ = CreateEdit(kIdPassword, ES_PASSWORD);
    showPasswordCheck_ = CreateWindowExW(0, L"BUTTON", L10n::Text(L"显示密码", L"Show password"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, host_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdShowPassword)), g_moduleHandle, nullptr);
    ApplyDefaultFont(showPasswordCheck_);
    DisableVisualTheme(showPasswordCheck_);

    playbackGroup_ = CreateGroupBox(L10n::Text(L"播放", L"Playback"));
    CreateLabel(L10n::Text(L"流格式", L"Stream format"));
    streamFormatEdit_ = CreateEdit(kIdStreamFormat);
    CreateLabel(L10n::Text(L"最大比特率", L"Max bitrate"));
    maxBitRateEdit_ = CreateEdit(kIdMaxBitRate, ES_NUMBER);

    libraryGroup_ = CreateGroupBox(L10n::Text(L"音乐库", L"Music Library"));
    CreateLabel(L10n::Text(L"库页面大小", L"Library page size"));
    libraryPageSizeEdit_ = CreateEdit(kIdLibraryPageSize, ES_NUMBER);
    CreateHint(L10n::Text(L"用于艺术家 / 专辑浏览。Subsonic API 上限为 500。",
        L"Used for Artists/Albums browsing. Subsonic API maximum is 500."));

    diagnosticsGroup_ = CreateGroupBox(L10n::Text(L"诊断", L"Diagnostics"));
    ignoreTlsCertificateErrorsCheck_ = CreateWindowExW(0, L"BUTTON", L10n::Text(L"允许自签名 HTTPS 证书", L"Allow self-signed HTTPS certificates"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, host_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdIgnoreTlsCertificateErrors)), g_moduleHandle, nullptr);
    ApplyDefaultFont(ignoreTlsCertificateErrorsCheck_);
    DisableVisualTheme(ignoreTlsCertificateErrorsCheck_);

    debugLoggingCheck_ = CreateWindowExW(0, L"BUTTON", L10n::Text(L"调试日志", L"Debug logging"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, host_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDebugLogging)), g_moduleHandle, nullptr);
    ApplyDefaultFont(debugLoggingCheck_);
    DisableVisualTheme(debugLoggingCheck_);

    testButton_ = CreateButton(kIdTestConnection, L10n::Text(L"测试连接", L"Test Connection"));
    statusLabel_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, host_, nullptr, g_moduleHandle, nullptr);
    ApplyDefaultFont(statusLabel_);
}

bool SubsonicOptionsFrame::CreateAimpControls(HWND parentWnd) {
    if (FAILED(core_.get()->QueryInterface(IID_IAIMPServiceUI, reinterpret_cast<void**>(&uiService_))) || !uiService_) {
        LogInfo(L"AIMP UI service is unavailable; falling back to Win32 options controls.");
        return false;
    }

    IAIMPString* formName = MakeAimpString(core_.get(), L"SubsonicOptions");
    if (!formName || FAILED(uiService_->CreateForm(parentWnd, AIMPUI_SERVICE_CREATEFORM_FLAGS_CHILD, formName, nullptr, &uiForm_)) || !uiForm_) {
        SafeRelease(formName);
        LogInfo(L"AIMP UI options form creation failed; falling back to Win32 options controls.");
        return false;
    }
    formName->Release();
    uiForm_->SetValueAsInt32(AIMPUI_FORM_PROPID_BORDERSTYLE, AIMPUI_FLAGS_BORDERSTYLE_NONE);
    uiForm_->SetValueAsInt32(AIMPUI_FORM_PROPID_PADDING, 8);
    uiForm_->SetPlacement(ClientPlacement());
    host_ = uiForm_->GetHandle();

    auto makeHandler = [&](UiAction action) -> IUnknown* {
        auto* handler = new UiChangeHandler(this, action);
        uiEventHandlers_.push_back(static_cast<IUnknown*>(handler));
        return static_cast<IUnknown*>(handler);
    };
    auto setText = [&](IAIMPPropertyList* control, int property, const std::wstring& text) {
        IAIMPString* value = MakeAimpString(core_.get(), text);
        if (value) {
            control->SetValueAsObject(property, value);
            value->Release();
        }
    };
    auto createGroup = [&](const std::wstring& caption, int x, int y, int w, int h) -> IAIMPUIGroupBox* {
        IAIMPUIGroupBox* group = nullptr;
        uiService_->CreateControl(uiForm_, uiForm_, nullptr, nullptr, IID_IAIMPUIGroupBox, reinterpret_cast<void**>(&group));
        if (group) {
            group->SetPlacement(Placement(x, y, w, h));
            group->SetValueAsInt32(AIMPUI_GROUPBOX_PROPID_BORDERS, AIMPUI_FLAGS_BORDERS_ALL);
            setText(group, AIMPUI_GROUPBOX_PROPID_CAPTION, caption);
        }
        return group;
    };
    auto createLabel = [&](IAIMPUIWinControl* parent, const std::wstring& text, int x, int y, int w, int h, bool hint = false) -> IAIMPUILabel* {
        IAIMPUILabel* label = nullptr;
        uiService_->CreateControl(uiForm_, parent, nullptr, nullptr, IID_IAIMPUILabel, reinterpret_cast<void**>(&label));
        if (label) {
            label->SetPlacement(Placement(x, y, w, h));
            label->SetValueAsInt32(AIMPUI_LABEL_PROPID_TRANSPARENT, 1);
            label->SetValueAsInt32(AIMPUI_LABEL_PROPID_WORDWRAP, 1);
            setText(label, AIMPUI_LABEL_PROPID_TEXT, text);
            if (hint) {
                uiHints_.push_back(label);
            } else {
                uiLabels_.push_back(label);
            }
        }
        return label;
    };
    auto createEdit = [&](IAIMPUIWinControl* parent, int x, int y, int w, int h, bool password = false) -> IAIMPUIEdit* {
        IAIMPUIEdit* edit = nullptr;
        uiService_->CreateControl(uiForm_, parent, nullptr, makeHandler(UiAction::Modified), IID_IAIMPUIEdit, reinterpret_cast<void**>(&edit));
        if (edit) {
            edit->SetPlacement(Placement(x, y, w, h));
            edit->SetValueAsInt32(AIMPUI_BASEEDIT_PROPID_BORDERS, AIMPUI_FLAGS_BORDERS_ALL);
            if (password) {
                edit->SetValueAsInt32(AIMPUI_EDIT_PROPID_PASSWORDCHAR, L'*');
            }
        }
        return edit;
    };
    auto createButton = [&](IAIMPUIWinControl* parent, const std::wstring& text, int x, int y, int w, int h, UiAction action) -> IAIMPUIButton* {
        IAIMPUIButton* button = nullptr;
        uiService_->CreateControl(uiForm_, parent, nullptr, makeHandler(action), IID_IAIMPUIButton, reinterpret_cast<void**>(&button));
        if (button) {
            button->SetPlacement(Placement(x, y, w, h));
            setText(button, AIMPUI_BUTTON_PROPID_CAPTION, text);
        }
        return button;
    };
    auto createCheck = [&](IAIMPUIWinControl* parent, const std::wstring& text, int x, int y, int w, int h, UiAction action) -> IAIMPUICheckBox* {
        IAIMPUICheckBox* check = nullptr;
        uiService_->CreateControl(uiForm_, parent, nullptr, makeHandler(action), IID_IAIMPUICheckBox, reinterpret_cast<void**>(&check));
        if (check) {
            check->SetPlacement(Placement(x, y, w, h));
            check->SetValueAsInt32(AIMPUI_CHECKBOX_PROPID_AUTOSIZE, 0);
            check->SetValueAsInt32(AIMPUI_CHECKBOX_PROPID_WORDWRAP, 1);
            setText(check, AIMPUI_CHECKBOX_PROPID_CAPTION, text);
        }
        return check;
    };

    const int x = 10;
    const int w = 610;
    const int labelW = 115;
    const int editX = 130;
    int y = 10;

    uiConnectionGroup_ = createGroup(L10n::Text(L"连接", L"Connection"), x, y, w, 148);
    createLabel(uiConnectionGroup_, L10n::Text(L"服务器地址", L"Server URL"), 14, 28, labelW, 22);
    uiServerUrlEdit_ = createEdit(uiConnectionGroup_, editX, 26, 450, 24);
    createLabel(uiConnectionGroup_, L10n::Text(L"用户名", L"Username"), 14, 58, labelW, 22);
    uiUsernameEdit_ = createEdit(uiConnectionGroup_, editX, 56, 450, 24);
    createLabel(uiConnectionGroup_, L10n::Text(L"密码", L"Password"), 14, 88, labelW, 22);
    uiPasswordEdit_ = createEdit(uiConnectionGroup_, editX, 86, 450, 24, true);
    uiShowPasswordCheck_ = createCheck(uiConnectionGroup_, L10n::Text(L"显示密码", L"Show password"), editX, 116, 160, 24, UiAction::TogglePassword);
    uiTestButton_ = createButton(uiConnectionGroup_, L10n::Text(L"测试连接", L"Test Connection"), editX + 174, 114, 132, 26, UiAction::TestConnection);
    uiStatusLabel_ = nullptr;
    uiService_->CreateControl(uiForm_, uiConnectionGroup_, nullptr, nullptr, IID_IAIMPUILabel, reinterpret_cast<void**>(&uiStatusLabel_));
    if (uiStatusLabel_) {
        uiStatusLabel_->SetPlacement(Placement(editX + 318, 118, 250, 22));
        uiStatusLabel_->SetValueAsInt32(AIMPUI_LABEL_PROPID_TRANSPARENT, 1);
    }
    y += 158;

    uiPlaybackGroup_ = createGroup(L10n::Text(L"播放", L"Playback"), x, y, w, 92);
    createLabel(uiPlaybackGroup_, L10n::Text(L"流格式", L"Stream format"), 14, 28, labelW, 22);
    uiStreamFormatEdit_ = createEdit(uiPlaybackGroup_, editX, 26, 120, 24);
    createLabel(uiPlaybackGroup_, L10n::Text(L"最大比特率", L"Max bitrate"), 14, 58, labelW, 22);
    uiMaxBitRateEdit_ = createEdit(uiPlaybackGroup_, editX, 56, 120, 24);
    y += 102;

    uiLibraryGroup_ = createGroup(L10n::Text(L"音乐库", L"Music Library"), x, y, w, 96);
    createLabel(uiLibraryGroup_, L10n::Text(L"库页面大小", L"Library page size"), 14, 28, labelW, 22);
    uiLibraryPageSizeEdit_ = createEdit(uiLibraryGroup_, editX, 26, 120, 24);
    createLabel(uiLibraryGroup_, L10n::Text(L"用于艺术家、专辑和曲目浏览。Subsonic API 上限为 500。", L"Used for Artists, Albums and Tracks. Subsonic API maximum is 500."), editX, 56, 450, 34, true);
    y += 106;

    uiDiagnosticsGroup_ = createGroup(L10n::Text(L"诊断", L"Diagnostics"), x, y, w, 88);
    uiIgnoreTlsCertificateErrorsCheck_ = createCheck(uiDiagnosticsGroup_, L10n::Text(L"允许自签名 HTTPS 证书", L"Allow self-signed HTTPS certificates"), editX, 28, 360, 24, UiAction::Modified);
    uiDebugLoggingCheck_ = createCheck(uiDiagnosticsGroup_, L10n::Text(L"调试日志", L"Debug logging"), editX, 56, 180, 24, UiAction::Modified);

    return true;
}

void SubsonicOptionsFrame::LayoutAimpControls() {
    if (!host_ || !uiForm_) {
        return;
    }

    RECT rect{};
    GetClientRect(host_, &rect);
    int width = rect.right - rect.left;
    if (width < 360) {
        if (HWND parent = GetParent(host_)) {
            GetClientRect(parent, &rect);
            width = rect.right - rect.left;
        }
    }
    if (width < 360) {
        width = 640;
    }

    const int margin = 14;
    const int groupPaddingX = 14;
    const int groupTopPadding = 24;
    const int groupBottomPadding = 12;
    const int labelWidth = 118;
    const int gap = 10;
    const int groupWidth = std::max(360, width - margin * 2);
    const int labelX = groupPaddingX;
    const int editX = labelX + labelWidth + gap;
    const int editWidth = std::max(170, groupWidth - groupPaddingX - editX);
    const int rowHeight = 24;
    const int rowGap = 9;
    const int groupGap = 10;
    int y = margin;

    auto place = [](IAIMPUIControl* control, int x, int y, int w, int h) {
        if (control) {
            control->SetPlacement(Placement(x, y, w, h));
        }
    };
    auto label = [&](size_t index) -> IAIMPUILabel* {
        return index < uiLabels_.size() ? uiLabels_[index] : nullptr;
    };
    auto layoutRow = [&](IAIMPUILabel* labelControl, IAIMPUIControl* editControl, int& localY, int controlWidth = 0) {
        place(labelControl, labelX, localY + 3, labelWidth, rowHeight);
        place(editControl, editX, localY, controlWidth > 0 ? std::min(controlWidth, editWidth) : editWidth, rowHeight);
        localY += rowHeight + rowGap;
    };
    auto finishGroup = [&](IAIMPUIGroupBox* group, int localY) {
        const int groupHeight = std::max(44, localY + groupBottomPadding - rowGap);
        place(group, margin, y, groupWidth, groupHeight);
        y += groupHeight + groupGap;
    };

    int localY = groupTopPadding;
    layoutRow(label(0), uiServerUrlEdit_, localY);
    layoutRow(label(1), uiUsernameEdit_, localY);
    layoutRow(label(2), uiPasswordEdit_, localY);
    place(uiShowPasswordCheck_, editX, localY, std::min(170, editWidth), rowHeight);
    localY += rowHeight + rowGap;
    constexpr int testButtonWidth = 132;
    place(uiTestButton_, editX, localY, std::min(testButtonWidth, editWidth), 28);
    if (editWidth >= 300) {
        place(uiStatusLabel_, editX + testButtonWidth + gap, localY + 4, editWidth - testButtonWidth - gap, rowHeight);
        localY += 28 + rowGap;
    } else {
        localY += 28 + 4;
        place(uiStatusLabel_, editX, localY, editWidth, rowHeight);
        localY += rowHeight + rowGap;
    }
    finishGroup(uiConnectionGroup_, localY);

    localY = groupTopPadding;
    layoutRow(label(3), uiStreamFormatEdit_, localY, 140);
    layoutRow(label(4), uiMaxBitRateEdit_, localY, 140);
    finishGroup(uiPlaybackGroup_, localY);

    localY = groupTopPadding;
    layoutRow(label(5), uiLibraryPageSizeEdit_, localY, 140);
    if (!uiHints_.empty()) {
        place(uiHints_[0], editX, localY - rowGap, editWidth, 34);
        localY += 28;
    }
    finishGroup(uiLibraryGroup_, localY);

    localY = groupTopPadding;
    place(uiIgnoreTlsCertificateErrorsCheck_, editX, localY, editWidth, rowHeight);
    localY += rowHeight + rowGap;
    place(uiDebugLoggingCheck_, editX, localY, editWidth, rowHeight);
    localY += rowHeight + rowGap;
    finishGroup(uiDiagnosticsGroup_, localY);
}

void SubsonicOptionsFrame::LayoutControls() {
    if (!host_) {
        return;
    }
    if (uiForm_) {
        LayoutAimpControls();
        return;
    }

    RECT rect{};
    GetClientRect(host_, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const bool compact = height > 0 && height < 520;
    const bool veryCompact = height > 0 && height < 450;
    const int margin = compact ? 8 : 14;
    const int groupPaddingX = compact ? 12 : 14;
    const int groupTopPadding = compact ? 18 : 24;
    const int groupBottomPadding = compact ? 8 : 12;
    const int labelWidth = 128;
    const int gap = 10;
    const int groupX = margin;
    const int groupWidth = std::max(300, width - margin * 2);
    const int labelX = groupX + groupPaddingX;
    const int editX = labelX + labelWidth + gap;
    const int editWidth = std::max(220, groupX + groupWidth - groupPaddingX - editX);
    const int rowHeight = veryCompact ? 20 : (compact ? 22 : 24);
    const int rowGap = compact ? 5 : 9;
    const int groupGap = compact ? 7 : 10;
    const int buttonHeight = compact ? 24 : 28;
    const int hintHeight = compact ? 30 : 34;
    int y = margin;

    auto layoutRow = [&](HWND label, HWND edit) {
        MoveControl(label, labelX, y + 3, labelWidth, rowHeight);
        MoveControl(edit, editX, y, editWidth, rowHeight);
        y += rowHeight + rowGap;
    };

    auto beginGroup = [&](HWND group) {
        const int top = y;
        y += groupTopPadding;
        return top;
    };
    auto endGroup = [&](HWND group, int top) {
        const int bottom = y + groupBottomPadding - rowGap;
        MoveControl(group, groupX, top, groupWidth, std::max(44, bottom - top));
        y = bottom + groupGap;
    };

    int top = beginGroup(connectionGroup_);
    layoutRow(labels_.size() > 0 ? labels_[0] : nullptr, serverUrlEdit_);
    layoutRow(labels_.size() > 1 ? labels_[1] : nullptr, usernameEdit_);
    layoutRow(labels_.size() > 2 ? labels_[2] : nullptr, passwordEdit_);
    MoveControl(showPasswordCheck_, editX, y - rowGap + 1, std::min(170, editWidth), rowHeight);
    y += rowHeight;
    MoveControl(testButton_, editX, y, 132, buttonHeight);
    MoveControl(statusLabel_, editX + 144, y + 5, std::max(160, editWidth - 144), rowHeight);
    y += buttonHeight + rowGap;
    endGroup(connectionGroup_, top);

    top = beginGroup(playbackGroup_);
    layoutRow(labels_.size() > 3 ? labels_[3] : nullptr, streamFormatEdit_);
    layoutRow(labels_.size() > 4 ? labels_[4] : nullptr, maxBitRateEdit_);
    endGroup(playbackGroup_, top);

    top = beginGroup(libraryGroup_);
    layoutRow(labels_.size() > 5 ? labels_[5] : nullptr, libraryPageSizeEdit_);
    if (!hints_.empty()) {
        MoveControl(hints_[0], editX, y - rowGap, editWidth, hintHeight);
        y += compact ? 22 : 28;
    }
    endGroup(libraryGroup_, top);

    top = beginGroup(diagnosticsGroup_);
    MoveControl(ignoreTlsCertificateErrorsCheck_, editX, y, editWidth, rowHeight);
    y += rowHeight + rowGap;
    MoveControl(debugLoggingCheck_, editX, y, editWidth, rowHeight);
    y += rowHeight + rowGap;
    endGroup(diagnosticsGroup_, top);
}

void SubsonicOptionsFrame::LoadIntoControls(const SubsonicConfig& config) {
    loading_ = true;
    if (uiForm_) {
        auto setEdit = [&](IAIMPUIEdit* edit, const std::wstring& text) {
            if (!edit) {
                return;
            }
            IAIMPString* value = MakeAimpString(core_.get(), text);
            if (value) {
                edit->SetValueAsObject(AIMPUI_BASEEDIT_PROPID_TEXT, value);
                value->Release();
            }
        };
        setEdit(uiServerUrlEdit_, config.serverUrl);
        setEdit(uiUsernameEdit_, config.username);
        setEdit(uiPasswordEdit_, config.password);
        setEdit(uiStreamFormatEdit_, config.streamFormat.empty() ? L"mp3" : config.streamFormat);
        setEdit(uiMaxBitRateEdit_, std::to_wstring(config.maxBitRate > 0 ? config.maxBitRate : 320));
        setEdit(uiLibraryPageSizeEdit_, std::to_wstring(std::clamp(config.libraryPageSize, 1, 500)));
        if (uiIgnoreTlsCertificateErrorsCheck_) {
            uiIgnoreTlsCertificateErrorsCheck_->SetValueAsInt32(AIMPUI_CHECKBOX_PROPID_STATE,
                config.ignoreTlsCertificateErrors ? AIMPUI_CHECKSTATE_CHECKED : AIMPUI_CHECKSTATE_UNCHECKED);
        }
        if (uiDebugLoggingCheck_) {
            uiDebugLoggingCheck_->SetValueAsInt32(AIMPUI_CHECKBOX_PROPID_STATE,
                config.debugLogging ? AIMPUI_CHECKSTATE_CHECKED : AIMPUI_CHECKSTATE_UNCHECKED);
        }
        if (uiShowPasswordCheck_) {
            uiShowPasswordCheck_->SetValueAsInt32(AIMPUI_CHECKBOX_PROPID_STATE, AIMPUI_CHECKSTATE_UNCHECKED);
        }
        if (uiPasswordEdit_) {
            uiPasswordEdit_->SetValueAsInt32(AIMPUI_EDIT_PROPID_PASSWORDCHAR, L'*');
        }
        SetStatus(L"");
        loading_ = false;
        return;
    }
    SetControlText(serverUrlEdit_, config.serverUrl);
    SetControlText(usernameEdit_, config.username);
    SetControlText(passwordEdit_, config.password);
    SetControlText(streamFormatEdit_, config.streamFormat.empty() ? L"mp3" : config.streamFormat);
    SetControlText(maxBitRateEdit_, std::to_wstring(config.maxBitRate > 0 ? config.maxBitRate : 320));
    SetControlText(libraryPageSizeEdit_, std::to_wstring(std::clamp(config.libraryPageSize, 1, 500)));
    SendMessageW(ignoreTlsCertificateErrorsCheck_, BM_SETCHECK, config.ignoreTlsCertificateErrors ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(debugLoggingCheck_, BM_SETCHECK, config.debugLogging ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(showPasswordCheck_, BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(passwordEdit_, EM_SETPASSWORDCHAR, L'*', 0);
    SetStatus(L"");
    loading_ = false;
}

SubsonicConfig SubsonicOptionsFrame::ReadFromControls() const {
    SubsonicConfig config = DefaultSubsonicConfig();
    if (uiForm_) {
        auto getEdit = [&](IAIMPUIEdit* edit) -> std::wstring {
            if (!edit) {
                return {};
            }
            IAIMPString* value = nullptr;
            if (SUCCEEDED(edit->GetValueAsObject(AIMPUI_BASEEDIT_PROPID_TEXT, IID_IAIMPString, reinterpret_cast<void**>(&value))) && value) {
                std::wstring result = FromAimpString(value);
                value->Release();
                return result;
            }
            return {};
        };
        auto isChecked = [](IAIMPUICheckBox* check) {
            int state = AIMPUI_CHECKSTATE_UNCHECKED;
            return check && SUCCEEDED(check->GetValueAsInt32(AIMPUI_CHECKBOX_PROPID_STATE, &state)) &&
                state == AIMPUI_CHECKSTATE_CHECKED;
        };
        config.serverUrl = getEdit(uiServerUrlEdit_);
        config.username = getEdit(uiUsernameEdit_);
        config.password = getEdit(uiPasswordEdit_);
        config.streamFormat = getEdit(uiStreamFormatEdit_);
        config.maxBitRate = ParseIntOr(getEdit(uiMaxBitRateEdit_), 320);
        config.libraryPageSize = ParseIntOr(getEdit(uiLibraryPageSizeEdit_), 500);
        config.ignoreTlsCertificateErrors = isChecked(uiIgnoreTlsCertificateErrorsCheck_);
        config.debugLogging = isChecked(uiDebugLoggingCheck_);
        NormalizeUiConfig(config);
        return config;
    }
    config.serverUrl = GetControlText(serverUrlEdit_);
    config.username = GetControlText(usernameEdit_);
    config.password = GetControlText(passwordEdit_);
    config.streamFormat = GetControlText(streamFormatEdit_);
    config.maxBitRate = ParseIntOr(GetControlText(maxBitRateEdit_), 320);
    config.libraryPageSize = ParseIntOr(GetControlText(libraryPageSizeEdit_), 500);
    config.ignoreTlsCertificateErrors = SendMessageW(ignoreTlsCertificateErrorsCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    config.debugLogging = SendMessageW(debugLoggingCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    NormalizeUiConfig(config);
    return config;
}

void SubsonicOptionsFrame::SaveFromControls() {
    const SubsonicConfig config = ReadFromControls();
    const SubsonicConfig previousConfig = LoadPluginConfig(core_.get()).value_or(DefaultSubsonicConfig());
    if (SameOptionsConfig(config, previousConfig)) {
        SetStatus(L"");
        return;
    }

    if (!previousConfig.ignoreTlsCertificateErrors && config.ignoreTlsCertificateErrors) {
        MessageBoxW(host_,
            L10n::Text(L"已禁用 TLS 证书校验。仅建议在受信任的自签名开发服务器上使用。",
                L"TLS certificate validation is disabled. Use this only for a trusted self-signed development server."),
            L"AIMP Subsonic " AIMP_SUBSONIC_VERSION,
            MB_OK | MB_ICONWARNING);
    }
    if (SavePluginConfig(core_.get(), config)) {
        if (plugin_) {
            plugin_->ApplyConfigFromOptions(config);
        }
        SetStatus(L10n::Text(L"已保存", L"Saved"));
    } else {
        SetStatus(L10n::Text(L"保存失败", L"Save failed"));
    }
}

void SubsonicOptionsFrame::TestConnection() {
    const SubsonicConfig config = ReadFromControls();
    if (config.serverUrl.empty() || config.username.empty() || config.password.empty()) {
        SetStatus(L10n::Text(L"请填写服务器地址、用户名和密码", L"Fill server URL, username, and password"));
        MessageBoxW(host_, L10n::Text(L"请先填写服务器地址、用户名和密码。", L"Fill server URL, username, and password first."), L"AIMP Subsonic " AIMP_SUBSONIC_VERSION, MB_OK | MB_ICONWARNING);
        return;
    }

    SetStatus(L10n::Text(L"正在测试…", L"Testing..."));
    const bool ok = plugin_ ? plugin_->TestSubsonicConfig(config) : SubsonicClient(config).Ping();
    SetStatus(ok ? L10n::Text(L"连接成功", L"Connection OK") : L10n::Text(L"连接失败", L"Connection failed"));
    MessageBoxW(host_, ok ? L10n::Text(L"Subsonic ping 成功。", L"Subsonic ping succeeded.") : L10n::Text(L"Subsonic ping 失败。如需要详细信息，请启用调试日志。", L"Subsonic ping failed. Enable Debug logging for details."),
        L"AIMP Subsonic " AIMP_SUBSONIC_VERSION, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
}

void SubsonicOptionsFrame::RedrawFrame() {
    if (!host_) {
        return;
    }
    RedrawWindow(host_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void SubsonicOptionsFrame::MarkModified() {
    if (loading_) {
        return;
    }
    IAIMPServiceOptionsDialog* options = nullptr;
    if (SUCCEEDED(core_.get()->QueryInterface(IID_IAIMPServiceOptionsDialog, reinterpret_cast<void**>(&options))) && options) {
        options->FrameModified(static_cast<IAIMPOptionsDialogFrame*>(this));
        options->Release();
    }
}

void SubsonicOptionsFrame::SetStatus(const std::wstring& value) {
    if (uiStatusLabel_) {
        IAIMPString* text = MakeAimpString(core_.get(), value);
        if (text) {
            uiStatusLabel_->SetValueAsObject(AIMPUI_LABEL_PROPID_TEXT, text);
            text->Release();
        }
        return;
    }
    SetControlText(statusLabel_, value);
}

void SubsonicOptionsFrame::UpdateTheme() {
    darkTheme_ = IsSystemDarkMode();
    if (darkTheme_) {
        backgroundColor_ = RGB(34, 34, 34);
        textColor_ = RGB(245, 245, 245);
        hintTextColor_ = RGB(210, 210, 210);
        editBackgroundColor_ = RGB(45, 45, 45);
        editTextColor_ = RGB(255, 255, 255);
    } else {
        backgroundColor_ = RGB(255, 255, 255);
        textColor_ = RGB(0, 0, 0);
        hintTextColor_ = RGB(80, 80, 80);
        editBackgroundColor_ = RGB(255, 255, 255);
        editTextColor_ = RGB(0, 0, 0);
    }
    DestroyThemeBrushes();
    backgroundBrush_ = CreateSolidBrush(backgroundColor_);
    editBackgroundBrush_ = CreateSolidBrush(editBackgroundColor_);

    LogInfo(L"Subsonic options theme updated. Mode=" + std::wstring(darkTheme_ ? L"dark" : L"light"));
}

void SubsonicOptionsFrame::DestroyThemeBrushes() {
    if (backgroundBrush_) {
        DeleteObject(backgroundBrush_);
        backgroundBrush_ = nullptr;
    }
    if (editBackgroundBrush_) {
        DeleteObject(editBackgroundBrush_);
        editBackgroundBrush_ = nullptr;
    }
}

HWND SubsonicOptionsFrame::CreateLabel(const std::wstring& caption) {
    HWND hwnd = CreateWindowExW(0, L"STATIC", caption.c_str(), WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, host_, nullptr, g_moduleHandle, nullptr);
    ApplyDefaultFont(hwnd);
    labels_.push_back(hwnd);
    return hwnd;
}

HWND SubsonicOptionsFrame::CreateHint(const std::wstring& caption) {
    HWND hwnd = CreateWindowExW(0, L"STATIC", caption.c_str(), WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, host_, nullptr, g_moduleHandle, nullptr);
    ApplyDefaultFont(hwnd);
    hints_.push_back(hwnd);
    return hwnd;
}

HWND SubsonicOptionsFrame::CreateGroupBox(const std::wstring& caption) {
    HWND hwnd = CreateWindowExW(0, L"BUTTON", caption.c_str(),
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 0, 0, host_, nullptr, g_moduleHandle, nullptr);
    ApplyDefaultFont(hwnd);
    DisableVisualTheme(hwnd);
    return hwnd;
}

HWND SubsonicOptionsFrame::CreateEdit(int id, DWORD style) {
    HWND hwnd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | style,
        0, 0, 0, 0, host_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_moduleHandle, nullptr);
    ApplyDefaultFont(hwnd);
    return hwnd;
}

HWND SubsonicOptionsFrame::CreateButton(int id, const std::wstring& caption) {
    HWND hwnd = CreateWindowExW(0, L"BUTTON", caption.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, host_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_moduleHandle, nullptr);
    ApplyDefaultFont(hwnd);
    return hwnd;
}

std::wstring SubsonicOptionsFrame::GetControlText(HWND hwnd) const {
    if (!hwnd) {
        return {};
    }
    const int length = GetWindowTextLengthW(hwnd);
    std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1);
    GetWindowTextW(hwnd, buffer.data(), static_cast<int>(buffer.size()));
    return std::wstring(buffer.data());
}

void SubsonicOptionsFrame::SetControlText(HWND hwnd, const std::wstring& text) {
    if (hwnd) {
        SetWindowTextW(hwnd, text.c_str());
    }
}
