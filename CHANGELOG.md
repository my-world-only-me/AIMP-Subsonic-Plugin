# Changelog

## Unreleased

- Added `Subsonic -> Import Server Playlists`: server playlists are created as real AIMP playlists in the Playlist Manager, filled with direct stream URLs and full metadata. Re-running the command updates previously imported playlists (rename included) using a persistent server-to-AIMP playlist link stored next to the metadata cache; already-imported playlists are also refreshed silently on AIMP startup. Playlists you closed in AIMP are not recreated automatically; run the import command to bring them back. The import is one-way (server to AIMP).

- Removed the 500-row cap in the Music Library views: Tracks, Favorites, Albums, and playlist/album/artist track lists now show the whole library. The library page size setting now only controls how many items are requested from the server per API call (`search3`/`getAlbumList2` allow at most 500); listings paginate until the server runs out of results.

- Fixed `getArtists` parsing so artists from every index group are collected; Navidrome groups artists by letter, and only the first group was read before.
- Fixed parsing of OpenSubsonic responses (Navidrome 0.51+) that nest arrays and objects inside song/album/playlist entries (`genres`, `artists`, `replayGain`, `releaseDate`, `contributors`, ...): `getSong` returned no track and `getPlaylists`/`getPlaylist` mixed entry objects into playlist objects.
- JSON object fields are now read only from the top level of each entry, so nested OpenSubsonic values (for example `releaseDate.year`) can no longer leak into track/album fields.
- Added `raw` stream format support: setting the stream format to `raw` requests the original file without transcoding and omits `maxBitRate`, which makes playback work on Navidrome servers without ffmpeg.
- Added dry parser tests with Navidrome-shaped responses (`aimp_subsonic_json_tests`).

## 1.0.0 - Initial public release

- Added a Subsonic/Navidrome music library storage for AIMP with Artists, Albums, Favorites, Tracks, and Playlists sections.
- Added DirectURL playback through Subsonic `/rest/stream.view` with configurable stream format and maximum bitrate.
- Added a persistent metadata index for tracks, artists, albums, playlists, starred tracks, and ordered album/playlist snapshots.
- Added a background `Build / Refresh Metadata Index` command using AIMP's thread service.
- Added a file-info provider and playlist metadata repair so direct stream URLs can recover title, artist, album, duration, rating, and Subsonic identity from the metadata cache.
- Added cover art loading and local artwork cache via Subsonic `getCoverArt`.
- Added Subsonic now-playing and played scrobble events.
- Added quick search support for central Music Library table rows, with `search3` fallback for global track searches.
- Added AIMP Options settings for server URL, username, password, stream format, bitrate, library page size, debug logging, and self-signed TLS mode.
- Added Windows DPAPI-protected password storage for AIMP settings, with `subsonic.local.json` kept only as an optional development fallback.
- Added diagnostic redaction for Subsonic auth query values and common secret fields before writing logs.
- Kept audio offline cache and server-side playlist write/delete operations out of the stable `main` release line.
