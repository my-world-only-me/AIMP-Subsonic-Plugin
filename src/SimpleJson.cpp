#include "SimpleJson.h"

#include <cctype>
#include <cstdlib>

#include "Url.h"

namespace {

void SkipWhitespace(const std::string& text, size_t& pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
}

std::optional<std::string> ParseJsonStringAt(const std::string& text, size_t& pos) {
    if (pos >= text.size() || text[pos] != '"') {
        return std::nullopt;
    }
    ++pos;
    std::string result;
    while (pos < text.size()) {
        const char ch = text[pos++];
        if (ch == '"') {
            return result;
        }
        if (ch != '\\') {
            result.push_back(ch);
            continue;
        }
        if (pos >= text.size()) {
            break;
        }
        const char escaped = text[pos++];
        switch (escaped) {
        case '"': result.push_back('"'); break;
        case '\\': result.push_back('\\'); break;
        case '/': result.push_back('/'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        case 'u': {
            if (pos + 4 > text.size()) {
                return std::nullopt;
            }
            const std::string hex = text.substr(pos, 4);
            pos += 4;
            const char* begin = hex.c_str();
            char* end = nullptr;
            const unsigned long code = std::strtoul(begin, &end, 16);
            if (end != begin + 4) {
                return std::nullopt;
            }
            if (code <= 0x7F) {
                result.push_back(static_cast<char>(code));
            } else if (code <= 0x7FF) {
                result.push_back(static_cast<char>(0xC0 | (code >> 6)));
                result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
                result.push_back(static_cast<char>(0xE0 | (code >> 12)));
                result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            break;
        }
        default:
            result.push_back(escaped);
            break;
        }
    }
    return std::nullopt;
}

std::optional<size_t> FindKeyValue(const std::string& objectText, const std::string& key) {
    size_t pos = 0;
    while ((pos = objectText.find('"', pos)) != std::string::npos) {
        size_t keyPos = pos;
        auto parsedKey = ParseJsonStringAt(objectText, keyPos);
        if (!parsedKey) {
            ++pos;
            continue;
        }
        SkipWhitespace(objectText, keyPos);
        if (keyPos >= objectText.size() || objectText[keyPos] != ':') {
            pos = keyPos;
            continue;
        }
        ++keyPos;
        SkipWhitespace(objectText, keyPos);
        if (*parsedKey == key) {
            return keyPos;
        }
        pos = keyPos;
    }
    return std::nullopt;
}

std::optional<std::string> ExtractObject(const std::string& text, size_t start) {
    if (start >= text.size() || text[start] != '{') {
        return std::nullopt;
    }
    int depth = 0;
    bool inString = false;
    bool escape = false;
    for (size_t pos = start; pos < text.size(); ++pos) {
        const char ch = text[pos];
        if (inString) {
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }
        if (ch == '"') {
            inString = true;
        } else if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) {
                return text.substr(start, pos - start + 1);
            }
        }
    }
    return std::nullopt;
}

std::vector<std::string> ParseJsonStringArrayAt(const std::string& text, size_t& pos) {
    std::vector<std::string> result;
    if (pos >= text.size() || text[pos] != '[') {
        return result;
    }
    ++pos;
    while (pos < text.size()) {
        SkipWhitespace(text, pos);
        if (pos < text.size() && text[pos] == ']') {
            ++pos;
            break;
        }
        auto value = ParseJsonStringAt(text, pos);
        if (!value) {
            break;
        }
        result.push_back(*value);
        SkipWhitespace(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < text.size() && text[pos] == ']') {
            ++pos;
        }
        break;
    }
    return result;
}

std::optional<std::string> ExtractArray(const std::string& text, size_t start) {
    if (start >= text.size() || text[start] != '[') {
        return std::nullopt;
    }
    int depth = 0;
    bool inString = false;
    bool escape = false;
    for (size_t pos = start; pos < text.size(); ++pos) {
        const char ch = text[pos];
        if (inString) {
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }
        if (ch == '"') {
            inString = true;
        } else if (ch == '[') {
            ++depth;
        } else if (ch == ']') {
            --depth;
            if (depth == 0) {
                return text.substr(start, pos - start + 1);
            }
        }
    }
    return std::nullopt;
}

// Skips one JSON value (string, object, array, number, or literal) starting at pos.
bool SkipJsonValue(const std::string& text, size_t& pos) {
    SkipWhitespace(text, pos);
    if (pos >= text.size()) {
        return false;
    }
    const char ch = text[pos];
    if (ch == '"') {
        return ParseJsonStringAt(text, pos).has_value();
    }
    if (ch == '{') {
        const auto object = ExtractObject(text, pos);
        if (!object) {
            return false;
        }
        pos += object->size();
        return true;
    }
    if (ch == '[') {
        const auto array = ExtractArray(text, pos);
        if (!array) {
            return false;
        }
        pos += array->size();
        return true;
    }
    while (pos < text.size() && text[pos] != ',' && text[pos] != '}' && text[pos] != ']' &&
        !std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    return true;
}

// Finds the value position of `key` looking only at the top level of a JSON
// object. OpenSubsonic servers such as Navidrome nest objects and arrays
// (genres, artists, replayGain, releaseDate, ...) inside song/album entries,
// so a whole-text scan can match a key that belongs to a nested value.
std::optional<size_t> FindTopLevelKeyValue(const std::string& objectText, const std::string& key) {
    size_t pos = 0;
    SkipWhitespace(objectText, pos);
    if (pos >= objectText.size() || objectText[pos] != '{') {
        return std::nullopt;
    }
    ++pos;
    while (pos < objectText.size()) {
        SkipWhitespace(objectText, pos);
        if (pos >= objectText.size() || objectText[pos] == '}') {
            break;
        }
        auto parsedKey = ParseJsonStringAt(objectText, pos);
        if (!parsedKey) {
            break;
        }
        SkipWhitespace(objectText, pos);
        if (pos >= objectText.size() || objectText[pos] != ':') {
            break;
        }
        ++pos;
        SkipWhitespace(objectText, pos);
        if (*parsedKey == key) {
            return pos;
        }
        if (!SkipJsonValue(objectText, pos)) {
            break;
        }
        SkipWhitespace(objectText, pos);
        if (pos < objectText.size() && objectText[pos] == ',') {
            ++pos;
        }
    }
    return std::nullopt;
}

std::optional<std::string> GetStringField(const std::string& objectText, const std::string& key) {
    auto valuePos = FindTopLevelKeyValue(objectText, key);
    if (!valuePos || *valuePos >= objectText.size() || objectText[*valuePos] != '"') {
        return std::nullopt;
    }
    size_t pos = *valuePos;
    return ParseJsonStringAt(objectText, pos);
}

std::optional<int> GetIntField(const std::string& objectText, const std::string& key) {
    auto valuePos = FindTopLevelKeyValue(objectText, key);
    if (!valuePos) {
        return std::nullopt;
    }
    try {
        return std::stoi(objectText.substr(*valuePos));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<long long> GetInt64Field(const std::string& objectText, const std::string& key) {
    auto valuePos = FindTopLevelKeyValue(objectText, key);
    if (!valuePos) {
        return std::nullopt;
    }
    try {
        return std::stoll(objectText.substr(*valuePos));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<bool> GetBoolField(const std::string& objectText, const std::string& key) {
    auto valuePos = FindTopLevelKeyValue(objectText, key);
    if (!valuePos) {
        return std::nullopt;
    }
    const auto tail = objectText.substr(*valuePos, 5);
    if (tail.rfind("true", 0) == 0) {
        return true;
    }
    if (tail.rfind("false", 0) == 0) {
        return false;
    }
    return std::nullopt;
}

std::vector<std::string> GetStringArrayField(const std::string& objectText, const std::string& key) {
    auto valuePos = FindTopLevelKeyValue(objectText, key);
    if (!valuePos || *valuePos >= objectText.size() || objectText[*valuePos] != '[') {
        return {};
    }
    size_t pos = *valuePos;
    return ParseJsonStringArrayAt(objectText, pos);
}

void CollectObjectsFromArray(const std::string& json, size_t arrayStart, std::vector<std::string>& objects) {
    size_t pos = arrayStart + 1;
    while (pos < json.size()) {
        SkipWhitespace(json, pos);
        if (pos >= json.size() || json[pos] == ']') {
            break;
        }
        if (json[pos] == '{') {
            auto object = ExtractObject(json, pos);
            if (!object) {
                break;
            }
            objects.push_back(*object);
            pos += object->size();
        } else if (!SkipJsonValue(json, pos)) {
            break;
        }
        SkipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == ',') {
            ++pos;
        }
    }
}

// Collects the objects stored under every occurrence of `key` whose value is
// an object or an array of objects. Servers such as Navidrome repeat the same
// key several times, for example one "artist" array per index letter in
// getArtists, so reading only the first occurrence drops most of the library.
std::vector<std::string> ExtractObjectsForKey(const std::string& json, const std::string& key) {
    std::vector<std::string> objects;
    size_t pos = 0;
    while ((pos = json.find('"', pos)) != std::string::npos) {
        size_t cursor = pos;
        auto parsedKey = ParseJsonStringAt(json, cursor);
        if (!parsedKey) {
            ++pos;
            continue;
        }
        SkipWhitespace(json, cursor);
        if (cursor >= json.size() || json[cursor] != ':') {
            pos = cursor > pos ? cursor : pos + 1;
            continue;
        }
        ++cursor;
        SkipWhitespace(json, cursor);
        if (cursor >= json.size()) {
            break;
        }
        if (*parsedKey != key) {
            pos = cursor;
            continue;
        }
        if (json[cursor] == '{') {
            if (auto object = ExtractObject(json, cursor)) {
                objects.push_back(*object);
                pos = cursor + object->size();
                continue;
            }
        } else if (json[cursor] == '[') {
            const size_t before = objects.size();
            CollectObjectsFromArray(json, cursor, objects);
            if (auto array = ExtractArray(json, cursor)) {
                pos = cursor + array->size();
                continue;
            }
            if (objects.size() > before) {
                pos = cursor + 1;
                continue;
            }
        }
        pos = cursor;
    }
    return objects;
}

TrackInfo ParseTrackObject(const std::string& object) {
    TrackInfo track;
    track.id = Utf8ToWideString(GetStringField(object, "id").value_or(""));
    track.title = Utf8ToWideString(GetStringField(object, "title").value_or(""));
    track.artist = Utf8ToWideString(GetStringField(object, "artist").value_or(""));
    track.album = Utf8ToWideString(GetStringField(object, "album").value_or(""));
    track.albumArtist = Utf8ToWideString(GetStringField(object, "albumArtist").value_or(""));
    track.artistId = Utf8ToWideString(GetStringField(object, "artistId").value_or(""));
    track.albumId = Utf8ToWideString(GetStringField(object, "albumId").value_or(""));
    track.coverArt = Utf8ToWideString(GetStringField(object, "coverArt").value_or(""));
    track.genre = Utf8ToWideString(GetStringField(object, "genre").value_or(""));
    track.playlistId = Utf8ToWideString(GetStringField(object, "playlistId").value_or(""));
    track.suffix = Utf8ToWideString(GetStringField(object, "suffix").value_or("mp3"));
    track.durationSeconds = GetIntField(object, "duration").value_or(0);
    track.year = GetIntField(object, "year").value_or(0);
    track.trackNumber = GetIntField(object, "track").value_or(0);
    track.discNumber = GetIntField(object, "discNumber").value_or(0);
    track.rating = GetIntField(object, "userRating").value_or(0);
    track.size = GetInt64Field(object, "size").value_or(0);
    track.starred = GetStringField(object, "starred").has_value() || GetBoolField(object, "starred").value_or(false);
    return track;
}

} // namespace

std::optional<std::string> JsonGetString(const std::string& objectText, const std::string& key) {
    auto valuePos = FindKeyValue(objectText, key);
    if (!valuePos || *valuePos >= objectText.size() || objectText[*valuePos] != '"') {
        return std::nullopt;
    }
    size_t pos = *valuePos;
    return ParseJsonStringAt(objectText, pos);
}

std::optional<int> JsonGetInt(const std::string& objectText, const std::string& key) {
    auto valuePos = FindKeyValue(objectText, key);
    if (!valuePos) {
        return std::nullopt;
    }
    try {
        return std::stoi(objectText.substr(*valuePos));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<long long> JsonGetInt64(const std::string& objectText, const std::string& key) {
    auto valuePos = FindKeyValue(objectText, key);
    if (!valuePos) {
        return std::nullopt;
    }
    try {
        return std::stoll(objectText.substr(*valuePos));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<bool> JsonGetBool(const std::string& objectText, const std::string& key) {
    auto valuePos = FindKeyValue(objectText, key);
    if (!valuePos) {
        return std::nullopt;
    }
    const auto tail = objectText.substr(*valuePos);
    if (tail.rfind("true", 0) == 0) {
        return true;
    }
    if (tail.rfind("false", 0) == 0) {
        return false;
    }
    return std::nullopt;
}

std::vector<std::string> JsonGetStringArray(const std::string& objectText, const std::string& key) {
    auto valuePos = FindKeyValue(objectText, key);
    if (!valuePos || *valuePos >= objectText.size() || objectText[*valuePos] != '[') {
        return {};
    }
    size_t pos = *valuePos;
    return ParseJsonStringArrayAt(objectText, pos);
}

std::vector<TrackInfo> ParseTrackArray(const std::string& json, const std::string& key) {
    std::vector<TrackInfo> tracks;
    for (const auto& object : ExtractObjectsForKey(json, key)) {
        TrackInfo track = ParseTrackObject(object);
        if (!track.id.empty()) {
            tracks.push_back(track);
        }
    }
    return tracks;
}

std::vector<TrackInfo> ParseRandomSongs(const std::string& json) {
    return ParseTrackArray(json, "song");
}

std::vector<PlaylistInfo> ParsePlaylists(const std::string& json) {
    return ParsePlaylistsForKey(json, "playlist");
}

std::vector<PlaylistInfo> ParsePlaylistsForKey(const std::string& json, const std::string& key) {
    std::vector<PlaylistInfo> playlists;
    for (const auto& object : ExtractObjectsForKey(json, key)) {
        PlaylistInfo playlist;
        playlist.id = Utf8ToWideString(GetStringField(object, "id").value_or(""));
        playlist.name = Utf8ToWideString(GetStringField(object, "name").value_or(""));
        playlist.owner = Utf8ToWideString(GetStringField(object, "owner").value_or(""));
        for (const auto& id : GetStringArrayField(object, "songIds")) {
            playlist.songIds.push_back(Utf8ToWideString(id));
        }
        playlist.songCount = GetIntField(object, "songCount").value_or(0);
        playlist.durationSeconds = GetIntField(object, "duration").value_or(0);
        playlist.isPublic = GetBoolField(object, "public").value_or(false);
        if (!playlist.id.empty()) {
            playlists.push_back(playlist);
        }
    }
    return playlists;
}

std::vector<ArtistInfo> ParseArtists(const std::string& json) {
    return ParseArtistsForKey(json, "artist");
}

std::vector<ArtistInfo> ParseArtistsForKey(const std::string& json, const std::string& key) {
    std::vector<ArtistInfo> artists;
    for (const auto& object : ExtractObjectsForKey(json, key)) {
        ArtistInfo artist;
        artist.id = Utf8ToWideString(GetStringField(object, "id").value_or(""));
        artist.name = Utf8ToWideString(GetStringField(object, "name").value_or(""));
        artist.coverArt = Utf8ToWideString(GetStringField(object, "coverArt").value_or(""));
        for (const auto& id : GetStringArrayField(object, "albumIds")) {
            artist.albumIds.push_back(Utf8ToWideString(id));
        }
        artist.albumCount = GetIntField(object, "albumCount").value_or(0);
        if (!artist.id.empty()) {
            artists.push_back(artist);
        }
    }
    return artists;
}

std::vector<AlbumInfo> ParseAlbums(const std::string& json, const std::string& key) {
    std::vector<AlbumInfo> albums;
    for (const auto& object : ExtractObjectsForKey(json, key)) {
        AlbumInfo album;
        album.id = Utf8ToWideString(GetStringField(object, "id").value_or(""));
        album.name = Utf8ToWideString(GetStringField(object, "name")
            .value_or(GetStringField(object, "title")
            .value_or(GetStringField(object, "album").value_or(""))));
        album.artist = Utf8ToWideString(GetStringField(object, "artist").value_or(""));
        album.artistId = Utf8ToWideString(GetStringField(object, "artistId").value_or(""));
        album.coverArt = Utf8ToWideString(GetStringField(object, "coverArt").value_or(""));
        album.genre = Utf8ToWideString(GetStringField(object, "genre").value_or(""));
        for (const auto& id : GetStringArrayField(object, "songIds")) {
            album.songIds.push_back(Utf8ToWideString(id));
        }
        album.songCount = GetIntField(object, "songCount").value_or(0);
        album.durationSeconds = GetIntField(object, "duration").value_or(0);
        album.year = GetIntField(object, "year").value_or(0);
        if (!album.id.empty()) {
            albums.push_back(album);
        }
    }
    return albums;
}
