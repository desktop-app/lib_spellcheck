// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "spellcheck/spellcheck_types.h"

namespace Platform::Language {

[[nodiscard]] LanguageId Recognize(QStringView text);

} // namespace Platform::Language

// Platform dependent implementations.
#ifdef Q_OS_MAC
#include "spellcheck/platform/mac/language_mac.h"
#elif defined Q_OS_WIN // Q_OS_MAC
#include "spellcheck/platform/win/language_win.h"
#elif defined Q_OS_UNIX // Q_OS_MAC || Q_OS_WIN
#include "spellcheck/platform/linux/language_linux.h"
#endif // Q_OS_MAC || Q_OS_WIN || Q_OS_UNIX
