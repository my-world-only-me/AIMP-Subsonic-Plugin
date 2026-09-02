# AIMP Subsonic 插件

[English](README.md) | [Русский](README.ru.md) | [简体中文](README.zh.md)

针对 Subsonic 兼容音乐服务器的 AIMP Desktop 5.40+ 原生插件。主要在 Navidrome 上测试,目标是把远程 Subsonic 音乐库变成 AIMP 音乐库中普通的数据源。

当前版本:`1.0.0`。

本 `main` 分支为稳定的在线播放分支。默认播放路径为直接的 Subsonic stream URL。离线音频缓存工作被有意保留在分支之外,直到它足够稳定才会向普通用户开放。

## 功能特性

- 在 AIMP 音乐库中独立的 `Subsonic` 存储。
- 浏览播放列表、艺术家、专辑、收藏和曲目。
- 通过 AIMP 常规行为播放中央表格:双击、拖放,或添加到 AIMP 播放列表。
- 直接的 `/rest/stream.view` 播放 URL,可配置格式与最大比特率。
- 持久的元数据索引:曲目、艺术家、专辑、播放列表、收藏曲目,以及有序的专辑/播放列表快照。
- 后台 `构建 / 刷新元数据索引` 命令。
- 音乐库中央表格的快速搜索。
- 服务器播放列表可在音乐库中浏览,其曲目可通过 AIMP 常规操作播放或添加。
- `导入服务器播放列表` 命令:服务器播放列表成为播放列表管理器中的真实 AIMP 播放列表(单向同步,启动时刷新)。
- Subsonic 正在播放与已播放 scrobble 事件。
- 基于 `getCoverArt` 的封面图提供器,带本地图片缓存。
- AIMP 选项中的设置页:连接、播放、音乐库、TLS、诊断。
- 令牌认证:`t=md5(password + salt)`,`s=<salt>`,`v=1.16.1`,`c=aimp-subsonic`,`f=json`。
- 针对 AIMP 设置的 DPAPI 保护密码存储。
- 可选的本地 `subsonic.local.json` 开发环境回退配置。
- 带认证查询值脱敏的诊断日志。

## 截图

### 音乐库

![Subsonic 音乐库浏览](docs/screenshots/central_menu.png)

### 音乐库结构

![Subsonic 音乐库树形结构](docs/screenshots/structure.png)

### Subsonic 命令

![Subsonic 右键菜单命令](docs/screenshots/context_menu_refresh_index.png)

### 设置

![Subsonic 插件设置](docs/screenshots/settings.png)

## 1.0.0 未包含的功能

- 离线音频播放/缓存。
- `subsonic://` 虚拟缓存播放。
- 将播放列表更改写回服务器(创建/更新/删除);播放列表管理器的导入是单向的,从服务器到 AIMP。
- 播客、视频、点唱机、聊天、书签,以及仅 OpenSubsonic 的扩展。

## 构建

项目使用 CMake、C++17、Visual Studio 2022/2026 Build Tools 和官方 AIMP SDK v5.40。CMake 按以下顺序查找 SDK:

- `-DAIMP_SDK_DIR=...`
- `%AIMP_SDK_DIR%`
- `third_party/aimp_sdk`
- `build/_deps/aimp_sdk_v540`
- `%TEMP%/aimp_sdk_v540`

如果未找到 SDK 且 `AIMP_SDK_AUTO_DOWNLOAD=ON`,CMake 会将其下载到 `build/_deps`。

配置与构建:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -T host=x64 -A x64
cmake --build build --config Release --target aimp_subsonic
```

干跑测试:

```powershell
cmake --build build --config Release --target aimp_subsonic_node_tests aimp_subsonic_security_tests aimp_subsonic_json_tests
build\Release\aimp_subsonic_node_tests.exe
build\Release\aimp_subsonic_security_tests.exe
build\Release\aimp_subsonic_json_tests.exe
```

对于 32 位 AIMP,使用 `-A Win32` 配置。

## 安装

将发布版 DLL 复制到:

```text
AIMP\Plugins\aimp_subsonic\aimp_subsonic.dll
```

然后重启 AIMP。

如果替换 DLL 后 AIMP 仍保留过期的插件元数据,请关闭 AIMP,并删除 `%APPDATA%\AIMP\AIMP.ini` 的 `[Plugins]` 与 `[Plugins.CachedInfo]` 节中的 `aimp_subsonic.dll` 条目。AIMP 会在下次启动时重新扫描该 DLL。

## 配置

打开 AIMP 选项,选择 `插件 -> Subsonic`。

必填字段:

- 服务器地址,例如 `https://music.example.com` 或 `http://192.168.0.10:4533`
- 用户名
- 密码

实用默认值:

- 流格式:`mp3`
- 最大比特率:`320`
- 加载上限:`500`(每个服务器请求的页面大小;列表会分页加载直至完整音乐库加载完成)
- 调试日志:关
- 允许自签名 HTTPS 证书:关

## Navidrome 说明

- 插件能理解 Navidrome 的 OpenSubsonic 响应,包括按字母分组艺术家索引,以及 `genres`、`artists`、`replayGain`、`releaseDate` 等嵌套条目字段。
- 流格式 `mp3` 会要求服务器转码,这需要 Navidrome 主机上安装 ffmpeg。如果你的 Navidrome 没有 ffmpeg(或希望按原始文件逐位播放),请将流格式设为 `raw`;插件随后请求 `format=raw` 并跳过 `maxBitRate`。AIMP 原生支持 FLAC/OGG/Opus。
- 使用令牌认证(`t`/`s`),明文密码不会出现在请求中;Navidrome 开箱即支持。

仅在受信任的私有服务器上使用自签名证书选项。它会关闭插件自身 WinHTTP 请求的 TLS 证书校验。直接播放 URL 会交给 AIMP,因此 AIMP 自身的网络栈可能仍然应用其自己的 TLS 行为。

## 开发配置回退

如果 AIMP 设置为空,插件可以读取 DLL 旁的 `subsonic.local.json`:

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

该文件被有意加入 gitignore。

## 推荐首次运行步骤

1. 在 AIMP 选项中配置服务器。
2. 打开 AIMP 音乐库并切换到 `Subsonic`。
3. 运行 `Subsonic -> 构建 / 刷新元数据索引`。
4. 浏览播放列表、艺术家、专辑、收藏或曲目。
5. 使用 AIMP 常规的双击或拖放行为播放或添加曲目。

## 日志

启用调试日志后,日志写入 DLL 旁:

```text
aimp_subsonic.log
```

写入日志前会脱敏认证查询值,如 `u`、`p`、`t`、`s`、`password`、`token`、`salt` 和 `Authorization`。

## 状态

这是首个公开版本。请将其视为实用的社区版本,而非完整的最终 Subsonic 客户端:音乐库浏览、播放、元数据、封面、设置、服务器播放列表查看和 scrobble 均已实现,而离线音频缓存、播放列表编辑/同步,以及更深入的 OpenSubsonic 功能计划在后续版本中开发。

## 待办 / 路线图

- 在独立开发分支中稳定离线音频缓存与离线 AIMP 播放列表支持。
- 添加安全的显式服务器播放列表编辑与本地到服务器播放列表同步 UX。
- 添加可选的 OpenSubsonic 功能,如歌词、书签与播放队列支持。
- 改进面向用户的网络/认证/服务器失败通知与错误报告。
- 添加更多使用模拟 Subsonic/Navidrome 响应的干跑集成测试。
- 准备打包的发布存档,方便安装。