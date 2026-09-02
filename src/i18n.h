#pragma once

// Lightweight UI-language helper for user-facing strings.
// Chinese Windows UI renders Chinese text; anything else falls back to English.

namespace L10n {

// True when the current Windows UI language is Chinese (Simplified or Traditional).
bool IsChineseUi();

// Returns zh when the UI is Chinese, otherwise en.
const wchar_t* Text(const wchar_t* zh, const wchar_t* en);

} // namespace L10n