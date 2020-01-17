// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// Author: Nicholas Guriev <guriev-ns@ya.ru>, public domain, 2019
// License: CC0, https://creativecommons.org/publicdomain/zero/1.0/legalcode

#include <QLocale>

#ifdef DESKTOP_APP_USE_PACKAGED
#include <Sonnet/GuessLanguage>
#else
#include "guesslanguage.h"
#endif

#include "spellcheck/spellcheck_utils.h"
#include "spellcheck/platform/linux/linux_enchant.h"

#include "spellcheck/platform/linux/spellcheck_linux.h"
#include "base/integration.h"
#include "base/flat_map.h"

namespace Platform::Spellchecker {
namespace {

constexpr auto kMinReliability = 0.1;
constexpr auto kMaxItems = 5;

using DictPtr = std::shared_ptr<enchant::Dict>;

class EnchantSpellChecker {
public:
	std::vector<QString> knownLanguages();
	bool checkSpelling(const QString &word);
	std::vector<QString> findSuggestions(const QString &word);
	void addWord(const QString &word);
	void ignoreWord(const QString &word);
	void removeWord(const QString &word);
	bool isWordInDictionary(const QString &word);
	static EnchantSpellChecker *instance();

private:
	EnchantSpellChecker();
	EnchantSpellChecker(const EnchantSpellChecker&) = delete;
	EnchantSpellChecker &operator=(const EnchantSpellChecker&) = delete;

	DictPtr getValidatorByWord(const QString &word);

	Sonnet::GuessLanguage _guessLanguage;
	enchant::Broker _brokerHandle;
	base::flat_map<QString, DictPtr> _validatorCache;

	QString _prevLang;
	QString _locale;
};

EnchantSpellChecker::EnchantSpellChecker() {
	_prevLang = _locale = QLocale::system().name();
	_guessLanguage.setLimits(kMaxItems, kMinReliability);
}

EnchantSpellChecker *EnchantSpellChecker::instance() {
	static EnchantSpellChecker capsule;
	return &capsule;
}

DictPtr EnchantSpellChecker::getValidatorByWord(const QString &word) {
	// If GuessLanguage can't guess the language, it fallback to the first available dictionary in this list
	// Note: when Sonnet is built statically, it can use only Hunspell
	const auto guessLangs = QStringList() << _prevLang << _locale;

	const auto lang = _guessLanguage.identify(word, guessLangs);
	_prevLang = lang;

	auto &validator = _validatorCache[lang];

	if (!validator) {
		try {
			validator.reset(_brokerHandle.request_dict(lang.toStdString()));
		} catch (const enchant::Exception &e) {
			base::Integration::Instance().logMessage(
				QStringLiteral("Catch after request_dict: %1").arg(e.what()));
		}
	}

	return validator;
}

std::vector<QString> EnchantSpellChecker::knownLanguages() {
	std::vector<QString> langs;
	_brokerHandle.list_dicts([](
			const char *language,
			const char *provider,
			const char *description,
			const char *filename,
			void *our_payload) {
		static_cast<decltype(langs)*>(our_payload)
			->push_back(QString::fromLatin1(language));
	}, &langs);
	return langs;
}

bool EnchantSpellChecker::checkSpelling(const QString &word) {
	auto validator = getValidatorByWord(word);
	if (!validator) return true;

	try {
		return validator->check(word.toStdString());
	} catch (const enchant::Exception &e) {
		base::Integration::Instance().logMessage(
			QStringLiteral("Catch after check %1: %2")
				.arg(word).arg(e.what()));
		return true;
	}
}

std::vector<QString> EnchantSpellChecker::findSuggestions(
		const QString &word) {
	auto validator = getValidatorByWord(word);
	std::vector<QString> result;
	if (!validator) return result;

	for (const auto &replacement : validator->suggest(word.toStdString())) {
		if (!replacement.empty()) {
			result.push_back(replacement.c_str());
		}
		if (result.size() >= Platform::Spellchecker::kMaxSuggestions) {
			break;
		}
	}
	return result;
}

void EnchantSpellChecker::addWord(const QString &word) {
	auto validator = getValidatorByWord(word);
	if (!validator) return;

	validator->add(word.toStdString());
}

void EnchantSpellChecker::ignoreWord(const QString &word) {
	auto validator = getValidatorByWord(word);
	if (!validator) return;

	validator->add_to_session(word.toStdString());
}

void EnchantSpellChecker::removeWord(const QString &word) {
	auto validator = getValidatorByWord(word);
	if (!validator) return;

	const auto w = word.toStdString();
	validator->remove_from_session(w);
	validator->remove(w);
}

bool EnchantSpellChecker::isWordInDictionary(const QString &word) {
	auto validator = getValidatorByWord(word);
	if (!validator) return false;

	return validator->is_added(word.toStdString());
}

} // namespace

void Init() {
}

bool IsAvailable() {
	static auto Available = enchant::loader::do_explicit_linking();
	return Available;
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
