#include "PlaylistBridge.h"

#include <cstdlib>

#include "AimpString.h"
#include "Url.h"
#include "i18n.h"

namespace {

bool StartsWithIgnoreCase(const std::wstring& value, const std::wstring& prefix) {
    return value.size() >= prefix.size() && _wcsnicmp(value.c_str(), prefix.c_str(), prefix.size()) == 0;
}

std::wstring ExtractQueryParam(const std::wstring& uri, const std::wstring& key) {
    const size_t question = uri.find(L'?');
    if (question == std::wstring::npos) {
        return {};
    }
    const std::wstring needle = key + L"=";
    size_t pos = question + 1;
    while (pos < uri.size()) {
        const size_t next = uri.find(L'&', pos);
        const size_t length = next == std::wstring::npos ? std::wstring::npos : next - pos;
        const std::wstring part = uri.substr(pos, length);
        if (part.rfind(needle, 0) == 0) {
            return UrlDecode(part.substr(needle.size()));
        }
        if (next == std::wstring::npos) {
            break;
        }
        pos = next + 1;
    }
    return {};
}

int ParsePositiveInt(const std::wstring& value) {
    if (value.empty()) {
        return 0;
    }
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(value.c_str(), &end, 10);
    return parsed > 0 ? static_cast<int>(parsed) : 0;
}

} // namespace

namespace PlaylistBridge {

std::wstring TrackDisplayText(const TrackInfo& track) {
    if (!track.artist.empty() && !track.title.empty()) {
        return track.artist + L" - " + track.title;
    }
    if (!track.title.empty()) {
        return track.title;
    }
    if (!track.artist.empty()) {
        return track.artist;
    }
    return L"Subsonic song " + track.id;
}

std::wstring ExtractSubsonicSongId(const std::wstring& uri) {
    const std::wstring id = ExtractQueryParam(uri, L"id");
    if (!id.empty() && uri.find(L"/rest/stream") != std::wstring::npos) {
        return id;
    }

    const std::wstring virtualPrefix = L"subsonic:\\\\song\\";
    if (StartsWithIgnoreCase(uri, virtualPrefix)) {
        std::wstring tail = uri.substr(virtualPrefix.size());
        const size_t slash = tail.find_first_of(L"\\/");
        if (slash != std::wstring::npos) {
            tail.resize(slash);
        }
        return tail;
    }

    const std::wstring legacyPrefix = L"subsonic://song/";
    if (StartsWithIgnoreCase(uri, legacyPrefix)) {
        std::wstring tail = uri.substr(legacyPrefix.size());
        const size_t slash = tail.find_first_of(L"\\/");
        if (slash != std::wstring::npos) {
            tail.resize(slash);
        }
        return tail;
    }
    return {};
}

std::wstring GetFileInfoString(IAIMPFileInfo* info, int propId) {
    if (!info) {
        return {};
    }
    IAIMPString* value = nullptr;
    std::wstring result;
    if (SUCCEEDED(info->GetValueAsObject(propId, IID_IAIMPString, reinterpret_cast<void**>(&value))) && value) {
        result = FromAimpString(value);
        value->Release();
    }
    return result;
}

std::wstring GetPlaylistItemString(IAIMPPlaylistItem* item, int propId) {
    if (!item) {
        return {};
    }
    IAIMPString* value = nullptr;
    std::wstring result;
    if (SUCCEEDED(item->GetValueAsObject(propId, IID_IAIMPString, reinterpret_cast<void**>(&value))) && value) {
        result = FromAimpString(value);
        value->Release();
    }
    return result;
}

std::wstring ExtractSubsonicSongId(IAIMPFileInfo* info) {
    std::wstring songId = ExtractSubsonicSongId(GetFileInfoString(info, AIMP_FILEINFO_PROPID_FILENAME));
    if (songId.empty()) {
        songId = ExtractSubsonicSongId(GetFileInfoString(info, AIMP_FILEINFO_PROPID_URL));
    }
    return songId;
}

TrackInfo BuildTrackFromFileInfo(const std::wstring& songId, IAIMPFileInfo* info) {
    TrackInfo track;
    track.id = songId;
    if (!info) {
        track.title = L10n::Text(L"Subsonic 曲目 ", L"Subsonic song ") + songId;
        return track;
    }

    track.title = GetFileInfoString(info, AIMP_FILEINFO_PROPID_TITLE);
    track.artist = GetFileInfoString(info, AIMP_FILEINFO_PROPID_ARTIST);
    track.album = GetFileInfoString(info, AIMP_FILEINFO_PROPID_ALBUM);
    track.albumArtist = GetFileInfoString(info, AIMP_FILEINFO_PROPID_ALBUMARTIST);
    track.genre = GetFileInfoString(info, AIMP_FILEINFO_PROPID_GENRE);
    track.year = ParsePositiveInt(GetFileInfoString(info, AIMP_FILEINFO_PROPID_DATE));
    track.trackNumber = ParsePositiveInt(GetFileInfoString(info, AIMP_FILEINFO_PROPID_TRACKNUMBER));

    double duration = 0.0;
    if (SUCCEEDED(info->GetValueAsFloat(AIMP_FILEINFO_PROPID_DURATION, &duration)) && duration > 0.0) {
        track.durationSeconds = static_cast<int>(duration + 0.5);
    }

    INT64 fileSize = 0;
    if (SUCCEEDED(info->GetValueAsInt64(AIMP_FILEINFO_PROPID_FILESIZE, &fileSize)) && fileSize > 0) {
        track.size = static_cast<long long>(fileSize);
    }

    int rating = 0;
    if (SUCCEEDED(info->GetValueAsInt32(AIMP_FILEINFO_PROPID_RATING, &rating)) && rating > 0) {
        track.rating = rating;
    }

    if (track.title.empty()) {
        track.title = L10n::Text(L"Subsonic 曲目 ", L"Subsonic song ") + songId;
    }
    return track;
}

} // namespace PlaylistBridge
