#include "AimpPlugin.h"

#include "apiMessages.h"
#include "apiPlayer.h"
#include "apiPlaylists.h"
#include "apiThreading.h"

#include "Config.h"
#include "Diagnostics.h"
#include "PlaylistBridge.h"
#include "SubsonicOptionsFrame.h"
#include "Version.h"
#include "i18n.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

using PlaylistBridge::BuildTrackFromFileInfo;
using PlaylistBridge::ExtractSubsonicSongId;
using PlaylistBridge::GetFileInfoString;
using PlaylistBridge::GetPlaylistItemString;
using PlaylistBridge::TrackDisplayText;

namespace {

HRESULT SetObjectProperty(IAIMPPropertyList* props, int propId, IUnknown* value) {
    return value ? props->SetValueAsObject(propId, value) : E_FAIL;
}

HRESULT SetMenuString(IAIMPCore* core, IAIMPMenuItem* item, int propId, const std::wstring& value) {
    IAIMPString* str = MakeAimpString(core, value);
    if (!str) {
        return E_FAIL;
    }
    const HRESULT hr = item->SetValueAsObject(propId, str);
    str->Release();
    return hr;
}

const wchar_t* PlaybackModeName(PlaybackMode mode) {
    switch (mode) {
    case PlaybackMode::DirectUrl:
        return L"DirectUrl";
    }
    return L"Unknown";
}

bool IsLikelyRawUri(const std::wstring& value) {
    return value.find(L"://") != std::wstring::npos ||
        value.find(L":\\\\") != std::wstring::npos ||
        value.find(L"/rest/stream") != std::wstring::npos;
}

HRESULT CreateMenuItem(IAIMPCore* core, IAIMPMenuItem* parent, IAIMPActionEvent* action, const std::wstring& id, const std::wstring& caption) {
    IAIMPMenuItem* item = nullptr;
    HRESULT hr = core->CreateObject(IID_IAIMPMenuItem, reinterpret_cast<void**>(&item));
    if (FAILED(hr)) {
        return hr;
    }

    SetMenuString(core, item, AIMP_MENUITEM_PROPID_ID, id);
    SetMenuString(core, item, AIMP_MENUITEM_PROPID_NAME, caption);
    SetObjectProperty(item, AIMP_MENUITEM_PROPID_PARENT, parent);
    SetObjectProperty(item, AIMP_MENUITEM_PROPID_EVENT, action);
    hr = core->RegisterExtension(IID_IAIMPServiceMenuManager, item);
    item->Release();
    return hr;
}

HRESULT SetFileInfoString(IAIMPCore* core, IAIMPFileInfo* info, int propId, const std::wstring& value) {
    if (value.empty()) {
        return S_OK;
    }
    IAIMPString* str = MakeAimpString(core, value);
    if (!str) {
        return E_FAIL;
    }
    const HRESULT hr = info->SetValueAsObject(propId, str);
    str->Release();
    return hr;
}

HRESULT SetPlaylistItemString(IAIMPCore* core, IAIMPPlaylistItem* item, int propId, const std::wstring& value) {
    if (value.empty()) {
        return S_OK;
    }
    IAIMPString* str = MakeAimpString(core, value);
    if (!str) {
        return E_FAIL;
    }
    const HRESULT hr = item->SetValueAsObject(propId, str);
    str->Release();
    return hr;
}

std::wstring GetPlaylistPropertyString(IAIMPPlaylist* playlist, int propId) {
    std::wstring result;
    IAIMPPropertyList* props = nullptr;
    if (playlist && SUCCEEDED(playlist->QueryInterface(IID_IAIMPPropertyList, reinterpret_cast<void**>(&props))) && props) {
        IAIMPString* value = nullptr;
        if (SUCCEEDED(props->GetValueAsObject(propId, IID_IAIMPString, reinterpret_cast<void**>(&value))) && value) {
            result = FromAimpString(value);
            value->Release();
        }
        props->Release();
    }
    return result;
}

HRESULT SetPlaylistPropertyString(IAIMPCore* core, IAIMPPlaylist* playlist, int propId, const std::wstring& value) {
    if (!playlist || value.empty()) {
        return S_OK;
    }
    IAIMPPropertyList* props = nullptr;
    if (FAILED(playlist->QueryInterface(IID_IAIMPPropertyList, reinterpret_cast<void**>(&props))) || !props) {
        return E_FAIL;
    }
    IAIMPString* str = MakeAimpString(core, value);
    HRESULT hr = E_FAIL;
    if (str) {
        hr = props->SetValueAsObject(propId, str);
        str->Release();
    }
    props->Release();
    return hr;
}

bool CopyTextToClipboard(const std::wstring& text) {
    if (text.empty() || !OpenClipboard(nullptr)) {
        return false;
    }
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!handle) {
        CloseClipboard();
        return false;
    }
    void* data = GlobalLock(handle);
    if (!data) {
        GlobalFree(handle);
        CloseClipboard();
        return false;
    }
    std::memcpy(data, text.c_str(), bytes);
    GlobalUnlock(handle);
    if (!SetClipboardData(CF_UNICODETEXT, handle)) {
        GlobalFree(handle);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

std::wstring FormatMetadataIndexSummary(const MetadataIndexBuildResult& result) {
    const bool zh = L10n::IsChineseUi();
    std::wstringstream message;
    if (result.canceled) {
        message << (zh ? L"Subsonic 元数据索引构建已取消。\r\n" : L"Subsonic metadata index build was canceled.\r\n");
    } else if (result.ok) {
        message << (zh ? L"Subsonic 元数据索引已刷新。\r\n" : L"Subsonic metadata index refreshed.\r\n");
    } else {
        message << (zh ? L"Subsonic 元数据索引未刷新。\r\n" : L"Subsonic metadata index was not refreshed.\r\n");
    }

    if (zh) {
        message << L"本次载入："
            << result.searchTracksLoaded << L" 首搜索曲目、"
            << result.albumTracksLoaded << L" 首专辑曲目、"
            << result.playlistTracksLoaded << L" 首播放列表曲目、"
            << result.starredTracksLoaded << L" 首收藏曲目。\r\n"
            << L"浏览："
            << result.artistsLoaded << L" 位艺术家、"
            << result.albumsLoaded << L" 张专辑（"
            << result.albumPagesLoaded << L" 页）、"
            << result.playlistsLoaded << L" 个播放列表。\r\n"
            << L"移除过期缓存曲目："
            << result.staleTracksPruned << L"。\r\n"
            << L"缓存总计："
            << result.cachedTracks << L" 首曲目、"
            << result.cachedArtists << L" 位艺术家、"
            << result.cachedAlbums << L" 张专辑、"
            << result.cachedPlaylists << L" 个播放列表。";
    } else {
        message << L"Loaded now: "
            << result.searchTracksLoaded << L" search tracks, "
            << result.albumTracksLoaded << L" album tracks, "
            << result.playlistTracksLoaded << L" playlist tracks, "
            << result.starredTracksLoaded << L" starred tracks.\r\n"
            << L"Browsed: "
            << result.artistsLoaded << L" artists, "
            << result.albumsLoaded << L" albums in "
            << result.albumPagesLoaded << L" page(s), "
            << result.playlistsLoaded << L" playlists.\r\n"
            << L"Removed stale cached tracks: "
            << result.staleTracksPruned << L".\r\n"
            << L"Cached total: "
            << result.cachedTracks << L" tracks, "
            << result.cachedArtists << L" artists, "
            << result.cachedAlbums << L" albums, "
            << result.cachedPlaylists << L" playlists.";
    }
    return message.str();
}

} // namespace

class PlaylistMetadataListener final : public IAIMPExtensionPlaylistManagerListener, public IAIMPPlaylistListener, public ComBase {
public:
    explicit PlaylistMetadataListener(AimpSubsonicPlugin* plugin) : plugin_(plugin) {}

    HRESULT WINAPI QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) {
            return E_POINTER;
        }
        *ppvObject = nullptr;
        if (riid == IID_IUnknown || riid == IID_IAIMPExtensionPlaylistManagerListener) {
            *ppvObject = static_cast<IAIMPExtensionPlaylistManagerListener*>(this);
        } else if (riid == IID_IAIMPPlaylistListener) {
            *ppvObject = static_cast<IAIMPPlaylistListener*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG WINAPI AddRef() override { return AddRefImpl(); }
    ULONG WINAPI Release() override { return ReleaseImpl(); }

    void WINAPI PlaylistActivated(IAIMPPlaylist* Playlist) override {
        if (plugin_) {
            plugin_->AttachPlaylistMetadataListener(Playlist);
            plugin_->RepairPlaylistMetadata(Playlist, false);
        }
    }

    void WINAPI PlaylistAdded(IAIMPPlaylist* Playlist) override {
        if (plugin_) {
            plugin_->AttachPlaylistMetadataListener(Playlist);
            plugin_->RepairPlaylistMetadata(Playlist, false);
        }
    }

    void WINAPI PlaylistRemoved(IAIMPPlaylist* Playlist) override {
        if (plugin_) {
            plugin_->DetachPlaylistMetadataListener(Playlist);
        }
    }

    void WINAPI Activated() override {
        if (plugin_) {
            plugin_->RepairLoadedPlaylistMetadata(false);
        }
    }

    void WINAPI Changed(LongWord Flags) override {
        if (plugin_) {
            plugin_->OnPlaylistContentChanged(Flags);
        }
    }

    void WINAPI Removed() override {
        if (plugin_) {
            plugin_->AttachPlaylistMetadataListeners();
        }
    }

private:
    AimpSubsonicPlugin* plugin_;
};

class MetadataIndexTask final : public IAIMPTask, public ComBase {
public:
    MetadataIndexTask(AimpSubsonicPlugin* plugin, uint64_t generation)
        : plugin_(plugin), generation_(generation) {
        if (plugin_) {
            plugin_->AddRef();
        }
    }

    ~MetadataIndexTask() override {
        if (plugin_) {
            plugin_->Release();
        }
    }

    HRESULT WINAPI QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) {
            return E_POINTER;
        }
        *ppvObject = nullptr;
        if (riid == IID_IUnknown || riid == IID_IAIMPTask) {
            *ppvObject = static_cast<IAIMPTask*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG WINAPI AddRef() override { return AddRefImpl(); }
    ULONG WINAPI Release() override { return ReleaseImpl(); }

    void WINAPI Execute(IAIMPTaskOwner* Owner) override {
        if (plugin_) {
            plugin_->RunMetadataIndexTask(Owner, generation_);
        }
    }

private:
    AimpSubsonicPlugin* plugin_;
    uint64_t generation_;
};

class MetadataIndexCompletionTask final : public IAIMPTask, public ComBase {
public:
    MetadataIndexCompletionTask(AimpSubsonicPlugin* plugin, MetadataIndexBuildResult result, uint64_t generation)
        : plugin_(plugin), result_(result), generation_(generation) {
        if (plugin_) {
            plugin_->AddRef();
        }
    }

    ~MetadataIndexCompletionTask() override {
        if (plugin_) {
            plugin_->Release();
        }
    }

    HRESULT WINAPI QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) {
            return E_POINTER;
        }
        *ppvObject = nullptr;
        if (riid == IID_IUnknown || riid == IID_IAIMPTask) {
            *ppvObject = static_cast<IAIMPTask*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG WINAPI AddRef() override { return AddRefImpl(); }
    ULONG WINAPI Release() override { return ReleaseImpl(); }

    void WINAPI Execute(IAIMPTaskOwner*) override {
        if (plugin_) {
            plugin_->CompleteMetadataIndexTask(result_, generation_);
        }
    }

private:
    AimpSubsonicPlugin* plugin_;
    MetadataIndexBuildResult result_;
    uint64_t generation_;
};

class PlaylistImportTask final : public IAIMPTask, public ComBase {
public:
    PlaylistImportTask(AimpSubsonicPlugin* plugin, uint64_t generation, bool interactive)
        : plugin_(plugin), generation_(generation), interactive_(interactive) {
        if (plugin_) {
            plugin_->AddRef();
        }
    }

    ~PlaylistImportTask() override {
        if (plugin_) {
            plugin_->Release();
        }
    }

    HRESULT WINAPI QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) {
            return E_POINTER;
        }
        *ppvObject = nullptr;
        if (riid == IID_IUnknown || riid == IID_IAIMPTask) {
            *ppvObject = static_cast<IAIMPTask*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG WINAPI AddRef() override { return AddRefImpl(); }
    ULONG WINAPI Release() override { return ReleaseImpl(); }

    void WINAPI Execute(IAIMPTaskOwner* Owner) override {
        if (plugin_) {
            plugin_->RunPlaylistImportTask(Owner, generation_, interactive_);
        }
    }

private:
    AimpSubsonicPlugin* plugin_;
    uint64_t generation_;
    bool interactive_;
};

class PlaylistImportApplyTask final : public IAIMPTask, public ComBase {
public:
    PlaylistImportApplyTask(AimpSubsonicPlugin* plugin, std::vector<ServerPlaylistSnapshot> snapshots,
        uint64_t generation, bool interactive)
        : plugin_(plugin), snapshots_(std::move(snapshots)), generation_(generation), interactive_(interactive) {
        if (plugin_) {
            plugin_->AddRef();
        }
    }

    ~PlaylistImportApplyTask() override {
        if (plugin_) {
            plugin_->Release();
        }
    }

    HRESULT WINAPI QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) {
            return E_POINTER;
        }
        *ppvObject = nullptr;
        if (riid == IID_IUnknown || riid == IID_IAIMPTask) {
            *ppvObject = static_cast<IAIMPTask*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG WINAPI AddRef() override { return AddRefImpl(); }
    ULONG WINAPI Release() override { return ReleaseImpl(); }

    void WINAPI Execute(IAIMPTaskOwner*) override {
        if (plugin_) {
            plugin_->ApplyServerPlaylists(snapshots_, generation_, interactive_);
        }
    }

private:
    AimpSubsonicPlugin* plugin_;
    std::vector<ServerPlaylistSnapshot> snapshots_;
    uint64_t generation_;
    bool interactive_;
};

AimpSubsonicPlugin::~AimpSubsonicPlugin() {
    Finalize();
}

HRESULT WINAPI AimpSubsonicPlugin::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) {
        return E_POINTER;
    }
    *ppvObject = nullptr;
    if (riid == IID_IUnknown) {
        *ppvObject = static_cast<IAIMPPlugin*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_IAIMPMessageHook) {
        *ppvObject = static_cast<IAIMPMessageHook*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

TChar* WINAPI AimpSubsonicPlugin::InfoGet(int Index) {
    switch (Index) {
    case AIMP_PLUGIN_INFO_NAME:
        return const_cast<TChar*>(L"AIMP Subsonic " AIMP_SUBSONIC_VERSION);
    case AIMP_PLUGIN_INFO_AUTHOR:
        return const_cast<TChar*>(L"Astra");
    case AIMP_PLUGIN_INFO_SHORT_DESCRIPTION:
        return const_cast<TChar*>(L10n::Text(L"加载并播放 Subsonic 兼容服务器上的流媒体。", L"Loads and plays Subsonic-compatible streams."));
    case AIMP_PLUGIN_INFO_FULL_DESCRIPTION:
        return const_cast<TChar*>(L10n::Text(L"Subsonic/Navidrome 与 AIMP 的集成：直接流式播放与音乐库存储。", L"Subsonic/Navidrome integration for AIMP with direct streaming and Music Library storage."));
    default:
        return const_cast<TChar*>(L"");
    }
}

LongWord WINAPI AimpSubsonicPlugin::InfoGetCategories() {
    return AIMP_PLUGIN_CATEGORY_ADDONS;
}

HRESULT WINAPI AimpSubsonicPlugin::Initialize(IAIMPCore* Core) {
    if (!Core) {
        return E_POINTER;
    }
    core_.reset(Core);

    auto config = LoadPluginConfig(core_.get());
    if (config) {
        ApplyConfig(*config);
    } else {
        LogInfo(L"Config was not loaded. Configure Subsonic in AIMP Options or use subsonic.local.json next to the plugin DLL.");
    }

    HRESULT hr = RegisterMusicLibrary();
    if (FAILED(hr)) {
        LogInfo(L"Music Library registration failed. HRESULT=" + std::to_wstring(static_cast<long>(hr)));
    }

    hr = RegisterAlbumArtProvider();
    if (FAILED(hr)) {
        LogInfo(L"Album art provider registration failed. HRESULT=" + std::to_wstring(static_cast<long>(hr)));
    }

    hr = RegisterFileInfoProvider();
    if (FAILED(hr)) {
        LogInfo(L"File info provider registration failed. HRESULT=" + std::to_wstring(static_cast<long>(hr)));
    }

    hr = RegisterPlaybackTracking();
    if (FAILED(hr)) {
        LogInfo(L"Playback tracking registration failed. HRESULT=" + std::to_wstring(static_cast<long>(hr)));
    }

    hr = RegisterPlaylistMetadataListener();
    if (FAILED(hr)) {
        LogInfo(L"Playlist metadata listener registration failed. HRESULT=" + std::to_wstring(static_cast<long>(hr)));
    }

    hr = RegisterOptions();
    if (FAILED(hr)) {
        LogInfo(L"Options frame registration failed. HRESULT=" + std::to_wstring(static_cast<long>(hr)));
    }

    hr = RegisterMenu();
    return hr;
}

HRESULT WINAPI AimpSubsonicPlugin::Finalize() {
    CancelPlaylistImportTask();
    CancelMetadataIndexTask();
    UnregisterPlaybackTracking();
    UnregisterPlaylistMetadataListener();
    ClearRegisteredExtensions();
    SafeRelease(optionsFrame_);
    SafeRelease(playlistMetadataListener_);
    SafeRelease(fileInfoProvider_);
    SafeRelease(albumArtProvider_);
    SafeRelease(musicLibrary_);
    playbackTracker_.reset();
    repository_.reset();
    client_.reset();
    core_.reset(nullptr);
    return S_OK;
}

void WINAPI AimpSubsonicPlugin::SystemNotification(int, IUnknown*) {
}

void WINAPI AimpSubsonicPlugin::CoreMessage(LongWord AMessage, int, void*, HRESULT*) {
    switch (AMessage) {
    case AIMP_MSG_EVENT_LOADED:
        RefreshImportedPlaylistsAtStartup();
        break;
    case AIMP_MSG_EVENT_STREAM_START:
    case AIMP_MSG_EVENT_STREAM_START_SUBTRACK:
    case AIMP_MSG_EVENT_PLAYING_FILE_INFO:
        HandleStreamStarted();
        break;
    case AIMP_MSG_EVENT_STREAM_END:
        HandleStreamEnded();
        break;
    case AIMP_MSG_EVENT_PLAYER_UPDATE_POSITION:
        HandlePlaybackPositionUpdate();
        break;
    default:
        break;
    }
}

void AimpSubsonicPlugin::ApplyConfig(const SubsonicConfig& config) {
    CancelPlaylistImportTask();
    CancelMetadataIndexTask();
    SetDebugLoggingEnabled(config.debugLogging);
    client_ = std::make_unique<SubsonicClient>(config);
    coverCache_ = std::make_unique<CoverArtCache>(client_.get());
    repository_ = std::make_unique<SubsonicRepository>(client_.get());
    playbackTracker_ = std::make_unique<PlaybackTracker>(repository_.get());
    if (musicLibrary_) {
        musicLibrary_->SetServices(repository_.get());
    }
    if (albumArtProvider_) {
        albumArtProvider_->SetServices(repository_.get(), coverCache_.get());
    }
    if (fileInfoProvider_) {
        fileInfoProvider_->SetRepository(repository_.get());
    }
    if (config.ignoreTlsCertificateErrors) {
        LogInfo(L"WARNING: TLS certificate validation is disabled for Subsonic requests.");
    }
    LogInfo(L"Subsonic config applied. Server=" + config.serverUrl +
        L", StreamFormat=" + config.streamFormat +
        L", MaxBitRate=" + std::to_wstring(config.maxBitRate));
}

void AimpSubsonicPlugin::ApplyConfigFromOptions(const SubsonicConfig& config) {
    if (config.serverUrl.empty() || config.username.empty() || config.password.empty()) {
        LogInfo(L"Subsonic settings were saved, but connection fields are incomplete.");
        return;
    }
    ApplyConfig(config);
}

bool AimpSubsonicPlugin::TestSubsonicConfig(const SubsonicConfig& config) const {
    if (config.serverUrl.empty() || config.username.empty() || config.password.empty()) {
        return false;
    }
    SubsonicClient testClient(config);
    return testClient.Ping();
}

void AimpSubsonicPlugin::CopySelectedStreamUrls() {
    LogInfo(L"CopySelectedStreamUrls started.");
    if (!repository_) {
        ShowPluginMessage(L10n::Text(L"Subsonic 配置未加载。请在 AIMP 选项 -> Subsonic 中配置，或在 aimp_subsonic.dll 旁使用 subsonic.local.json。",
            L"Subsonic config is not loaded. Open AIMP Options -> Subsonic or use subsonic.local.json next to aimp_subsonic.dll."));
        return;
    }

    const std::vector<TrackInfo> tracks = CollectActivePlaylistTracks(true);
    if (tracks.empty()) {
        ShowPluginMessage(L10n::Text(L"没有选中的 Subsonic 曲目可复制。", L"No selected Subsonic tracks to copy."));
        return;
    }

    std::wstring text;
    for (const auto& track : tracks) {
        if (!text.empty()) {
            text += L"\r\n";
        }
        text += repository_->BuildStreamUrl(track.id);
    }
    if (CopyTextToClipboard(text)) {
        ShowPluginMessage(L10n::Text(L"已复制 Subsonic 流媒体地址：", L"Copied Subsonic stream URLs: ") + std::to_wstring(tracks.size()));
    } else {
        ShowPluginMessage(L10n::Text(L"复制 Subsonic 流媒体地址到剪贴板失败。", L"Failed to copy Subsonic stream URLs to clipboard."));
    }
}

void AimpSubsonicPlugin::RefreshMetadataCache() {
    LogInfo(L"Metadata index build requested.");
    if (!repository_) {
        ShowPluginMessage(L10n::Text(L"Subsonic 配置未加载。请在 AIMP 选项 -> Subsonic 中配置。", L"Subsonic config is not loaded. Open AIMP Options -> Subsonic."));
        return;
    }

    IAIMPServiceThreads* threads = nullptr;
    if (core_.get() && SUCCEEDED(core_.get()->QueryInterface(IID_IAIMPServiceThreads, reinterpret_cast<void**>(&threads))) && threads) {
        HRESULT hr = S_OK;
        uint64_t generation = 0;
        MetadataIndexTask* task = nullptr;
        {
            std::lock_guard lock(metadataIndexMutex_);
            if (metadataIndexRunning_) {
                threads->Release();
                ShowPluginMessage(L10n::Text(L"Subsonic 元数据索引正在刷新中。", L"Subsonic metadata index is already being refreshed."));
                return;
            }
            metadataIndexRunning_ = true;
            metadataIndexTask_ = 0;
            generation = ++metadataIndexGeneration_;
            task = new MetadataIndexTask(this, generation);
            hr = threads->ExecuteInThread(task, &metadataIndexTask_);
            if (FAILED(hr)) {
                metadataIndexRunning_ = false;
                metadataIndexTask_ = 0;
                ++metadataIndexGeneration_;
            } else {
                metadataIndexTaskObject_ = task;
                task = nullptr;
            }
        }
        if (task) {
            task->Release();
        }
        threads->Release();
        if (FAILED(hr)) {
            ShowPluginMessage(L10n::Text(L"启动 Subsonic 元数据索引任务失败。如需要详细信息，请启用调试日志。", L"Failed to start Subsonic metadata index task. Enable Debug logging for details."));
            LogInfo(L"Metadata index task start failed. HRESULT=" + std::to_wstring(static_cast<long>(hr)));
            return;
        }
        LogInfo(L"Subsonic metadata index refresh started in background.");
        return;
    }

    LogInfo(L"IAIMPServiceThreads is unavailable; metadata index build will run synchronously.");
    uint64_t generation = 0;
    {
        std::lock_guard lock(metadataIndexMutex_);
        if (metadataIndexRunning_) {
            ShowPluginMessage(L10n::Text(L"Subsonic 元数据索引正在刷新中。", L"Subsonic metadata index is already being refreshed."));
            return;
        }
        metadataIndexRunning_ = true;
        metadataIndexTask_ = 0;
        generation = ++metadataIndexGeneration_;
    }
    RunMetadataIndexTask(nullptr, generation);
}

void AimpSubsonicPlugin::RunMetadataIndexTask(IAIMPTaskOwner* owner, uint64_t generation) {
    auto isCanceled = [owner]() {
        return owner && owner->IsCanceled();
    };

    MetadataIndexBuildResult result;
    if (!repository_) {
        LogInfo(L"Metadata index task skipped: repository is unavailable.");
    } else {
        result = repository_->BuildMetadataIndex(isCanceled);
    }

    if (!result.canceled && core_.get()) {
        IAIMPServiceThreads* threads = nullptr;
        if (SUCCEEDED(core_.get()->QueryInterface(IID_IAIMPServiceThreads, reinterpret_cast<void**>(&threads))) && threads) {
            auto* task = new MetadataIndexCompletionTask(this, result, generation);
            const HRESULT hr = threads->ExecuteInMainThread(task, 0);
            task->Release();
            threads->Release();
            if (SUCCEEDED(hr)) {
                return;
            }
            LogInfo(L"Metadata index completion could not be queued to main thread. HRESULT=" +
                std::to_wstring(static_cast<long>(hr)));
        }
    }

    if (result.canceled) {
        LogInfo(L"Metadata index task finished as canceled.");
    }
    FinishMetadataIndexTask(generation);
}

void AimpSubsonicPlugin::CompleteMetadataIndexTask(const MetadataIndexBuildResult& result, uint64_t generation) {
    {
        std::lock_guard lock(metadataIndexMutex_);
        if (generation != metadataIndexGeneration_) {
            LogInfo(L"Stale metadata index completion ignored. Generation=" +
                std::to_wstring(generation) +
                L", Current=" + std::to_wstring(metadataIndexGeneration_));
            return;
        }
    }

    if (!result.canceled && musicLibrary_) {
        musicLibrary_->FlushCache(0);
        musicLibrary_->NotifyChanged();
    }

    FinishMetadataIndexTask(generation);

    if (!result.canceled && core_.get()) {
        ShowPluginMessage(FormatMetadataIndexSummary(result));
    } else if (result.canceled) {
        LogInfo(L"Metadata index task finished as canceled.");
    }
}

void AimpSubsonicPlugin::FinishMetadataIndexTask(uint64_t generation) {
    MetadataIndexTask* task = nullptr;
    {
        std::lock_guard lock(metadataIndexMutex_);
        if (generation != metadataIndexGeneration_) {
            return;
        }
        task = metadataIndexTaskObject_;
        metadataIndexTaskObject_ = nullptr;
        metadataIndexRunning_ = false;
        metadataIndexTask_ = 0;
    }
    if (task) {
        task->Release();
    }
}

void AimpSubsonicPlugin::ImportServerPlaylists(bool interactive) {
    LogInfo(L"Server playlist import requested. Interactive=" + std::to_wstring(interactive ? 1 : 0));
    if (!repository_) {
        if (interactive) {
            ShowPluginMessage(L10n::Text(L"Subsonic 配置未加载。请在 AIMP 选项 -> Subsonic 中配置。", L"Subsonic config is not loaded. Open AIMP Options -> Subsonic."));
        }
        return;
    }

    IAIMPServiceThreads* threads = nullptr;
    if (core_.get() && SUCCEEDED(core_.get()->QueryInterface(IID_IAIMPServiceThreads, reinterpret_cast<void**>(&threads))) && threads) {
        HRESULT hr = S_OK;
        PlaylistImportTask* task = nullptr;
        {
            std::lock_guard lock(playlistImportMutex_);
            if (playlistImportRunning_) {
                threads->Release();
                if (interactive) {
                    ShowPluginMessage(L10n::Text(L"Subsonic 播放列表正在导入中。", L"Subsonic playlists are already being imported."));
                }
                return;
            }
            playlistImportRunning_ = true;
            playlistImportTask_ = 0;
            const uint64_t generation = ++playlistImportGeneration_;
            task = new PlaylistImportTask(this, generation, interactive);
            hr = threads->ExecuteInThread(task, &playlistImportTask_);
            if (FAILED(hr)) {
                playlistImportRunning_ = false;
                playlistImportTask_ = 0;
                ++playlistImportGeneration_;
            } else {
                playlistImportTaskObject_ = task;
                task = nullptr;
            }
        }
        if (task) {
            task->Release();
        }
        threads->Release();
        if (FAILED(hr)) {
            if (interactive) {
                ShowPluginMessage(L10n::Text(L"启动 Subsonic 播放列表导入任务失败。请启用调试日志获取详细信息。", L"Failed to start Subsonic playlist import task. Enable Debug logging for details."));
            }
            LogInfo(L"Playlist import task start failed. HRESULT=" + std::to_wstring(static_cast<long>(hr)));
            return;
        }
        LogInfo(L"Subsonic playlist import started in background.");
        return;
    }

    LogInfo(L"IAIMPServiceThreads is unavailable; playlist import will run synchronously.");
    uint64_t generation = 0;
    {
        std::lock_guard lock(playlistImportMutex_);
        if (playlistImportRunning_) {
            if (interactive) {
                ShowPluginMessage(L10n::Text(L"Subsonic 播放列表正在导入中。", L"Subsonic playlists are already being imported."));
            }
            return;
        }
        playlistImportRunning_ = true;
        playlistImportTask_ = 0;
        generation = ++playlistImportGeneration_;
    }
    RunPlaylistImportTask(nullptr, generation, interactive);
}

void AimpSubsonicPlugin::RunPlaylistImportTask(IAIMPTaskOwner* owner, uint64_t generation, bool interactive) {
    auto isCanceled = [owner]() {
        return owner && owner->IsCanceled();
    };

    std::vector<ServerPlaylistSnapshot> snapshots;
    if (!repository_) {
        LogInfo(L"Playlist import task skipped: repository is unavailable.");
    } else {
        auto playlists = repository_->GetPlaylists();
        if (playlists.empty()) {
            playlists = repository_->GetCachedPlaylists();
        }
        for (const auto& playlist : playlists) {
            if (isCanceled()) {
                LogInfo(L"Playlist import task canceled while loading tracks.");
                FinishPlaylistImportTask(generation);
                return;
            }
            if (playlist.id.empty()) {
                continue;
            }
            ServerPlaylistSnapshot snapshot;
            snapshot.playlist = playlist;
            snapshot.tracks = repository_->GetPlaylistTracks(playlist.id);
            if (snapshot.tracks.empty()) {
                snapshot.tracks = repository_->GetCachedPlaylistTracks(playlist.id);
            }
            snapshots.push_back(std::move(snapshot));
        }
        LogInfo(L"Playlist import task loaded server playlists: " + std::to_wstring(snapshots.size()));
    }

    if (owner == nullptr) {
        // Synchronous fallback already runs on the calling (main) thread.
        ApplyServerPlaylists(snapshots, generation, interactive);
        return;
    }

    if (core_.get()) {
        IAIMPServiceThreads* threads = nullptr;
        if (SUCCEEDED(core_.get()->QueryInterface(IID_IAIMPServiceThreads, reinterpret_cast<void**>(&threads))) && threads) {
            auto* task = new PlaylistImportApplyTask(this, std::move(snapshots), generation, interactive);
            const HRESULT hr = threads->ExecuteInMainThread(task, 0);
            task->Release();
            threads->Release();
            if (SUCCEEDED(hr)) {
                return;
            }
            LogInfo(L"Playlist import apply could not be queued to main thread. HRESULT=" +
                std::to_wstring(static_cast<long>(hr)));
        }
    }
    FinishPlaylistImportTask(generation);
}

void AimpSubsonicPlugin::ApplyServerPlaylists(const std::vector<ServerPlaylistSnapshot>& snapshots, uint64_t generation, bool interactive) {
    {
        std::lock_guard lock(playlistImportMutex_);
        if (generation != playlistImportGeneration_) {
            LogInfo(L"Stale playlist import apply ignored. Generation=" + std::to_wstring(generation) +
                L", Current=" + std::to_wstring(playlistImportGeneration_));
            return;
        }
    }

    // Non-interactive startup sync only refreshes playlists that still exist
    // in AIMP; playlists the user closed are not recreated automatically.
    const bool createMissing = interactive;
    int created = 0;
    int updated = 0;
    int skipped = 0;
    int failed = 0;

    IAIMPServicePlaylistManager* service = nullptr;
    if (repository_ && core_.get() &&
        SUCCEEDED(core_.get()->QueryInterface(IID_IAIMPServicePlaylistManager, reinterpret_cast<void**>(&service))) && service) {
        const auto links = repository_->GetPlaylistLinks();
        for (const auto& snapshot : snapshots) {
            IAIMPPlaylist* playlist = nullptr;
            bool isNew = false;

            const auto link = links.find(snapshot.playlist.id);
            if (link != links.end()) {
                IAIMPString* aimpId = MakeAimpString(core_.get(), link->second);
                if (aimpId) {
                    if (FAILED(service->GetLoadedPlaylistByID(aimpId, &playlist))) {
                        playlist = nullptr;
                    }
                    aimpId->Release();
                }
            }
            if (!playlist && !snapshot.playlist.name.empty()) {
                const int count = service->GetLoadedPlaylistCount();
                for (int i = 0; i < count && !playlist; ++i) {
                    IAIMPPlaylist* candidate = nullptr;
                    if (SUCCEEDED(service->GetLoadedPlaylist(i, &candidate)) && candidate) {
                        const std::wstring name = GetPlaylistPropertyString(candidate, AIMP_PLAYLIST_PROPID_NAME);
                        if (_wcsicmp(name.c_str(), snapshot.playlist.name.c_str()) == 0) {
                            playlist = candidate;
                        } else {
                            candidate->Release();
                        }
                    }
                }
            }
            if (!playlist) {
                if (!createMissing) {
                    ++skipped;
                    continue;
                }
                const std::wstring caption = snapshot.playlist.name.empty() ? snapshot.playlist.id : snapshot.playlist.name;
                IAIMPString* name = MakeAimpString(core_.get(), caption);
                if (name) {
                    if (FAILED(service->CreatePlaylist(name, FALSE, &playlist))) {
                        playlist = nullptr;
                    } else {
                        isNew = true;
                    }
                    name->Release();
                }
            }
            if (!playlist) {
                ++failed;
                LogInfo(L"Playlist import could not open or create AIMP playlist. Playlist=" + snapshot.playlist.name);
                continue;
            }

            if (!isNew && !snapshot.playlist.name.empty() &&
                GetPlaylistPropertyString(playlist, AIMP_PLAYLIST_PROPID_NAME) != snapshot.playlist.name) {
                SetPlaylistPropertyString(core_.get(), playlist, AIMP_PLAYLIST_PROPID_NAME, snapshot.playlist.name);
            }

            playlist->BeginUpdate();
            playlist->DeleteAll();
            playlist->EndUpdate();
            const HRESULT hr = snapshot.tracks.empty()
                ? S_OK
                : AddTracksToPlaylist(playlist, snapshot.tracks, PlaybackMode::DirectUrl);

            const std::wstring aimpId = GetPlaylistPropertyString(playlist, AIMP_PLAYLIST_PROPID_ID);
            if (!aimpId.empty()) {
                repository_->SetPlaylistLink(snapshot.playlist.id, aimpId);
            }

            if (FAILED(hr)) {
                ++failed;
                LogInfo(L"Playlist import failed to fill playlist. Playlist=" + snapshot.playlist.name +
                    L", HRESULT=" + std::to_wstring(static_cast<long>(hr)));
            } else if (isNew) {
                ++created;
            } else {
                ++updated;
            }
            LogInfo(L"Playlist import applied. Playlist=" + snapshot.playlist.name +
                L", Tracks=" + std::to_wstring(snapshot.tracks.size()) +
                L", New=" + std::to_wstring(isNew ? 1 : 0));
            playlist->Release();
        }
        service->Release();
    } else {
        LogInfo(L"Playlist import apply skipped: IAIMPServicePlaylistManager unavailable.");
    }

    FinishPlaylistImportTask(generation);
    LogInfo(L"Playlist import finished. Created=" + std::to_wstring(created) +
        L", Updated=" + std::to_wstring(updated) +
        L", Skipped=" + std::to_wstring(skipped) +
        L", Failed=" + std::to_wstring(failed));

    if (interactive) {
        if (snapshots.empty()) {
            ShowPluginMessage(L10n::Text(L"服务器上没有找到 Subsonic 播放列表。", L"No Subsonic playlists were found on the server."));
        } else {
            std::wstringstream message;
            if (L10n::IsChineseUi()) {
                message << L"已将 Subsonic 播放列表导入播放列表管理器。\r\n"
                    << L"新建: " << created
                    << L", 更新: " << updated
                    << L", 失败: " << failed << L"。";
            } else {
                message << L"Subsonic playlists imported to the Playlist Manager.\r\n"
                    << L"Created: " << created
                    << L", Updated: " << updated
                    << L", Failed: " << failed << L".";
            }
            ShowPluginMessage(message.str());
        }
    }
}

void AimpSubsonicPlugin::FinishPlaylistImportTask(uint64_t generation) {
    PlaylistImportTask* task = nullptr;
    {
        std::lock_guard lock(playlistImportMutex_);
        if (generation != playlistImportGeneration_) {
            return;
        }
        task = playlistImportTaskObject_;
        playlistImportTaskObject_ = nullptr;
        playlistImportRunning_ = false;
        playlistImportTask_ = 0;
    }
    if (task) {
        task->Release();
    }
}

void AimpSubsonicPlugin::CancelPlaylistImportTask() {
    TTaskHandle task = 0;
    PlaylistImportTask* taskObject = nullptr;
    {
        std::lock_guard lock(playlistImportMutex_);
        task = playlistImportTask_;
        taskObject = playlistImportTaskObject_;
        playlistImportTaskObject_ = nullptr;
        if (playlistImportRunning_ || playlistImportTask_ != 0) {
            ++playlistImportGeneration_;
        }
        playlistImportRunning_ = false;
        playlistImportTask_ = 0;
    }
    if (task != 0 && core_.get()) {
        IAIMPServiceThreads* threads = nullptr;
        if (SUCCEEDED(core_.get()->QueryInterface(IID_IAIMPServiceThreads, reinterpret_cast<void**>(&threads))) && threads) {
            LogInfo(L"Canceling active playlist import task.");
            threads->Cancel(task, AIMP_SERVICE_THREADS_FLAGS_WAITFOR);
            threads->Release();
        }
    }
    if (taskObject) {
        taskObject->Release();
    }
}

void AimpSubsonicPlugin::RefreshImportedPlaylistsAtStartup() {
    if (startupPlaylistSyncDone_) {
        return;
    }
    startupPlaylistSyncDone_ = true;
    if (!repository_) {
        return;
    }
    if (repository_->GetPlaylistLinks().empty()) {
        LogInfo(L"Startup playlist sync skipped: no playlists were imported before.");
        return;
    }
    LogInfo(L"Startup playlist sync started.");
    ImportServerPlaylists(false);
}

void AimpSubsonicPlugin::CancelMetadataIndexTask() {
    TTaskHandle task = 0;
    MetadataIndexTask* taskObject = nullptr;
    {
        std::lock_guard lock(metadataIndexMutex_);
        task = metadataIndexTask_;
        taskObject = metadataIndexTaskObject_;
        metadataIndexTaskObject_ = nullptr;
        if (metadataIndexRunning_ || metadataIndexTask_ != 0) {
            ++metadataIndexGeneration_;
        }
        metadataIndexRunning_ = false;
        metadataIndexTask_ = 0;
    }
    if (task != 0 && core_.get()) {
        IAIMPServiceThreads* threads = nullptr;
        if (SUCCEEDED(core_.get()->QueryInterface(IID_IAIMPServiceThreads, reinterpret_cast<void**>(&threads))) && threads) {
            LogInfo(L"Canceling active metadata index task.");
            threads->Cancel(task, AIMP_SERVICE_THREADS_FLAGS_WAITFOR);
            threads->Release();
        }
    }
    if (taskObject) {
        taskObject->Release();
    }
}

void AimpSubsonicPlugin::OpenSettings() {
    LogInfo(L"OpenSettings started.");
    if (!optionsFrame_) {
        ShowPluginMessage(L10n::Text(L"Subsonic 设置面板未初始化。", L"Subsonic settings frame is not initialized."));
        return;
    }
    IAIMPServiceOptionsDialog* options = nullptr;
    if (SUCCEEDED(core_.get()->QueryInterface(IID_IAIMPServiceOptionsDialog, reinterpret_cast<void**>(&options))) && options) {
        options->FrameShow(static_cast<IAIMPOptionsDialogFrame*>(optionsFrame_), TRUE);
        options->Release();
    } else {
        ShowPluginMessage(L10n::Text(L"无法打开 AIMP 选项对话框。", L"Cannot open AIMP options dialog."));
    }
}

HRESULT AimpSubsonicPlugin::RegisterMenu() {
    IAIMPServiceMenuManager* menuService = nullptr;
    if (FAILED(core_.get()->QueryInterface(IID_IAIMPServiceMenuManager, reinterpret_cast<void**>(&menuService)))) {
        return E_FAIL;
    }

    MenuAction* copySelectedUrlsAction = new MenuAction(this, MenuCommand::CopySelectedStreamUrls);
    MenuAction* refreshMetadataCacheAction = new MenuAction(this, MenuCommand::RefreshMetadataCache);
    MenuAction* importPlaylistsAction = new MenuAction(this, MenuCommand::ImportServerPlaylists);
    MenuAction* openSettingsAction = new MenuAction(this, MenuCommand::OpenSettings);
    HRESULT firstError = E_FAIL;
    bool registeredAny = false;

    const std::pair<int, const wchar_t*> locations[] = {
        { AIMP_MENUID_COMMON_UTILITIES, L"aimp_subsonic.utilities" },
    };

    for (const auto& location : locations) {
        IAIMPMenuItem* parent = nullptr;
        HRESULT hr = menuService->GetBuiltIn(location.first, &parent);
        if (SUCCEEDED(hr)) {
            IAIMPMenuItem* subsonicMenu = nullptr;
            const std::wstring submenuId = std::wstring(location.second) + L".root";
            hr = core_.get()->CreateObject(IID_IAIMPMenuItem, reinterpret_cast<void**>(&subsonicMenu));
            if (SUCCEEDED(hr) && subsonicMenu) {
                SetMenuString(core_.get(), subsonicMenu, AIMP_MENUITEM_PROPID_ID, submenuId);
                SetMenuString(core_.get(), subsonicMenu, AIMP_MENUITEM_PROPID_NAME, L"Subsonic");
                SetObjectProperty(subsonicMenu, AIMP_MENUITEM_PROPID_PARENT, parent);
                hr = core_.get()->RegisterExtension(IID_IAIMPServiceMenuManager, subsonicMenu);
            }
            if (SUCCEEDED(hr)) {
                const std::wstring refreshId = std::wstring(location.second) + L".refresh_metadata_cache";
                hr = CreateMenuItem(core_.get(), subsonicMenu, refreshMetadataCacheAction, refreshId, L10n::Text(L"构建 / 刷新元数据索引", L"Build / Refresh Metadata Index"));
            }
            if (SUCCEEDED(hr)) {
                const std::wstring importId = std::wstring(location.second) + L".import_server_playlists";
                hr = CreateMenuItem(core_.get(), subsonicMenu, importPlaylistsAction, importId, L10n::Text(L"导入服务器播放列表", L"Import Server Playlists"));
            }
            if (SUCCEEDED(hr)) {
                const std::wstring settingsId = std::wstring(location.second) + L".open_settings";
                hr = CreateMenuItem(core_.get(), subsonicMenu, openSettingsAction, settingsId, L10n::Text(L"设置", L"Settings"));
            }
            SafeRelease(subsonicMenu);
            parent->Release();
        }
        if (SUCCEEDED(hr)) {
            registeredAny = true;
        } else if (firstError == E_FAIL) {
            firstError = hr;
        }
    }

    const std::pair<int, const wchar_t*> contextLocations[] = {
        { AIMP_MENUID_PLAYER_PLAYLIST_CONTEXT_FUNCTIONS, L"aimp_subsonic.context.functions" },
    };
    for (const auto& location : contextLocations) {
        IAIMPMenuItem* parent = nullptr;
        HRESULT hr = menuService->GetBuiltIn(location.first, &parent);
        if (SUCCEEDED(hr)) {
            IAIMPMenuItem* subsonicMenu = nullptr;
            hr = core_.get()->CreateObject(IID_IAIMPMenuItem, reinterpret_cast<void**>(&subsonicMenu));
            if (SUCCEEDED(hr) && subsonicMenu) {
                SetMenuString(core_.get(), subsonicMenu, AIMP_MENUITEM_PROPID_ID, std::wstring(location.second) + L".root");
                SetMenuString(core_.get(), subsonicMenu, AIMP_MENUITEM_PROPID_NAME, L"Subsonic");
                SetObjectProperty(subsonicMenu, AIMP_MENUITEM_PROPID_PARENT, parent);
                hr = core_.get()->RegisterExtension(IID_IAIMPServiceMenuManager, subsonicMenu);
            }
            if (SUCCEEDED(hr)) {
                hr = CreateMenuItem(core_.get(), subsonicMenu, copySelectedUrlsAction,
                    std::wstring(location.second) + L".copy_stream_urls", L10n::Text(L"复制流媒体地址", L"Copy Stream URLs"));
            }
            if (SUCCEEDED(hr)) {
                hr = CreateMenuItem(core_.get(), subsonicMenu, openSettingsAction,
                    std::wstring(location.second) + L".settings", L10n::Text(L"设置", L"Settings"));
            }
            SafeRelease(subsonicMenu);
            parent->Release();
        }
        if (SUCCEEDED(hr)) {
            registeredAny = true;
        } else if (firstError == E_FAIL) {
            firstError = hr;
        }
    }

    const std::pair<int, const wchar_t*> musicLibraryContextLocations[] = {
        { AIMP_MENUID_ML_TABLE_CONTEXT_FUNCTIONS, L"aimp_subsonic.ml_table.functions" },
    };
    for (const auto& location : musicLibraryContextLocations) {
        IAIMPMenuItem* parent = nullptr;
        HRESULT hr = menuService->GetBuiltIn(location.first, &parent);
        if (SUCCEEDED(hr)) {
            IAIMPMenuItem* subsonicMenu = nullptr;
            hr = core_.get()->CreateObject(IID_IAIMPMenuItem, reinterpret_cast<void**>(&subsonicMenu));
            if (SUCCEEDED(hr) && subsonicMenu) {
                SetMenuString(core_.get(), subsonicMenu, AIMP_MENUITEM_PROPID_ID, std::wstring(location.second) + L".root");
                SetMenuString(core_.get(), subsonicMenu, AIMP_MENUITEM_PROPID_NAME, L"Subsonic");
                SetObjectProperty(subsonicMenu, AIMP_MENUITEM_PROPID_PARENT, parent);
                hr = core_.get()->RegisterExtension(IID_IAIMPServiceMenuManager, subsonicMenu);
            }
            if (SUCCEEDED(hr)) {
                hr = CreateMenuItem(core_.get(), subsonicMenu, refreshMetadataCacheAction,
                    std::wstring(location.second) + L".refresh_metadata_cache", L10n::Text(L"构建 / 刷新元数据索引", L"Build / Refresh Metadata Index"));
            }
            if (SUCCEEDED(hr)) {
                hr = CreateMenuItem(core_.get(), subsonicMenu, importPlaylistsAction,
                    std::wstring(location.second) + L".import_server_playlists", L10n::Text(L"导入服务器播放列表", L"Import Server Playlists"));
            }
            if (SUCCEEDED(hr)) {
                hr = CreateMenuItem(core_.get(), subsonicMenu, openSettingsAction,
                    std::wstring(location.second) + L".settings", L10n::Text(L"设置", L"Settings"));
            }
            SafeRelease(subsonicMenu);
            parent->Release();
        }
        if (SUCCEEDED(hr)) {
            registeredAny = true;
        } else if (firstError == E_FAIL) {
            firstError = hr;
        }
    }

    copySelectedUrlsAction->Release();
    refreshMetadataCacheAction->Release();
    importPlaylistsAction->Release();
    openSettingsAction->Release();
    menuService->Release();
    return registeredAny ? S_OK : firstError;
}

HRESULT AimpSubsonicPlugin::RegisterMusicLibrary() {
    musicLibrary_ = new SubsonicMusicLibraryStorage(core_.get(), repository_.get());
    RegisterOwnedExtension(IID_IAIMPServiceMusicLibrary, static_cast<IAIMPMLExtensionDataStorage*>(musicLibrary_));
    LogInfo(L"Subsonic Music Library storage registration requested.");
    return S_OK;
}

HRESULT AimpSubsonicPlugin::RegisterAlbumArtProvider() {
    albumArtProvider_ = new SubsonicAlbumArtProvider(core_.get(), repository_.get(), coverCache_.get());
    RegisterOwnedExtension(IID_IAIMPServiceAlbumArt, static_cast<IAIMPExtensionAlbumArtProvider3*>(albumArtProvider_));
    LogInfo(L"Subsonic album art provider registration requested.");
    return S_OK;
}

HRESULT AimpSubsonicPlugin::RegisterFileInfoProvider() {
    fileInfoProvider_ = new SubsonicFileInfoProvider(core_.get(), repository_.get());
    RegisterOwnedExtension(IID_IAIMPServiceFileInfo, static_cast<IAIMPExtensionFileInfoProvider*>(fileInfoProvider_));
    LogInfo(L"Subsonic file info provider registration requested.");
    return S_OK;
}

HRESULT AimpSubsonicPlugin::RegisterOptions() {
    optionsFrame_ = new SubsonicOptionsFrame(core_.get(), this);
    RegisterOwnedExtension(IID_IAIMPServiceOptionsDialog, static_cast<IAIMPOptionsDialogFrame*>(optionsFrame_));
    LogInfo(L"Subsonic options frame registration requested.");
    return S_OK;
}

HRESULT AimpSubsonicPlugin::RegisterPlaybackTracking() {
    if (messageHooked_) {
        return S_OK;
    }
    if (FAILED(core_.get()->QueryInterface(IID_IAIMPServiceMessageDispatcher, reinterpret_cast<void**>(&messageDispatcher_)))) {
        messageDispatcher_ = nullptr;
        return E_FAIL;
    }
    const HRESULT hr = messageDispatcher_->Hook(static_cast<IAIMPMessageHook*>(this));
    messageHooked_ = SUCCEEDED(hr);
    LogInfo(L"Playback tracking hook " + std::wstring(messageHooked_ ? L"registered." : L"failed."));
    return hr;
}

HRESULT AimpSubsonicPlugin::RegisterPlaylistMetadataListener() {
    if (playlistMetadataListener_) {
        AttachPlaylistMetadataListeners();
        return S_OK;
    }
    playlistMetadataListener_ = new PlaylistMetadataListener(this);
    RegisterOwnedExtension(IID_IAIMPServicePlaylistManager,
        static_cast<IAIMPExtensionPlaylistManagerListener*>(playlistMetadataListener_));
    AttachPlaylistMetadataListeners();
    LogInfo(L"Subsonic playlist metadata listener registration requested.");
    return S_OK;
}

void AimpSubsonicPlugin::UnregisterPlaybackTracking() {
    if (messageDispatcher_) {
        if (messageHooked_) {
            messageDispatcher_->Unhook(static_cast<IAIMPMessageHook*>(this));
        }
        messageHooked_ = false;
        SafeRelease(messageDispatcher_);
    }
    ResetScrobbleState();
}

void AimpSubsonicPlugin::UnregisterPlaylistMetadataListener() {
    if (!playlistMetadataListener_) {
        metadataWatchedPlaylists_.clear();
        return;
    }
    for (auto* playlist : metadataWatchedPlaylists_) {
        if (playlist) {
            playlist->ListenerRemove(static_cast<IAIMPPlaylistListener*>(playlistMetadataListener_));
            playlist->Release();
        }
    }
    metadataWatchedPlaylists_.clear();
}

void AimpSubsonicPlugin::AttachPlaylistMetadataListeners() {
    if (!playlistMetadataListener_ || !core_.get()) {
        return;
    }
    IAIMPServicePlaylistManager* playlistService = nullptr;
    if (FAILED(core_.get()->QueryInterface(IID_IAIMPServicePlaylistManager, reinterpret_cast<void**>(&playlistService))) || !playlistService) {
        return;
    }
    const int count = playlistService->GetLoadedPlaylistCount();
    for (int i = 0; i < count; ++i) {
        IAIMPPlaylist* playlist = nullptr;
        if (SUCCEEDED(playlistService->GetLoadedPlaylist(i, &playlist)) && playlist) {
            AttachPlaylistMetadataListener(playlist);
            playlist->Release();
        }
    }
    playlistService->Release();
}

void AimpSubsonicPlugin::AttachPlaylistMetadataListener(IAIMPPlaylist* playlist) {
    if (!playlist || !playlistMetadataListener_) {
        return;
    }
    if (std::find(metadataWatchedPlaylists_.begin(), metadataWatchedPlaylists_.end(), playlist) != metadataWatchedPlaylists_.end()) {
        return;
    }
    if (SUCCEEDED(playlist->ListenerAdd(static_cast<IAIMPPlaylistListener*>(playlistMetadataListener_)))) {
        playlist->AddRef();
        metadataWatchedPlaylists_.push_back(playlist);
        LogInfo(L"Subsonic playlist metadata listener attached.");
    }
}

void AimpSubsonicPlugin::DetachPlaylistMetadataListener(IAIMPPlaylist* playlist) {
    if (!playlist || !playlistMetadataListener_) {
        return;
    }
    auto it = std::find(metadataWatchedPlaylists_.begin(), metadataWatchedPlaylists_.end(), playlist);
    if (it == metadataWatchedPlaylists_.end()) {
        return;
    }
    playlist->ListenerRemove(static_cast<IAIMPPlaylistListener*>(playlistMetadataListener_));
    playlist->Release();
    metadataWatchedPlaylists_.erase(it);
}

void AimpSubsonicPlugin::OnPlaylistContentChanged(LongWord flags) {
    if ((flags & (AIMP_PLAYLIST_NOTIFY_CONTENT | AIMP_PLAYLIST_NOTIFY_FILEINFO)) == 0) {
        return;
    }
    RepairLoadedPlaylistMetadata(false);
}

void AimpSubsonicPlugin::RepairLoadedPlaylistMetadata(bool allowNetwork) {
    if (!core_.get()) {
        return;
    }
    IAIMPServicePlaylistManager* playlistService = nullptr;
    if (FAILED(core_.get()->QueryInterface(IID_IAIMPServicePlaylistManager, reinterpret_cast<void**>(&playlistService))) || !playlistService) {
        return;
    }
    const int count = playlistService->GetLoadedPlaylistCount();
    for (int i = 0; i < count; ++i) {
        IAIMPPlaylist* playlist = nullptr;
        if (SUCCEEDED(playlistService->GetLoadedPlaylist(i, &playlist)) && playlist) {
            RepairPlaylistMetadata(playlist, allowNetwork);
            playlist->Release();
        }
    }
    playlistService->Release();
}

void AimpSubsonicPlugin::RepairPlaylistMetadata(IAIMPPlaylist* playlist, bool allowNetwork) {
    if (!playlist || !repository_ || repairingPlaylistMetadata_) {
        return;
    }
    repairingPlaylistMetadata_ = true;
    int fixed = 0;
    const int count = playlist->GetItemCount();
    playlist->BeginUpdate();
    for (int i = 0; i < count; ++i) {
        IAIMPPlaylistItem* item = nullptr;
        if (SUCCEEDED(playlist->GetItem(i, IID_IAIMPPlaylistItem, reinterpret_cast<void**>(&item))) && item) {
            if (RepairPlaylistItemMetadata(item, allowNetwork)) {
                ++fixed;
            }
            item->Release();
        }
    }
    playlist->EndUpdate();
    repairingPlaylistMetadata_ = false;
    if (fixed > 0) {
        LogInfo(L"Subsonic playlist metadata repaired. Items=" + std::to_wstring(fixed));
    }
}

bool AimpSubsonicPlugin::RepairPlaylistItemMetadata(IAIMPPlaylistItem* item, bool allowNetwork) {
    if (!item || !repository_) {
        return false;
    }

    const std::wstring itemFileName = GetPlaylistItemString(item, AIMP_PLAYLISTITEM_PROPID_FILENAME);
    const std::wstring displayText = GetPlaylistItemString(item, AIMP_PLAYLISTITEM_PROPID_DISPLAYTEXT);

    std::wstring songId = ExtractSubsonicSongId(itemFileName);
    std::wstring currentTitle;
    IAIMPFileInfo* currentInfo = nullptr;
    if (SUCCEEDED(item->GetValueAsObject(AIMP_PLAYLISTITEM_PROPID_FILEINFO, IID_IAIMPFileInfo, reinterpret_cast<void**>(&currentInfo))) && currentInfo) {
        if (songId.empty()) {
            songId = ExtractSubsonicSongId(currentInfo);
        }
        currentTitle = GetFileInfoString(currentInfo, AIMP_FILEINFO_PROPID_TITLE);
        currentInfo->Release();
    }
    if (songId.empty()) {
        return false;
    }

    const bool needsDisplayRepair = displayText.empty() || IsLikelyRawUri(displayText);
    const bool needsFileInfoRepair = currentTitle.empty() || IsLikelyRawUri(currentTitle);
    if (!needsDisplayRepair && !needsFileInfoRepair) {
        return false;
    }

    std::optional<TrackInfo> track = repository_->FindTrackMetadata(songId);
    if (!track && allowNetwork) {
        track = repository_->GetSong(songId);
    }
    if (!track) {
        return false;
    }

    IAIMPFileInfo* info = nullptr;
    if (SUCCEEDED(core_.get()->CreateObject(IID_IAIMPFileInfo, reinterpret_cast<void**>(&info))) && info) {
        SetTrackInfo(info, *track, PlaybackMode::DirectUrl);
        item->SetValueAsObject(AIMP_PLAYLISTITEM_PROPID_FILEINFO, info);
        info->Release();
    }
    SetPlaylistItemString(core_.get(), item, AIMP_PLAYLISTITEM_PROPID_DISPLAYTEXT, TrackDisplayText(*track));
    return true;
}

void AimpSubsonicPlugin::HandleStreamStarted() {
    if (!playbackTracker_) {
        return;
    }
    double duration = 0.0;
    double position = 0.0;
    const std::wstring songId = CurrentPlaybackSongId(&duration, &position);
    playbackTracker_->HandleStreamStarted(songId, duration, position);
}

void AimpSubsonicPlugin::HandleStreamEnded() {
    if (playbackTracker_) {
        playbackTracker_->HandleStreamEnded();
    }
}

void AimpSubsonicPlugin::HandlePlaybackPositionUpdate() {
    if (!playbackTracker_) {
        return;
    }

    double duration = 0.0;
    double position = 0.0;
    const std::wstring songId = CurrentPlaybackSongId(&duration, &position);
    playbackTracker_->HandlePlaybackPositionUpdate(songId, duration, position);
}

void AimpSubsonicPlugin::ResetScrobbleState() {
    if (playbackTracker_) {
        playbackTracker_->Reset();
    }
}

std::wstring AimpSubsonicPlugin::CurrentPlaybackSongId(double* durationSeconds, double* positionSeconds) {
    if (durationSeconds) {
        *durationSeconds = 0.0;
    }
    if (positionSeconds) {
        *positionSeconds = 0.0;
    }

    IAIMPServicePlayer* player = nullptr;
    if (FAILED(core_.get()->QueryInterface(IID_IAIMPServicePlayer, reinterpret_cast<void**>(&player))) || !player) {
        return {};
    }

    IAIMPFileInfo* info = nullptr;
    std::wstring filenameUri;
    std::wstring urlUri;
    if (SUCCEEDED(player->GetInfo(&info)) && info) {
        IAIMPString* fileName = nullptr;
        if (SUCCEEDED(info->GetValueAsObject(AIMP_FILEINFO_PROPID_FILENAME, IID_IAIMPString, reinterpret_cast<void**>(&fileName)))) {
            filenameUri = FromAimpString(fileName);
            fileName->Release();
        }
        IAIMPString* url = nullptr;
        if (SUCCEEDED(info->GetValueAsObject(AIMP_FILEINFO_PROPID_URL, IID_IAIMPString, reinterpret_cast<void**>(&url)))) {
            urlUri = FromAimpString(url);
            url->Release();
        }
        if (durationSeconds) {
            info->GetValueAsFloat(AIMP_FILEINFO_PROPID_DURATION, durationSeconds);
        }
        info->Release();
    }

    if (durationSeconds && *durationSeconds <= 0.0) {
        player->GetDuration(durationSeconds);
    }
    if (positionSeconds) {
        player->GetPosition(positionSeconds);
    }
    player->Release();

    std::wstring songId = ExtractSubsonicSongId(filenameUri);
    if (songId.empty()) {
        songId = ExtractSubsonicSongId(urlUri);
    }
    return songId;
}

HRESULT AimpSubsonicPlugin::AddTracksToPlaylist(const std::vector<TrackInfo>& tracks, PlaybackMode mode) {
    LogInfo(L"AddTracksToPlaylist started. Count=" + std::to_wstring(tracks.size()) +
        L", Mode=" + std::wstring(PlaybackModeName(mode)));
    IAIMPServicePlaylistManager* playlistService = nullptr;
    if (FAILED(core_.get()->QueryInterface(IID_IAIMPServicePlaylistManager, reinterpret_cast<void**>(&playlistService)))) {
        LogInfo(L"AddTracksToPlaylist failed: IAIMPServicePlaylistManager unavailable.");
        return E_FAIL;
    }

    IAIMPPlaylist* playlist = nullptr;
    HRESULT hr = playlistService->GetActivePlaylist(&playlist);
    if (FAILED(hr)) {
        LogInfo(L"AddTracksToPlaylist failed: GetActivePlaylist HRESULT=" + std::to_wstring(static_cast<long>(hr)));
        playlistService->Release();
        return hr;
    }

    hr = AddTracksToPlaylist(playlist, tracks, mode);
    playlist->Release();
    playlistService->Release();
    return hr;
}

HRESULT AimpSubsonicPlugin::AddTracksToPlaylist(IAIMPPlaylist* playlist, const std::vector<TrackInfo>& tracks, PlaybackMode mode) {
    if (!playlist) {
        return E_POINTER;
    }
    IAIMPObjectList* list = nullptr;
    HRESULT hr = core_.get()->CreateObject(IID_IAIMPObjectList, reinterpret_cast<void**>(&list));
    if (FAILED(hr)) {
        LogInfo(L"AddTracksToPlaylist failed: cannot create IAIMPObjectList HRESULT=" + std::to_wstring(static_cast<long>(hr)));
        return hr;
    }

    for (const auto& track : tracks) {
        IAIMPFileInfo* info = nullptr;
        if (SUCCEEDED(core_.get()->CreateObject(IID_IAIMPFileInfo, reinterpret_cast<void**>(&info)))) {
            if (SUCCEEDED(SetTrackInfo(info, track, mode))) {
                list->Add(info);
                LogInfo(L"Prepared playlist item. Id=" + track.id +
                    L", Title=" + track.title +
                    L", Suffix=" + track.suffix);
            }
            info->Release();
        }
    }

    if (list->GetCount() == 0) {
        list->Release();
        return E_FAIL;
    }

    const int startIndex = playlist->GetItemCount();
    hr = playlist->AddList(list, AIMP_PLAYLIST_ADD_FLAGS_NOCHECKFORMAT | AIMP_PLAYLIST_ADD_FLAGS_FILEINFO, -1);
    LogInfo(L"Playlist AddList completed. Items=" + std::to_wstring(list->GetCount()) +
        L", HRESULT=" + std::to_wstring(static_cast<long>(hr)));
    list->Release();

    if (SUCCEEDED(hr)) {
        const HRESULT metadataHr = ApplyPlaylistItemMetadata(playlist, startIndex, tracks, mode);
        if (FAILED(metadataHr)) {
            LogInfo(L"ApplyPlaylistItemMetadata failed. HRESULT=" + std::to_wstring(static_cast<long>(metadataHr)));
        }
    }
    return hr;
}

HRESULT AimpSubsonicPlugin::ApplyPlaylistItemMetadata(IAIMPPlaylist* playlist, int startIndex, const std::vector<TrackInfo>& tracks, PlaybackMode mode) {
    if (!playlist) {
        return E_POINTER;
    }
    HRESULT result = S_OK;
    for (size_t i = 0; i < tracks.size(); ++i) {
        IAIMPPlaylistItem* item = nullptr;
        const int itemIndex = startIndex + static_cast<int>(i);
        HRESULT hr = playlist->GetItem(itemIndex, IID_IAIMPPlaylistItem, reinterpret_cast<void**>(&item));
        if (FAILED(hr) || !item) {
            result = hr;
            continue;
        }

        IAIMPFileInfo* info = nullptr;
        if (SUCCEEDED(core_.get()->CreateObject(IID_IAIMPFileInfo, reinterpret_cast<void**>(&info)))) {
            SetTrackInfo(info, tracks[i], mode);
            item->SetValueAsObject(AIMP_PLAYLISTITEM_PROPID_FILEINFO, info);
            info->Release();
        }
        SetPlaylistItemString(core_.get(), item, AIMP_PLAYLISTITEM_PROPID_DISPLAYTEXT, TrackDisplayText(tracks[i]));
        item->ReloadInfo();
        item->Release();
    }
    return result;
}

HRESULT AimpSubsonicPlugin::SetTrackInfo(IAIMPFileInfo* info, const TrackInfo& track, PlaybackMode mode) {
    const std::wstring streamUri = repository_ ? repository_->BuildStreamUrl(track.id) : L"";
    std::wstring uri = streamUri;
    if (uri.empty()) {
        uri = L"subsonic-online-missing-url://" + track.id;
    }
    LogInfo(L"SetTrackInfo. Mode=" + std::wstring(PlaybackModeName(mode)) +
        L", Id=" + track.id +
        L", URI=" + RedactSensitiveUrl(uri));
    SetFileInfoString(core_.get(), info, AIMP_FILEINFO_PROPID_FILENAME, uri);
    SetFileInfoString(core_.get(), info, AIMP_FILEINFO_PROPID_URL, streamUri.empty() ? uri : streamUri);
    SetFileInfoString(core_.get(), info, AIMP_FILEINFO_PROPID_TITLE, track.title);
    SetFileInfoString(core_.get(), info, AIMP_FILEINFO_PROPID_ARTIST, track.artist);
    SetFileInfoString(core_.get(), info, AIMP_FILEINFO_PROPID_ALBUM, track.album);
    SetFileInfoString(core_.get(), info, AIMP_FILEINFO_PROPID_ALBUMARTIST, track.albumArtist);
    SetFileInfoString(core_.get(), info, AIMP_FILEINFO_PROPID_GENRE, track.genre);
    if (track.year > 0) {
        SetFileInfoString(core_.get(), info, AIMP_FILEINFO_PROPID_DATE, std::to_wstring(track.year));
    }
    if (track.trackNumber > 0) {
        SetFileInfoString(core_.get(), info, AIMP_FILEINFO_PROPID_TRACKNUMBER, std::to_wstring(track.trackNumber));
    }
    if (track.rating > 0) {
        info->SetValueAsInt32(AIMP_FILEINFO_PROPID_RATING, track.rating);
    }
    if (track.durationSeconds > 0) {
        info->SetValueAsFloat(AIMP_FILEINFO_PROPID_DURATION, static_cast<double>(track.durationSeconds));
    }
    if (track.size > 0) {
        info->SetValueAsInt64(AIMP_FILEINFO_PROPID_FILESIZE, track.size);
    }
    return S_OK;
}

std::vector<TrackInfo> AimpSubsonicPlugin::CollectSubsonicTracks(IAIMPPlaylist* playlist, bool selectedOnly) {
    std::vector<TrackInfo> tracks;
    if (!playlist) {
        return tracks;
    }

    const int count = playlist->GetItemCount();
    for (int i = 0; i < count; ++i) {
        IAIMPPlaylistItem* item = nullptr;
        if (FAILED(playlist->GetItem(i, IID_IAIMPPlaylistItem, reinterpret_cast<void**>(&item))) || !item) {
            continue;
        }
        if (selectedOnly) {
            int selected = 0;
            if (FAILED(item->GetValueAsInt32(AIMP_PLAYLISTITEM_PROPID_SELECTED, &selected)) || selected == 0) {
                item->Release();
                continue;
            }
        }

        std::wstring songId = ExtractSubsonicSongId(GetPlaylistItemString(item, AIMP_PLAYLISTITEM_PROPID_FILENAME));
        TrackInfo track;
        IAIMPFileInfo* info = nullptr;
        if (SUCCEEDED(item->GetValueAsObject(AIMP_PLAYLISTITEM_PROPID_FILEINFO, IID_IAIMPFileInfo, reinterpret_cast<void**>(&info))) && info) {
            if (songId.empty()) {
                songId = ExtractSubsonicSongId(info);
            }
            if (!songId.empty()) {
                track = BuildTrackFromFileInfo(songId, info);
            }
            info->Release();
        }

        if (songId.empty()) {
            item->Release();
            continue;
        }

        if (track.id.empty()) {
            track.id = songId;
            track.title = GetPlaylistItemString(item, AIMP_PLAYLISTITEM_PROPID_DISPLAYTEXT);
            if (track.title.empty()) {
                track.title = L"Subsonic song " + songId;
            }
        }
        tracks.push_back(track);
        item->Release();
    }
    LogInfo(L"Collected Subsonic tracks from active playlist. Count=" + std::to_wstring(tracks.size()) +
        L", SelectedOnly=" + std::to_wstring(selectedOnly ? 1 : 0));
    return tracks;
}

std::vector<TrackInfo> AimpSubsonicPlugin::CollectActivePlaylistTracks(bool selectedOnly) {
    IAIMPServicePlaylistManager* playlistService = nullptr;
    if (FAILED(core_.get()->QueryInterface(IID_IAIMPServicePlaylistManager, reinterpret_cast<void**>(&playlistService)))) {
        return {};
    }

    IAIMPPlaylist* playlist = nullptr;
    HRESULT hr = playlistService->GetActivePlaylist(&playlist);
    playlistService->Release();
    if (FAILED(hr) || !playlist) {
        return {};
    }

    auto tracks = CollectSubsonicTracks(playlist, selectedOnly);
    playlist->Release();
    return tracks;
}

void AimpSubsonicPlugin::RegisterOwnedExtension(REFIID serviceId, IUnknown* extension) {
    if (!extension || !core_.get()) {
        return;
    }
    if (SUCCEEDED(core_.get()->RegisterExtension(serviceId, extension))) {
        extension->AddRef();
        registeredExtensions_.push_back(extension);
    }
}

void AimpSubsonicPlugin::ClearRegisteredExtensions() {
    if (core_.get()) {
        for (IUnknown* extension : registeredExtensions_) {
            core_.get()->UnregisterExtension(extension);
        }
    }
    for (IUnknown* extension : registeredExtensions_) {
        extension->Release();
    }
    registeredExtensions_.clear();
}

HRESULT WINAPI MenuAction::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) {
        return E_POINTER;
    }
    *ppvObject = nullptr;
    if (riid == IID_IUnknown || riid == IID_IAIMPActionEvent) {
        *ppvObject = static_cast<IAIMPActionEvent*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

void WINAPI MenuAction::OnExecute(IUnknown*) {
    if (plugin_) {
        switch (command_) {
        case MenuCommand::CopySelectedStreamUrls:
            plugin_->CopySelectedStreamUrls();
            break;
        case MenuCommand::RefreshMetadataCache:
            plugin_->RefreshMetadataCache();
            break;
        case MenuCommand::ImportServerPlaylists:
            plugin_->ImportServerPlaylists();
            break;
        case MenuCommand::OpenSettings:
            plugin_->OpenSettings();
            break;
        }
    }
}
