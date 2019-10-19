// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "spellcheck/platform/linux/spellcheck_linux.h"

namespace Platform::Spellchecker {

bool CheckSpelling(const QString &wordToCheck) {
	return true;
}

void FillSuggestionList(
	const QString &wrongWord,
	std::vector<QString> *optionalSuggestions) {
}

void AddWord(const QString &word) {
}

void RemoveWord(const QString &word) {
}

void IgnoreWord(const QString &word) {
}

bool IsWordInDictionary(const QString &wordToCheck) {
	return false;
}

void CheckSpellingText(
	const QString &text,
	MisspelledWords *misspelledWordRanges) {
}

} // namespace Platform::Spellchecker
