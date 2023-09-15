// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "spellcheck/spellcheck_types.h"
#include "spellcheck/spellcheck_utils.h"

namespace Platform::Spellchecker {

constexpr auto kMaxSuggestions = 5;

[[nodiscard]] bool IsSystemSpellchecker();
[[nodiscard]] bool CheckSpelling(const QString &wordToCheck);
[[nodiscard]] bool IsWordInDictionary(const QString &wordToCheck);

void Init();
std::vector<QString> ActiveLanguages();
void FillSuggestionList(
	const QString &wrongWord,
	std::vector<QString> *optionalSuggestions);

void AddWord(const QString &word);
void RemoveWord(const QString &word);
void IgnoreWord(const QString &word);

void CheckSpellingText(
	const QString &text,
	MisspelledWords *misspelledWords);

void UpdateLanguages(std::vector<int> languages);

} // namespace Platform::Spellchecker
