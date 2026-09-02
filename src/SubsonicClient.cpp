#include "SubsonicClient.h"

#include <algorithm>
#include <fstream>
#include <chrono>
#include <utility>
#include <vector>
#include <windows.h>
#include <winhttp.h>

#include "Diagnostics.h"
#include "Url.h"
#include "Version.h"

namespace {

struct HttpResponse {
    DWORD status{0};
    std::string body;
    std::wstring error;
};

DWORD InsecureTlsFlags() {
    return SECURITY_FLAG_IGNORE_UNKNOWN_CA |
        SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
        SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
        SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
}

bool IsSubsonicOk(const HttpResponse& response) {
    return response.status >= 200 && response.status < 300 &&
        JsonGetString(response.body, "status").value_or("") == "ok";
}

std::optional<URL_COMPONENTS> CrackUrl(const std::wstring& url, std::wstring& host, std::wstring& path) {
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        return std::nullopt;
    }

    host.assign(components.lpszHostName, components.dwHostNameLength);
    path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    return components;
}

void ApplyTlsOptions(HINTERNET request, bool ignoreTlsCertificateErrors) {
    if (!request || !ignoreTlsCertificateErrors) {
        return;
    }
    DWORD flags = InsecureTlsFlags();
    if (WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags))) {
        LogInfo(L"WinHTTP TLS certificate validation disabled for this request.");
    } else {
        LogInfo(FormatLastWindowsError(L"WinHTTP failed to disable TLS certificate validation"));
    }
}

HttpResponse HttpGet(const std::wstring& url, bool ignoreTlsCertificateErrors) {
    HttpResponse response;
    LogInfo(L"HTTP GET: " + RedactSensitiveUrl(url));
    std::wstring host;
    std::wstring path;
    const auto components = CrackUrl(url, host, path);
    if (!components) {
        response.error = L"Cannot parse URL.";
        return response;
    }

    HINTERNET session = WinHttpOpen(AIMP_SUBSONIC_USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        response.error = FormatLastWindowsError(L"WinHttpOpen failed");
        return response;
    }

    HINTERNET connect = WinHttpConnect(session, host.c_str(), components->nPort, 0);
    HINTERNET request = nullptr;
    if (connect) {
        const DWORD flags = components->nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (components->nScheme == INTERNET_SCHEME_HTTPS) {
            ApplyTlsOptions(request, ignoreTlsCertificateErrors);
        }
    }

    if (!connect) {
        response.error = FormatLastWindowsError(L"WinHttpConnect failed");
    } else if (!request) {
        response.error = FormatLastWindowsError(L"WinHttpOpenRequest failed");
    } else if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        response.error = FormatLastWindowsError(L"WinHttpSendRequest failed");
    } else if (!WinHttpReceiveResponse(request, nullptr)) {
        response.error = FormatLastWindowsError(L"WinHttpReceiveResponse failed");
    } else {
        DWORD statusSize = sizeof(response.status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &response.status, &statusSize, WINHTTP_NO_HEADER_INDEX);

        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
            std::vector<char> buffer(available);
            DWORD read = 0;
            if (!WinHttpReadData(request, buffer.data(), available, &read) || read == 0) {
                break;
            }
            response.body.append(buffer.data(), buffer.data() + read);
        }
    }

    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    if (response.error.empty()) {
        LogInfo(L"HTTP GET completed. Status=" + std::to_wstring(response.status) +
            L", Bytes=" + std::to_wstring(response.body.size()));
    } else {
        LogInfo(L"HTTP GET failed. " + response.error);
    }
    return response;
}

bool HttpDownloadToFile(const std::wstring& url, const std::filesystem::path& destination, bool ignoreTlsCertificateErrors) {
    LogInfo(L"HTTP download: " + RedactSensitiveUrl(url) + L" -> " + destination.wstring());
    std::wstring host;
    std::wstring path;
    const auto components = CrackUrl(url, host, path);
    if (!components) {
        LogInfo(L"HTTP file download failed: cannot parse URL.");
        return false;
    }
    std::filesystem::create_directories(destination.parent_path());
    std::ofstream file(destination, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }

    HINTERNET session = WinHttpOpen(AIMP_SUBSONIC_USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        LogInfo(FormatLastWindowsError(L"HTTP file download failed: WinHttpOpen failed"));
        return false;
    }

    HINTERNET connect = WinHttpConnect(session, host.c_str(), components->nPort, 0);
    HINTERNET request = nullptr;
    bool ok = false;
    if (connect) {
        const DWORD flags = components->nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (components->nScheme == INTERNET_SCHEME_HTTPS) {
            ApplyTlsOptions(request, ignoreTlsCertificateErrors);
        }
    }

    if (!connect) {
        LogInfo(FormatLastWindowsError(L"HTTP file download failed: WinHttpConnect failed"));
    } else if (!request) {
        LogInfo(FormatLastWindowsError(L"HTTP file download failed: WinHttpOpenRequest failed"));
    } else if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        LogInfo(FormatLastWindowsError(L"HTTP file download failed: WinHttpSendRequest failed"));
    } else if (!WinHttpReceiveResponse(request, nullptr)) {
        LogInfo(FormatLastWindowsError(L"HTTP file download failed: WinHttpReceiveResponse failed"));
    } else {
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
        ok = status >= 200 && status < 300;

        DWORD available = 0;
        while (ok && WinHttpQueryDataAvailable(request, &available) && available > 0) {
            std::vector<char> buffer(available);
            DWORD read = 0;
            if (!WinHttpReadData(request, buffer.data(), available, &read) || read == 0) {
                break;
            }
            file.write(buffer.data(), static_cast<std::streamsize>(read));
            ok = static_cast<bool>(file);
        }
    }

    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    if (!ok) {
        file.close();
        std::error_code ignored;
        std::filesystem::remove(destination, ignored);
        LogInfo(L"HTTP download failed; partial file removed.");
    } else {
        file.flush();
        std::error_code ignored;
        LogInfo(L"HTTP download completed. Bytes=" + std::to_wstring(std::filesystem::file_size(destination, ignored)));
    }
    return ok;
}

} // namespace

SubsonicClient::SubsonicClient(SubsonicConfig config)
    : config_(std::move(config)) {
}

bool SubsonicClient::Ping() const {
    const auto response = HttpGet(ApiUrl(L"ping", L""), config_.ignoreTlsCertificateErrors);
    const bool ok = IsSubsonicOk(response);
    if (!ok) {
        std::wstring message = L"Subsonic ping failed.";
        if (!response.error.empty()) {
            message += L" " + response.error;
        } else {
            message += L" HTTP status: " + std::to_wstring(response.status) + L".";
        }
        LogInfo(message);
    }
    return ok;
}

std::vector<TrackInfo> SubsonicClient::GetStarredSongs() const {
    const auto response = HttpGet(ApiUrl(L"getStarred2", L""), config_.ignoreTlsCertificateErrors);
    if (response.status < 200 || response.status >= 300) {
        LogInfo(L"getStarred2 failed. HTTP status: " + std::to_wstring(response.status) +
            (response.error.empty() ? L"" : L". " + response.error));
        return {};
    }
    auto tracks = ParseTrackArray(response.body, "song");
    for (auto& track : tracks) {
        track.starred = true;
    }
    LogInfo(L"getStarred2 parsed tracks: " + std::to_wstring(tracks.size()));
    return tracks;
}

std::vector<ArtistInfo> SubsonicClient::GetArtists() const {
    const auto response = HttpGet(ApiUrl(L"getArtists", L""), config_.ignoreTlsCertificateErrors);
    if (response.status < 200 || response.status >= 300) {
        LogInfo(L"getArtists failed. HTTP status: " + std::to_wstring(response.status) +
            (response.error.empty() ? L"" : L". " + response.error));
        return {};
    }
    auto artists = ParseArtists(response.body);
    LogInfo(L"getArtists parsed artists: " + std::to_wstring(artists.size()));
    return artists;
}

std::vector<AlbumInfo> SubsonicClient::GetArtistAlbums(const std::wstring& artistId) const {
    const auto response = HttpGet(ApiUrl(L"getArtist", L"id=" + UrlEncode(artistId)), config_.ignoreTlsCertificateErrors);
    if (response.status < 200 || response.status >= 300) {
        LogInfo(L"getArtist failed. ArtistId=" + artistId +
            L", HTTP status: " + std::to_wstring(response.status) +
            (response.error.empty() ? L"" : L". " + response.error));
        return {};
    }
    auto albums = ParseAlbums(response.body, "album");
    LogInfo(L"getArtist parsed albums: " + std::to_wstring(albums.size()) +
        L", ArtistId=" + artistId);
    return albums;
}

std::vector<AlbumInfo> SubsonicClient::GetAlbumList2(const std::wstring& type, int size, int offset) const {
    const int safeSize = std::clamp(size, 1, 500);
    const std::wstring query = L"type=" + UrlEncode(type) +
        L"&size=" + std::to_wstring(safeSize) +
        L"&offset=" + std::to_wstring(std::max(0, offset));
    const auto response = HttpGet(ApiUrl(L"getAlbumList2", query), config_.ignoreTlsCertificateErrors);
    if (response.status < 200 || response.status >= 300) {
        LogInfo(L"getAlbumList2 failed. Type=" + type +
            L", HTTP status: " + std::to_wstring(response.status) +
            (response.error.empty() ? L"" : L". " + response.error));
        return {};
    }
    auto albums = ParseAlbums(response.body, "album");
    LogInfo(L"getAlbumList2 parsed albums: " + std::to_wstring(albums.size()) +
        L", Type=" + type);
    return albums;
}

std::vector<TrackInfo> SubsonicClient::GetAlbumTracks(const std::wstring& albumId) const {
    const auto response = HttpGet(ApiUrl(L"getAlbum", L"id=" + UrlEncode(albumId)), config_.ignoreTlsCertificateErrors);
    if (response.status < 200 || response.status >= 300) {
        LogInfo(L"getAlbum failed. AlbumId=" + albumId +
            L", HTTP status: " + std::to_wstring(response.status) +
            (response.error.empty() ? L"" : L". " + response.error));
        return {};
    }
    auto tracks = ParseTrackArray(response.body, "song");
    LogInfo(L"getAlbum parsed tracks: " + std::to_wstring(tracks.size()) +
        L", AlbumId=" + albumId);
    return tracks;
}

std::optional<TrackInfo> SubsonicClient::GetSong(const std::wstring& id) const {
    if (id.empty()) {
        return std::nullopt;
    }
    const auto response = HttpGet(ApiUrl(L"getSong", L"id=" + UrlEncode(id)), config_.ignoreTlsCertificateErrors);
    if (response.status < 200 || response.status >= 300) {
        LogInfo(L"getSong failed. Id=" + id +
            L", HTTP status: " + std::to_wstring(response.status) +
            (response.error.empty() ? L"" : L". " + response.error));
        return std::nullopt;
    }
    auto tracks = ParseTrackArray(response.body, "song");
    if (tracks.empty()) {
        LogInfo(L"getSong parsed no track. Id=" + id);
        return std::nullopt;
    }
    LogInfo(L"getSong parsed track. Id=" + id + L", Title=" + tracks.front().title);
    return tracks.front();
}

std::vector<PlaylistInfo> SubsonicClient::GetPlaylists() const {
    const auto response = HttpGet(ApiUrl(L"getPlaylists", L""), config_.ignoreTlsCertificateErrors);
    if (response.status < 200 || response.status >= 300) {
        LogInfo(L"getPlaylists failed. HTTP status: " + std::to_wstring(response.status) +
            (response.error.empty() ? L"" : L". " + response.error));
        return {};
    }
    auto playlists = ParsePlaylists(response.body);
    LogInfo(L"getPlaylists parsed playlists: " + std::to_wstring(playlists.size()));
    return playlists;
}

std::vector<TrackInfo> SubsonicClient::GetPlaylistTracks(const std::wstring& playlistId) const {
    const auto response = HttpGet(ApiUrl(L"getPlaylist", L"id=" + UrlEncode(playlistId)), config_.ignoreTlsCertificateErrors);
    if (response.status < 200 || response.status >= 300) {
        LogInfo(L"getPlaylist failed. PlaylistId=" + playlistId +
            L", HTTP status: " + std::to_wstring(response.status) +
            (response.error.empty() ? L"" : L". " + response.error));
        return {};
    }
    auto tracks = ParseTrackArray(response.body, "entry");
    for (auto& track : tracks) {
        track.playlistId = playlistId;
    }
    LogInfo(L"getPlaylist parsed tracks: " + std::to_wstring(tracks.size()) +
        L", PlaylistId=" + playlistId);
    return tracks;
}

std::vector<TrackInfo> SubsonicClient::SearchSongs(const std::wstring& queryText, int count, int offset) const {
    const int safeCount = std::clamp(count, 1, 500);
    const int safeOffset = std::max(0, offset);
    const std::wstring query =
        L"query=" + UrlEncode(queryText) +
        L"&artistCount=0&albumCount=0&songCount=" + std::to_wstring(safeCount) +
        L"&songOffset=" + std::to_wstring(safeOffset);
    const auto response = HttpGet(ApiUrl(L"search3", query), config_.ignoreTlsCertificateErrors);
    if (response.status < 200 || response.status >= 300) {
        LogInfo(L"search3 failed. HTTP status: " + std::to_wstring(response.status) +
            (response.error.empty() ? L"" : L". " + response.error));
        return {};
    }
    auto tracks = ParseTrackArray(response.body, "song");
    LogInfo(L"search3 parsed tracks: " + std::to_wstring(tracks.size()) +
        L", Offset=" + std::to_wstring(safeOffset));
    return tracks;
}

bool SubsonicClient::Scrobble(const std::wstring& songId, bool submission) const {
    if (songId.empty()) {
        LogInfo(L"scrobble skipped: empty song id.");
        return false;
    }

    std::wstring query = L"id=" + UrlEncode(songId) +
        L"&submission=" + std::wstring(submission ? L"true" : L"false");
    if (submission) {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        query += L"&time=" + std::to_wstring(millis);
    }

    const auto response = HttpGet(ApiUrl(L"scrobble", query), config_.ignoreTlsCertificateErrors);
    const bool ok = IsSubsonicOk(response);
    LogInfo(L"scrobble completed. SongId=" + songId +
        L", Submission=" + std::to_wstring(submission ? 1 : 0) +
        L", OK=" + std::to_wstring(ok ? 1 : 0));
    return ok;
}

bool SubsonicClient::DownloadCoverArtToFile(const std::wstring& coverArtId, const std::filesystem::path& destination) const {
    if (coverArtId.empty()) {
        LogInfo(L"getCoverArt skipped: empty coverArt id.");
        return false;
    }
    return HttpDownloadToFile(BuildCoverArtUrl(coverArtId), destination, config_.ignoreTlsCertificateErrors);
}

std::wstring SubsonicClient::BuildStreamUrl(const std::wstring& id) const {
    const std::wstring format = config_.streamFormat.empty() ? L"mp3" : config_.streamFormat;
    if (_wcsicmp(format.c_str(), L"raw") == 0) {
        // format=raw asks the server to skip transcoding entirely. Navidrome
        // honors it even when no transcoder (ffmpeg) is installed, so do not
        // send maxBitRate, which would force a transcoding decision.
        return ApiUrl(L"stream", L"id=" + UrlEncode(id) + L"&format=raw");
    }
    const int maxBitRate = config_.maxBitRate > 0 ? config_.maxBitRate : 320;
    return ApiUrl(L"stream",
        L"id=" + UrlEncode(id) +
        L"&format=" + UrlEncode(format) +
        L"&maxBitRate=" + std::to_wstring(maxBitRate) +
        L"&estimateContentLength=true");
}

std::wstring SubsonicClient::BuildCoverArtUrl(const std::wstring& coverArtId) const {
    return ApiUrl(L"getCoverArt", L"id=" + UrlEncode(coverArtId));
}

std::wstring SubsonicClient::MetadataCacheKey() const {
    std::wstring key = Md5Hex(config_.serverUrl + L"|" + config_.username);
    return key.empty() ? L"default" : key;
}

int SubsonicClient::LibraryPageSize() const {
    return std::clamp(config_.libraryPageSize, 1, 500);
}

std::wstring SubsonicClient::ApiUrl(const std::wstring& method, const std::wstring& extraQuery) const {
    std::wstring result = config_.serverUrl + L"/rest/" + method + L".view?";
    result += AuthQuery();
    if (!extraQuery.empty()) {
        result += L"&" + extraQuery;
    }
    return result;
}

std::wstring SubsonicClient::AuthQuery() const {
    const std::wstring salt = RandomSalt();
    const std::wstring token = Md5Hex(config_.password + salt);
    return L"u=" + UrlEncode(config_.username) +
        L"&t=" + token +
        L"&s=" + UrlEncode(salt) +
        L"&v=1.16.1&c=aimp-subsonic&f=json";
}
