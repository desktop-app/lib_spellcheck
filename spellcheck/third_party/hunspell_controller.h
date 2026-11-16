// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "spellcheck/platform/platform_spellcheck.h"

#include <crl/crl_object_on_queue.h>

#include <map>

class Hunspell;

namespace Platform::Spellchecker::ThirdParty {

constexpr auto kTimeLimitSuggestion = crl::time(1000);

class HunspellEngine;

// One queue owns all state, so there is no locking.
class HunspellService final {
public:
	HunspellService();
	~HunspellService();

	[[nodiscard]] std::vector<QString> updateLanguages(
		const std::vector<QString> &langs);
	[[nodiscard]] bool checkSpelling(const QString &wordToCheck);
	[[nodiscard]] MisspelledWords checkSpellingText(const QString &text);
	[[nodiscard]] std::vector<std::vector<QString>> suggestionSources(
		const QString &wrongWord,
		int generation);
	[[nodiscard]] std::vector<QString> lookupSuggestions(
		const QString &wrongWord,
		int generation);

	void addWord(const QString &word);
	void removeWord(const QString &word);
	void ignoreWord(const QString &word);

private:
	using WordsMap = std::map<QChar::Script, std::vector<QString>>;

	void writeToFile();
	void readFile();

	std::vector<QString> &addedWords(const QString &word);

	std::vector<std::unique_ptr<HunspellEngine>> _engines;
	// An empty dictionary with our remembered words for suggestions.
	std::unique_ptr<Hunspell> _customDict;
	WordsMap _ignoredWords;
	WordsMap _addedWords;

};

// For platform spellcheckers that use Hunspell on its queue
[[nodiscard]] crl::object_on_queue<HunspellService> &Queue();
[[nodiscard]] int NextLookupGeneration();
[[nodiscard]] bool IsLookupObsolete(int generation);
[[nodiscard]] std::vector<QString> InterleaveSuggestions(
	const std::vector<std::vector<QString>> &sources);

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
