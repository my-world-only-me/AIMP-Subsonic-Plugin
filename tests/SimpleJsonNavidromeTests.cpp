#include <iostream>
#include <string>

#include "SimpleJson.h"

// Dry parser tests using Navidrome/OpenSubsonic shaped responses. Navidrome
// repeats keys per index group and nests objects/arrays (genres, artists,
// replayGain, releaseDate, ...) inside entries, which the parser must handle.

namespace {

bool g_ok = true;

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        g_ok = false;
    }
}

void TestArtistsAcrossIndexGroups() {
    const std::string json = R"({"subsonic-response":{"status":"ok","version":"1.16.1",
        "type":"navidrome","serverVersion":"0.55.2","openSubsonic":true,
        "artists":{"index":[
            {"name":"A","artist":[
                {"id":"a1","name":"Aerosmith","coverArt":"ar-a1","albumCount":3,
                 "artistImageUrl":"https://x/ar-a1","roles":["artist","albumartist"]},
                {"id":"a2","name":"Air","coverArt":"ar-a2","albumCount":1}
            ]},
            {"name":"B","artist":[
                {"id":"b1","name":"Beck","coverArt":"ar-b1","albumCount":2}
            ]},
            {"name":"C","artist":[
                {"id":"c1","name":"Can","coverArt":"ar-c1","albumCount":4}
            ]}
        ],"lastModified":1750000000000,"ignoredArticles":"The El La Los"}}})";

    const auto artists = ParseArtists(json);
    Expect(artists.size() == 4, "getArtists must collect artists from every index group");
    if (artists.size() == 4) {
        Expect(artists[0].id == L"a1" && artists[0].albumCount == 3, "first artist parsed");
        Expect(artists[2].id == L"b1", "artist from second index group parsed");
        Expect(artists[3].id == L"c1", "artist from third index group parsed");
    }
}

void TestSingleSongWithNestedOpenSubsonicFields() {
    const std::string json = R"({"subsonic-response":{"status":"ok",
        "song":{"id":"s1","parent":"al1","isDir":false,"title":"Tarkan Song",
            "album":"Karma","artist":"Tarkan","track":5,"year":2001,"genre":"Pop",
            "coverArt":"mf-s1","size":9016839,"contentType":"audio/flac","suffix":"flac",
            "duration":251,"bitRate":287,"path":"Tarkan/Karma/05.flac",
            "discNumber":1,"created":"2024-01-01T00:00:00Z","albumId":"al1","artistId":"a9",
            "type":"music","userRating":4,"starred":"2024-05-01T10:00:00Z",
            "bpm":0,"comment":"","sortName":"tarkan song","mediaType":"song",
            "musicBrainzId":"11111111-2222-3333-4444-555555555555",
            "genres":[{"name":"Pop"},{"name":"World"}],
            "replayGain":{"trackGain":-6.62,"trackPeak":1,"albumGain":-7.1,"albumPeak":1},
            "channelCount":2,"samplingRate":44100,
            "artists":[{"id":"a9","name":"Tarkan"}],
            "displayArtist":"Tarkan",
            "albumArtists":[{"id":"a9","name":"Tarkan"}],
            "displayAlbumArtist":"Tarkan",
            "contributors":[{"role":"composer","artist":{"id":"zz","name":"Nested Composer"}}],
            "displayComposer":"Nested Composer","moods":[],
            "explicitStatus":""}}})";

    const auto tracks = ParseTrackArray(json, "song");
    Expect(tracks.size() == 1, "getSong with nested arrays must yield exactly one track");
    if (tracks.size() == 1) {
        const auto& track = tracks.front();
        Expect(track.id == L"s1", "song id read from the top level, not a nested object");
        Expect(track.title == L"Tarkan Song", "song title parsed");
        Expect(track.artist == L"Tarkan", "song artist read from the legacy string field");
        Expect(track.artistId == L"a9", "song artistId parsed");
        Expect(track.albumId == L"al1", "song albumId parsed");
        Expect(track.year == 2001, "song year parsed");
        Expect(track.durationSeconds == 251, "song duration parsed");
        Expect(track.suffix == L"flac", "song suffix parsed");
        Expect(track.rating == 4, "song userRating parsed");
        Expect(track.starred, "song starred timestamp detected");
    }
}

void TestAlbumListWithNestedReleaseDates() {
    const std::string json = R"({"subsonic-response":{"status":"ok",
        "albumList2":{"album":[
            {"id":"al1","parent":"a1","isDir":true,"title":"Pump","name":"Pump","album":"Pump",
             "artist":"Aerosmith","year":1989,"genre":"Rock","coverArt":"al-al1",
             "duration":2887,"created":"2024-01-01T00:00:00Z","artistId":"a1","songCount":10,
             "isVideo":false,"bpm":0,"mediaType":"album",
             "musicBrainzId":"","genres":[{"name":"Rock"}],
             "replayGain":{},"channelCount":0,"samplingRate":0,
             "artists":[{"id":"a1","name":"Aerosmith"}],"displayArtist":"Aerosmith",
             "explicitStatus":"","version":"Remastered",
             "originalReleaseDate":{"year":1989,"month":9,"day":12},
             "releaseDate":{"year":2019,"month":8,"day":23},
             "isCompilation":false,"sortName":"pump",
             "discTitles":[{"disc":1,"title":""}]},
            {"id":"al2","name":"Sea Change","artist":"Beck","artistId":"b1","coverArt":"al-al2",
             "songCount":12,"duration":3120,"year":2002,"genre":"Folk",
             "releaseDate":{"year":2002,"month":9,"day":24}}
        ]}}})";

    const auto albums = ParseAlbums(json, "album");
    Expect(albums.size() == 2, "albumList2 albums parsed");
    if (albums.size() == 2) {
        Expect(albums[0].id == L"al1", "album id parsed");
        Expect(albums[0].name == L"Pump", "album name parsed");
        Expect(albums[0].year == 1989, "album year must come from the top-level field, not releaseDate");
        Expect(albums[0].artist == L"Aerosmith", "album artist parsed");
        Expect(albums[0].songCount == 10, "album songCount parsed");
        Expect(albums[1].year == 2002, "second album year parsed");
    }
}

void TestArtistAlbums() {
    const std::string json = R"({"subsonic-response":{"status":"ok",
        "artist":{"id":"a1","name":"Aerosmith","coverArt":"ar-a1","albumCount":2,
            "artistImageUrl":"https://x/a1",
            "album":[
                {"id":"al1","name":"Pump","artist":"Aerosmith","artistId":"a1",
                 "coverArt":"al-al1","songCount":10,"duration":2887,"year":1989,"genre":"Rock",
                 "genres":[{"name":"Rock"}],"artists":[{"id":"a1","name":"Aerosmith"}]},
                {"id":"al3","name":"Get a Grip","artist":"Aerosmith","artistId":"a1",
                 "coverArt":"al-al3","songCount":14,"duration":3700,"year":1993,"genre":"Rock"}
            ]}}})";

    const auto albums = ParseAlbums(json, "album");
    Expect(albums.size() == 2, "getArtist albums parsed");
    if (albums.size() == 2) {
        Expect(albums[0].id == L"al1" && albums[1].id == L"al3", "getArtist album ids parsed");
    }
}

void TestStarred2() {
    const std::string json = R"({"subsonic-response":{"status":"ok",
        "starred2":{
            "artist":[{"id":"a1","name":"Aerosmith"}],
            "album":[{"id":"al1","name":"Pump","artist":"Aerosmith","artistId":"a1"}],
            "song":[
                {"id":"s1","title":"Janie","album":"Pump","artist":"Aerosmith",
                 "albumId":"al1","artistId":"a1","duration":250,"suffix":"mp3",
                 "starred":"2024-05-01T10:00:00Z","genres":[{"name":"Rock"}]},
                {"id":"s2","title":"Loser","album":"Mellow Gold","artist":"Beck",
                 "albumId":"al9","artistId":"b1","duration":234,"suffix":"ogg",
                 "starred":"2024-06-01T10:00:00Z"}
            ]}}})";

    const auto tracks = ParseTrackArray(json, "song");
    Expect(tracks.size() == 2, "getStarred2 songs parsed");
    if (tracks.size() == 2) {
        Expect(tracks[0].id == L"s1" && tracks[1].id == L"s2", "starred song ids parsed");
        Expect(tracks[0].starred && tracks[1].starred, "starred timestamps detected");
    }
}

void TestPlaylistEntries() {
    const std::string json = R"({"subsonic-response":{"status":"ok",
        "playlist":{"id":"pl1","name":"Favorites","owner":"akin","public":false,
            "songCount":2,"duration":485,"created":"2024-01-01T00:00:00Z",
            "changed":"2024-06-01T00:00:00Z","coverArt":"pl-pl1",
            "entry":[
                {"id":"s1","title":"Janie","artist":"Aerosmith","album":"Pump",
                 "albumId":"al1","artistId":"a1","duration":250,"suffix":"mp3",
                 "replayGain":{"trackGain":-5.5}},
                {"id":"s2","title":"Loser","artist":"Beck","album":"Mellow Gold",
                 "albumId":"al9","artistId":"b1","duration":235,"suffix":"ogg"}
            ]}}})";

    const auto tracks = ParseTrackArray(json, "entry");
    Expect(tracks.size() == 2, "getPlaylist entries parsed");

    const auto playlists = ParsePlaylists(json);
    Expect(playlists.size() == 1, "playlist object parsed");
    if (playlists.size() == 1) {
        Expect(playlists[0].id == L"pl1" && playlists[0].songCount == 2, "playlist fields parsed");
        Expect(playlists[0].owner == L"akin", "playlist owner parsed");
    }
}

void TestStatusLookupStaysDeep() {
    const std::string ok = R"({"subsonic-response":{"status":"ok","version":"1.16.1"}})";
    Expect(JsonGetString(ok, "status").value_or("") == "ok", "nested status readable for ping");

    const std::string failed = R"({"subsonic-response":{"status":"failed",
        "error":{"code":40,"message":"Wrong username or password"}}})";
    Expect(JsonGetString(failed, "status").value_or("") == "failed", "failed status readable");
}

} // namespace

int main() {
    TestArtistsAcrossIndexGroups();
    TestSingleSongWithNestedOpenSubsonicFields();
    TestAlbumListWithNestedReleaseDates();
    TestArtistAlbums();
    TestStarred2();
    TestPlaylistEntries();
    TestStatusLookupStaysDeep();

    if (g_ok) {
        std::cout << "All Navidrome JSON parser tests passed.\n";
        return 0;
    }
    return 1;
}
