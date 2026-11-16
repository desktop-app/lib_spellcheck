// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// Author: Nicholas Guriev <guriev-ns@ya.ru>, public domain, 2019
// License: CC0, https://creativecommons.org/publicdomain/zero/1.0/legalcode

#include <future>
#include <set>
#include <QLocale>

#include "spellcheck/platform/linux/linux_enchant.h"

#include "spellcheck/platform/linux/spellcheck_linux.h"
#include "spellcheck/third_party/hunspell_controller.h"
#include "base/debug_log.h"

namespace Platform::Spellchecker {
namespace {

std::vector<QString> ActiveLanguagesMirror;

class EnchantSpellChecker {
public:
	auto knownLanguages();
	bool checkSpelling(const QString &word);
	auto findSuggestions(const QString &word);
	void addWord(const QString &wordToAdd);
	void ignoreWord(const QString &word);
	void removeWord(const QString &word);
	bool isWordInDictionary(const QString &word);
	MisspelledWords checkSpellingText(const QString &text);
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
		try {
			_validators.push_back(DictPtr(_brokerHandle->request_dict(language)));
		} catch (const enchant::Exception &e) {
			DEBUG_LOG(("Catch after request_dict: %1").arg(e.what()));
		}
	}
}

// Used on the Hunspell queue only
EnchantSpellChecker *EnchantSpellChecker::instance() {
	static EnchantSpellChecker capsule;
	return &capsule;
}

auto EnchantSpellChecker::knownLanguages() {
	return _validators | ranges::views::transform([](const auto &validator) {
		return QString(validator->get_lang().c_str());
	}) | ranges::to_vector;
}

bool EnchantSpellChecker::checkSpelling(const QString &word) {
	auto w = word.toStdString();
	return ranges::any_of(_validators, [&](const auto &validator) {
		try {
			return validator->check(w);
		} catch (const enchant::Exception &e) {
			DEBUG_LOG(("Catch after check '%1': %2").arg(word, e.what()));
			return true;
		}
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
	if (_validators.empty()) {
		return;
	}
	auto word = wordToAdd.toStdString();
	auto &&first = _validators.front();
	first->add(word);
	first->add_to_session(word);
}

void EnchantSpellChecker::ignoreWord(const QString &word) {
	if (_validators.empty()) {
		return;
	}
	_validators.front()->add_to_session(word.toStdString());
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

MisspelledWords EnchantSpellChecker::checkSpellingText(const QString &text) {
	return ::Spellchecker::RangesFromText(text, [&](const QString &word) {
		return !::Spellchecker::IsWordSkippable(word)
			&& checkSpelling(word);
	});
}

template <typename Callback>
auto RunOnQueue(Callback callback) {
	using Result = decltype(callback(*EnchantSpellChecker::instance()));
	auto promise = std::promise<Result>();
	auto future = promise.get_future();
	ThirdParty::Queue().with([&](ThirdParty::HunspellService &) {
		promise.set_value(callback(*EnchantSpellChecker::instance()));
	});
	return future.get();
}

} // namespace

void Init() {
}

std::vector<QString> ActiveLanguages() {
	return ActiveLanguagesMirror;
}

void UpdateLanguages(std::vector<int> languages) {
	ThirdParty::Queue().with([](ThirdParty::HunspellService &) {
		auto loaded = EnchantSpellChecker::instance()->knownLanguages();
		crl::on_main([loaded = std::move(loaded)]() mutable {
			ActiveLanguagesMirror = loaded;
			::Spellchecker::UpdateSupportedScripts(std::move(loaded));
		});
	});
}

bool CheckSpelling(const QString &wordToCheck) {
	return RunOnQueue([&](EnchantSpellChecker &instance) {
		return instance.checkSpelling(wordToCheck);
	});
}

void FillSuggestionList(
		const QString &wrongWord,
		std::vector<QString> *variants) {
	*variants = RunOnQueue([&](EnchantSpellChecker &instance) {
		return instance.findSuggestions(wrongWord);
	});
}

void AddWord(const QString &word) {
	ThirdParty::Queue().with([word](ThirdParty::HunspellService &) {
		EnchantSpellChecker::instance()->addWord(word);
	});
}

void RemoveWord(const QString &word) {
	ThirdParty::Queue().with([word](ThirdParty::HunspellService &) {
		EnchantSpellChecker::instance()->removeWord(word);
	});
}

void IgnoreWord(const QString &word) {
	ThirdParty::Queue().with([word](ThirdParty::HunspellService &) {
		EnchantSpellChecker::instance()->ignoreWord(word);
	});
}

bool IsWordInDictionary(const QString &wordToCheck) {
	return RunOnQueue([&](EnchantSpellChecker &instance) {
		return instance.isWordInDictionary(wordToCheck);
	});
}

void CheckSpellingText(
		const QString &text,
		MisspelledWords *misspelledWords) {
	*misspelledWords = RunOnQueue([&](EnchantSpellChecker &instance) {
		return instance.checkSpellingText(text);
	});
}

void CheckSpelling(QString word, FnMut<void(bool correct)> callback) {
	ThirdParty::Queue().with([
		word = std::move(word),
		callback = std::move(callback)
	](ThirdParty::HunspellService &) mutable {
		const auto correct = EnchantSpellChecker::instance()->checkSpelling(
			word);
		crl::on_main([correct, callback = std::move(callback)]() mutable {
			callback(correct);
		});
	});
}

void CheckSpellingText(
		QString text,
		FnMut<void(MisspelledWords &&)> callback) {
	ThirdParty::Queue().with([
		text = std::move(text),
		callback = std::move(callback)
	](ThirdParty::HunspellService &) mutable {
		auto misspelled = EnchantSpellChecker::instance()->checkSpellingText(
			text);
		crl::on_main([
			misspelled = std::move(misspelled),
			callback = std::move(callback)]() mutable {
			callback(std::move(misspelled));
		});
	});
}

void LookupWord(
		QString word,
		FnMut<void(bool correct, std::vector<QString> &&)> callback) {
	const auto generation = ThirdParty::NextLookupGeneration();
	ThirdParty::Queue().with([
		generation,
		word = std::move(word),
		callback = std::move(callback)
	](ThirdParty::HunspellService &) mutable {
		const auto instance = EnchantSpellChecker::instance();
		const auto correct = instance->checkSpelling(word);
		auto suggestions = (correct
			|| ThirdParty::IsLookupObsolete(generation))
			? std::vector<QString>()
			: instance->findSuggestions(word);
		crl::on_main([
			correct,
			suggestions = std::move(suggestions),
			callback = std::move(callback)]() mutable {
			callback(correct, std::move(suggestions));
		});
	});
}

bool IsSystemSpellchecker() {
	return true;
}

} // namespace Platform::Spellchecker
