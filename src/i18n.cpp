#include "i18n.h"

#include <windows.h>

namespace L10n {

bool IsChineseUi() {
    // Detects whether the Windows UI language is Chinese (Simplified or Traditional),
    // which is more accurate than the user locale format.
    static const bool cached = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
    return cached;
}

const wchar_t* Text(const wchar_t* zh, const wchar_t* en) {
    return IsChineseUi() ? zh : en;
}

} // namespace L10n