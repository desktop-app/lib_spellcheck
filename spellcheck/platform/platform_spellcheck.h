// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

namespace Platform {
namespace Spellchecker {

constexpr auto kMaxSuggestions = 5;

[[nodiscard]] bool CheckSpelling(const QString &wordToCheck);
[[nodiscard]] bool IsWordInDictionary(const QString &wordToCheck);

void FillSuggestionList(
	const QString &wrongWord,
	std::vector<QString> *optionalSuggestions);

void AddWord(const QString &word);
void RemoveWord(const QString &word);
void IgnoreWord(const QString &word);

} // namespace Spellchecker
} // namespace Platform

#ifdef Q_OS_MAC
#include "spellcheck/platform/mac/spellcheck_mac.h"
#elif defined Q_OS_LINUX // Q_OS_MAC
#include "spellcheck/platform/linux/spellcheck_linux.h"
#elif defined Q_OS_WIN // Q_OS_MAC || Q_OS_LINUX
#include "spellcheck/platform/win/spellcheck_win.h"
#endif // Q_OS_MAC || Q_OS_LINUX || Q_OS_WIN
