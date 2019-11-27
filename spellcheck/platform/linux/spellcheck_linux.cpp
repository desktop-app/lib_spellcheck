// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// Author: Nicholas Guriev <guriev-ns@ya.ru>, public domain, 2019
// License: CC0, https://creativecommons.org/publicdomain/zero/1.0/legalcode

#include <set>
#include <QLocale>

#include "spellcheck/spellcheck_utils.h"
#include "spellcheck/platform/linux/linux_enchant.h"

#include "spellcheck/platform/linux/spellcheck_linux.h"

namespace Platform::Spellchecker {
namespace {

class EnchantSpellChecker {
public:
	bool isAvailable();
	auto knownLanguages();
	bool checkSpelling(const QString &word);
	auto findSuggestions(const QString &word);
	void addWord(const QString &wordToAdd);
	void ignoreWord(const QString &word);
	void removeWord(const QString &word);
	bool isWordInDictionary(const QString &word);
	static EnchantSpellChecker *instance();

private:
	EnchantSpellChecker();
	EnchantSpellChecker(const EnchantSpellChecker&) = delete;
	EnchantSpellChecker& operator =(const EnchantSpellChecker&) = delete;

	std::unique_ptr<enchant::Broker> _brokerHandle;
	std::vector<std::unique_ptr<enchant::Dict>> _validators;
};

EnchantSpellChecker::EnchantSpellChecker() {
	if (!enchant::loader::do_explicit_linking()) return;

	std::set<std::string> langs;
	_brokerHandle = std::make_unique<enchant::Broker>();
	_brokerHandle->list_dicts([](
			const char *language,
			const char *provider,
			const char *description,
			const char *filename,
			void *our_payload) {
		static_cast<decltype(langs)*>(our_payload)->insert(language);
	}, &langs);
	using DictPtr = std::unique_ptr<enchant::Dict>;
	_validators.reserve(langs.size());
	try {
		std::string langTag = QLocale::system().name().toStdString();
		_validators.push_back(DictPtr(_brokerHandle->request_dict(langTag)));
		langs.erase(langTag);
	} catch (const enchant::Exception &e) {
		// no first dictionary found
	}
	for (const std::string &language : langs) {
		_validators.push_back(DictPtr(_brokerHandle->request_dict(language)));
	}
}

EnchantSpellChecker *EnchantSpellChecker::instance() {
	static EnchantSpellChecker capsule;
	return &capsule;
}

bool EnchantSpellChecker::isAvailable() {
	return !_validators.empty();
}

auto EnchantSpellChecker::knownLanguages() {
	return _validators | ranges::views::transform([](const auto &validator) {
		return QString(validator->get_lang().c_str());
	}) | ranges::to_vector;
}

bool EnchantSpellChecker::checkSpelling(const QString &word) {
	auto w = word.toStdString();
	return ranges::any_of(_validators, [&w](const auto &validator) {
		return validator->check(w);
	}) || _validators.empty();
}

auto EnchantSpellChecker::findSuggestions(const QString &word) {
	auto w = word.toStdString();
	std::vector<QString> result;
	for (const auto &validator : _validators) {
		for (const auto &replacement : validator->suggest(w)) {
			if (!replacement.empty()) {
				result.push_back(replacement.c_str());
			}
			if (result.size() >= Platform::Spellchecker::kMaxSuggestions) {
				break;
			}
		}
		if (!result.empty()) {
			break;
		}
	}
	return result;
}

void EnchantSpellChecker::addWord(const QString &wordToAdd) {
	auto word = wordToAdd.toStdString();
	auto &&first = _validators.at(0);
	first->add(word);
	first->add_to_session(word);
}

void EnchantSpellChecker::ignoreWord(const QString &word) {
	_validators.at(0)->add_to_session(word.toStdString());
}

void EnchantSpellChecker::removeWord(const QString &word) {
	auto w = word.toStdString();
	for (const auto &validator : _validators) {
		validator->remove_from_session(w);
		validator->remove(w);
	}
}

bool EnchantSpellChecker::isWordInDictionary(const QString &word) {
	auto w = word.toStdString();
	return ranges::any_of(_validators, [&w](const auto &validator) {
		return validator->is_added(w);
	});
}

} // namespace

bool IsAvailable() {
	return EnchantSpellChecker::instance()->isAvailable();
}

void KnownLanguages(std::vector<QString> *langCodes) {
	*langCodes = EnchantSpellChecker::instance()->knownLanguages();
}

bool CheckSpelling(const QString &wordToCheck) {
	return EnchantSpellChecker::instance()->checkSpelling(wordToCheck);
}

void FillSuggestionList(
		const QString &wrongWord,
		std::vector<QString> *variants) {
	*variants = EnchantSpellChecker::instance()->findSuggestions(wrongWord);
}

void AddWord(const QString &word) {
	EnchantSpellChecker::instance()->addWord(word);
}

void RemoveWord(const QString &word) {
	EnchantSpellChecker::instance()->removeWord(word);
}

void IgnoreWord(const QString &word) {
	EnchantSpellChecker::instance()->ignoreWord(word);
}

bool IsWordInDictionary(const QString &wordToCheck) {
	return EnchantSpellChecker::instance()->isWordInDictionary(wordToCheck);
}

void CheckSpellingText(
		const QString &text,
		MisspelledWords *misspelledWords) {
	*misspelledWords = ::Spellchecker::RangesFromText(text, CheckSpelling);
}

} // namespace Platform::Spellchecker
