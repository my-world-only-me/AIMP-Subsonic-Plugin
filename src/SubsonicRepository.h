#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <filesystem>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "SimpleJson.h"
#include "SubsonicClient.h"

struct MetadataIndexBuildResult {
    bool ok{false};
    bool canceled{false};
    int playlistsLoaded{0};
    int playlistTrackListsLoaded{0};
    int playlistTracksLoaded{0};
    int artistsLoaded{0};
    int artistAlbumListsLoaded{0};
    int albumPagesLoaded{0};
    int albumsLoaded{0};
    int albumTrackListsLoaded{0};
    int albumTracksLoaded{0};
    int starredTracksLoaded{0};
    int searchTrackPagesLoaded{0};
    int searchTracksLoaded{0};
    int staleTracksPruned{0};
    size_t cachedTracks{0};
    size_t cachedArtists{0};
    size_t cachedAlbums{0};
    size_t cachedPlaylists{0};
};

class SubsonicRepository {
public:
    SubsonicRepository() = default;
    explicit SubsonicRepository(SubsonicClient* client);

    void SetServices(SubsonicClient* client);
    bool IsConfigured() const;

    bool Ping() const;
    int LibraryPageSize() const;

    std::vector<TrackInfo> GetStarredSongs() const;
    std::vector<ArtistInfo> GetArtists() const;
    std::vector<AlbumInfo> GetArtistAlbums(const std::wstring& artistId) const;
    std::vector<AlbumInfo> GetAlbumList2(const std::wstring& type, int size, int offset = 0) const;
    std::vector<TrackInfo> GetAlbumTracks(const std::wstring& albumId) const;
    std::optional<TrackInfo> GetSong(const std::wstring& id) const;
    std::vector<PlaylistInfo> GetPlaylists() const;
    std::vector<TrackInfo> GetPlaylistTracks(const std::wstring& playlistId) const;
    std::vector<TrackInfo> SearchSongs(const std::wstring& query, int count, int offset = 0) const;
    std::vector<TrackInfo> SearchAllSongs(const std::wstring& query) const;
    std::vector<AlbumInfo> GetAllAlbums(const std::wstring& type) const;
    MetadataIndexBuildResult BuildMetadataIndex(const std::function<bool()>& shouldCancel = {}) const;

    // Server playlist id -> AIMP playlist id links for the Playlist Manager
    // import. Stored per server/account next to the metadata cache.
    std::map<std::wstring, std::wstring> GetPlaylistLinks() const;
    void SetPlaylistLink(const std::wstring& serverPlaylistId, const std::wstring& aimpPlaylistId) const;

    bool Scrobble(const std::wstring& songId, bool submission) const;

    std::wstring BuildStreamUrl(const std::wstring& id) const;
    std::vector<TrackInfo> GetCachedTracks(int limit = 0) const;
    std::vector<TrackInfo> GetCachedStarredTracks(int limit = 0) const;
    std::vector<TrackInfo> GetCachedPlaylistTracks(const std::wstring& playlistId, int limit = 0) const;
    std::vector<TrackInfo> GetCachedAlbumTracks(const std::wstring& albumId, int limit = 0) const;
    std::vector<TrackInfo> GetCachedArtistTracks(const std::wstring& artistId, int limit = 0) const;
    std::vector<PlaylistInfo> GetCachedPlaylists(int limit = 0) const;
    std::vector<ArtistInfo> GetCachedArtists(int limit = 0) const;
    std::vector<AlbumInfo> GetCachedAlbums(int limit = 0) const;
    std::vector<AlbumInfo> GetCachedArtistAlbums(const std::wstring& artistId, int limit = 0) const;
    size_t CachedTrackCount() const;
    size_t CachedArtistCount() const;
    size_t CachedAlbumCount() const;
    size_t CachedPlaylistCount() const;
    void RememberTrack(const TrackInfo& track) const;
    void RememberTracks(const std::vector<TrackInfo>& tracks) const;
    void RememberPlaylists(const std::vector<PlaylistInfo>& playlists) const;
    void RememberArtists(const std::vector<ArtistInfo>& artists) const;
    void RememberAlbums(const std::vector<AlbumInfo>& albums) const;
    std::optional<TrackInfo> FindTrackMetadata(const std::wstring& id) const;

private:
    void LoadMetadataCache() const;
    void SaveMetadataCacheLocked() const;
    void RememberPlaylistTracks(const std::wstring& playlistId, const std::vector<TrackInfo>& tracks) const;
    void RememberAlbumTracks(const std::wstring& albumId, const std::vector<TrackInfo>& tracks) const;
    void RememberStarredTracks(const std::vector<TrackInfo>& tracks) const;
    void RememberAllTracks(const std::vector<TrackInfo>& tracks) const;
    int RememberFullTrackSnapshot(
        const std::vector<TrackInfo>& tracks,
        const std::unordered_set<std::wstring>& observedTrackIds,
        const std::unordered_set<std::wstring>& observedAlbumIds,
        const std::unordered_set<std::wstring>& observedArtistIds) const;
    void RememberArtistAlbums(const std::wstring& artistId, const std::vector<AlbumInfo>& albums) const;
    bool RememberTrackLocked(const TrackInfo& track) const;
    bool RememberPlaylistLocked(const PlaylistInfo& playlist) const;
    bool RememberArtistLocked(const ArtistInfo& artist) const;
    bool RememberAlbumLocked(const AlbumInfo& album) const;
    std::filesystem::path PlaylistLinksPath() const;
    std::vector<TrackInfo> TracksByIdsLocked(const std::vector<std::wstring>& ids, int limit) const;
    std::vector<AlbumInfo> AlbumsByIdsLocked(const std::vector<std::wstring>& ids, int limit) const;
    std::filesystem::path MetadataCachePath() const;

    SubsonicClient* client_{nullptr};
    mutable std::mutex tracksMutex_;
    mutable std::unordered_map<std::wstring, TrackInfo> tracks_;
    mutable std::unordered_map<std::wstring, PlaylistInfo> playlists_;
    mutable std::unordered_map<std::wstring, ArtistInfo> artists_;
    mutable std::unordered_map<std::wstring, AlbumInfo> albums_;
    mutable std::vector<std::wstring> allTrackIds_;
    mutable std::vector<std::wstring> starredTrackIds_;
    mutable std::wstring metadataCacheKey_;
    mutable bool metadataCacheLoaded_{false};
};
