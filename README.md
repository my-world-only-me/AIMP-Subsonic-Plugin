# AIMP Subsonic Plugin

[English](README.md) | [Русский](README.ru.md)

Native AIMP Desktop 5.40+ plugin for Subsonic-compatible music servers. It is tested primarily with Navidrome and focuses on making a remote Subsonic library feel like a normal AIMP Music Library source.

Current version: `1.0.0`.

This `main` branch is the stable online playback line. Direct Subsonic stream URLs are the default playback path. Offline audio cache work is intentionally kept outside this branch until it is stable enough for regular users.

## Features

- Separate `Subsonic` storage in AIMP Music Library.
- Browsing for Playlists, Artists, Albums, Favorites, and Tracks.
- Central table playback through normal AIMP behavior: double-click, drag/drop, or adding rows to an AIMP playlist.
- Direct `/rest/stream.view` playback URLs with configurable format and max bitrate.
- Persistent metadata index for tracks, artists, albums, playlists, starred tracks, and ordered album/playlist snapshots.
- Background `Build / Refresh Metadata Index` command.
- Quick search in the central Music Library table.
- Server playlists are browsable in the Music Library; their tracks can be played or added through normal AIMP actions.
- `Import Server Playlists` command: server playlists become real AIMP playlists in the Playlist Manager (one-way sync, refreshed on startup).
- Subsonic now-playing and played scrobble events.
- Cover art provider using `getCoverArt`, with a local image cache.
- Settings page in AIMP Options: connection, playback, library, TLS, diagnostics.
- Token auth: `t=md5(password + salt)`, `s=<salt>`, `v=1.16.1`, `c=aimp-subsonic`, `f=json`.
- DPAPI-protected password storage for AIMP settings.
- Optional local `subsonic.local.json` fallback for development.
- Diagnostic log with redacted auth query values.

## Screenshots

### Music Library

![Subsonic Music Library browsing](docs/screenshots/central_menu.png)

### Library Structure

![Subsonic library tree structure](docs/screenshots/structure.png)

### Subsonic Commands

![Subsonic context menu commands](docs/screenshots/context_menu_refresh_index.png)

### Settings

![Subsonic plugin settings](docs/screenshots/settings.png)

## Not Included In 1.0.0

- Offline audio playback/cache.
- `subsonic://` virtual cache playback.
- Writing playlist changes back to the server (create/update/delete); the Playlist Manager import is one-way, server to AIMP.
- Podcasts, video, jukebox, chat, bookmarks, and OpenSubsonic-only extensions.

## Build

The project uses CMake, C++17, Visual Studio 2022/2026 Build Tools, and the official AIMP SDK v5.40. CMake searches for the SDK in:

- `-DAIMP_SDK_DIR=...`
- `%AIMP_SDK_DIR%`
- `third_party/aimp_sdk`
- `build/_deps/aimp_sdk_v540`
- `%TEMP%/aimp_sdk_v540`

If the SDK is not found and `AIMP_SDK_AUTO_DOWNLOAD=ON`, CMake downloads it into `build/_deps`.

Configure and build:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -T host=x64 -A x64
cmake --build build --config Release --target aimp_subsonic
```

Dry tests:

```powershell
cmake --build build --config Release --target aimp_subsonic_node_tests aimp_subsonic_security_tests aimp_subsonic_json_tests
build\Release\aimp_subsonic_node_tests.exe
build\Release\aimp_subsonic_security_tests.exe
build\Release\aimp_subsonic_json_tests.exe
```

For 32-bit AIMP, configure with `-A Win32`.

## Install

Copy the release DLL to:

```text
AIMP\Plugins\aimp_subsonic\aimp_subsonic.dll
```

Then restart AIMP.

If AIMP keeps stale plugin metadata after replacing the DLL, close AIMP and remove the `aimp_subsonic.dll` entries from the `[Plugins]` and `[Plugins.CachedInfo]` sections in `%APPDATA%\AIMP\AIMP.ini`. AIMP will rescan the DLL on the next launch.

## Configuration

Open AIMP Options and select `Plugins -> Subsonic`.

Required fields:

- Server URL, for example `https://music.example.com` or `http://192.168.0.10:4533`
- Username
- Password

Useful defaults:

- Stream format: `mp3`
- Max bitrate: `320`
- Library page size: `500` (per-request page size for server calls; listings paginate until the full library is loaded)
- Debug logging: off
- Allow self-signed HTTPS certificates: off

## Navidrome Notes

- The plugin understands Navidrome's OpenSubsonic responses, including letter-grouped artist indexes and nested entry fields such as `genres`, `artists`, `replayGain`, and `releaseDate`.
- Stream format `mp3` asks the server to transcode, which requires ffmpeg on the Navidrome host. If your Navidrome runs without ffmpeg (or you want the original files bit-perfect), set the stream format to `raw`; the plugin then requests `format=raw` and skips `maxBitRate`. AIMP plays FLAC/OGG/Opus natively.
- Token authentication (`t`/`s`) is used, so the plain password is never sent in requests; Navidrome supports this out of the box.

Use the self-signed certificate option only for a trusted private server. It disables TLS certificate validation for the plugin's own WinHTTP requests. Direct playback URLs are handed to AIMP, so AIMP's own network stack may still apply its own TLS behavior.

## Development Config Fallback

If AIMP settings are empty, the plugin can read `subsonic.local.json` next to the DLL:

```json
{
  "serverUrl": "https://music.example.com",
  "username": "user",
  "password": "password",
  "streamFormat": "mp3",
  "maxBitRate": 320,
  "libraryPageSize": 500,
  "ignoreTlsCertificateErrors": false,
  "debugLogging": false
}
```

The file is intentionally gitignored.

## Recommended First Run

1. Configure the server in AIMP Options.
2. Open AIMP Music Library and switch to `Subsonic`.
3. Run `Subsonic -> Build / Refresh Metadata Index`.
4. Browse Playlists, Artists, Albums, Favorites, or Tracks.
5. Use AIMP's normal double-click or drag/drop behavior to play or add tracks.

## Logs

When debug logging is enabled, the log is written next to the DLL:

```text
aimp_subsonic.log
```

Auth query values such as `u`, `p`, `t`, `s`, `password`, `token`, `salt`, and `Authorization` are redacted before writing to the log.

## Status

This is the first public release. Please treat it as a practical community release rather than a final full Subsonic client: library browsing, playback, metadata, cover art, settings, server playlist viewing, and scrobble are implemented, while offline audio cache, playlist editing/sync, and deeper OpenSubsonic features are planned for later development.

## TODO / Roadmap

- Stabilize offline audio cache and offline AIMP playlist support in a separate development branch.
- Add safe explicit server playlist editing and local-to-server playlist synchronization UX.
- Add optional OpenSubsonic features such as lyrics, bookmarks, and play queue support.
- Improve user-facing notifications and error reporting for network/auth/server failures.
- Add more dry integration tests with mocked Subsonic/Navidrome responses.
- Prepare packaged release archives for easier installation.
