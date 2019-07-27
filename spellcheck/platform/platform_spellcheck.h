// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

namespace Platform {
namespace Spellcheck {

[[nodiscard]] bool CheckSpelling(const QString &wordToCheck, int tag);

} // namespace Spellcheck
} // namespace Platform

#ifdef Q_OS_MAC
#include "spellcheck/platform/mac/spellcheck_mac.h"
#elif defined Q_OS_LINUX // Q_OS_MAC
#include "spellcheck/platform/linux/spellcheck_linux.h"
#elif defined Q_OS_WIN // Q_OS_MAC || Q_OS_LINUX
#include "spellcheck/platform/win/spellcheck_win.h"
#endif // Q_OS_MAC || Q_OS_LINUX || Q_OS_WIN
