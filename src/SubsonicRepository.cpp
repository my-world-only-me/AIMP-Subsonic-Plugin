#include "SubsonicRepository.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_set>
#include <utility>

#include "Config.h"
#include "Diagnostics.h"
#include "Url.h"

namespace {

std::filesystem::path MetadataCacheRoot() {
    return std::filesystem::path(PluginDirectory()) / L"cache" / L"metadata";
}

std::string JsonEscape(const std::wstring& value) {
    const std::string utf8 = WideToUtf8(value);
    std::string result;
    result.reserve(utf8.size() + 8);
    for (const unsigned char ch : utf8) {
        switch (ch) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (ch < 0x20) {
                static constexpr char kHex[] = "0123456789abcdef";
                result += "\\u00";
                result.push_back(kHex[(ch >> 4) & 0x0F]);
                result.push_back(kHex[ch & 0x0F]);
            } else {
                result.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return result;
}

void AppendJsonString(std::ostream& out, const char* key, const std::wstring& value, bool comma = true) {
    out << "\"" << key << "\":\"" << JsonEscape(value) << "\"";
    if (comma) {
        out << ",";
    }
}

void AppendJsonInt(std::ostream& out, const char* key, int value, bool comma = true) {
    out << "\"" << key << "\":" << value;
    if (comma) {
        out << ",";
    }
}

void AppendJsonInt64(std::ostream& out, const char* key, long long value, bool comma = true) {
    out << "\"" << key << "\":" << value;
    if (comma) {
        out << ",";
    }
}

void AppendJsonStringArray(std::ostream& out, const char* key, const std::vector<std::wstring>& values, bool comma = true) {
    out << "\"" << key << "\":[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << "\"" << JsonEscape(values[i]) << "\"";
    }
    out << "]";
    if (comma) {
        out << ",";
    }
}

template <typename T>
void ApplyLimit(std::vector<T>& values, int limit) {
    if (limit > 0 && values.size() > static_cast<size_t>(limit)) {
        values.resize(static_cast<size_t>(limit));
    }
}

int CompareText(const std::wstring& left, const std::wstring& right) {
    return _wcsicmp(left.c_str(), right.c_str());
}

std::vector<std::wstring> TrackIds(const std::vector<TrackInfo>& tracks) {
    std::vector<std::wstring> ids;
    ids.reserve(tracks.size());
    for (const auto& track : tracks) {
        if (!track.id.empty()) {
            ids.push_back(track.id);
        }
    }
    return ids;
}

void AddTrackIds(std::unordered_set<std::wstring>& ids, const std::vector<TrackInfo>& tracks) {
    for (const auto& track : tracks) {
        if (!track.id.empty()) {
            ids.insert(track.id);
        }
    }
}

void AddAlbumIds(std::unordered_set<std::wstring>& ids, const std::vector<AlbumInfo>& albums) {
    for (const auto& album : albums) {
        if (!album.id.empty()) {
            ids.insert(album.id);
        }
    }
}

void AddArtistIds(std::unordered_set<std::wstring>& ids, const std::vector<ArtistInfo>& artists) {
    for (const auto& artist : artists) {
        if (!artist.id.empty()) {
            ids.insert(artist.id);
        }
    }
}

void AddTrackParents(
    std::unordered_set<std::wstring>& albumIds,
    std::unordered_set<std::wstring>& artistIds,
    const std::vector<TrackInfo>& tracks) {
    for (const auto& track : tracks) {
        if (!track.albumId.empty()) {
            albumIds.insert(track.albumId);
        }
        if (!track.artistId.empty()) {
            artistIds.insert(track.artistId);
        }
    }
}

bool KeepOnlyIds(std::vector<std::wstring>& ids, const std::unordered_set<std::wstring>& keepIds) {
    const auto oldSize = ids.size();
    ids.erase(std::remove_if(ids.begin(), ids.end(), [&](const std::wstring& id) {
        return keepIds.find(id) == keepIds.end();
    }), ids.end());
    return ids.size() != oldSize;
}

std::vector<std::wstring> AlbumIds(const std::vector<AlbumInfo>& albums) {
    std::vector<std::wstring> ids;
    ids.reserve(albums.size());
    for (const auto& album : albums) {
        if (!album.id.empty()) {
            ids.push_back(album.id);
        }
    }
    return ids;
}

bool ShouldCancel(const std::function<bool()>& shouldCancel) {
    return shouldCancel && shouldCancel();
}

} // namespace

SubsonicRepository::SubsonicRepository(SubsonicClient* client)
    : client_(client) {
    LoadMetadataCache();
}

void SubsonicRepository::SetServices(SubsonicClient* client) {
    client_ = client;
    LoadMetadataCache();
}

bool SubsonicRepository::IsConfigured() const {
    return client_ != nullptr;
}

bool SubsonicRepository::Ping() const {
    return client_ && client_->Ping();
}

int SubsonicRepository::LibraryPageSize() const {
    return client_ ? client_->LibraryPageSize() : 500;
}

std::vector<TrackInfo> SubsonicRepository::GetStarredSongs() const {
    if (!client_) {
        return {};
    }
    auto tracks = client_->GetStarredSongs();
    RememberStarredTracks(tracks);
    return tracks;
}

std::vector<ArtistInfo> SubsonicRepository::GetArtists() const {
    if (!client_) {
        return {};
    }
    auto artists = client_->GetArtists();
    RememberArtists(artists);
    return artists;
}

std::vector<AlbumInfo> SubsonicRepository::GetArtistAlbums(const std::wstring& artistId) const {
    if (!client_) {
        return {};
    }
    auto albums = client_->GetArtistAlbums(artistId);
    RememberArtistAlbums(artistId, albums);
    return albums;
}

std::vector<AlbumInfo> SubsonicRepository::GetAlbumList2(const std::wstring& type, int size, int offset) const {
    if (!client_) {
        return {};
    }
    auto albums = client_->GetAlbumList2(type, size, offset);
    RememberAlbums(albums);
    return albums;
}

std::vector<TrackInfo> SubsonicRepository::GetAlbumTracks(const std::wstring& albumId) const {
    if (!client_) {
        return {};
    }
    auto tracks = client_->GetAlbumTracks(albumId);
    RememberAlbumTracks(albumId, tracks);
    return tracks;
}

std::optional<TrackInfo> SubsonicRepository::GetSong(const std::wstring& id) const {
    if (id.empty()) {
        return std::nullopt;
    }
    if (auto track = FindTrackMetadata(id)) {
        return track;
    }
    if (!client_) {
        return std::nullopt;
    }
    auto track = client_->GetSong(id);
    if (track) {
        RememberTrack(*track);
    }
    return track;
}

std::vector<PlaylistInfo> SubsonicRepository::GetPlaylists() const {
    if (!client_) {
        return {};
    }
    auto playlists = client_->GetPlaylists();
    RememberPlaylists(playlists);
    return playlists;
}

std::vector<TrackInfo> SubsonicRepository::GetPlaylistTracks(const std::wstring& playlistId) const {
    if (!client_) {
        return {};
    }
    auto tracks = client_->GetPlaylistTracks(playlistId);
    RememberPlaylistTracks(playlistId, tracks);
    return tracks;
}

std::vector<TrackInfo> SubsonicRepository::SearchSongs(const std::wstring& query, int count, int offset) const {
    if (!client_) {
        return {};
    }
    auto tracks = client_->SearchSongs(query, count, offset);
    if (query.empty() && offset <= 0) {
        RememberAllTracks(tracks);
    } else {
        RememberTracks(tracks);
    }
    return tracks;
}

std::vector<TrackInfo> SubsonicRepository::SearchAllSongs(const std::wstring& query) const {
    std::vector<TrackInfo> all;
    if (!client_) {
        return all;
    }
    const int pageSize = LibraryPageSize();
    std::unordered_set<std::wstring> seenIds;
    int offset = 0;
    while (true) {
        const auto page = client_->SearchSongs(query, pageSize, offset);
        if (page.empty()) {
            break;
        }
        int newTracks = 0;
        for (const auto& track : page) {
            if (!track.id.empty() && seenIds.insert(track.id).second) {
                all.push_back(track);
                ++newTracks;
            }
        }
        if (static_cast<int>(page.size()) < pageSize || newTracks == 0) {
            break;
        }
        offset += pageSize;
    }
    LogInfo(L"SearchAllSongs completed. Query='" + query +
        L"', Tracks=" + std::to_wstring(all.size()));
    if (query.empty() && !all.empty()) {
        RememberAllTracks(all);
    } else {
        RememberTracks(all);
    }
    return all;
}

std::vector<AlbumInfo> SubsonicRepository::GetAllAlbums(const std::wstring& type) const {
    std::vector<AlbumInfo> all;
    if (!client_) {
        return all;
    }
    const int pageSize = LibraryPageSize();
    std::unordered_set<std::wstring> seenIds;
    int offset = 0;
    while (true) {
        const auto page = client_->GetAlbumList2(type, pageSize, offset);
        if (page.empty()) {
            break;
        }
        int newAlbums = 0;
        for (const auto& album : page) {
            if (!album.id.empty() && seenIds.insert(album.id).second) {
                all.push_back(album);
                ++newAlbums;
            }
        }
        if (static_cast<int>(page.size()) < pageSize || newAlbums == 0) {
            break;
        }
        offset += pageSize;
    }
    LogInfo(L"GetAllAlbums completed. Type=" + type +
        L", Albums=" + std::to_wstring(all.size()));
    RememberAlbums(all);
    return all;
}

MetadataIndexBuildResult SubsonicRepository::BuildMetadataIndex(const std::function<bool()>& shouldCancel) const {
    MetadataIndexBuildResult result;
    if (!client_) {
        LogInfo(L"Metadata index build skipped: Subsonic client is unavailable.");
        return result;
    }

    const int pageSize = LibraryPageSize();
    LogInfo(L"Metadata index build started. PageSize=" + std::to_wstring(pageSize));

    auto canceled = [&]() {
        const bool value = ShouldCancel(shouldCancel);
        if (value) {
            result.canceled = true;
            LogInfo(L"Metadata index build cancellation requested.");
        }
        return value;
    };

    if (canceled()) {
        return result;
    }

    std::unordered_set<std::wstring> observedTrackIds;
    std::unordered_set<std::wstring> observedAlbumIds;
    std::unordered_set<std::wstring> observedArtistIds;

    const auto playlists = GetPlaylists();
    result.playlistsLoaded = static_cast<int>(playlists.size());
    for (const auto& playlist : playlists) {
        if (canceled()) {
            return result;
        }
        const auto tracks = GetPlaylistTracks(playlist.id);
        AddTrackIds(observedTrackIds, tracks);
        AddTrackParents(observedAlbumIds, observedArtistIds, tracks);
        ++result.playlistTrackListsLoaded;
        result.playlistTracksLoaded += static_cast<int>(tracks.size());
        LogInfo(L"Metadata index playlist cached. Playlist=" + playlist.name +
            L", Tracks=" + std::to_wstring(tracks.size()));
    }

    if (canceled()) {
        return result;
    }

    const auto artists = GetArtists();
    AddArtistIds(observedArtistIds, artists);
    result.artistsLoaded = static_cast<int>(artists.size());
    for (const auto& artist : artists) {
        if (canceled()) {
            return result;
        }
        const auto albums = GetArtistAlbums(artist.id);
        AddAlbumIds(observedAlbumIds, albums);
        ++result.artistAlbumListsLoaded;
        LogInfo(L"Metadata index artist albums cached. Artist=" + artist.name +
            L", Albums=" + std::to_wstring(albums.size()));
    }

    std::unordered_set<std::wstring> seenAlbumIds;
    int offset = 0;
    while (!canceled()) {
        const auto albums = GetAlbumList2(L"alphabeticalByArtist", pageSize, offset);
        if (albums.empty()) {
            break;
        }
        ++result.albumPagesLoaded;
        int newAlbums = 0;
        for (const auto& album : albums) {
            if (!album.artistId.empty()) {
                observedArtistIds.insert(album.artistId);
            }
            if (!album.id.empty() && seenAlbumIds.insert(album.id).second) {
                observedAlbumIds.insert(album.id);
                ++newAlbums;
            }
        }
        result.albumsLoaded += static_cast<int>(albums.size());
        LogInfo(L"Metadata index album page cached. Offset=" + std::to_wstring(offset) +
            L", Albums=" + std::to_wstring(albums.size()) +
            L", NewAlbums=" + std::to_wstring(newAlbums));
        if (static_cast<int>(albums.size()) < pageSize || newAlbums == 0) {
            break;
        }
        offset += pageSize;
    }

    const auto cachedAlbums = GetCachedAlbums(0);
    for (const auto& album : cachedAlbums) {
        if (canceled()) {
            return result;
        }
        const auto tracks = GetAlbumTracks(album.id);
        AddTrackIds(observedTrackIds, tracks);
        AddTrackParents(observedAlbumIds, observedArtistIds, tracks);
        ++result.albumTrackListsLoaded;
        result.albumTracksLoaded += static_cast<int>(tracks.size());
        LogInfo(L"Metadata index album tracks cached. Album=" + album.name +
            L", Tracks=" + std::to_wstring(tracks.size()));
    }

    if (!canceled()) {
        const auto starred = GetStarredSongs();
        AddTrackIds(observedTrackIds, starred);
        AddTrackParents(observedAlbumIds, observedArtistIds, starred);
        result.starredTracksLoaded = static_cast<int>(starred.size());
        LogInfo(L"Metadata index starred tracks cached. Tracks=" + std::to_wstring(starred.size()));
    }

    std::vector<TrackInfo> searchSnapshot;
    std::unordered_set<std::wstring> seenSearchTrackIds;
    offset = 0;
    while (!canceled()) {
        const auto tracks = client_->SearchSongs(L"", pageSize, offset);
        if (tracks.empty()) {
            break;
        }
        ++result.searchTrackPagesLoaded;
        int newTracks = 0;
        for (const auto& track : tracks) {
            if (track.id.empty()) {
                continue;
            }
            observedTrackIds.insert(track.id);
            if (!track.albumId.empty()) {
                observedAlbumIds.insert(track.albumId);
            }
            if (!track.artistId.empty()) {
                observedArtistIds.insert(track.artistId);
            }
            if (seenSearchTrackIds.insert(track.id).second) {
                searchSnapshot.push_back(track);
                ++newTracks;
            }
        }
        RememberTracks(tracks);
        result.searchTracksLoaded += static_cast<int>(tracks.size());
        LogInfo(L"Metadata index search page cached. Offset=" + std::to_wstring(offset) +
            L", Tracks=" + std::to_wstring(tracks.size()) +
            L", NewTracks=" + std::to_wstring(newTracks));
        if (static_cast<int>(tracks.size()) < pageSize || newTracks == 0) {
            break;
        }
        offset += pageSize;
    }
    if (!searchSnapshot.empty()) {
        result.staleTracksPruned = RememberFullTrackSnapshot(
            searchSnapshot,
            observedTrackIds,
            observedAlbumIds,
            observedArtistIds);
    }

    result.cachedTracks = CachedTrackCount();
    result.cachedArtists = CachedArtistCount();
    result.cachedAlbums = CachedAlbumCount();
    result.cachedPlaylists = CachedPlaylistCount();
    result.ok = !result.canceled;

    LogInfo(L"Metadata index build finished. OK=" + std::to_wstring(result.ok ? 1 : 0) +
        L", CachedTracks=" + std::to_wstring(result.cachedTracks) +
        L", CachedArtists=" + std::to_wstring(result.cachedArtists) +
        L", CachedAlbums=" + std::to_wstring(result.cachedAlbums) +
        L", CachedPlaylists=" + std::to_wstring(result.cachedPlaylists));
    return result;
}

std::filesystem::path SubsonicRepository::PlaylistLinksPath() const {
    const std::wstring key = client_ ? client_->MetadataCacheKey() : std::wstring(L"default");
    return MetadataCacheRoot() / (key + L".playlists.json");
}

std::map<std::wstring, std::wstring> SubsonicRepository::GetPlaylistLinks() const {
    std::map<std::wstring, std::wstring> links;
    std::ifstream file(PlaylistLinksPath(), std::ios::binary);
    if (!file) {
        return links;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    const auto serverIds = JsonGetStringArray(text, "serverIds");
    const auto aimpIds = JsonGetStringArray(text, "aimpIds");
    const size_t count = std::min(serverIds.size(), aimpIds.size());
    for (size_t i = 0; i < count; ++i) {
        if (!serverIds[i].empty() && !aimpIds[i].empty()) {
            links[Utf8ToWideString(serverIds[i])] = Utf8ToWideString(aimpIds[i]);
        }
    }
    return links;
}

void SubsonicRepository::SetPlaylistLink(const std::wstring& serverPlaylistId, const std::wstring& aimpPlaylistId) const {
    if (serverPlaylistId.empty() || aimpPlaylistId.empty()) {
        return;
    }
    auto links = GetPlaylistLinks();
    const auto it = links.find(serverPlaylistId);
    if (it != links.end() && it->second == aimpPlaylistId) {
        return;
    }
    links[serverPlaylistId] = aimpPlaylistId;

    const auto path = PlaylistLinksPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        LogInfo(L"Subsonic playlist links directory creation failed: " + Utf8ToWideString(ec.message()));
        return;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        LogInfo(L"Subsonic playlist links save failed: cannot open file.");
        return;
    }
    std::vector<std::wstring> serverIds;
    std::vector<std::wstring> aimpIds;
    serverIds.reserve(links.size());
    aimpIds.reserve(links.size());
    for (const auto& [serverId, aimpId] : links) {
        serverIds.push_back(serverId);
        aimpIds.push_back(aimpId);
    }
    out << "{\"version\":1,";
    AppendJsonStringArray(out, "serverIds", serverIds);
    AppendJsonStringArray(out, "aimpIds", aimpIds, false);
    out << "}";
    LogInfo(L"Subsonic playlist links saved. Links=" + std::to_wstring(links.size()));
}

bool SubsonicRepository::Scrobble(const std::wstring& songId, bool submission) const {
    return client_ && client_->Scrobble(songId, submission);
}

std::wstring SubsonicRepository::BuildStreamUrl(const std::wstring& id) const {
    return client_ ? client_->BuildStreamUrl(id) : std::wstring();
}

std::vector<TrackInfo> SubsonicRepository::GetCachedTracks(int limit) const {
    LoadMetadataCache();
    std::vector<TrackInfo> result;
    {
        std::lock_guard lock(tracksMutex_);
        if (!allTrackIds_.empty()) {
            result = TracksByIdsLocked(allTrackIds_, 0);
            std::set<std::wstring> knownIds(allTrackIds_.begin(), allTrackIds_.end());
            std::vector<TrackInfo> additiveTracks;
            additiveTracks.reserve(tracks_.size());
            for (const auto& [id, track] : tracks_) {
                if (knownIds.find(id) == knownIds.end()) {
                    additiveTracks.push_back(track);
                }
            }
            std::sort(additiveTracks.begin(), additiveTracks.end(), [](const TrackInfo& left, const TrackInfo& right) {
                const int artist = CompareText(left.artist, right.artist);
                if (artist != 0) return artist < 0;
                const int album = CompareText(left.album, right.album);
                if (album != 0) return album < 0;
                return CompareText(left.title, right.title) < 0;
            });
            result.insert(result.end(), additiveTracks.begin(), additiveTracks.end());
            ApplyLimit(result, limit);
            return result;
        }
        result.reserve(tracks_.size());
        for (const auto& [id, track] : tracks_) {
            result.push_back(track);
        }
    }
    std::sort(result.begin(), result.end(), [](const TrackInfo& left, const TrackInfo& right) {
        const int artist = CompareText(left.artist, right.artist);
        if (artist != 0) return artist < 0;
        const int album = CompareText(left.album, right.album);
        if (album != 0) return album < 0;
        return CompareText(left.title, right.title) < 0;
    });
    ApplyLimit(result, limit);
    return result;
}

std::vector<TrackInfo> SubsonicRepository::GetCachedStarredTracks(int limit) const {
    LoadMetadataCache();
    {
        std::lock_guard lock(tracksMutex_);
        if (!starredTrackIds_.empty()) {
            return TracksByIdsLocked(starredTrackIds_, limit);
        }
    }
    auto tracks = GetCachedTracks(0);
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [](const TrackInfo& track) {
        return !track.starred;
    }), tracks.end());
    ApplyLimit(tracks, limit);
    return tracks;
}

std::vector<TrackInfo> SubsonicRepository::GetCachedPlaylistTracks(const std::wstring& playlistId, int limit) const {
    LoadMetadataCache();
    {
        std::lock_guard lock(tracksMutex_);
        const auto playlist = playlists_.find(playlistId);
        if (playlist != playlists_.end() && !playlist->second.songIds.empty()) {
            return TracksByIdsLocked(playlist->second.songIds, limit);
        }
    }
    auto tracks = GetCachedTracks(0);
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [&](const TrackInfo& track) {
        return track.playlistId != playlistId;
    }), tracks.end());
    ApplyLimit(tracks, limit);
    return tracks;
}

std::vector<TrackInfo> SubsonicRepository::GetCachedAlbumTracks(const std::wstring& albumId, int limit) const {
    LoadMetadataCache();
    {
        std::lock_guard lock(tracksMutex_);
        const auto album = albums_.find(albumId);
        if (album != albums_.end() && !album->second.songIds.empty()) {
            return TracksByIdsLocked(album->second.songIds, limit);
        }
    }
    auto tracks = GetCachedTracks(0);
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [&](const TrackInfo& track) {
        return track.albumId != albumId;
    }), tracks.end());
    ApplyLimit(tracks, limit);
    return tracks;
}

std::vector<TrackInfo> SubsonicRepository::GetCachedArtistTracks(const std::wstring& artistId, int limit) const {
    LoadMetadataCache();
    {
        std::lock_guard lock(tracksMutex_);
        const auto artist = artists_.find(artistId);
        if (artist != artists_.end() && !artist->second.albumIds.empty()) {
            std::vector<std::wstring> trackIds;
            for (const auto& albumId : artist->second.albumIds) {
                const auto album = albums_.find(albumId);
                if (album != albums_.end()) {
                    trackIds.insert(trackIds.end(), album->second.songIds.begin(), album->second.songIds.end());
                }
            }
            if (!trackIds.empty()) {
                return TracksByIdsLocked(trackIds, limit);
            }
        }
    }
    auto tracks = GetCachedTracks(0);
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [&](const TrackInfo& track) {
        return track.artistId != artistId;
    }), tracks.end());
    ApplyLimit(tracks, limit);
    return tracks;
}

std::vector<PlaylistInfo> SubsonicRepository::GetCachedPlaylists(int limit) const {
    LoadMetadataCache();
    std::vector<PlaylistInfo> result;
    {
        std::lock_guard lock(tracksMutex_);
        result.reserve(playlists_.size());
        for (const auto& [id, playlist] : playlists_) {
            result.push_back(playlist);
        }
    }
    std::sort(result.begin(), result.end(), [](const PlaylistInfo& left, const PlaylistInfo& right) {
        return CompareText(left.name, right.name) < 0;
    });
    ApplyLimit(result, limit);
    return result;
}

std::vector<ArtistInfo> SubsonicRepository::GetCachedArtists(int limit) const {
    LoadMetadataCache();
    std::vector<ArtistInfo> result;
    {
        std::lock_guard lock(tracksMutex_);
        result.reserve(artists_.size());
        for (const auto& [id, artist] : artists_) {
            result.push_back(artist);
        }
    }
    std::sort(result.begin(), result.end(), [](const ArtistInfo& left, const ArtistInfo& right) {
        return CompareText(left.name, right.name) < 0;
    });
    ApplyLimit(result, limit);
    return result;
}

std::vector<AlbumInfo> SubsonicRepository::GetCachedAlbums(int limit) const {
    LoadMetadataCache();
    std::vector<AlbumInfo> result;
    {
        std::lock_guard lock(tracksMutex_);
        result.reserve(albums_.size());
        for (const auto& [id, album] : albums_) {
            result.push_back(album);
        }
    }
    std::sort(result.begin(), result.end(), [](const AlbumInfo& left, const AlbumInfo& right) {
        const int artist = CompareText(left.artist, right.artist);
        if (artist != 0) return artist < 0;
        return CompareText(left.name, right.name) < 0;
    });
    ApplyLimit(result, limit);
    return result;
}

std::vector<AlbumInfo> SubsonicRepository::GetCachedArtistAlbums(const std::wstring& artistId, int limit) const {
    LoadMetadataCache();
    {
        std::lock_guard lock(tracksMutex_);
        const auto artist = artists_.find(artistId);
        if (artist != artists_.end() && !artist->second.albumIds.empty()) {
            return AlbumsByIdsLocked(artist->second.albumIds, limit);
        }
    }
    auto albums = GetCachedAlbums(0);
    albums.erase(std::remove_if(albums.begin(), albums.end(), [&](const AlbumInfo& album) {
        return album.artistId != artistId;
    }), albums.end());
    ApplyLimit(albums, limit);
    return albums;
}

size_t SubsonicRepository::CachedTrackCount() const {
    LoadMetadataCache();
    std::lock_guard lock(tracksMutex_);
    return tracks_.size();
}

size_t SubsonicRepository::CachedArtistCount() const {
    LoadMetadataCache();
    std::lock_guard lock(tracksMutex_);
    return artists_.size();
}

size_t SubsonicRepository::CachedAlbumCount() const {
    LoadMetadataCache();
    std::lock_guard lock(tracksMutex_);
    return albums_.size();
}

size_t SubsonicRepository::CachedPlaylistCount() const {
    LoadMetadataCache();
    std::lock_guard lock(tracksMutex_);
    return playlists_.size();
}

void SubsonicRepository::RememberTrack(const TrackInfo& track) const {
    if (track.id.empty()) {
        return;
    }
    std::lock_guard lock(tracksMutex_);
    if (RememberTrackLocked(track)) {
        SaveMetadataCacheLocked();
    }
}

void SubsonicRepository::RememberTracks(const std::vector<TrackInfo>& tracks) const {
    bool changed = false;
    std::lock_guard lock(tracksMutex_);
    for (const auto& track : tracks) {
        changed = RememberTrackLocked(track) || changed;
    }
    if (changed) {
        SaveMetadataCacheLocked();
    }
}

void SubsonicRepository::RememberPlaylists(const std::vector<PlaylistInfo>& playlists) const {
    bool changed = false;
    std::lock_guard lock(tracksMutex_);
    for (const auto& playlist : playlists) {
        changed = RememberPlaylistLocked(playlist) || changed;
    }
    if (changed) {
        SaveMetadataCacheLocked();
    }
}

void SubsonicRepository::RememberArtists(const std::vector<ArtistInfo>& artists) const {
    bool changed = false;
    std::lock_guard lock(tracksMutex_);
    for (const auto& artist : artists) {
        changed = RememberArtistLocked(artist) || changed;
    }
    if (changed) {
        SaveMetadataCacheLocked();
    }
}

void SubsonicRepository::RememberAlbums(const std::vector<AlbumInfo>& albums) const {
    bool changed = false;
    std::lock_guard lock(tracksMutex_);
    for (const auto& album : albums) {
        changed = RememberAlbumLocked(album) || changed;
    }
    if (changed) {
        SaveMetadataCacheLocked();
    }
}

void SubsonicRepository::RememberPlaylistTracks(const std::wstring& playlistId, const std::vector<TrackInfo>& tracks) const {
    bool changed = false;
    std::lock_guard lock(tracksMutex_);
    for (const auto& track : tracks) {
        changed = RememberTrackLocked(track) || changed;
    }
    if (!playlistId.empty()) {
        auto& playlist = playlists_[playlistId];
        playlist.id = playlistId;
        const auto ids = TrackIds(tracks);
        if (playlist.songIds != ids) {
            playlist.songIds = ids;
            changed = true;
        }
        if (playlist.songCount <= 0) {
            playlist.songCount = static_cast<int>(ids.size());
        }
    }
    if (changed) {
        SaveMetadataCacheLocked();
    }
}

void SubsonicRepository::RememberAlbumTracks(const std::wstring& albumId, const std::vector<TrackInfo>& tracks) const {
    bool changed = false;
    std::lock_guard lock(tracksMutex_);
    for (const auto& track : tracks) {
        changed = RememberTrackLocked(track) || changed;
    }
    if (!albumId.empty()) {
        auto& album = albums_[albumId];
        album.id = albumId;
        const auto ids = TrackIds(tracks);
        if (album.songIds != ids) {
            album.songIds = ids;
            changed = true;
        }
        if (album.songCount <= 0) {
            album.songCount = static_cast<int>(ids.size());
            changed = true;
        }
        if (!tracks.empty()) {
            const auto& first = tracks.front();
            const std::wstring artist = first.albumArtist.empty() ? first.artist : first.albumArtist;
            if (album.name.empty() && !first.album.empty()) {
                album.name = first.album;
                changed = true;
            }
            if (album.artist.empty() && !artist.empty()) {
                album.artist = artist;
                changed = true;
            }
            if (album.artistId.empty() && !first.artistId.empty()) {
                album.artistId = first.artistId;
                changed = true;
            }
            if (album.coverArt.empty() && !first.coverArt.empty()) {
                album.coverArt = first.coverArt;
                changed = true;
            }
            if (album.genre.empty() && !first.genre.empty()) {
                album.genre = first.genre;
                changed = true;
            }
            if (album.year <= 0 && first.year > 0) {
                album.year = first.year;
                changed = true;
            }
        }
    }
    if (changed) {
        SaveMetadataCacheLocked();
    }
}

void SubsonicRepository::RememberStarredTracks(const std::vector<TrackInfo>& tracks) const {
    bool changed = false;
    std::lock_guard lock(tracksMutex_);
    for (const auto& track : tracks) {
        TrackInfo starredTrack = track;
        starredTrack.starred = true;
        changed = RememberTrackLocked(starredTrack) || changed;
    }
    const auto ids = TrackIds(tracks);
    if (starredTrackIds_ != ids) {
        starredTrackIds_ = ids;
        changed = true;
    }
    if (changed) {
        SaveMetadataCacheLocked();
    }
}

void SubsonicRepository::RememberAllTracks(const std::vector<TrackInfo>& tracks) const {
    bool changed = false;
    std::lock_guard lock(tracksMutex_);
    for (const auto& track : tracks) {
        changed = RememberTrackLocked(track) || changed;
    }
    const auto ids = TrackIds(tracks);
    if (allTrackIds_ != ids) {
        allTrackIds_ = ids;
        changed = true;
    }
    if (changed) {
        SaveMetadataCacheLocked();
    }
}

int SubsonicRepository::RememberFullTrackSnapshot(
    const std::vector<TrackInfo>& tracks,
    const std::unordered_set<std::wstring>& observedTrackIds,
    const std::unordered_set<std::wstring>& observedAlbumIds,
    const std::unordered_set<std::wstring>& observedArtistIds) const {
    bool changed = false;
    int pruned = 0;
    std::lock_guard lock(tracksMutex_);

    for (const auto& track : tracks) {
        changed = RememberTrackLocked(track) || changed;
    }

    const auto ids = TrackIds(tracks);
    if (allTrackIds_ != ids) {
        allTrackIds_ = ids;
        changed = true;
    }

    std::unordered_set<std::wstring> keepIds = observedTrackIds;
    if (keepIds.empty()) {
        keepIds.insert(ids.begin(), ids.end());
    }
    if (!keepIds.empty()) {
        for (auto it = tracks_.begin(); it != tracks_.end();) {
            if (keepIds.find(it->first) == keepIds.end()) {
                it = tracks_.erase(it);
                ++pruned;
                changed = true;
            } else {
                ++it;
            }
        }

        changed = KeepOnlyIds(allTrackIds_, keepIds) || changed;
        changed = KeepOnlyIds(starredTrackIds_, keepIds) || changed;
        for (auto& entry : playlists_) {
            auto& playlist = entry.second;
            const bool filtered = KeepOnlyIds(playlist.songIds, keepIds);
            if (filtered) {
                playlist.songCount = static_cast<int>(playlist.songIds.size());
                changed = true;
            }
        }

        std::unordered_set<std::wstring> albumsToRemove;
        for (auto& entry : albums_) {
            const auto& albumId = entry.first;
            auto& album = entry.second;
            if (!observedAlbumIds.empty() && observedAlbumIds.find(albumId) == observedAlbumIds.end()) {
                albumsToRemove.insert(albumId);
                continue;
            }
            const bool hadKnownSongs = !album.songIds.empty();
            const bool filtered = KeepOnlyIds(album.songIds, keepIds);
            if (filtered) {
                album.songCount = static_cast<int>(album.songIds.size());
                changed = true;
            }
            if (hadKnownSongs && album.songIds.empty()) {
                albumsToRemove.insert(albumId);
            }
        }
        int prunedAlbums = 0;
        for (auto it = albums_.begin(); it != albums_.end();) {
            if (albumsToRemove.find(it->first) != albumsToRemove.end()) {
                it = albums_.erase(it);
                ++prunedAlbums;
                changed = true;
            } else {
                ++it;
            }
        }

        std::unordered_set<std::wstring> keepAlbumIds;
        keepAlbumIds.reserve(albums_.size());
        for (const auto& [albumId, album] : albums_) {
            keepAlbumIds.insert(albumId);
        }

        std::unordered_set<std::wstring> artistsToRemove;
        for (auto& entry : artists_) {
            const auto& artistId = entry.first;
            auto& artist = entry.second;
            if (!observedArtistIds.empty() && observedArtistIds.find(artistId) == observedArtistIds.end()) {
                artistsToRemove.insert(artistId);
                continue;
            }
            if (artist.albumIds.empty()) {
                continue;
            }
            const bool filtered = KeepOnlyIds(artist.albumIds, keepAlbumIds);
            if (filtered) {
                artist.albumCount = static_cast<int>(artist.albumIds.size());
                changed = true;
            }
            if (artist.albumIds.empty()) {
                artistsToRemove.insert(artistId);
            }
        }
        int prunedArtists = 0;
        for (auto it = artists_.begin(); it != artists_.end();) {
            if (artistsToRemove.find(it->first) != artistsToRemove.end()) {
                it = artists_.erase(it);
                ++prunedArtists;
                changed = true;
            } else {
                ++it;
            }
        }

        if (prunedAlbums > 0 || prunedArtists > 0) {
            LogInfo(L"Subsonic metadata cache pruned stale albums/artists. Albums=" +
                std::to_wstring(prunedAlbums) +
                L", Artists=" + std::to_wstring(prunedArtists));
        }
    }

    if (changed) {
        SaveMetadataCacheLocked();
    }
    if (pruned > 0) {
        LogInfo(L"Subsonic metadata cache pruned stale tracks: " + std::to_wstring(pruned));
    }
    return pruned;
}

void SubsonicRepository::RememberArtistAlbums(const std::wstring& artistId, const std::vector<AlbumInfo>& albums) const {
    bool changed = false;
    std::lock_guard lock(tracksMutex_);
    for (const auto& album : albums) {
        changed = RememberAlbumLocked(album) || changed;
    }
    if (!artistId.empty()) {
        auto& artist = artists_[artistId];
        artist.id = artistId;
        const auto ids = AlbumIds(albums);
        if (artist.albumIds != ids) {
            artist.albumIds = ids;
            changed = true;
        }
        if (artist.albumCount <= 0) {
            artist.albumCount = static_cast<int>(ids.size());
            changed = true;
        }
        if (artist.name.empty() && !albums.empty()) {
            artist.name = albums.front().artist;
            changed = true;
        }
    }
    if (changed) {
        SaveMetadataCacheLocked();
    }
}

std::optional<TrackInfo> SubsonicRepository::FindTrackMetadata(const std::wstring& id) const {
    LoadMetadataCache();
    std::lock_guard lock(tracksMutex_);
    const auto it = tracks_.find(id);
    return it != tracks_.end() ? std::optional<TrackInfo>(it->second) : std::nullopt;
}

void SubsonicRepository::LoadMetadataCache() const {
    const std::wstring key = client_ ? client_->MetadataCacheKey() : std::wstring();
    std::lock_guard lock(tracksMutex_);
    if (metadataCacheLoaded_ && metadataCacheKey_ == key) {
        return;
    }

    tracks_.clear();
    playlists_.clear();
    artists_.clear();
    albums_.clear();
    allTrackIds_.clear();
    starredTrackIds_.clear();
    metadataCacheKey_ = key;
    metadataCacheLoaded_ = true;
    if (metadataCacheKey_.empty()) {
        return;
    }

    std::ifstream file(MetadataCachePath(), std::ios::binary);
    if (!file) {
        return;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    for (const auto& id : JsonGetStringArray(text, "allTrackIds")) {
        allTrackIds_.push_back(Utf8ToWideString(id));
    }
    for (const auto& id : JsonGetStringArray(text, "starredTrackIds")) {
        starredTrackIds_.push_back(Utf8ToWideString(id));
    }
    auto cachedTracks = ParseTrackArray(text, "cachedTrack");
    if (cachedTracks.empty()) {
        cachedTracks = ParseTrackArray(text, "track");
    }
    for (const auto& track : cachedTracks) {
        RememberTrackLocked(track);
    }
    auto cachedPlaylists = ParsePlaylistsForKey(text, "cachedPlaylist");
    if (cachedPlaylists.empty() && text.find("\"cachedPlaylist\"") == std::string::npos) {
        cachedPlaylists = ParsePlaylists(text);
    }
    for (const auto& playlist : cachedPlaylists) {
        RememberPlaylistLocked(playlist);
    }
    for (const auto& artist : ParseArtistsForKey(text, "cachedArtist")) {
        RememberArtistLocked(artist);
    }
    for (const auto& album : ParseAlbums(text, "cachedAlbum")) {
        RememberAlbumLocked(album);
    }
    LogInfo(L"Subsonic metadata cache loaded. Tracks=" + std::to_wstring(tracks_.size()) +
        L", Artists=" + std::to_wstring(artists_.size()) +
        L", Albums=" + std::to_wstring(albums_.size()) +
        L", Playlists=" + std::to_wstring(playlists_.size()) +
        L", AllTrackIds=" + std::to_wstring(allTrackIds_.size()) +
        L", StarredTrackIds=" + std::to_wstring(starredTrackIds_.size()));
}

void SubsonicRepository::SaveMetadataCacheLocked() const {
    if (metadataCacheKey_.empty()) {
        return;
    }
    std::error_code ec;
    const auto path = MetadataCachePath();
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        LogInfo(L"Subsonic metadata cache directory creation failed: " + Utf8ToWideString(ec.message()));
        return;
    }

    std::filesystem::path tempPath = path;
    tempPath += L".tmp";
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            LogInfo(L"Subsonic metadata cache save failed: cannot open temp file.");
            return;
        }
        out << "{\"version\":1,";
        AppendJsonStringArray(out, "allTrackIds", allTrackIds_);
        AppendJsonStringArray(out, "starredTrackIds", starredTrackIds_);
        out << "\"cachedTrack\":[";
        bool first = true;
        for (const auto& [id, track] : tracks_) {
            if (!first) {
                out << ",";
            }
            first = false;
            out << "{";
            AppendJsonString(out, "id", track.id);
            AppendJsonString(out, "title", track.title);
            AppendJsonString(out, "artist", track.artist);
            AppendJsonString(out, "album", track.album);
            AppendJsonString(out, "albumArtist", track.albumArtist);
            AppendJsonString(out, "artistId", track.artistId);
            AppendJsonString(out, "albumId", track.albumId);
            AppendJsonString(out, "coverArt", track.coverArt);
            AppendJsonString(out, "genre", track.genre);
            AppendJsonString(out, "playlistId", track.playlistId);
            AppendJsonString(out, "suffix", track.suffix);
            AppendJsonInt(out, "duration", track.durationSeconds);
            AppendJsonInt(out, "year", track.year);
            AppendJsonInt(out, "track", track.trackNumber);
            AppendJsonInt(out, "discNumber", track.discNumber);
            AppendJsonInt(out, "userRating", track.rating);
            AppendJsonInt64(out, "size", track.size);
            out << "\"starred\":" << (track.starred ? "true" : "false");
            out << "}";
        }
        out << "],\"cachedPlaylist\":[";
        first = true;
        for (const auto& [id, playlist] : playlists_) {
            if (!first) {
                out << ",";
            }
            first = false;
            out << "{";
            AppendJsonString(out, "id", playlist.id);
            AppendJsonString(out, "name", playlist.name);
            AppendJsonString(out, "owner", playlist.owner);
            AppendJsonStringArray(out, "songIds", playlist.songIds);
            AppendJsonInt(out, "songCount", playlist.songCount);
            AppendJsonInt(out, "duration", playlist.durationSeconds);
            out << "\"public\":" << (playlist.isPublic ? "true" : "false");
            out << "}";
        }
        out << "],\"cachedArtist\":[";
        first = true;
        for (const auto& [id, artist] : artists_) {
            if (!first) {
                out << ",";
            }
            first = false;
            out << "{";
            AppendJsonString(out, "id", artist.id);
            AppendJsonString(out, "name", artist.name);
            AppendJsonString(out, "coverArt", artist.coverArt);
            AppendJsonStringArray(out, "albumIds", artist.albumIds);
            AppendJsonInt(out, "albumCount", artist.albumCount, false);
            out << "}";
        }
        out << "],\"cachedAlbum\":[";
        first = true;
        for (const auto& [id, album] : albums_) {
            if (!first) {
                out << ",";
            }
            first = false;
            out << "{";
            AppendJsonString(out, "id", album.id);
            AppendJsonString(out, "name", album.name);
            AppendJsonString(out, "artist", album.artist);
            AppendJsonString(out, "artistId", album.artistId);
            AppendJsonString(out, "coverArt", album.coverArt);
            AppendJsonString(out, "genre", album.genre);
            AppendJsonStringArray(out, "songIds", album.songIds);
            AppendJsonInt(out, "songCount", album.songCount);
            AppendJsonInt(out, "duration", album.durationSeconds);
            AppendJsonInt(out, "year", album.year, false);
            out << "}";
        }
        out << "]}";
        if (!out) {
            LogInfo(L"Subsonic metadata cache save failed while writing.");
            return;
        }
    }

    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tempPath, path, ec);
    }
    if (ec) {
        LogInfo(L"Subsonic metadata cache save failed: " + Utf8ToWideString(ec.message()));
    } else {
        LogInfo(L"Subsonic metadata cache saved. Tracks=" + std::to_wstring(tracks_.size()) +
            L", Artists=" + std::to_wstring(artists_.size()) +
            L", Albums=" + std::to_wstring(albums_.size()) +
            L", Playlists=" + std::to_wstring(playlists_.size()) +
            L", AllTrackIds=" + std::to_wstring(allTrackIds_.size()) +
            L", StarredTrackIds=" + std::to_wstring(starredTrackIds_.size()));
    }
}

bool SubsonicRepository::RememberTrackLocked(const TrackInfo& track) const {
    if (track.id.empty()) {
        return false;
    }
    const auto it = tracks_.find(track.id);
    if (it != tracks_.end() &&
        it->second.title == track.title &&
        it->second.artist == track.artist &&
        it->second.album == track.album &&
        it->second.albumArtist == track.albumArtist &&
        it->second.artistId == track.artistId &&
        it->second.albumId == track.albumId &&
        it->second.coverArt == track.coverArt &&
        it->second.genre == track.genre &&
        it->second.playlistId == track.playlistId &&
        it->second.suffix == track.suffix &&
        it->second.durationSeconds == track.durationSeconds &&
        it->second.year == track.year &&
        it->second.trackNumber == track.trackNumber &&
        it->second.discNumber == track.discNumber &&
        it->second.rating == track.rating &&
        it->second.size == track.size &&
        it->second.starred == track.starred) {
        return false;
    }
    tracks_[track.id] = track;
    return true;
}

bool SubsonicRepository::RememberPlaylistLocked(const PlaylistInfo& playlist) const {
    if (playlist.id.empty()) {
        return false;
    }
    PlaylistInfo merged = playlist;
    const auto it = playlists_.find(playlist.id);
    if (it != playlists_.end() && merged.songIds.empty()) {
        merged.songIds = it->second.songIds;
    }
    if (it != playlists_.end() &&
        it->second.name == merged.name &&
        it->second.owner == merged.owner &&
        it->second.songIds == merged.songIds &&
        it->second.songCount == merged.songCount &&
        it->second.durationSeconds == merged.durationSeconds &&
        it->second.isPublic == merged.isPublic) {
        return false;
    }
    playlists_[playlist.id] = std::move(merged);
    return true;
}

bool SubsonicRepository::RememberArtistLocked(const ArtistInfo& artist) const {
    if (artist.id.empty()) {
        return false;
    }
    ArtistInfo merged = artist;
    const auto it = artists_.find(artist.id);
    if (it != artists_.end() && merged.albumIds.empty()) {
        merged.albumIds = it->second.albumIds;
    }
    if (it != artists_.end() &&
        it->second.name == merged.name &&
        it->second.coverArt == merged.coverArt &&
        it->second.albumIds == merged.albumIds &&
        it->second.albumCount == merged.albumCount) {
        return false;
    }
    artists_[artist.id] = std::move(merged);
    return true;
}

bool SubsonicRepository::RememberAlbumLocked(const AlbumInfo& album) const {
    if (album.id.empty()) {
        return false;
    }
    AlbumInfo merged = album;
    const auto it = albums_.find(album.id);
    if (it != albums_.end() && merged.songIds.empty()) {
        merged.songIds = it->second.songIds;
    }
    if (it != albums_.end() &&
        it->second.name == merged.name &&
        it->second.artist == merged.artist &&
        it->second.artistId == merged.artistId &&
        it->second.coverArt == merged.coverArt &&
        it->second.genre == merged.genre &&
        it->second.songIds == merged.songIds &&
        it->second.songCount == merged.songCount &&
        it->second.durationSeconds == merged.durationSeconds &&
        it->second.year == merged.year) {
        return false;
    }
    albums_[album.id] = std::move(merged);
    return true;
}

std::vector<TrackInfo> SubsonicRepository::TracksByIdsLocked(const std::vector<std::wstring>& ids, int limit) const {
    std::vector<TrackInfo> result;
    result.reserve(limit > 0 ? std::min(static_cast<size_t>(limit), ids.size()) : ids.size());
    for (const auto& id : ids) {
        const auto it = tracks_.find(id);
        if (it != tracks_.end()) {
            result.push_back(it->second);
            if (limit > 0 && result.size() >= static_cast<size_t>(limit)) {
                break;
            }
        }
    }
    return result;
}

std::vector<AlbumInfo> SubsonicRepository::AlbumsByIdsLocked(const std::vector<std::wstring>& ids, int limit) const {
    std::vector<AlbumInfo> result;
    result.reserve(limit > 0 ? std::min(static_cast<size_t>(limit), ids.size()) : ids.size());
    for (const auto& id : ids) {
        const auto it = albums_.find(id);
        if (it != albums_.end()) {
            result.push_back(it->second);
            if (limit > 0 && result.size() >= static_cast<size_t>(limit)) {
                break;
            }
        }
    }
    return result;
}

std::filesystem::path SubsonicRepository::MetadataCachePath() const {
    const std::wstring fileName = metadataCacheKey_.empty() ? L"default" : metadataCacheKey_;
    return MetadataCacheRoot() / (fileName + L".json");
}
