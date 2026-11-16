// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//

#include <future>

#include "spellcheck/platform/linux/linux_enchant.h"

#include "spellcheck/platform/linux/spellcheck_linux.h"
#include "spellcheck/third_party/hunspell_controller.h"
#include "base/debug_log.h"

namespace Platform::Spellchecker {
namespace {

std::vector<QString> ActiveLanguagesMirror;

class EnchantSpellChecker {
public:
	// Returns the languages left without a dictionary
	std::vector<QString> updateLanguages(const std::vector<QString> &langs);
	std::vector<QString> languages() const;
	bool checkSpelling(const QString &word);
	std::vector<std::vector<QString>> suggestionSources(
		const QString &wrongWord,
		int generation);
	static EnchantSpellChecker *instance();

private:
	struct Validator {
		QString lang;
		QChar::Script script = QChar::Script_Unknown;
		std::unique_ptr<enchant::Dict> dict;
	};

	EnchantSpellChecker();
	EnchantSpellChecker(const EnchantSpellChecker&) = delete;
	EnchantSpellChecker& operator =(const EnchantSpellChecker&) = delete;

	std::unique_ptr<enchant::Broker> _brokerHandle;
	std::vector<Validator> _validators;
};

EnchantSpellChecker::EnchantSpellChecker() {
	if (!enchant::loader::do_explicit_linking()) return;
	_brokerHandle = std::make_unique<enchant::Broker>();
}

// Used on the Hunspell queue only
EnchantSpellChecker *EnchantSpellChecker::instance() {
	static EnchantSpellChecker capsule;
	return &capsule;
}

std::vector<QString> EnchantSpellChecker::updateLanguages(
		const std::vector<QString> &langs) {
	_validators.erase(ranges::remove_if(_validators, [&](const auto &v) {
		return !ranges::contains(langs, v.lang);
	}), end(_validators));

	auto result = std::vector<QString>();
	for (const auto &lang : langs) {
		if (ranges::contains(_validators, lang, &Validator::lang)) {
			continue;
		}
		auto dict = std::unique_ptr<enchant::Dict>();
		if (_brokerHandle) {
			try {
				dict.reset(_brokerHandle->request_dict(lang.toStdString()));
			} catch (const enchant::Exception &) {
			}
		}
		if (!dict) {
			result.push_back(lang);
			continue;
		}
		_validators.push_back({
			.lang = lang,
			.script = ::Spellchecker::LocaleToScriptCode(lang),
			.dict = std::move(dict),
		});
	}
	return result;
}

std::vector<QString> EnchantSpellChecker::languages() const {
	return ranges::views::all(
		_validators
	) | ranges::views::transform(
		&Validator::lang
	) | ranges::to_vector;
}

bool EnchantSpellChecker::checkSpelling(const QString &word) {
	const auto script = ::Spellchecker::WordScript(word);
	const auto w = word.toStdString();
	return ranges::any_of(_validators, [&](const Validator &validator) {
		if (validator.script != script) {
			return false;
		}
		try {
			return validator.dict->check(w);
		} catch (const enchant::Exception &e) {
			DEBUG_LOG(("Catch after check '%1': %2").arg(word, e.what()));
			return true;
		}
	});
}

std::vector<std::vector<QString>> EnchantSpellChecker::suggestionSources(
		const QString &wrongWord,
		int generation) {
	const auto script = ::Spellchecker::WordScript(wrongWord);
	const auto w = wrongWord.toStdString();
	auto result = std::vector<std::vector<QString>>();
	const auto startTime = crl::now();
	for (const auto &validator : _validators) {
		if (ThirdParty::IsLookupObsolete(generation)) {
			return {};
		}
		if ((crl::now() - startTime) > ThirdParty::kTimeLimitSuggestion) {
			break;
		}
		if (validator.script != script) {
			continue;
		}
		const auto guesses = validator.dict->suggest(w);
		result.push_back(ranges::views::all(
			guesses
		) | ranges::views::filter([](const std::string &guess) {
			return !guess.empty();
		}) | ranges::views::take(
			kMaxSuggestions
		) | ranges::views::transform([](const std::string &guess) {
			return QString::fromStdString(guess);
		}) | ranges::to_vector);
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
	for (auto &source : EnchantSpellChecker::instance()->suggestionSources(
			word,
			generation)) {
		sources.push_back(std::move(source));
	}
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
	auto codes = ranges::views::all(
		languages
	) | ranges::views::transform([](int langId) {
		return ::Spellchecker::LocaleFromLangId(langId).name();
	}) | ranges::to_vector;

	::Spellchecker::UpdateSupportedScripts(std::vector<QString>());
	ThirdParty::Queue().with([codes = std::move(codes)](
			ThirdParty::HunspellService &hunspell) {
		const auto instance = EnchantSpellChecker::instance();
		const auto rest = instance->updateLanguages(codes);
		auto loaded = instance->languages();
		for (auto &lang : hunspell.updateLanguages(rest)) {
			loaded.push_back(std::move(lang));
		}
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
	return false;
}

} // namespace Platform::Spellchecker
