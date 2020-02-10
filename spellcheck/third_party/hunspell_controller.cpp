// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//

#include "spellcheck/third_party/hunspell_controller.h"

#include "hunspell/hunspell.hxx"
#include "spellcheck/spellcheck_value.h"

#include <mutex>
#include <shared_mutex>

#include <QFileInfo>
#include <QTextCodec>

namespace Platform::Spellchecker::ThirdParty {
namespace {

using WordsMap = std::map<QChar::Script, std::vector<QString>>;

// Maximum number of words in the custom spellcheck dictionary.
constexpr auto kMaxSyncableDictionaryWords = 1300;

#ifdef Q_OS_WIN
const auto kLineBreak = QByteArrayLiteral("\r\n");
#else // Q_OS_WIN
const auto kLineBreak = QByteArrayLiteral("\n");
#endif // Q_OS_WIN

auto LocaleNameFromLangId(int langId) {
	return ::Spellchecker::LocaleFromLangId(langId).name();
}

QString CustomDictionaryPath() {
	return QStringLiteral("%1/%2")
		.arg(::Spellchecker::WorkingDirPath())
		.arg("custom");
}

class HunspellEngine {
public:
	HunspellEngine(const QString &lang);
	~HunspellEngine() = default;

	bool isValid() const;

	bool spell(const QString &word) const;

	void suggest(
		const QString &wrongWord,
		std::vector<QString> *optionalSuggestions);

	QString lang();
	QChar::Script script();

	HunspellEngine(const HunspellEngine &) = delete;
	HunspellEngine &operator=(const HunspellEngine &) = delete;

private:
	QString _lang;
	QChar::Script _script;
	std::unique_ptr<Hunspell> _hunspell;
	QTextCodec *_codec;

};


class HunspellService {
public:
	HunspellService();
	~HunspellService() = default;

	void updateLanguages(std::vector<QString> langs);
	std::vector<QString> activeLanguages();
	[[nodiscard]] bool checkSpelling(const QString &wordToCheck);

	void fillSuggestionList(
		const QString &wrongWord,
		std::vector<QString> *optionalSuggestions);

	void addWord(const QString &word);
	void removeWord(const QString &word);
	void ignoreWord(const QString &word);
	bool isWordInDictionary(const QString &word);

private:
	void writeToFile();
	void readFile();

	std::vector<QString> &addedWords(const QString &word);

	std::vector<std::unique_ptr<HunspellEngine>> _engines;
	std::vector<QString> _activeLanguages;
	WordsMap _ignoredWords;
	WordsMap _addedWords;

	std::atomic<int> _epoch = 0;

	std::shared_mutex _engineMutex;

};

HunspellEngine::HunspellEngine(const QString &lang)
: _lang(lang)
, _script(::Spellchecker::LocaleToScriptCode(lang))
, _hunspell(nullptr)
, _codec(nullptr) {
	const auto workingDir = ::Spellchecker::WorkingDirPath();
	if (workingDir.isEmpty()) {
		return;
	}
	const auto dictPath = QString("%1/%2/%2")
		.arg(workingDir)
		.arg(lang)
		.toUtf8();

	const auto affPath = dictPath + ".aff";
	const auto dicPath = dictPath + ".dic";

	if (!QFileInfo(affPath).isFile() || !QFileInfo(dicPath).isFile()) {
		return;
	}

	_hunspell = std::make_unique<Hunspell>(affPath, dicPath);
	_codec = QTextCodec::codecForName(_hunspell->get_dic_encoding());
	if (!_codec) {
		_hunspell.reset();
	}
}

bool HunspellEngine::isValid() const {
	return _hunspell != nullptr;
}

bool HunspellEngine::spell(const QString &word) const {
	return _hunspell->spell(_codec->fromUnicode(word).toStdString());
}

void HunspellEngine::suggest(
	const QString &wrongWord,
	std::vector<QString> *optionalSuggestions) {
	const auto stdWord = _codec->fromUnicode(wrongWord).toStdString();

	for (const auto &guess : _hunspell->suggest(stdWord)) {
		if (optionalSuggestions->size()	== kMaxSuggestions) {
			return;
		}
		const auto qguess = _codec->toUnicode(
			guess.data(),
			guess.length());
		if (ranges::contains(*optionalSuggestions, qguess)) {
			continue;
		}
		optionalSuggestions->push_back(qguess);
	}
}

QString HunspellEngine::lang() {
	return _lang;
}

QChar::Script HunspellEngine::script() {
	return _script;
}

std::vector<QString> HunspellService::activeLanguages() {
	return _activeLanguages;
}

HunspellService::HunspellService() {
	readFile();
}

std::vector<QString> &HunspellService::addedWords(const QString &word) {
	return _addedWords[::Spellchecker::WordScript(&word)];
}

void HunspellService::updateLanguages(std::vector<QString> langs) {
	_epoch += 1;

	_activeLanguages.clear();

	const auto savedEpoch = _epoch.load();
	crl::async([=] {
		using UniqueEngine = std::unique_ptr<HunspellEngine>;

		const auto engineLangFilter = [&](const UniqueEngine &engine) {
			return engine ? ranges::contains(langs, engine->lang()) : false;
		};

		if (savedEpoch != _epoch.load()) {
			return;
		}

		const auto engineLang = [](const UniqueEngine &engine) {
			return engine ? engine->lang() : QString();
		};

		std::shared_lock sharedLock(_engineMutex);

		auto missedLangs = ranges::view::all(
			langs
		) | ranges::views::filter([&](auto &lang) {
			return !ranges::contains(_engines, lang, engineLang);
		}) | ranges::to_vector;

		sharedLock.unlock();

		// Added new enabled engines.
		auto localEngines = ranges::view::all(
			missedLangs
		) | ranges::views::transform([&](auto &lang) -> UniqueEngine {
			if (savedEpoch != _epoch.load()) {
				return nullptr;
			}
			auto engine = std::make_unique<HunspellEngine>(lang);
			if (!engine->isValid()) {
				return nullptr;
			}
			return std::move(engine);
		}) | ranges::to_vector;

		if (savedEpoch != _epoch.load()) {
			return;
		}

		std::unique_lock uniqueLock(_engineMutex);

		_engines = ranges::views::concat(
			_engines, localEngines
		) | ranges::views::filter(
			// All filtered objects will be automatically released.
			engineLangFilter
		) | ranges::views::transform([](auto &engine) {
			return std::move(engine);
		}) | ranges::to_vector;

		uniqueLock.unlock();

		crl::on_main([=] {
			if (savedEpoch != _epoch.load()) {
				return;
			}
			_epoch = 0;
			_activeLanguages = ranges::view::all(
				_engines
			) | ranges::views::transform(&HunspellEngine::lang)
			| ranges::to_vector;
			::Spellchecker::UpdateSupportedScripts(_activeLanguages);
		});

	});
}

bool HunspellService::checkSpelling(const QString &wordToCheck) {
	const auto wordScript = ::Spellchecker::WordScript(&wordToCheck);
	if (ranges::contains(_ignoredWords[wordScript], wordToCheck)) {
		return true;
	}
	if (ranges::contains(_addedWords[wordScript], wordToCheck)) {
		return true;
	}
	std::shared_lock lock(_engineMutex);
	for (const auto &engine : _engines) {
		if (wordScript != engine->script()) {
			continue;
		}
		if (engine->spell(wordToCheck)) {
			return true;
		}
	}

	return false;
}

void HunspellService::fillSuggestionList(
	const QString &wrongWord,
	std::vector<QString> *optionalSuggestions) {
	const auto wordScript = ::Spellchecker::WordScript(&wrongWord);
	std::shared_lock lock(_engineMutex);
	for (const auto &engine : _engines) {
		if (wordScript != engine->script()) {
			continue;
		}
		if (optionalSuggestions->size()	== kMaxSuggestions) {
			return;
		}
		engine->suggest(wrongWord, optionalSuggestions);
	}
}

void HunspellService::ignoreWord(const QString &word) {
	const auto wordScript = ::Spellchecker::WordScript(&word);
	_ignoredWords[wordScript].push_back(word);
}

bool HunspellService::isWordInDictionary(const QString &word) {
	return ranges::contains(addedWords(word), word);
}

void HunspellService::addWord(const QString &word) {
	const auto count = ranges::accumulate(
		ranges::view::values(_addedWords),
		0,
		ranges::plus(),
		&std::vector<QString>::size);
	if (count > kMaxSyncableDictionaryWords) {
		return;
	}
	addedWords(word).push_back(word);
	writeToFile();
}

void HunspellService::removeWord(const QString &word) {
	auto &vector = addedWords(word);
	vector.erase(ranges::remove(vector, word), end(vector));
	writeToFile();
}

void HunspellService::writeToFile() {
	auto f = QFile(CustomDictionaryPath());
	if (!f.open(QIODevice::WriteOnly)) {
		return;
	}
	auto &&temp = ranges::views::join(
		ranges::view::values(_addedWords))
	| ranges::view::transform([](auto &str) {
		return str + kLineBreak;
	});
	const auto result = ranges::accumulate(std::move(temp), QString{});
	f.write(result.toUtf8());
	f.close();
}

void HunspellService::readFile() {
	using namespace ::Spellchecker;

	auto f = QFile(CustomDictionaryPath());
	if (!f.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto data = f.readAll();
	f.close();
	if (data.isEmpty()) {
		return;
	}

	// {"a", "1", "β"};
	auto splitedWords = QString(data).split(kLineBreak)
		| ranges::to_vector
		| ranges::actions::sort
		| ranges::actions::unique;

	// {{"a"}, {"β"}};
	auto groupedWords = ranges::view::all(
		splitedWords
	) | ranges::views::filter([](auto &word) {
		// Ignore words with mixed scripts or non-words characters.
		return !word.isEmpty() && !IsWordSkippable(&word, false);
	}) | ranges::views::take(
		kMaxSyncableDictionaryWords
	) | ranges::view::group_by([](auto &a, auto &b) {
		return WordScript(&a) == WordScript(&b);
	}) | ranges::to_vector;

	// {QChar::Script_Latin, QChar::Script_Greek};
	auto &&scripts = ranges::view::all(
		groupedWords
	) | ranges::view::transform([](auto &vector) {
		return WordScript(&vector.front());
	});

	// {QChar::Script_Latin : {"a"}, QChar::Script_Greek : {"β"}};
	auto &&zip = ranges::view::zip(
		scripts, groupedWords
	);
#ifndef Q_OS_WIN
	_addedWords = zip | ranges::to<WordsMap>;
#else
	// This is a workaround for the MSVC compiler.
	// Something is wrong with the group_by method or with me. =(
	for (auto &&[script, words] : zip) {
		_addedWords[script] = std::move(words);
	}
#endif
	writeToFile();
}

////// End of HunspellService class.


std::unique_ptr<HunspellService>& SharedSpellChecker() {
	static auto spellchecker = std::make_unique<HunspellService>();
	return spellchecker;
}


} // namespace

bool CheckSpelling(const QString &wordToCheck) {
	return SharedSpellChecker()->checkSpelling(wordToCheck);
}

void FillSuggestionList(
	const QString &wrongWord,
	std::vector<QString> *optionalSuggestions) {
	SharedSpellChecker()->fillSuggestionList(wrongWord, optionalSuggestions);
}

void AddWord(const QString &word) {
	SharedSpellChecker()->addWord(word);
}

void RemoveWord(const QString &word) {
	SharedSpellChecker()->removeWord(word);
}

void IgnoreWord(const QString &word) {
	SharedSpellChecker()->ignoreWord(word);
}

bool IsWordInDictionary(const QString &wordToCheck) {
	return SharedSpellChecker()->isWordInDictionary(wordToCheck);
}

void UpdateLanguages(std::vector<int> languages) {

	const auto languageCodes = ranges::view::all(
		languages
	) | ranges::views::transform(
		LocaleNameFromLangId
	) | ranges::to_vector;

	::Spellchecker::UpdateSupportedScripts(std::vector<QString>());
	SharedSpellChecker()->updateLanguages(languageCodes);
}

std::vector<QString> ActiveLanguages() {
	return SharedSpellChecker()->activeLanguages();
}

void CheckSpellingText(
	const QString &text,
	MisspelledWords *misspelledWords) {
	*misspelledWords = ::Spellchecker::RangesFromText(
		text,
		[](const QString &word) {
			return !::Spellchecker::IsWordSkippable(&word)
				&& CheckSpelling(word);
		});
}

} // namespace Platform::Spellchecker::ThirdParty
