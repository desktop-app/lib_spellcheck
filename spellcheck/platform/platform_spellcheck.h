// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "spellcheck/spellcheck_types.h"

namespace Platform::Spellchecker {

constexpr auto kMaxSuggestions = 5;

[[nodiscard]] bool CheckSpelling(const QString &wordToCheck);
[[nodiscard]] bool IsWordInDictionary(const QString &wordToCheck);

void FillSuggestionList(
	const QString &wrongWord,
	std::vector<QString> *optionalSuggestions);

void AddWord(const QString &word);
void RemoveWord(const QString &word);
void IgnoreWord(const QString &word);

void CheckSpellingText(
	const QString &text,
	MisspelledWords *misspelledWordRanges);

} // namespace Platform::Spellchecker

// Platform dependent implementations.
// TODO: We should use Hunspell for Win 7 and Linux.

#ifdef Q_OS_MAC
#include "spellcheck/platform/mac/spellcheck_mac.h"
#elif defined Q_OS_WIN // Q_OS_MAC
#include "spellcheck/platform/win/spellcheck_win.h"
#elif defined Q_OS_WINRT || defined Q_OS_LINUX // Q_OS_MAC || Q_OS_WIN
#include "spellcheck/platform/linux/spellcheck_linux.h"
#endif // Q_OS_MAC || Q_OS_WIN || Q_OS_WINRT || Q_OS_LINUX
