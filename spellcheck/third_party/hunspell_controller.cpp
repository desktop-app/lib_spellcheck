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

#include <QDir>
#include <QFileInfo>
#include <QTextCodec>

namespace Platform::Spellchecker::ThirdParty {
namespace {

using WordsMap = std::map<QChar::Script, std::vector<QString>>;

// Maximum number of words in the custom spellcheck dictionary.
constexpr auto kMaxSyncableDictionaryWords = 1300;
constexpr auto kTimeLimitSuggestion = crl::time(1000);

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
	~HunspellService();

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

	std::shared_ptr<std::vector<std::unique_ptr<HunspellEngine>>> _engines;
	std::vector<QString> _activeLanguages;
	// Use an empty Hunspell dictionary to fill it with our remembered words
	// for getting suggests.
	std::unique_ptr<Hunspell> _customDict;
	WordsMap _ignoredWords;
	WordsMap _addedWords;

	std::shared_ptr<std::atomic<int>> _epoch;
	std::atomic<int> _suggestionsEpoch = 0;

	std::shared_ptr<std::shared_mutex> _engineMutex;

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
	const auto rawPath = QString("%1/%2/%2").arg(workingDir).arg(lang);
	const auto dictPath = QDir::toNativeSeparators(rawPath).toUtf8();

	const auto affPath = dictPath + ".aff";
	const auto dicPath = dictPath + ".dic";

	if (!QFileInfo(affPath).isFile() || !QFileInfo(dicPath).isFile()) {
		return;
	}

#ifdef Q_OS_WIN
	_hunspell = std::make_unique<Hunspell>(
		"\\\\?\\" + affPath,
		"\\\\?\\" + dicPath);
#else // Q_OS_WIN
	_hunspell = std::make_unique<Hunspell>(affPath, dicPath);
#endif // !Q_OS_WIN

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

// Thread: Any.
HunspellService::HunspellService()
: _engines(std::make_shared<std::vector<std::unique_ptr<HunspellEngine>>>())
, _customDict(std::make_unique<Hunspell>("", ""))
, _epoch(std::make_shared<std::atomic<int>>(0))
, _engineMutex(std::make_shared<std::shared_mutex>()) {
	readFile();
}

// Thread: Main.
HunspellService::~HunspellService() {
	std::unique_lock lock(*_engineMutex);
}

// Thread: Main.
std::vector<QString> &HunspellService::addedWords(const QString &word) {
	return _addedWords[::Spellchecker::WordScript(&word)];
}

// Thread: Main.
void HunspellService::updateLanguages(std::vector<QString> langs) {
	Expects(_suggestionsEpoch.load() == 0);
	*_epoch += 1;

	_activeLanguages.clear();

	const auto savedEpoch = _epoch.get()->load();
	crl::async([=,
		epoch = _epoch,
		engineMutex = _engineMutex,
		engines = _engines] {
		using UniqueEngine = std::unique_ptr<HunspellEngine>;

		const auto engineLangFilter = [&](const UniqueEngine &engine) {
			return engine ? ranges::contains(langs, engine->lang()) : false;
		};

		if (savedEpoch != epoch.get()->load()) {
			return;
		}

		const auto engineLang = [](const UniqueEngine &engine) {
			return engine ? engine->lang() : QString();
		};

		const auto missedLangs = [&] {
			std::shared_lock lock(*engineMutex);

			return ranges::view::all(
				langs
			) | ranges::views::filter([&](auto &lang) {
				return !ranges::contains(*engines, lang, engineLang);
			}) | ranges::to_vector;
		}();

		// Added new enabled engines.
		auto localEngines = ranges::view::all(
			missedLangs
		) | ranges::views::transform([&](auto &lang) -> UniqueEngine {
			if (savedEpoch != epoch.get()->load()) {
				return nullptr;
			}
			auto engine = std::make_unique<HunspellEngine>(lang);
			if (!engine->isValid()) {
				return nullptr;
			}
			return std::move(engine);
		}) | ranges::to_vector;

		if (savedEpoch != epoch.get()->load()) {
			return;
		}

		{
			std::unique_lock lock(*engineMutex);

			*engines = ranges::views::concat(
				*engines, localEngines
			) | ranges::views::filter(
				// All filtered objects will be automatically released.
				engineLangFilter
			) | ranges::views::transform([](auto &engine) {
				return std::move(engine);
			}) | ranges::to_vector;
		}

		crl::on_main([=] {
			if (savedEpoch != epoch.get()->load()) {
				return;
			}
			*epoch = 0;
			_activeLanguages = ranges::view::all(
				*engines
			) | ranges::views::transform(&HunspellEngine::lang)
			| ranges::to_vector;
			::Spellchecker::UpdateSupportedScripts(_activeLanguages);
		});

	});
}

// Thread: Any.
bool HunspellService::checkSpelling(const QString &wordToCheck) {
	const auto wordScript = ::Spellchecker::WordScript(&wordToCheck);
	if (ranges::contains(_ignoredWords[wordScript], wordToCheck)) {
		return true;
	}
	if (ranges::contains(_addedWords[wordScript], wordToCheck)) {
		return true;
	}
	std::shared_lock lock(*_engineMutex);
	for (const auto &engine : *_engines) {
		if (wordScript != engine->script()) {
			continue;
		}
		if (engine->spell(wordToCheck)) {
			return true;
		}
	}

	return false;
}

// Thread: Any.
void HunspellService::fillSuggestionList(
	const QString &wrongWord,
	std::vector<QString> *optionalSuggestions) {
	const auto wordScript = ::Spellchecker::WordScript(&wrongWord);

	const auto customGuesses = _customDict->suggest(wrongWord.toStdString());
	*optionalSuggestions = ranges::view::all(
		customGuesses
	) | ranges::views::take(
		kMaxSuggestions
	) | ranges::views::transform([](auto &guess) {
		return QString::fromStdString(guess);
	}) | ranges::to_vector;

	const auto startTime = crl::now();

	_suggestionsEpoch++;
	const auto savedEpoch = _suggestionsEpoch.load();

	{
		std::shared_lock lock(*_engineMutex);
		for (const auto &engine : *_engines) {
			if (_suggestionsEpoch.load() > savedEpoch) {
				// There is a newer request to fill suggestion list,
				// So we should drop the current one.
				optionalSuggestions->clear();
				break;
			}
			if (optionalSuggestions->size()	== kMaxSuggestions
				|| ((crl::now() - startTime) > kTimeLimitSuggestion)) {
				break;
			}
			if (wordScript != engine->script()) {
				continue;
			}
			engine->suggest(wrongWord, optionalSuggestions);
		}
	}
	_suggestionsEpoch--;
}

// Thread: Main.
void HunspellService::ignoreWord(const QString &word) {
	const auto wordScript = ::Spellchecker::WordScript(&word);
	_customDict->add(word.toStdString());
	_ignoredWords[wordScript].push_back(word);
}

// Thread: Main.
bool HunspellService::isWordInDictionary(const QString &word) {
	return ranges::contains(addedWords(word), word);
}

// Thread: Main.
void HunspellService::addWord(const QString &word) {
	const auto count = ranges::accumulate(
		ranges::view::values(_addedWords),
		0,
		ranges::plus(),
		&std::vector<QString>::size);
	if (count > kMaxSyncableDictionaryWords) {
		return;
	}
	_customDict->add(word.toStdString());
	addedWords(word).push_back(word);
	writeToFile();
}

// Thread: Main.
void HunspellService::removeWord(const QString &word) {
	_customDict->remove(word.toStdString());
	auto &vector = addedWords(word);
	vector.erase(ranges::remove(vector, word), end(vector));
	writeToFile();
}

// Thread: Main.
void HunspellService::writeToFile() {
	auto f = QFile(CustomDictionaryPath());
	if (!f.open(QIODevice::WriteOnly)) {
		return;
	}
	auto &&temp = ranges::views::join(
		ranges::view::values(_addedWords)
	) | ranges::view::transform([&](auto &str) {
		return str + kLineBreak;
	});
	const auto result = ranges::accumulate(std::move(temp), QString{});
	f.write(result.toUtf8());
	f.close();
}

// Thread: Main.
void HunspellService::readFile() {
	using namespace ::Spellchecker;

	auto f = QFile(CustomDictionaryPath());

	if (const auto info = QFileInfo(f);
		!info.isFile()
		|| (info.size() > 100 * 1024)
		|| !f.open(QIODevice::ReadOnly)) {
		if (info.isDir()) {
			QDir(info.path()).removeRecursively();
		}
		return;
	}
	const auto data = f.readAll();
	f.close();
	if (data.isEmpty()) {
		return;
	}

	// {"a", "1", "β"};
	auto splitedWords = QString::fromUtf8(data).split(kLineBreak)
		| ranges::to_vector
		| ranges::actions::sort
		| ranges::actions::unique;

	auto filteredWords = (
		splitedWords
	) | ranges::views::filter([](auto &word) {
		// Ignore words with mixed scripts or non-words characters.
		return !word.isEmpty() && !IsWordSkippable(&word, false);
	}) | ranges::views::take(
		kMaxSyncableDictionaryWords
	) | ranges::views::transform([](auto &word) {
		return std::move(word);
	}) | ranges::to_vector;

	ranges::for_each(filteredWords, [&](auto &word) {
		_customDict->add(word.toStdString());
	});

	// {{"a"}, {"β"}};
	auto groupedWords = ranges::view::all(
		filteredWords
	) | ranges::view::group_by([](auto &a, auto &b) {
		return WordScript(&a) == WordScript(&b);
	}) | ranges::view::transform([](auto &&rng) {
		return rng | ranges::to_vector;
	}) | ranges::to_vector;

	// {QChar::Script_Latin, QChar::Script_Greek};
	auto scripts = ranges::view::all(
		groupedWords
	) | ranges::view::transform([](auto &vector) {
		return WordScript(&vector.front());
	}) | ranges::to_vector;

	// {QChar::Script_Latin : {"a"}, QChar::Script_Greek : {"β"}};
	auto &&zip = ranges::view::zip(
		scripts, groupedWords
	);
	_addedWords = zip | ranges::to<WordsMap>();

}

////// End of HunspellService class.


HunspellService &SharedSpellChecker() {
	static auto spellchecker = HunspellService();
	return spellchecker;
}


} // namespace

bool CheckSpelling(const QString &wordToCheck) {
	return SharedSpellChecker().checkSpelling(wordToCheck);
}

void FillSuggestionList(
	const QString &wrongWord,
	std::vector<QString> *optionalSuggestions) {
	SharedSpellChecker().fillSuggestionList(wrongWord, optionalSuggestions);
}

void AddWord(const QString &word) {
	SharedSpellChecker().addWord(word);
}

void RemoveWord(const QString &word) {
	SharedSpellChecker().removeWord(word);
}

void IgnoreWord(const QString &word) {
	SharedSpellChecker().ignoreWord(word);
}

bool IsWordInDictionary(const QString &wordToCheck) {
	return SharedSpellChecker().isWordInDictionary(wordToCheck);
}

void UpdateLanguages(std::vector<int> languages) {

	const auto languageCodes = ranges::view::all(
		languages
	) | ranges::views::transform(
		LocaleNameFromLangId
	) | ranges::to_vector;

	::Spellchecker::UpdateSupportedScripts(std::vector<QString>());
	SharedSpellChecker().updateLanguages(languageCodes);
}

std::vector<QString> ActiveLanguages() {
	return SharedSpellChecker().activeLanguages();
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
