#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "apiActions.h"
#include "apiMenu.h"
#include "apiMessages.h"
#include "apiPlaylists.h"
#include "apiPlugin.h"
#include "apiThreading.h"

#include "AimpString.h"
#include "ComBase.h"
#include "CoverArtCache.h"
#include "PlaybackTracker.h"
#include "SubsonicAlbumArtProvider.h"
#include "SubsonicClient.h"
#include "SubsonicFileInfoProvider.h"
#include "SubsonicMusicLibrary.h"
#include "SubsonicRepository.h"

class SubsonicOptionsFrame;
class PlaylistMetadataListener;
class MetadataIndexTask;
class MetadataIndexCompletionTask;
class PlaylistImportTask;
class PlaylistImportApplyTask;

enum class PlaybackMode {
    DirectUrl,
};

enum class MenuCommand {
    CopySelectedStreamUrls,
    RefreshMetadataCache,
    ImportServerPlaylists,
    OpenSettings,
};

struct ServerPlaylistSnapshot {
    PlaylistInfo playlist;
    std::vector<TrackInfo> tracks;
};

class AimpSubsonicPlugin final : public IAIMPPlugin, public IAIMPMessageHook, public ComBase {
public:
    AimpSubsonicPlugin() = default;
    ~AimpSubsonicPlugin() override;

    HRESULT WINAPI QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG WINAPI AddRef() override { return AddRefImpl(); }
    ULONG WINAPI Release() override { return ReleaseImpl(); }

    TChar* WINAPI InfoGet(int Index) override;
    LongWord WINAPI InfoGetCategories() override;
    HRESULT WINAPI Initialize(IAIMPCore* Core) override;
    HRESULT WINAPI Finalize() override;
    void WINAPI SystemNotification(int NotifyID, IUnknown* Data) override;
    void WINAPI CoreMessage(LongWord AMessage, int AParam1, void* AParam2, HRESULT* AResult) override;

    void CopySelectedStreamUrls();
    void RefreshMetadataCache();
    void ImportServerPlaylists(bool interactive = true);
    void OpenSettings();
    void ApplyConfigFromOptions(const SubsonicConfig& config);
    bool TestSubsonicConfig(const SubsonicConfig& config) const;

private:
    friend class PlaylistMetadataListener;
    friend class MetadataIndexTask;
    friend class MetadataIndexCompletionTask;
    friend class PlaylistImportTask;
    friend class PlaylistImportApplyTask;

    void ApplyConfig(const SubsonicConfig& config);
    HRESULT RegisterMenu();
    HRESULT RegisterMusicLibrary();
    HRESULT RegisterOptions();
    HRESULT RegisterAlbumArtProvider();
    HRESULT RegisterFileInfoProvider();
    HRESULT RegisterPlaybackTracking();
    HRESULT RegisterPlaylistMetadataListener();
    void UnregisterPlaybackTracking();
    void UnregisterPlaylistMetadataListener();
    void AttachPlaylistMetadataListeners();
    void AttachPlaylistMetadataListener(IAIMPPlaylist* playlist);
    void DetachPlaylistMetadataListener(IAIMPPlaylist* playlist);
    void OnPlaylistContentChanged(LongWord flags);
    void RepairLoadedPlaylistMetadata(bool allowNetwork);
    void RepairPlaylistMetadata(IAIMPPlaylist* playlist, bool allowNetwork);
    bool RepairPlaylistItemMetadata(IAIMPPlaylistItem* item, bool allowNetwork);
    void HandleStreamStarted();
    void HandleStreamEnded();
    void HandlePlaybackPositionUpdate();
    void ResetScrobbleState();
    std::wstring CurrentPlaybackSongId(double* durationSeconds, double* positionSeconds);
    HRESULT AddTracksToPlaylist(const std::vector<TrackInfo>& tracks, PlaybackMode mode);
    HRESULT AddTracksToPlaylist(IAIMPPlaylist* playlist, const std::vector<TrackInfo>& tracks, PlaybackMode mode);
    HRESULT ApplyPlaylistItemMetadata(IAIMPPlaylist* playlist, int startIndex, const std::vector<TrackInfo>& tracks, PlaybackMode mode);
    HRESULT SetTrackInfo(IAIMPFileInfo* info, const TrackInfo& track, PlaybackMode mode);
    std::vector<TrackInfo> CollectSubsonicTracks(IAIMPPlaylist* playlist, bool selectedOnly = false);
    std::vector<TrackInfo> CollectActivePlaylistTracks(bool selectedOnly);
    void RegisterOwnedExtension(REFIID serviceId, IUnknown* extension);
    void ClearRegisteredExtensions();
    void RunMetadataIndexTask(IAIMPTaskOwner* owner, uint64_t generation);
    void CompleteMetadataIndexTask(const MetadataIndexBuildResult& result, uint64_t generation);
    void FinishMetadataIndexTask(uint64_t generation);
    void CancelMetadataIndexTask();
    void RunPlaylistImportTask(IAIMPTaskOwner* owner, uint64_t generation, bool interactive);
    void ApplyServerPlaylists(const std::vector<ServerPlaylistSnapshot>& snapshots, uint64_t generation, bool interactive);
    void FinishPlaylistImportTask(uint64_t generation);
    void CancelPlaylistImportTask();
    void RefreshImportedPlaylistsAtStartup();

    AimpCoreRef core_;
    std::unique_ptr<SubsonicClient> client_;
    std::unique_ptr<CoverArtCache> coverCache_;
    std::unique_ptr<SubsonicRepository> repository_;
    std::unique_ptr<PlaybackTracker> playbackTracker_;
    SubsonicMusicLibraryStorage* musicLibrary_{nullptr};
    SubsonicAlbumArtProvider* albumArtProvider_{nullptr};
    SubsonicFileInfoProvider* fileInfoProvider_{nullptr};
    SubsonicOptionsFrame* optionsFrame_{nullptr};
    PlaylistMetadataListener* playlistMetadataListener_{nullptr};
    IAIMPServiceMessageDispatcher* messageDispatcher_{nullptr};
    bool messageHooked_{false};
    bool repairingPlaylistMetadata_{false};
    std::mutex metadataIndexMutex_;
    bool metadataIndexRunning_{false};
    TTaskHandle metadataIndexTask_{0};
    MetadataIndexTask* metadataIndexTaskObject_{nullptr};
    uint64_t metadataIndexGeneration_{0};
    std::mutex playlistImportMutex_;
    bool playlistImportRunning_{false};
    TTaskHandle playlistImportTask_{0};
    PlaylistImportTask* playlistImportTaskObject_{nullptr};
    uint64_t playlistImportGeneration_{0};
    bool startupPlaylistSyncDone_{false};
    std::vector<IUnknown*> registeredExtensions_;
    std::vector<IAIMPPlaylist*> metadataWatchedPlaylists_;
};

class MenuAction final : public IAIMPActionEvent, public ComBase {
public:
    MenuAction(AimpSubsonicPlugin* plugin, MenuCommand command) : plugin_(plugin), command_(command) {}

    HRESULT WINAPI QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG WINAPI AddRef() override { return AddRefImpl(); }
    ULONG WINAPI Release() override { return ReleaseImpl(); }
    void WINAPI OnExecute(IUnknown* Data) override;

private:
    AimpSubsonicPlugin* plugin_;
    MenuCommand command_;
};
