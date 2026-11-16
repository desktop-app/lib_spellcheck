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

[[nodiscard]] bool Check(
		ThirdParty::HunspellService &hunspell,
		const QString &word) {
	return hunspell.checkSpelling(word)
		|| EnchantSpellChecker::instance()->checkSpelling(word);
}

[[nodiscard]] MisspelledWords CheckText(
		ThirdParty::HunspellService &hunspell,
		const QString &text) {
	return ::Spellchecker::RangesFromText(text, [&](const QString &word) {
		return !::Spellchecker::IsWordSkippable(word)
			&& Check(hunspell, word);
	});
}

[[nodiscard]] std::vector<QString> Suggestions(
		ThirdParty::HunspellService &hunspell,
		const QString &word,
		int generation) {
	if (ThirdParty::IsLookupObsolete(generation)) {
		return {};
	}
	auto sources = hunspell.suggestionSources(word, generation);
	sources.push_back(EnchantSpellChecker::instance()->findSuggestions(word));
	return ThirdParty::InterleaveSuggestions(sources);
}

template <typename Callback>
auto RunOnQueue(Callback callback) {
	using Result = decltype(
		callback(std::declval<ThirdParty::HunspellService&>()));
	auto promise = std::promise<Result>();
	auto future = promise.get_future();
	ThirdParty::Queue().with([&](ThirdParty::HunspellService &hunspell) {
		promise.set_value(callback(hunspell));
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
	return RunOnQueue([&](ThirdParty::HunspellService &hunspell) {
		return Check(hunspell, wordToCheck);
	});
}

void FillSuggestionList(
		const QString &wrongWord,
		std::vector<QString> *variants) {
	const auto generation = ThirdParty::NextLookupGeneration();
	*variants = RunOnQueue([&](ThirdParty::HunspellService &hunspell) {
		return Suggestions(hunspell, wrongWord, generation);
	});
}

void AddWord(const QString &word) {
	ThirdParty::AddWord(word);
}

void RemoveWord(const QString &word) {
	ThirdParty::RemoveWord(word);
}

void IgnoreWord(const QString &word) {
	ThirdParty::IgnoreWord(word);
}

bool IsWordInDictionary(const QString &wordToCheck) {
	return ThirdParty::IsWordInDictionary(wordToCheck);
}

void CheckSpellingText(
		const QString &text,
		MisspelledWords *misspelledWords) {
	*misspelledWords = RunOnQueue([&](ThirdParty::HunspellService &hunspell) {
		return CheckText(hunspell, text);
	});
}

void CheckSpelling(QString word, FnMut<void(bool correct)> callback) {
	ThirdParty::Queue().with([
		word = std::move(word),
		callback = std::move(callback)
	](ThirdParty::HunspellService &hunspell) mutable {
		const auto correct = Check(hunspell, word);
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
	](ThirdParty::HunspellService &hunspell) mutable {
		auto misspelled = CheckText(hunspell, text);
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
	](ThirdParty::HunspellService &hunspell) mutable {
		const auto correct = Check(hunspell, word);
		auto suggestions = correct
			? std::vector<QString>()
			: Suggestions(hunspell, word, generation);
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
