#include "SubsonicMusicLibrary.h"

#include <algorithm>
#include <cwctype>
#include <oleauto.h>

#include "Diagnostics.h"
#include "i18n.h"

namespace {

const wchar_t* kStorageId = L"aimp_subsonic.storage";
const wchar_t* kStorageCaption = L"Subsonic";
const wchar_t* kGroupingPresetId = L"aimp_subsonic.grouping.default";

const wchar_t* kFieldTitle = L"Title";
const wchar_t* kFieldArtist = L"Artist";
const wchar_t* kFieldAlbum = L"Album";
const wchar_t* kFieldAlbumArtist = L"AlbumArtist";
const wchar_t* kFieldGenre = L"Genre";
const wchar_t* kFieldYear = L"Year";
const wchar_t* kFieldTrackNumber = L"TrackNumber";
const wchar_t* kFieldRating = L"Rating";
const wchar_t* kFieldUserMark = L"UserMark";
const wchar_t* kFieldNode = L"SubsonicNode";
const wchar_t* kFieldParentNode = L"SubsonicParentNode";
const wchar_t* kFieldKind = L"SubsonicKind";
const wchar_t* kFieldSongId = L"SubsonicSongId";
const wchar_t* kFieldAlbumId = L"SubsonicAlbumId";
const wchar_t* kFieldArtistId = L"SubsonicArtistId";
const wchar_t* kFieldPlaylistId = L"SubsonicPlaylistId";

const wchar_t* kTokenPlaylists = L"root:playlists";
const wchar_t* kTokenArtists = L"root:artists";
const wchar_t* kTokenAlbums = L"root:albums";
const wchar_t* kTokenFavorites = L"root:favorites";
const wchar_t* kTokenTracks = L"root:tracks";
const wchar_t* kTokenPlaylistPrefix = L"playlist:";
const wchar_t* kTokenArtistPrefix = L"artist:";
const wchar_t* kTokenAlbumPrefix = L"album:";

const wchar_t* kCaptionPlaylists = L10n::Text(L"播放列表", L"Playlists");
const wchar_t* kCaptionArtists = L10n::Text(L"艺术家", L"Artists");
const wchar_t* kCaptionAlbums = L10n::Text(L"专辑", L"Albums");
const wchar_t* kCaptionFavorites = L10n::Text(L"收藏", L"Favorites");
const wchar_t* kCaptionTracks = L10n::Text(L"曲目", L"Tracks");
const wchar_t kNodePathSeparator = L'\\';
const wchar_t* kPathPlaylists = L"Playlists";
const wchar_t* kPathArtists = L"Artists";
const wchar_t* kPathAlbums = L"Albums";
const wchar_t* kPathFavorites = L"Favorites";
const wchar_t* kPathTracks = L"Tracks";

bool EqualsIgnoreCase(const std::wstring& left, const std::wstring& right) {
    return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

bool StartsWithIgnoreCase(const std::wstring& value, const std::wstring& prefix) {
    return value.size() >= prefix.size() && _wcsnicmp(value.c_str(), prefix.c_str(), prefix.size()) == 0;
}

wchar_t LowerChar(wchar_t value) {
    return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(value)));
}

bool ContainsIgnoreCase(const std::wstring& value, const std::wstring& query) {
    if (query.empty()) {
        return true;
    }
    if (value.empty() || query.size() > value.size()) {
        return false;
    }
    return std::search(value.begin(), value.end(), query.begin(), query.end(),
        [](wchar_t left, wchar_t right) {
            return LowerChar(left) == LowerChar(right);
        }) != value.end();
}

std::vector<std::wstring> SplitSearchTokens(const std::wstring& searchText) {
    std::vector<std::wstring> tokens;
    std::wstring token;
    for (wchar_t ch : searchText) {
        if (std::iswspace(static_cast<wint_t>(ch))) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
            token.push_back(ch);
        }
    }
    if (!token.empty()) {
        tokens.push_back(token);
    }
    return tokens;
}

bool TrackMatchesSearchToken(const TrackInfo& track, const std::wstring& token) {
    return ContainsIgnoreCase(track.title, token) ||
        ContainsIgnoreCase(track.artist, token) ||
        ContainsIgnoreCase(track.album, token) ||
        ContainsIgnoreCase(track.albumArtist, token) ||
        ContainsIgnoreCase(track.genre, token) ||
        ContainsIgnoreCase(track.id, token);
}

bool TrackMatchesSearch(const TrackInfo& track, const std::vector<std::wstring>& tokens) {
    for (const auto& token : tokens) {
        if (!TrackMatchesSearchToken(track, token)) {
            return false;
        }
    }
    return true;
}

std::vector<TrackInfo> FilterTracksBySearch(const std::vector<TrackInfo>& tracks, const std::wstring& searchText) {
    const auto tokens = SplitSearchTokens(searchText);
    if (tokens.empty()) {
        return tracks;
    }

    std::vector<TrackInfo> filtered;
    filtered.reserve(tracks.size());
    for (const auto& track : tracks) {
        if (TrackMatchesSearch(track, tokens)) {
            filtered.push_back(track);
        }
    }
    return filtered;
}

std::wstring IntToStringOrEmpty(int value) {
    return value > 0 ? std::to_wstring(value) : std::wstring();
}

std::wstring SafeNodeCaption(const std::wstring& caption, const std::wstring& fallback) {
    std::wstring result = caption.empty() ? fallback : caption;
    std::replace(result.begin(), result.end(), L'\\', L'/');
    return result;
}

std::wstring StripPrefix(const std::wstring& value, const std::wstring& prefix) {
    return StartsWithIgnoreCase(value, prefix) ? value.substr(prefix.size()) : std::wstring();
}

std::wstring RootNodePath(const std::wstring& caption) {
    return caption + std::wstring(1, kNodePathSeparator);
}

const std::wstring& PlaylistsRootPath() {
    static const std::wstring value = RootNodePath(kPathPlaylists);
    return value;
}

const std::wstring& ArtistsRootPath() {
    static const std::wstring value = RootNodePath(kPathArtists);
    return value;
}

const std::wstring& AlbumsRootPath() {
    static const std::wstring value = RootNodePath(kPathAlbums);
    return value;
}

const std::wstring& FavoritesRootPath() {
    static const std::wstring value = RootNodePath(kPathFavorites);
    return value;
}

const std::wstring& TracksRootPath() {
    static const std::wstring value = RootNodePath(kPathTracks);
    return value;
}

std::wstring NormalizeNodePath(std::wstring value) {
    std::replace(value.begin(), value.end(), L'/', kNodePathSeparator);
    if (!value.empty() &&
        !StartsWithIgnoreCase(value, kTokenPlaylistPrefix) &&
        !StartsWithIgnoreCase(value, kTokenArtistPrefix) &&
        !StartsWithIgnoreCase(value, kTokenAlbumPrefix) &&
        value.back() != kNodePathSeparator) {
        value.push_back(kNodePathSeparator);
    }
    return value;
}

std::wstring ChildNodePath(const std::wstring& parent, const std::wstring& caption) {
    return parent + SafeNodeCaption(caption, L"") + std::wstring(1, kNodePathSeparator);
}

std::wstring JoinForLog(const std::vector<std::wstring>& values) {
    std::wstring result;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            result += L" > ";
        }
        result += values[i];
    }
    return result;
}

std::wstring JoinCsvForLog(const std::vector<std::wstring>& values) {
    std::wstring result;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            result += L", ";
        }
        result += values[i];
    }
    return result;
}

int NodeSpecificity(const std::wstring& node) {
    if (StartsWithIgnoreCase(node, kTokenPlaylistPrefix) ||
        StartsWithIgnoreCase(node, kTokenAlbumPrefix)) {
        return 100;
    }
    if (StartsWithIgnoreCase(node, kTokenArtistPrefix)) {
        return 90;
    }
    if (EqualsIgnoreCase(node, kTokenFavorites) || EqualsIgnoreCase(node, kTokenTracks)) {
        return 80;
    }
    if (EqualsIgnoreCase(node, kTokenPlaylists) ||
        EqualsIgnoreCase(node, kTokenArtists) ||
        EqualsIgnoreCase(node, kTokenAlbums)) {
        return 10;
    }
    return node.empty() ? 0 : 1;
}

std::wstring BestNodeFromFilters(const std::vector<std::wstring>& nodes) {
    std::wstring best;
    int bestScore = -1;
    for (const auto& node : nodes) {
        const int score = NodeSpecificity(node);
        if (score > bestScore) {
            best = node;
            bestScore = score;
        }
    }
    return best;
}

std::wstring VariantToWString(const VARIANT& value) {
    if (value.vt == VT_BSTR && value.bstrVal) {
        return std::wstring(value.bstrVal, SysStringLen(value.bstrVal));
    }
    if ((value.vt == VT_UNKNOWN || value.vt == VT_DISPATCH) && value.punkVal) {
        IAIMPString* str = nullptr;
        if (SUCCEEDED(value.punkVal->QueryInterface(IID_IAIMPString, reinterpret_cast<void**>(&str))) && str) {
            std::wstring result = FromAimpString(str);
            str->Release();
            return result;
        }
    }
    return {};
}

const wchar_t* FilterOperationName(int operation) {
    switch (operation) {
    case AIMPML_FIELDFILTER_OPERATION_EQUALS:
        return L"EQUALS";
    case AIMPML_FIELDFILTER_OPERATION_NOTEQUALS:
        return L"NOTEQUALS";
    case AIMPML_FIELDFILTER_OPERATION_BETWEEN:
        return L"BETWEEN";
    case AIMPML_FIELDFILTER_OPERATION_LESSTHAN:
        return L"LESSTHAN";
    case AIMPML_FIELDFILTER_OPERATION_LESSTHANOREQUALS:
        return L"LESSTHANOREQUALS";
    case AIMPML_FIELDFILTER_OPERATION_GREATERTHAN:
        return L"GREATERTHAN";
    case AIMPML_FIELDFILTER_OPERATION_GREATERTHANOREQUALS:
        return L"GREATERTHANOREQUALS";
    case AIMPML_FIELDFILTER_OPERATION_CONTAINS:
        return L"CONTAINS";
    case AIMPML_FIELDFILTER_OPERATION_BEGINSWITH:
        return L"BEGINSWITH";
    case AIMPML_FIELDFILTER_OPERATION_ENDSWITH:
        return L"ENDSWITH";
    case AIMPML_FIELDFILTER_OPERATION_ISLASTXDAYS:
        return L"ISLASTXDAYS";
    default:
        return L"UNKNOWN";
    }
}

std::wstring ReadFilterStringProperty(IAIMPMLDataFilter* filter, int propertyId) {
    if (!filter) {
        return {};
    }

    IAIMPString* value = nullptr;
    std::wstring result;
    if (SUCCEEDED(filter->GetValueAsObject(propertyId, IID_IAIMPString, reinterpret_cast<void**>(&value))) && value) {
        result = FromAimpString(value);
    }
    SafeRelease(value);
    if (!result.empty()) {
        return result;
    }

    VARIANT* variantValue = nullptr;
    if (SUCCEEDED(filter->GetValueAsVariant(propertyId, &variantValue)) && variantValue) {
        result = VariantToWString(*variantValue);
    }
    return RedactSensitiveUrl(result);
}

int ReadFilterIntProperty(IAIMPMLDataFilter* filter, int propertyId) {
    if (!filter) {
        return -1;
    }
    int value = -1;
    if (FAILED(filter->GetValueAsInt32(propertyId, &value))) {
        return -1;
    }
    return value;
}

std::wstring FieldFilterValueForLog(IAIMPMLDataFieldFilter* filter, int propertyId) {
    if (!filter) {
        return {};
    }
    IAIMPString* value = nullptr;
    std::wstring result;
    if (SUCCEEDED(filter->GetValueAsObject(propertyId, IID_IAIMPString, reinterpret_cast<void**>(&value))) && value) {
        result = FromAimpString(value);
    }
    SafeRelease(value);
    if (!result.empty()) {
        return result;
    }

    VARIANT* variantValue = nullptr;
    if (SUCCEEDED(filter->GetValueAsVariant(propertyId, &variantValue)) && variantValue) {
        result = VariantToWString(*variantValue);
    }
    return RedactSensitiveUrl(result);
}

void CollectFieldFilterDescriptions(IAIMPMLDataFilterGroup* filter, std::vector<std::wstring>& descriptions) {
    if (!filter) {
        return;
    }
    for (int i = 0; i < filter->GetChildCount(); ++i) {
        IAIMPMLDataFilterGroup* group = nullptr;
        if (SUCCEEDED(filter->GetChild(i, IID_IAIMPMLDataFilterGroup, reinterpret_cast<void**>(&group))) && group) {
            CollectFieldFilterDescriptions(group, descriptions);
            group->Release();
            continue;
        }

        IAIMPMLDataFieldFilter* fieldFilter = nullptr;
        if (FAILED(filter->GetChild(i, IID_IAIMPMLDataFieldFilter, reinterpret_cast<void**>(&fieldFilter))) || !fieldFilter) {
            continue;
        }

        IAIMPMLDataField* field = nullptr;
        IAIMPString* fieldName = nullptr;
        std::wstring name = L"<unknown>";
        if (SUCCEEDED(fieldFilter->GetValueAsObject(AIMPML_FIELDFILTER_FIELD, IID_IAIMPMLDataField, reinterpret_cast<void**>(&field))) &&
            field &&
            SUCCEEDED(field->GetValueAsObject(AIMPML_FIELD_PROPID_NAME, IID_IAIMPString, reinterpret_cast<void**>(&fieldName))) &&
            fieldName) {
            name = FromAimpString(fieldName);
        }

        int operation = -1;
        fieldFilter->GetValueAsInt32(AIMPML_FIELDFILTER_OPERATION, &operation);
        std::wstring value1 = FieldFilterValueForLog(fieldFilter, AIMPML_FIELDFILTER_VALUE1);
        std::wstring value2 = FieldFilterValueForLog(fieldFilter, AIMPML_FIELDFILTER_VALUE2);

        std::wstring description = name + L" " + FilterOperationName(operation) + L" '" + value1 + L"'";
        if (!value2.empty()) {
            description += L" .. '" + value2 + L"'";
        }
        descriptions.push_back(std::move(description));

        SafeRelease(fieldName);
        SafeRelease(field);
        fieldFilter->Release();
    }
}

struct DataFilterDiagnostics {
    int limit{-1};
    int offset{-1};
    std::wstring searchString;
    std::wstring alphabeticIndex;
    std::vector<std::wstring> fieldFilters;
};

DataFilterDiagnostics ReadDataFilterDiagnostics(IAIMPMLDataFilter* filter) {
    DataFilterDiagnostics diagnostics;
    diagnostics.limit = ReadFilterIntProperty(filter, AIMPML_FILTER_LIMIT);
    diagnostics.offset = ReadFilterIntProperty(filter, AIMPML_FILTER_OFFSET);
    diagnostics.searchString = ReadFilterStringProperty(filter, AIMPML_FILTER_SEARCHSTRING);
    diagnostics.alphabeticIndex = ReadFilterStringProperty(filter, AIMPML_FILTER_ALPHABETICINDEX);
    CollectFieldFilterDescriptions(filter, diagnostics.fieldFilters);
    return diagnostics;
}

std::wstring IntOrUnsetForLog(int value) {
    return value >= 0 ? std::to_wstring(value) : L"<unset>";
}

std::wstring FormatDataFilterDiagnostics(const DataFilterDiagnostics& diagnostics) {
    std::wstring result = L"Filter{limit=" + IntOrUnsetForLog(diagnostics.limit) +
        L", offset=" + IntOrUnsetForLog(diagnostics.offset) +
        L", search='" + diagnostics.searchString +
        L"', alpha='" + diagnostics.alphabeticIndex +
        L"', fieldFilters=[" + JoinCsvForLog(diagnostics.fieldFilters) + L"]}";
    return result;
}

int TreeImageForRow(const std::wstring& kind, const std::wstring& node) {
    if (EqualsIgnoreCase(node, kTokenFavorites)) {
        return AIMPML_FIELDIMAGE_STAR;
    }
    if (EqualsIgnoreCase(kind, L"artist")) {
        return AIMPML_FIELDIMAGE_ARTIST;
    }
    if (EqualsIgnoreCase(kind, L"album")) {
        return AIMPML_FIELDIMAGE_DISK;
    }
    if (EqualsIgnoreCase(kind, L"song")) {
        return AIMPML_FIELDIMAGE_NOTE;
    }
    return AIMPML_FIELDIMAGE_FOLDER;
}

LongWord TreeFlagsForRow(const std::wstring& kind, const std::wstring& node) {
    LongWord flags = AIMPML_GROUPINGTREENODE_FLAG_STANDALONE;
    if (EqualsIgnoreCase(kind, L"artist") ||
        EqualsIgnoreCase(node, kTokenPlaylists) ||
        EqualsIgnoreCase(node, kTokenArtists) ||
        EqualsIgnoreCase(node, kTokenAlbums)) {
        flags |= AIMPML_GROUPINGTREENODE_FLAG_HASCHILDREN;
    }
    return flags;
}

std::wstring AlbumDisplayTitle(const AlbumInfo& album, bool includeArtist) {
    std::wstring title;
    if (includeArtist && !album.artist.empty()) {
        title = SafeNodeCaption(album.artist, album.id) + L" - ";
    }
    title += SafeNodeCaption(album.name, album.id);
    if (album.year > 0) {
        title += L" (" + std::to_wstring(album.year) + L")";
    }
    return title;
}

} // namespace

struct SubsonicMusicLibraryStorage::LibraryRow {
    std::wstring id;
    std::wstring fileName;
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring albumArtist;
    std::wstring genre;
    std::wstring node;
    std::wstring parentNode;
    std::wstring kind;
    std::wstring songId;
    std::wstring albumId;
    std::wstring artistId;
    std::wstring playlistId;
    int durationSeconds{0};
    int year{0};
    int trackNumber{0};
    int rating{0};
    long long size{0};
};

class SubsonicLibrarySelection final : public IAIMPMLDataProviderSelection, public ComBase {
    using LibraryRow = SubsonicMusicLibraryStorage::LibraryRow;

public:
    SubsonicLibrarySelection(std::vector<std::wstring> fields, std::vector<SubsonicMusicLibraryStorage::LibraryRow> rows)
        : fields_(std::move(fields)), rows_(std::move(rows)) {
    }

    HRESULT WINAPI QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) {
            return E_POINTER;
        }
        *ppvObject = nullptr;
        if (riid == IID_IUnknown || riid == IID_IAIMPMLDataProviderSelection) {
            *ppvObject = static_cast<IAIMPMLDataProviderSelection*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG WINAPI AddRef() override { return AddRefImpl(); }
    ULONG WINAPI Release() override { return ReleaseImpl(); }

    DOUBLE WINAPI GetValueAsFloat(int FieldIndex) override {
        const auto* row = CurrentRow();
        if (!row) {
            return 0.0;
        }
        const auto field = FieldName(FieldIndex);
        if (EqualsIgnoreCase(field, AIMPML_RESERVED_FIELD_DURATION)) {
            return static_cast<DOUBLE>(row->durationSeconds);
        }
        return 0.0;
    }

    int WINAPI GetValueAsInt32(int FieldIndex) override {
        const auto* row = CurrentRow();
        if (!row) {
            return 0;
        }
        const auto field = FieldName(FieldIndex);
        if (EqualsIgnoreCase(field, kFieldYear)) {
            return row->year;
        }
        if (EqualsIgnoreCase(field, kFieldTrackNumber)) {
            return row->trackNumber;
        }
        if (EqualsIgnoreCase(field, kFieldRating) || EqualsIgnoreCase(field, kFieldUserMark) ||
            EqualsIgnoreCase(field, AIMPML_RESERVED_FIELD_USERMARK)) {
            return row->rating;
        }
        return 0;
    }

    INT64 WINAPI GetValueAsInt64(int FieldIndex) override {
        const auto* row = CurrentRow();
        if (!row) {
            return 0;
        }
        const auto field = FieldName(FieldIndex);
        if (EqualsIgnoreCase(field, AIMPML_RESERVED_FIELD_FILESIZE)) {
            return row->size;
        }
        return 0;
    }

    TChar* WINAPI GetValueAsString(int FieldIndex, int* Length) override {
        const auto* row = CurrentRow();
        buffer_.clear();
        if (row) {
            buffer_ = ValueAsString(*row, FieldName(FieldIndex));
        }
        if (Length) {
            *Length = static_cast<int>(buffer_.size());
        }
        return buffer_.empty() ? const_cast<TChar*>(L"") : buffer_.data();
    }

    BOOL WINAPI NextRow() override {
        if (rows_.empty() || rowIndex_ + 1 >= rows_.size()) {
            return FALSE;
        }
        ++rowIndex_;
        return TRUE;
    }

private:
    const LibraryRow* CurrentRow() const {
        if (rows_.empty() || rowIndex_ >= rows_.size()) {
            return nullptr;
        }
        return &rows_[rowIndex_];
    }

    std::wstring FieldName(int index) const {
        if (index < 0 || static_cast<size_t>(index) >= fields_.size()) {
            return {};
        }
        return fields_[index];
    }

    std::wstring ValueAsString(const LibraryRow& row, const std::wstring& field) const {
        if (EqualsIgnoreCase(field, AIMPML_RESERVED_FIELD_ID)) {
            return row.id;
        }
        if (EqualsIgnoreCase(field, AIMPML_RESERVED_FIELD_FILENAME)) {
            return row.fileName;
        }
        if (EqualsIgnoreCase(field, AIMPML_RESERVED_FIELD_DURATION)) {
            return IntToStringOrEmpty(row.durationSeconds);
        }
        if (EqualsIgnoreCase(field, AIMPML_RESERVED_FIELD_FILESIZE)) {
            return row.size > 0 ? std::to_wstring(row.size) : std::wstring();
        }
        if (EqualsIgnoreCase(field, kFieldTitle)) {
            return row.title;
        }
        if (EqualsIgnoreCase(field, kFieldArtist)) {
            return row.artist;
        }
        if (EqualsIgnoreCase(field, kFieldAlbum)) {
            return row.album;
        }
        if (EqualsIgnoreCase(field, kFieldAlbumArtist)) {
            return row.albumArtist;
        }
        if (EqualsIgnoreCase(field, kFieldGenre)) {
            return row.genre;
        }
        if (EqualsIgnoreCase(field, kFieldYear)) {
            return IntToStringOrEmpty(row.year);
        }
        if (EqualsIgnoreCase(field, kFieldTrackNumber)) {
            return IntToStringOrEmpty(row.trackNumber);
        }
        if (EqualsIgnoreCase(field, kFieldRating) || EqualsIgnoreCase(field, kFieldUserMark)) {
            return IntToStringOrEmpty(row.rating);
        }
        if (EqualsIgnoreCase(field, kFieldNode)) {
            return row.node;
        }
        if (EqualsIgnoreCase(field, kFieldParentNode)) {
            return row.parentNode;
        }
        if (EqualsIgnoreCase(field, kFieldKind)) {
            return row.kind;
        }
        if (EqualsIgnoreCase(field, kFieldSongId)) {
            return row.songId;
        }
        if (EqualsIgnoreCase(field, kFieldAlbumId)) {
            return row.albumId;
        }
        if (EqualsIgnoreCase(field, kFieldArtistId)) {
            return row.artistId;
        }
        if (EqualsIgnoreCase(field, kFieldPlaylistId)) {
            return row.playlistId;
        }
        return {};
    }

    std::vector<std::wstring> fields_;
    std::vector<LibraryRow> rows_;
    size_t rowIndex_{0};
    std::wstring buffer_;
};

class SubsonicGroupingTreeSelectionResult final : public IAIMPMLGroupingTreeDataProviderSelection, public ComBase {
    using LibraryRow = SubsonicMusicLibraryStorage::LibraryRow;

public:
    SubsonicGroupingTreeSelectionResult(IAIMPCore* core, std::vector<LibraryRow> rows)
        : core_(core), rows_(std::move(rows)) {
    }

    HRESULT WINAPI QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) {
            return E_POINTER;
        }
        *ppvObject = nullptr;
        if (riid == IID_IUnknown || riid == IID_IAIMPMLGroupingTreeDataProviderSelection) {
            *ppvObject = static_cast<IAIMPMLGroupingTreeDataProviderSelection*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG WINAPI AddRef() override { return AddRefImpl(); }
    ULONG WINAPI Release() override { return ReleaseImpl(); }

    HRESULT WINAPI GetDisplayValue(IAIMPString** S) override {
        if (!S) {
            return E_POINTER;
        }
        const auto* row = CurrentRow();
        *S = MakeAimpString(core_.get(), row ? row->title : std::wstring());
        return *S ? S_OK : E_FAIL;
    }

    LongWord WINAPI GetFlags() override {
        const auto* row = CurrentRow();
        return row ? TreeFlagsForRow(row->kind, row->node) : 0;
    }

    HRESULT WINAPI GetImageIndex(int* Index) override {
        if (!Index) {
            return E_POINTER;
        }
        const auto* row = CurrentRow();
        *Index = row ? TreeImageForRow(row->kind, row->node) : AIMPML_FIELDIMAGE_FOLDER;
        return S_OK;
    }

    HRESULT WINAPI GetValue(IAIMPString** FieldName, VARIANT* Value) override {
        if (!FieldName || !Value) {
            return E_POINTER;
        }
        VariantInit(Value);
        const auto* row = CurrentRow();
        *FieldName = MakeAimpString(core_.get(), kFieldParentNode);
        if (!*FieldName) {
            return E_FAIL;
        }
        Value->vt = VT_BSTR;
        Value->bstrVal = SysAllocString((row ? row->node : std::wstring()).c_str());
        return Value->bstrVal ? S_OK : E_OUTOFMEMORY;
    }

    BOOL WINAPI NextRow() override {
        if (rows_.empty() || rowIndex_ + 1 >= rows_.size()) {
            return FALSE;
        }
        ++rowIndex_;
        return TRUE;
    }

private:
    const LibraryRow* CurrentRow() const {
        if (rows_.empty() || rowIndex_ >= rows_.size()) {
            return nullptr;
        }
        return &rows_[rowIndex_];
    }

    AimpCoreRef core_;
    std::vector<LibraryRow> rows_;
    size_t rowIndex_{0};
};

SubsonicMusicLibraryStorage::SubsonicMusicLibraryStorage(IAIMPCore* core, SubsonicRepository* repository)
    : core_(core), repository_(repository) {
}

SubsonicMusicLibraryStorage::~SubsonicMusicLibraryStorage() {
    Finalize();
}

void SubsonicMusicLibraryStorage::SetServices(SubsonicRepository* repository) {
    std::lock_guard lock(indexMutex_);
    repository_ = repository;
    trackIndex_.clear();
    playlistIndex_.clear();
    artistIndex_.clear();
    albumIndex_.clear();
    artistNodeIndex_.clear();
    albumNodeIndex_.clear();
    playlistNodeIndex_.clear();
    LogInfo(L"Subsonic Music Library services updated and local index cleared.");
}

void SubsonicMusicLibraryStorage::NotifyChanged() {
    IAIMPMLDataStorageManager* manager = manager_;
    if (!manager) {
        LogInfo(L"Subsonic Music Library manager is unavailable; changed notification skipped.");
        return;
    }
    manager->AddRef();
    manager->Changed();
    manager->Release();
    LogInfo(L"Subsonic Music Library changed notification sent.");
}

HRESULT WINAPI SubsonicMusicLibraryStorage::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) {
        return E_POINTER;
    }
    *ppvObject = nullptr;
    if (riid == IID_IUnknown || riid == IID_IAIMPPropertyList || riid == IID_IAIMPMLExtensionDataStorage) {
        *ppvObject = static_cast<IAIMPMLExtensionDataStorage*>(this);
    } else if (riid == IID_IAIMPMLDataProvider) {
        *ppvObject = static_cast<IAIMPMLDataProvider*>(this);
    } else if (riid == IID_IAIMPMLGroupingTreeDataProvider || riid == IID_IAIMPMLGroupingTreeDataProvider2) {
        *ppvObject = static_cast<IAIMPMLGroupingTreeDataProvider2*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

HRESULT WINAPI SubsonicMusicLibraryStorage::GetValueAsFloat(int, double*) {
    return E_INVALIDARG;
}

HRESULT WINAPI SubsonicMusicLibraryStorage::GetValueAsInt32(int PropertyID, int* Value) {
    if (!Value) {
        return E_POINTER;
    }
    if (PropertyID == AIMPML_DATASTORAGE_PROPID_CAPABILITIES) {
        *Value = AIMPML_DATASTORAGE_CAP_FILTERING |
            AIMPML_DATASTORAGE_CAP_GROUPINGPRESETS |
            AIMPML_DATASTORAGE_CAP_NOBOOKMARKS;
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT WINAPI SubsonicMusicLibraryStorage::GetValueAsInt64(int, INT64*) {
    return E_INVALIDARG;
}

HRESULT WINAPI SubsonicMusicLibraryStorage::GetValueAsObject(int PropertyID, REFIID IID, void** Value) {
    if (!Value) {
        return E_POINTER;
    }
    *Value = nullptr;
    if (IID != IID_IAIMPString) {
        return E_INVALIDARG;
    }
    if (PropertyID == AIMPML_DATASTORAGE_PROPID_ID) {
        *Value = MakeAimpString(core_.get(), kStorageId);
        return *Value ? S_OK : E_FAIL;
    }
    if (PropertyID == AIMPML_DATASTORAGE_PROPID_CAPTION) {
        *Value = MakeAimpString(core_.get(), kStorageCaption);
        return *Value ? S_OK : E_FAIL;
    }
    if (PropertyID == AIMPML_DATASTORAGE_PROPID_GROUPINGPRESET) {
        *Value = MakeAimpString(core_.get(), kGroupingPresetId);
        return *Value ? S_OK : E_FAIL;
    }
    return E_INVALIDARG;
}

HRESULT WINAPI SubsonicMusicLibraryStorage::SetValueAsFloat(int, const double) {
    return E_NOTIMPL;
}

HRESULT WINAPI SubsonicMusicLibraryStorage::SetValueAsInt32(int, int) {
    return E_NOTIMPL;
}

HRESULT WINAPI SubsonicMusicLibraryStorage::SetValueAsInt64(int, const INT64) {
    return E_NOTIMPL;
}

HRESULT WINAPI SubsonicMusicLibraryStorage::SetValueAsObject(int, IUnknown*) {
    return E_NOTIMPL;
}

void WINAPI SubsonicMusicLibraryStorage::Finalize() {
    SafeRelease(manager_);
}

void WINAPI SubsonicMusicLibraryStorage::Initialize(IAIMPMLDataStorageManager* Manager) {
    if (manager_ == Manager) {
        return;
    }
    SafeRelease(manager_);
    manager_ = Manager;
    if (manager_) {
        manager_->AddRef();
    }
    LogInfo(L"Subsonic Music Library storage initialized.");
}

HRESULT WINAPI SubsonicMusicLibraryStorage::ConfigLoad(IAIMPConfig*, IAIMPString*) {
    return S_OK;
}

HRESULT WINAPI SubsonicMusicLibraryStorage::ConfigSave(IAIMPConfig*, IAIMPString*) {
    return S_OK;
}

HRESULT WINAPI SubsonicMusicLibraryStorage::GetFields(int Schema, IAIMPObjectList** List) {
    if (!List) {
        return E_POINTER;
    }
    *List = nullptr;
    IAIMPObjectList* list = nullptr;
    HRESULT hr = core_.get()->CreateObject(IID_IAIMPObjectList, reinterpret_cast<void**>(&list));
    if (FAILED(hr)) {
        return hr;
    }

    if (Schema == AIMPML_FIELDS_SCHEMA_ALL) {
        struct FieldDef {
            const wchar_t* name;
            int type;
            int flags;
        };
        const FieldDef fields[] = {
            { AIMPML_RESERVED_FIELD_ID, AIMPML_FIELDTYPE_STRING, AIMPML_FIELDFLAG_INTERNAL | AIMPML_FIELDFLAG_REQUIRED },
            { AIMPML_RESERVED_FIELD_FILENAME, AIMPML_FIELDTYPE_FILENAME, AIMPML_FIELDFLAG_REQUIRED },
            { AIMPML_RESERVED_FIELD_FILESIZE, AIMPML_FIELDTYPE_FILESIZE, 0 },
            { AIMPML_RESERVED_FIELD_DURATION, AIMPML_FIELDTYPE_DURATION, 0 },
            { kFieldTitle, AIMPML_FIELDTYPE_STRING, AIMPML_FIELDFLAG_FILTERING },
            { kFieldArtist, AIMPML_FIELDTYPE_STRING, AIMPML_FIELDFLAG_FILTERING },
            { kFieldAlbum, AIMPML_FIELDTYPE_STRING, AIMPML_FIELDFLAG_FILTERING },
            { kFieldAlbumArtist, AIMPML_FIELDTYPE_STRING, AIMPML_FIELDFLAG_FILTERING },
            { kFieldGenre, AIMPML_FIELDTYPE_STRING, AIMPML_FIELDFLAG_FILTERING },
            { kFieldYear, AIMPML_FIELDTYPE_INT32, AIMPML_FIELDFLAG_FILTERING },
            { kFieldTrackNumber, AIMPML_FIELDTYPE_INT32, 0 },
            { kFieldRating, AIMPML_FIELDTYPE_INT32, 0 },
            { kFieldUserMark, AIMPML_FIELDTYPE_INT32, 0 },
            { kFieldSongId, AIMPML_FIELDTYPE_STRING, AIMPML_FIELDFLAG_INTERNAL | AIMPML_FIELDFLAG_FILTERING },
            { kFieldAlbumId, AIMPML_FIELDTYPE_STRING, AIMPML_FIELDFLAG_INTERNAL | AIMPML_FIELDFLAG_FILTERING },
            { kFieldArtistId, AIMPML_FIELDTYPE_STRING, AIMPML_FIELDFLAG_INTERNAL | AIMPML_FIELDFLAG_FILTERING },
            { kFieldPlaylistId, AIMPML_FIELDTYPE_STRING, AIMPML_FIELDFLAG_INTERNAL | AIMPML_FIELDFLAG_FILTERING },
            { kFieldKind, AIMPML_FIELDTYPE_STRING, AIMPML_FIELDFLAG_INTERNAL | AIMPML_FIELDFLAG_FILTERING },
            { kFieldNode, AIMPML_FIELDTYPE_FILENAME, AIMPML_FIELDFLAG_INTERNAL | AIMPML_FIELDFLAG_GROUPING | AIMPML_FIELDFLAG_FILTERING },
            { kFieldParentNode, AIMPML_FIELDTYPE_STRING, AIMPML_FIELDFLAG_INTERNAL | AIMPML_FIELDFLAG_GROUPING | AIMPML_FIELDFLAG_FILTERING },
        };
        for (const auto& field : fields) {
            IAIMPMLDataField* dataField = CreateField(field.name, field.type, field.flags);
            if (dataField) {
                list->Add(dataField);
                dataField->Release();
            }
        }
    } else if (Schema == AIMPML_FIELDS_SCHEMA_TABLE_VIEW_DEFAULT ||
        Schema == AIMPML_FIELDS_SCHEMA_TABLE_VIEW_GROUPDETAILS ||
        Schema == AIMPML_FIELDS_SCHEMA_TABLE_VIEW_ALBUMTHUMBNAILS) {
        AddFieldName(list, kFieldTitle);
        AddFieldName(list, kFieldArtist);
        AddFieldName(list, kFieldAlbum);
        AddFieldName(list, AIMPML_RESERVED_FIELD_DURATION);
        AddFieldName(list, kFieldGenre);
        AddFieldName(list, kFieldYear);
    } else if (Schema == AIMPML_FIELDS_SCHEMA_TABLE_GROUPBY ||
        Schema == AIMPML_FIELDS_SCHEMA_TABLE_GROUPDETAILS) {
        AddFieldName(list, kFieldArtist);
        AddFieldName(list, kFieldAlbum);
    }

    *List = list;
    return S_OK;
}

HRESULT WINAPI SubsonicMusicLibraryStorage::GetGroupingPresets(int Schema, IAIMPMLGroupingPresets* Presets) {
    if (!Presets) {
        return E_POINTER;
    }
    if (Schema != AIMPML_GROUPINGPRESETS_SCHEMA_BUILTIN && Schema != AIMPML_GROUPINGPRESETS_SCHEMA_DEFAULT) {
        return S_OK;
    }

    IAIMPString* id = MakeAimpString(core_.get(), kGroupingPresetId);
    IAIMPString* name = MakeAimpString(core_.get(), L"Subsonic");
    if (!id || !name) {
        SafeRelease(id);
        SafeRelease(name);
        return E_FAIL;
    }

    IAIMPString* fieldName = MakeAimpString(core_.get(), kFieldNode);
    if (!fieldName) {
        id->Release();
        name->Release();
        return E_FAIL;
    }

    IAIMPMLGroupingPresetStandard* preset = nullptr;
    const HRESULT hr = Presets->Add3(id, name, 0, fieldName, &preset);
    SafeRelease(preset);
    fieldName->Release();
    id->Release();
    name->Release();
    return hr;
}

void WINAPI SubsonicMusicLibraryStorage::FlushCache(int) {
    std::lock_guard lock(indexMutex_);
    trackIndex_.clear();
    playlistIndex_.clear();
    artistIndex_.clear();
    albumIndex_.clear();
    artistNodeIndex_.clear();
    albumNodeIndex_.clear();
    playlistNodeIndex_.clear();
    LogInfo(L"Subsonic Music Library in-memory index flushed.");
}

HRESULT WINAPI SubsonicMusicLibraryStorage::GetData(IAIMPObjectList* Fields, IAIMPMLDataFilter* Filter, IUnknown** Data) {
    if (!Fields || !Data) {
        return E_POINTER;
    }
    *Data = nullptr;

    const std::vector<std::wstring> fieldNames = ReadFieldNames(Fields);
    const std::wstring node = ExtractNodeFilter(Filter);
    const auto filterDiagnostics = ReadDataFilterDiagnostics(Filter);
    const bool debugLogging = IsDebugLoggingEnabled();
    std::vector<LibraryRow> rows;
    if (fieldNames.size() == 1 && EqualsIgnoreCase(fieldNames.front(), kFieldNode)) {
        rows = BuildGroupingRows(node);
    } else {
        rows = BuildTableRows(node, filterDiagnostics.searchString);
    }

    if (debugLogging) {
        const auto nodeRef = ResolveNodeForDiagnostics(node);
        LogInfo(L"MusicLibrary GetData. Node='" + node +
            L"', " + FormatSubsonicLibraryNodeRef(nodeRef) +
            L", " + FormatDataFilterDiagnostics(filterDiagnostics) +
            L", Fields=" + std::to_wstring(fieldNames.size()) +
            L" [" + JoinCsvForLog(fieldNames) + L"]" +
            L", Rows=" + std::to_wstring(rows.size()));
        LogInfo(L"MusicLibrary paging probe. Node='" + node +
            L"', Limit=" + IntOrUnsetForLog(filterDiagnostics.limit) +
            L", Offset=" + IntOrUnsetForLog(filterDiagnostics.offset) +
            L", Search='" + filterDiagnostics.searchString +
            L"', RowsReturned=" + std::to_wstring(rows.size()));
    }

    auto* selection = new SubsonicLibrarySelection(fieldNames, std::move(rows));
    *Data = static_cast<IAIMPMLDataProviderSelection*>(selection);
    return S_OK;
}

HRESULT WINAPI SubsonicMusicLibraryStorage::AppendFilter(IAIMPMLDataFilterGroup* Filter, IAIMPMLGroupingTreeSelection* Selection) {
    if (!Filter) {
        return E_POINTER;
    }
    const std::wstring node = ExtractTreeSelectionNode(Selection);
    if (node.empty()) {
        LogInfo(L"MusicLibrary AppendFilter skipped: selected node is empty.");
        return S_OK;
    }

    IAIMPMLDataField* field = CreateField(kFieldParentNode, AIMPML_FIELDTYPE_STRING,
        AIMPML_FIELDFLAG_INTERNAL | AIMPML_FIELDFLAG_GROUPING | AIMPML_FIELDFLAG_FILTERING);
    if (!field) {
        LogInfo(L"MusicLibrary AppendFilter failed: could not create SubsonicParentNode field.");
        return E_FAIL;
    }

    VARIANT value{};
    VariantInit(&value);
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(node.c_str());
    IAIMPMLDataFieldFilter* addedFilter = nullptr;
    const HRESULT hr = value.bstrVal
        ? Filter->Add(field, &value, nullptr, AIMPML_FIELDFILTER_OPERATION_EQUALS, &addedFilter)
        : E_OUTOFMEMORY;
    if (IsDebugLoggingEnabled()) {
        LogInfo(L"MusicLibrary AppendFilter. Node='" + node +
            L"', " + FormatSubsonicLibraryNodeRef(ResolveNodeForDiagnostics(node)) +
            L", HRESULT=" + std::to_wstring(static_cast<long>(hr)));
    }
    SafeRelease(addedFilter);
    VariantClear(&value);
    field->Release();
    return hr;
}

LongWord WINAPI SubsonicMusicLibraryStorage::GetCapabilities() {
    return AIMPML_GROUPINGTREEDATAPROVIDER_CAP_HIDEALLDATA | AIMPML_GROUPINGTREEDATAPROVIDER_CAP_DONTSORT;
}

HRESULT WINAPI SubsonicMusicLibraryStorage::GetData(IAIMPMLGroupingTreeSelection* Selection, IAIMPMLGroupingTreeDataProviderSelection** Data) {
    if (!Data) {
        return E_POINTER;
    }
    *Data = nullptr;
    const std::wstring node = ExtractTreeSelectionNode(Selection);
    auto rows = BuildGroupingRows(node);
    if (IsDebugLoggingEnabled()) {
        LogInfo(L"MusicLibrary grouping tree GetData. Node='" + node +
            L"', " + FormatSubsonicLibraryNodeRef(ResolveNodeForDiagnostics(node)) +
            L", Rows=" + std::to_wstring(rows.size()));
    }
    auto* result = new SubsonicGroupingTreeSelectionResult(core_.get(), std::move(rows));
    *Data = static_cast<IAIMPMLGroupingTreeDataProviderSelection*>(result);
    return S_OK;
}

HRESULT WINAPI SubsonicMusicLibraryStorage::GetFieldForAlphabeticIndex(IAIMPString** FieldName) {
    if (!FieldName) {
        return E_POINTER;
    }
    *FieldName = MakeAimpString(core_.get(), kFieldTitle);
    return *FieldName ? S_OK : E_FAIL;
}

TChar WINAPI SubsonicMusicLibraryStorage::GetPathSeparator() {
    return L'/';
}

IAIMPMLDataField* SubsonicMusicLibraryStorage::CreateField(const std::wstring& name, int type, int flags) {
    IAIMPMLDataField* field = nullptr;
    if (FAILED(core_.get()->CreateObject(IID_IAIMPMLDataField, reinterpret_cast<void**>(&field)))) {
        return nullptr;
    }
    field->SetValueAsInt32(AIMPML_FIELD_PROPID_TYPE, type);
    field->SetValueAsInt32(AIMPML_FIELD_PROPID_FLAGS, flags);
    IAIMPString* nameObject = MakeAimpString(core_.get(), name);
    if (nameObject) {
        field->SetValueAsObject(AIMPML_FIELD_PROPID_NAME, nameObject);
        nameObject->Release();
    }
    return field;
}

HRESULT SubsonicMusicLibraryStorage::AddFieldName(IAIMPObjectList* list, const std::wstring& name) {
    IAIMPString* value = MakeAimpString(core_.get(), name);
    if (!value) {
        return E_FAIL;
    }
    const HRESULT hr = list->Add(value);
    value->Release();
    return hr;
}

std::vector<std::wstring> SubsonicMusicLibraryStorage::ReadFieldNames(IAIMPObjectList* fields) const {
    std::vector<std::wstring> names;
    if (!fields) {
        return names;
    }
    for (int i = 0; i < fields->GetCount(); ++i) {
        IAIMPString* value = nullptr;
        if (SUCCEEDED(fields->GetObject(i, IID_IAIMPString, reinterpret_cast<void**>(&value)))) {
            names.push_back(FromAimpString(value));
            value->Release();
        } else {
            names.emplace_back();
        }
    }
    return names;
}

void SubsonicMusicLibraryStorage::CollectNodeFilters(IAIMPMLDataFilterGroup* filter, std::vector<std::wstring>& nodes) const {
    if (!filter) {
        return;
    }
    for (int i = 0; i < filter->GetChildCount(); ++i) {
        IAIMPMLDataFilterGroup* group = nullptr;
        if (SUCCEEDED(filter->GetChild(i, IID_IAIMPMLDataFilterGroup, reinterpret_cast<void**>(&group)))) {
            CollectNodeFilters(group, nodes);
            group->Release();
            continue;
        }

        IAIMPMLDataFieldFilter* fieldFilter = nullptr;
        if (FAILED(filter->GetChild(i, IID_IAIMPMLDataFieldFilter, reinterpret_cast<void**>(&fieldFilter)))) {
            continue;
        }

        IAIMPMLDataField* field = nullptr;
        IAIMPString* fieldName = nullptr;
        IAIMPString* value = nullptr;
        int operation = -1;
        std::wstring result;

        if (SUCCEEDED(fieldFilter->GetValueAsObject(AIMPML_FIELDFILTER_FIELD, IID_IAIMPMLDataField, reinterpret_cast<void**>(&field))) &&
            SUCCEEDED(field->GetValueAsObject(AIMPML_FIELD_PROPID_NAME, IID_IAIMPString, reinterpret_cast<void**>(&fieldName)))) {
            const std::wstring filterFieldName = FromAimpString(fieldName);
            if ((EqualsIgnoreCase(filterFieldName, kFieldNode) || EqualsIgnoreCase(filterFieldName, kFieldParentNode)) &&
                SUCCEEDED(fieldFilter->GetValueAsInt32(AIMPML_FIELDFILTER_OPERATION, &operation)) &&
                (operation == AIMPML_FIELDFILTER_OPERATION_EQUALS || operation == AIMPML_FIELDFILTER_OPERATION_BEGINSWITH)) {
                if (SUCCEEDED(fieldFilter->GetValueAsObject(AIMPML_FIELDFILTER_VALUE1, IID_IAIMPString, reinterpret_cast<void**>(&value))) && value) {
                    result = FromAimpString(value);
                } else {
                    VARIANT* variantValue = nullptr;
                    if (SUCCEEDED(fieldFilter->GetValueAsVariant(AIMPML_FIELDFILTER_VALUE1, &variantValue)) && variantValue) {
                        result = VariantToWString(*variantValue);
                    }
                }
            }
        }

        SafeRelease(value);
        SafeRelease(fieldName);
        SafeRelease(field);
        fieldFilter->Release();

        if (!result.empty()) {
            nodes.push_back(result);
        }
    }
}

std::wstring SubsonicMusicLibraryStorage::ExtractNodeFilter(IAIMPMLDataFilterGroup* filter) const {
    std::vector<std::wstring> nodes;
    CollectNodeFilters(filter, nodes);
    const std::wstring node = BestNodeFromFilters(nodes);
    if (!nodes.empty() && IsDebugLoggingEnabled()) {
        LogInfo(L"MusicLibrary table filter nodes='" + JoinForLog(nodes) + L"', selected='" + node + L"'.");
    }
    return node;
}

std::wstring SubsonicMusicLibraryStorage::ExtractTreeSelectionNode(IAIMPMLGroupingTreeSelection* selection) const {
    if (!selection || selection->GetCount() <= 0) {
        return {};
    }

    std::vector<std::wstring> nodes;
    for (int i = 0; i < selection->GetCount(); ++i) {
        IAIMPString* fieldName = nullptr;
        VARIANT value{};
        VariantInit(&value);
        if (SUCCEEDED(selection->GetValue(i, &fieldName, &value)) &&
            fieldName) {
            const std::wstring selectionFieldName = FromAimpString(fieldName);
            if (!EqualsIgnoreCase(selectionFieldName, kFieldNode) &&
                !EqualsIgnoreCase(selectionFieldName, kFieldParentNode)) {
                SafeRelease(fieldName);
                VariantClear(&value);
                continue;
            }
            const std::wstring node = VariantToWString(value);
            if (!node.empty()) {
                nodes.push_back(node);
            }
        }
        SafeRelease(fieldName);
        VariantClear(&value);
    }
    const std::wstring selected = nodes.empty() ? std::wstring() : nodes.front();
    if (IsDebugLoggingEnabled()) {
        LogInfo(L"MusicLibrary tree selection nodes='" + JoinForLog(nodes) + L"', selected='" + selected + L"'.");
    }
    return selected;
}

std::vector<SubsonicMusicLibraryStorage::LibraryRow> SubsonicMusicLibraryStorage::BuildGroupingRows(const std::wstring& node) {
    std::vector<LibraryRow> rows;
    const std::wstring effectiveNode = NormalizeNodePath(node);
    if (effectiveNode.empty()) {
        rows.push_back({ ArtistsRootPath(), {}, kCaptionArtists, {}, {}, {}, {}, ArtistsRootPath(), {}, L"root" });
        rows.push_back({ AlbumsRootPath(), {}, kCaptionAlbums, {}, {}, {}, {}, AlbumsRootPath(), {}, L"root" });
        rows.push_back({ FavoritesRootPath(), {}, kCaptionFavorites, {}, {}, {}, {}, FavoritesRootPath(), {}, L"root" });
        rows.push_back({ TracksRootPath(), {}, kCaptionTracks, {}, {}, {}, {}, TracksRootPath(), {}, L"root" });
        rows.push_back({ PlaylistsRootPath(), {}, kCaptionPlaylists, {}, {}, {}, {}, PlaylistsRootPath(), {}, L"root" });
        return rows;
    }

    if (EqualsIgnoreCase(effectiveNode, kTokenPlaylists) || EqualsIgnoreCase(effectiveNode, PlaylistsRootPath())) {
        for (const auto& playlist : LoadPlaylists()) {
            const std::wstring nodeValue = ChildNodePath(PlaylistsRootPath(), playlist.name);
            LibraryRow row;
            row.id = nodeValue;
            row.title = playlist.name;
            row.node = nodeValue;
            row.kind = L"playlist";
            row.playlistId = playlist.id;
            row.durationSeconds = playlist.durationSeconds;
            rows.push_back(std::move(row));
            std::lock_guard lock(indexMutex_);
            playlistNodeIndex_[nodeValue] = playlist.id;
        }
    } else if (EqualsIgnoreCase(effectiveNode, kTokenArtists) || EqualsIgnoreCase(effectiveNode, ArtistsRootPath())) {
        for (const auto& artist : LoadArtists()) {
            const std::wstring nodeValue = ChildNodePath(ArtistsRootPath(), artist.name);
            LibraryRow row;
            row.id = nodeValue;
            row.title = artist.name;
            row.node = nodeValue;
            row.kind = L"artist";
            row.artistId = artist.id;
            rows.push_back(std::move(row));
            std::lock_guard lock(indexMutex_);
            artistNodeIndex_[nodeValue] = artist.id;
        }
    } else if (StartsWithIgnoreCase(effectiveNode, kTokenArtistPrefix) ||
        StartsWithIgnoreCase(effectiveNode, ArtistsRootPath())) {
        std::wstring artistId = StartsWithIgnoreCase(effectiveNode, kTokenArtistPrefix)
            ? StripPrefix(effectiveNode, kTokenArtistPrefix)
            : ResolveNodeId(artistNodeIndex_, effectiveNode);
        if (!artistId.empty()) {
            for (const auto& album : LoadArtistAlbums(artistId)) {
                const std::wstring nodeValue = ChildNodePath(effectiveNode, AlbumDisplayTitle(album, false));
                LibraryRow row;
                row.id = nodeValue;
                row.title = AlbumDisplayTitle(album, false);
                row.artist = album.artist;
                row.node = nodeValue;
                row.kind = L"album";
                row.albumId = album.id;
                row.artistId = album.artistId;
                row.durationSeconds = album.durationSeconds;
                row.year = album.year;
                rows.push_back(std::move(row));
                std::lock_guard lock(indexMutex_);
                albumNodeIndex_[nodeValue] = album.id;
            }
        }
    } else if (EqualsIgnoreCase(effectiveNode, kTokenAlbums) || EqualsIgnoreCase(effectiveNode, AlbumsRootPath())) {
        for (const auto& album : LoadAlbums()) {
            const std::wstring nodeValue = ChildNodePath(AlbumsRootPath(), AlbumDisplayTitle(album, true));
            LibraryRow row;
            row.id = nodeValue;
            row.title = AlbumDisplayTitle(album, true);
            row.artist = album.artist;
            row.node = nodeValue;
            row.kind = L"album";
            row.albumId = album.id;
            row.artistId = album.artistId;
            row.durationSeconds = album.durationSeconds;
            row.year = album.year;
            rows.push_back(std::move(row));
            std::lock_guard lock(indexMutex_);
            albumNodeIndex_[nodeValue] = album.id;
        }
    }
    return rows;
}

std::vector<SubsonicMusicLibraryStorage::LibraryRow> SubsonicMusicLibraryStorage::BuildTableRows(
    const std::wstring& node,
    const std::wstring& searchText) {
    std::vector<TrackInfo> tracks;
    std::wstring effectiveNode = NormalizeNodePath(node);

    if (!repository_) {
        LogInfo(L"MusicLibrary table request ignored: Subsonic client is unavailable.");
        return {};
    }
    if (effectiveNode.empty()) {
        return {};
    }

    const bool hasSearch = !SplitSearchTokens(searchText).empty();

    if (EqualsIgnoreCase(effectiveNode, kTokenFavorites) || EqualsIgnoreCase(effectiveNode, FavoritesRootPath())) {
        tracks = repository_->GetCachedStarredTracks();
        if (tracks.empty()) {
            tracks = repository_->GetStarredSongs();
        } else {
            LogInfo(L"MusicLibrary Favorites served from metadata cache. Tracks=" + std::to_wstring(tracks.size()));
        }
    } else if (EqualsIgnoreCase(effectiveNode, kTokenTracks) || EqualsIgnoreCase(effectiveNode, TracksRootPath())) {
        tracks = repository_->GetCachedTracks();
        const bool tracksFromCache = !tracks.empty();
        if (!tracks.empty()) {
            LogInfo(L"MusicLibrary Tracks served from metadata cache. Tracks=" + std::to_wstring(tracks.size()) +
                L", CachedTotal=" + std::to_wstring(repository_->CachedTrackCount()));
        } else {
            tracks = repository_->SearchAllSongs(L"");
        }
        if (!tracksFromCache && tracks.empty()) {
            auto scannedTracks = LoadTracksFromAlbumScan(0);
            if (scannedTracks.size() > tracks.size()) {
                LogInfo(L"MusicLibrary Tracks used album scan fallback. SearchRows=" +
                    std::to_wstring(tracks.size()) +
                    L", ScanRows=" + std::to_wstring(scannedTracks.size()));
                tracks = std::move(scannedTracks);
            }
        }
        if (hasSearch) {
            auto filteredTracks = FilterTracksBySearch(tracks, searchText);
            if (filteredTracks.empty()) {
                auto serverTracks = repository_->SearchAllSongs(searchText);
                if (!serverTracks.empty()) {
                    LogInfo(L"MusicLibrary Tracks search used server search3 fallback. Search='" + searchText +
                        L"', ServerRows=" + std::to_wstring(serverTracks.size()));
                    tracks = std::move(serverTracks);
                } else {
                    tracks = std::move(filteredTracks);
                }
            } else {
                tracks = std::move(filteredTracks);
            }
        }
    } else if (StartsWithIgnoreCase(effectiveNode, kTokenPlaylistPrefix) ||
        StartsWithIgnoreCase(effectiveNode, PlaylistsRootPath())) {
        const std::wstring playlistId = StartsWithIgnoreCase(effectiveNode, kTokenPlaylistPrefix)
            ? StripPrefix(effectiveNode, kTokenPlaylistPrefix)
            : ResolveNodeId(playlistNodeIndex_, effectiveNode);
        if (!playlistId.empty()) {
            tracks = repository_->GetCachedPlaylistTracks(playlistId);
            if (tracks.empty()) {
                tracks = repository_->GetPlaylistTracks(playlistId);
            } else {
                LogInfo(L"MusicLibrary playlist served from metadata cache. PlaylistId=" + playlistId +
                    L", Tracks=" + std::to_wstring(tracks.size()));
            }
        } else {
            LogInfo(L"MusicLibrary playlist node was not found in index: " + effectiveNode);
        }
    } else if (StartsWithIgnoreCase(effectiveNode, kTokenAlbumPrefix) ||
        StartsWithIgnoreCase(effectiveNode, AlbumsRootPath()) ||
        (StartsWithIgnoreCase(effectiveNode, ArtistsRootPath()) &&
            !ResolveNodeId(albumNodeIndex_, effectiveNode).empty())) {
        const std::wstring albumId = StartsWithIgnoreCase(effectiveNode, kTokenAlbumPrefix)
            ? StripPrefix(effectiveNode, kTokenAlbumPrefix)
            : ResolveNodeId(albumNodeIndex_, effectiveNode);
        if (!albumId.empty()) {
            tracks = repository_->GetCachedAlbumTracks(albumId);
            if (tracks.empty()) {
                tracks = repository_->GetAlbumTracks(albumId);
            } else {
                LogInfo(L"MusicLibrary album served from metadata cache. AlbumId=" + albumId +
                    L", Tracks=" + std::to_wstring(tracks.size()));
            }
        } else {
            LogInfo(L"MusicLibrary album node was not found in index: " + effectiveNode);
        }
    } else if (StartsWithIgnoreCase(effectiveNode, kTokenArtistPrefix) ||
        StartsWithIgnoreCase(effectiveNode, ArtistsRootPath())) {
        const std::wstring artistId = StartsWithIgnoreCase(effectiveNode, kTokenArtistPrefix)
            ? StripPrefix(effectiveNode, kTokenArtistPrefix)
            : ResolveNodeId(artistNodeIndex_, effectiveNode);
        tracks = repository_->GetCachedArtistTracks(artistId);
        if (!tracks.empty()) {
            LogInfo(L"MusicLibrary artist served from metadata cache. ArtistId=" + artistId +
                L", Tracks=" + std::to_wstring(tracks.size()));
        } else {
            for (const auto& album : LoadArtistAlbums(artistId)) {
                auto albumTracks = repository_->GetAlbumTracks(album.id);
                tracks.insert(tracks.end(), albumTracks.begin(), albumTracks.end());
            }
        }
    } else {
        return {};
    }

    if (hasSearch &&
        !EqualsIgnoreCase(effectiveNode, kTokenTracks) &&
        !EqualsIgnoreCase(effectiveNode, TracksRootPath())) {
        const size_t beforeFilter = tracks.size();
        tracks = FilterTracksBySearch(tracks, searchText);
        LogInfo(L"MusicLibrary local search filter applied. Node='" + effectiveNode +
            L"', Search='" + searchText +
            L"', Before=" + std::to_wstring(beforeFilter) +
            L", After=" + std::to_wstring(tracks.size()));
    } else if (hasSearch) {
        LogInfo(L"MusicLibrary tracks search filter applied. Search='" + searchText +
            L"', Rows=" + std::to_wstring(tracks.size()));
    }

    RememberTracks(tracks);

    std::vector<LibraryRow> rows;
    rows.reserve(tracks.size());
    for (const auto& track : tracks) {
        LibraryRow row;
        row.id = L"song:" + track.id;
        row.fileName = repository_->BuildStreamUrl(track.id);
        row.title = track.title.empty() ? track.id : track.title;
        row.artist = track.artist;
        row.album = track.album;
        row.albumArtist = track.albumArtist;
        row.genre = track.genre;
        row.node = effectiveNode;
        row.parentNode = effectiveNode;
        row.kind = L"song";
        row.songId = track.id;
        row.albumId = track.albumId;
        row.artistId = track.artistId;
        row.playlistId = track.playlistId;
        row.durationSeconds = track.durationSeconds;
        row.year = track.year;
        row.trackNumber = track.trackNumber;
        row.rating = track.rating;
        row.size = track.size;
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<TrackInfo> SubsonicMusicLibraryStorage::LoadTracksFromAlbumScan(int maxTracks) {
    std::vector<TrackInfo> tracks;
    if (!repository_) {
        return tracks;
    }

    const auto albums = repository_->GetAllAlbums(L"alphabeticalByArtist");
    for (const auto& album : albums) {
        auto albumTracks = repository_->GetAlbumTracks(album.id);
        for (auto& track : albumTracks) {
            tracks.push_back(std::move(track));
            if (maxTracks > 0 && static_cast<int>(tracks.size()) >= maxTracks) {
                LogInfo(L"MusicLibrary album scan reached target rows: " + std::to_wstring(tracks.size()));
                return tracks;
            }
        }
    }
    LogInfo(L"MusicLibrary album scan completed. Albums=" + std::to_wstring(albums.size()) +
        L", Tracks=" + std::to_wstring(tracks.size()));
    return tracks;
}

void SubsonicMusicLibraryStorage::RememberTracks(const std::vector<TrackInfo>& tracks) {
    {
        std::lock_guard lock(indexMutex_);
        for (const auto& track : tracks) {
            trackIndex_[track.id] = track;
        }
    }
    if (repository_) {
        repository_->RememberTracks(tracks);
    }
}

std::vector<PlaylistInfo> SubsonicMusicLibraryStorage::LoadPlaylists() {
    if (!repository_) {
        return {};
    }
    auto playlists = repository_->GetCachedPlaylists();
    if (playlists.empty()) {
        playlists = repository_->GetPlaylists();
    } else {
        LogInfo(L"MusicLibrary playlists served from metadata cache. Playlists=" + std::to_wstring(playlists.size()));
    }
    std::lock_guard lock(indexMutex_);
    playlistIndex_ = playlists;
    return playlists;
}

std::vector<ArtistInfo> SubsonicMusicLibraryStorage::LoadArtists() {
    if (!repository_) {
        return {};
    }
    auto artists = repository_->GetCachedArtists();
    if (artists.empty()) {
        artists = repository_->GetArtists();
    } else {
        LogInfo(L"MusicLibrary artists served from metadata cache. Artists=" + std::to_wstring(artists.size()));
    }
    std::sort(artists.begin(), artists.end(), [](const ArtistInfo& left, const ArtistInfo& right) {
        return _wcsicmp(left.name.c_str(), right.name.c_str()) < 0;
    });
    std::lock_guard lock(indexMutex_);
    artistIndex_ = artists;
    return artists;
}

std::vector<AlbumInfo> SubsonicMusicLibraryStorage::LoadArtistAlbums(const std::wstring& artistId) {
    if (!repository_ || artistId.empty()) {
        return {};
    }
    auto albums = repository_->GetCachedArtistAlbums(artistId);
    if (albums.empty()) {
        albums = repository_->GetArtistAlbums(artistId);
    } else {
        LogInfo(L"MusicLibrary artist albums served from metadata cache. ArtistId=" + artistId +
            L", Albums=" + std::to_wstring(albums.size()));
    }
    std::sort(albums.begin(), albums.end(), [](const AlbumInfo& left, const AlbumInfo& right) {
        if (left.year != right.year) {
            return left.year < right.year;
        }
        return _wcsicmp(left.name.c_str(), right.name.c_str()) < 0;
    });
    return albums;
}

std::vector<AlbumInfo> SubsonicMusicLibraryStorage::LoadAlbums() {
    if (!repository_) {
        return {};
    }
    auto albums = repository_->GetCachedAlbums();
    if (albums.empty()) {
        albums = repository_->GetAllAlbums(L"alphabeticalByArtist");
    } else {
        LogInfo(L"MusicLibrary albums served from metadata cache. Albums=" + std::to_wstring(albums.size()) +
            L", CachedTotal=" + std::to_wstring(repository_->CachedAlbumCount()));
    }
    std::lock_guard lock(indexMutex_);
    albumIndex_ = albums;
    return albums;
}

std::wstring SubsonicMusicLibraryStorage::ResolveNodeId(const std::map<std::wstring, std::wstring>& index, const std::wstring& node) const {
    std::lock_guard lock(indexMutex_);
    const auto it = index.find(node);
    return it != index.end() ? it->second : std::wstring();
}

SubsonicLibraryNodeRef SubsonicMusicLibraryStorage::ResolveNodeForDiagnostics(const std::wstring& node) const {
    SubsonicLibraryNodeRef result;
    result.path = NormalizeNodePath(node);
    if (result.path.empty()) {
        result.kind = SubsonicLibraryNodeKind::Root;
        return result;
    }

    if (EqualsIgnoreCase(result.path, kTokenPlaylists) || EqualsIgnoreCase(result.path, PlaylistsRootPath())) {
        result.kind = SubsonicLibraryNodeKind::PlaylistsRoot;
        return result;
    }
    if (EqualsIgnoreCase(result.path, kTokenArtists) || EqualsIgnoreCase(result.path, ArtistsRootPath())) {
        result.kind = SubsonicLibraryNodeKind::ArtistsRoot;
        return result;
    }
    if (EqualsIgnoreCase(result.path, kTokenAlbums) || EqualsIgnoreCase(result.path, AlbumsRootPath())) {
        result.kind = SubsonicLibraryNodeKind::AlbumsRoot;
        return result;
    }
    if (EqualsIgnoreCase(result.path, kTokenFavorites) || EqualsIgnoreCase(result.path, FavoritesRootPath())) {
        result.kind = SubsonicLibraryNodeKind::FavoritesRoot;
        return result;
    }
    if (EqualsIgnoreCase(result.path, kTokenTracks) || EqualsIgnoreCase(result.path, TracksRootPath())) {
        result.kind = SubsonicLibraryNodeKind::TracksRoot;
        return result;
    }

    if (StartsWithIgnoreCase(result.path, kTokenPlaylistPrefix)) {
        result.kind = SubsonicLibraryNodeKind::Playlist;
        result.id = StripPrefix(result.path, kTokenPlaylistPrefix);
        return result;
    }
    if (StartsWithIgnoreCase(result.path, kTokenArtistPrefix)) {
        result.kind = SubsonicLibraryNodeKind::Artist;
        result.id = StripPrefix(result.path, kTokenArtistPrefix);
        return result;
    }
    if (StartsWithIgnoreCase(result.path, kTokenAlbumPrefix)) {
        result.kind = SubsonicLibraryNodeKind::Album;
        result.id = StripPrefix(result.path, kTokenAlbumPrefix);
        return result;
    }

    if (StartsWithIgnoreCase(result.path, PlaylistsRootPath())) {
        result.kind = SubsonicLibraryNodeKind::Playlist;
        result.id = ResolveNodeId(playlistNodeIndex_, result.path);
        return result;
    }
    if (StartsWithIgnoreCase(result.path, AlbumsRootPath())) {
        result.kind = SubsonicLibraryNodeKind::Album;
        result.id = ResolveNodeId(albumNodeIndex_, result.path);
        return result;
    }
    if (StartsWithIgnoreCase(result.path, ArtistsRootPath())) {
        result.id = ResolveNodeId(albumNodeIndex_, result.path);
        if (!result.id.empty()) {
            result.kind = SubsonicLibraryNodeKind::Album;
            return result;
        }
        result.kind = SubsonicLibraryNodeKind::Artist;
        result.id = ResolveNodeId(artistNodeIndex_, result.path);
        return result;
    }

    return result;
}
