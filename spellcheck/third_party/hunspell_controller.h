// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "spellcheck/platform/platform_spellcheck.h"

namespace Platform::Spellchecker::ThirdParty {

void CheckSpelling(QString word, FnMut<void(bool correct)> callback);
void CheckSpellingText(QString text, FnMut<void(MisspelledWords &&)> callback);
void LookupWord(
	QString word,
	FnMut<void(bool correct, std::vector<QString> &&suggestions)> callback);

[[nodiscard]] bool IsWordInDictionary(const QString &wordToCheck);
std::vector<QString> ActiveLanguages();
void AddWord(const QString &word);
void RemoveWord(const QString &word);
void IgnoreWord(const QString &word);
void UpdateLanguages(std::vector<int> languages);

// Blocking bridges for the old Windows fallback.
[[nodiscard]] bool CheckSpelling(const QString &wordToCheck);
void FillSuggestionList(
	const QString &wrongWord,
	std::vector<QString> *optionalSuggestions);
void CheckSpellingText(
	const QString &text,
	MisspelledWords *misspelledWords);

} // namespace Platform::Spellchecker::ThirdParty
