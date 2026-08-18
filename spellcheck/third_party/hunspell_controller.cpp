// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//

#include "spellcheck/third_party/hunspell_controller.h"

#include "spellcheck/spellcheck_value.h"

#include <crl/crl_object_on_queue.h>

#include <atomic>
#include <future>
#include <optional>

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include <xxhash.h>

#include <hunspell/hunspell.hxx>

#if __has_include(<glib/glib.hpp>)
#include <glib/glib.hpp>

using namespace gi::repository;
#elif QT_VERSION < QT_VERSION_CHECK(6, 0, 0) // __has_include(<glib/glib.hpp>)
#include <QTextCodec>
#endif // Qt < 6.0.0

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

const auto kChecksumPrefix = QByteArrayLiteral("checksum_v1 = ");

std::vector<QString> ActiveLanguagesMirror;
std::vector<QString> AddedWordsMirror;

std::atomic<int> LookupGeneration/* = 0*/;

struct PathPair {
	QByteArray aff;
	QByteArray dic;
};

[[nodiscard]] PathPair PreparePaths(const QString &aff, const QString &dic) {
	const auto convert = [&](const QString &path) {
		const auto result = QDir::toNativeSeparators(path).toUtf8();
#ifdef Q_OS_WIN
		return "\\\\?\\" + result;
#else // Q_OS_WIN
		return result;
#endif // !Q_OS_WIN
	};

	return {
		.aff = convert(aff),
		.dic = convert(dic),
	};
}

auto LocaleNameFromLangId(int langId) {
	return ::Spellchecker::LocaleFromLangId(langId).name();
}

QString CustomDictionaryPath() {
	return QStringLiteral("%1/%2").arg(
		::Spellchecker::WorkingDirPath(),
		"custom");
}

[[nodiscard]] quint64 CountChecksum(const QByteArray &data) {
	return XXH64(data.constData(), data.size(), 0);
}

[[nodiscard]] QByteArray ReadRawFile(const QString &path) {
	auto f = QFile(path);
	if (const auto info = QFileInfo(f)
		; !info.isFile()
		|| (info.size() > 100 * 1024)
		|| !f.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	return f.readAll();
}

[[nodiscard]] std::optional<QByteArray> ReadVerifiedBody(
		const QString &path) {
	const auto data = ReadRawFile(path);
	const auto index = data.lastIndexOf(kChecksumPrefix);
	if (index < 0 || (index > 0 && data[index - 1] != '\n')) {
		return std::nullopt;
	}
	const auto line = data.mid(index + kChecksumPrefix.size()).trimmed();
	auto ok = false;
	const auto checksum = line.toULongLong(&ok, 16);
	auto body = data.left(index);
	if (!ok || (CountChecksum(body) != checksum)) {
		return std::nullopt;
	}
	return body;
}

class CharsetConverter final {
public:
	CharsetConverter(const std::string &charset)
	: _isUtf8(IsUtf8Name(charset))
#if __has_include(<glib/glib.hpp>)
	, _charset(charset)
#elif QT_VERSION < QT_VERSION_CHECK(6, 0, 0) // __has_include(<glib/glib.hpp>)
	, _codec(_isUtf8 ? nullptr : QTextCodec::codecForName(charset.c_str()))
#endif // Qt < 6.0.0
	{}

	[[nodiscard]] bool isValid() const {
		if (_isUtf8) {
			// QString::toStdString / fromStdString are UTF-8 on all platforms.
			return true;
		}
#if __has_include(<glib/glib.hpp>)
		const uchar empty[] = "";
		return GLib::convert(empty, 0, _charset, "UTF-8")
			&& GLib::convert(empty, 0, "UTF-8", _charset);
#elif QT_VERSION < QT_VERSION_CHECK(6, 0, 0) // __has_include(<glib/glib.hpp>)
		return _codec;
#else // Qt < 6.0.0
		return false;
#endif // Qt >= 6.0.0 && !__has_include(<glib/glib.hpp>)
	}

	[[nodiscard]] std::string fromUnicode(const QString &data) {
		if (_isUtf8) {
			return data.toStdString();
		}
#if __has_include(<glib/glib.hpp>)
		const auto utf8 = data.toStdString();
		return GLib::convert(
			reinterpret_cast<const uchar*>(utf8.data()),
			utf8.size(),
			_charset,
			"UTF-8",
			nullptr,
			nullptr) | ranges::to<std::string>;
#elif QT_VERSION < QT_VERSION_CHECK(6, 0, 0) // __has_include(<glib/glib.hpp>)
		return _codec->fromUnicode(data).toStdString();
#else // Qt < 6.0.0
		return {};
#endif // Qt >= 6.0.0 && !__has_include(<glib/glib.hpp>)
	}

	[[nodiscard]] QString toUnicode(const std::string &data) {
		if (_isUtf8) {
			return QString::fromStdString(data);
		}
#if __has_include(<glib/glib.hpp>)
		return QString::fromStdString(GLib::convert(
			reinterpret_cast<const uchar*>(data.data()),
			data.size(),
			"UTF-8",
			_charset,
			nullptr,
			nullptr) | ranges::to<std::string>);
#elif QT_VERSION < QT_VERSION_CHECK(6, 0, 0) // __has_include(<glib/glib.hpp>)
		return _codec->toUnicode(data.data(), data.size());
#else // Qt < 6.0.0
		return {};
#endif // Qt >= 6.0.0 && !__has_include(<glib/glib.hpp>)
	}

private:
	[[nodiscard]] static bool IsUtf8Name(const std::string &charset) {
		auto upper = std::string();
		upper.reserve(charset.size());
		for (const auto ch : charset) {
			if (ch != '-' && ch != '_') {
				upper.push_back(std::toupper(
					static_cast<unsigned char>(ch)));
			}
		}
		return upper == "UTF8";
	}

	const bool _isUtf8 = false;
#if __has_include(<glib/glib.hpp>)
	std::string _charset;
#elif QT_VERSION < QT_VERSION_CHECK(6, 0, 0) // __has_include(<glib/glib.hpp>)
	QTextCodec *_codec = nullptr;
#endif // Qt < 6.0.0

};

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
	std::unique_ptr<CharsetConverter> _converter;

};

// One queue owns all state, so there is no locking.
class HunspellService final {
public:
	HunspellService();

	void updateLanguages(const std::vector<QString> &langs);
	[[nodiscard]] bool checkSpelling(const QString &wordToCheck);
	[[nodiscard]] MisspelledWords checkSpellingText(const QString &text);
	[[nodiscard]] std::vector<QString> lookupSuggestions(
		const QString &wrongWord,
		int generation);

	void addWord(const QString &word);
	void removeWord(const QString &word);
	void ignoreWord(const QString &word);

private:
	void writeToFile();
	void readFile();

	std::vector<QString> &addedWords(const QString &word);

	std::vector<std::unique_ptr<HunspellEngine>> _engines;
	// An empty hunspell dictionary to fill it with our remembered words
	// for getting suggests.
	std::unique_ptr<Hunspell> _customDict;
	WordsMap _ignoredWords;
	WordsMap _addedWords;

};

HunspellEngine::HunspellEngine(const QString &lang)
: _lang(lang)
, _script(::Spellchecker::LocaleToScriptCode(lang)) {
	const auto workingDir = ::Spellchecker::WorkingDirPath();
	if (workingDir.isEmpty()) {
		return;
	}
	const auto rawPath = QString("%1/%2/%2").arg(workingDir, lang);
	const auto affPath = rawPath + ".aff";
	const auto dicPath = rawPath + ".dic";

	if (!QFileInfo(affPath).isFile() || !QFileInfo(dicPath).isFile()) {
		return;
	}
	const auto prepared = PreparePaths(affPath, dicPath);
	_hunspell = std::make_unique<Hunspell>(
		prepared.aff.constData(),
		prepared.dic.constData());

	_converter = std::make_unique<CharsetConverter>(
		_hunspell->get_dic_encoding());
	if (!_converter->isValid()) {
		_hunspell = nullptr;
	}
}

bool HunspellEngine::isValid() const {
	return _hunspell != nullptr;
}

bool HunspellEngine::spell(const QString &word) const {
	return _hunspell->spell(_converter->fromUnicode(word));
}

void HunspellEngine::suggest(
	const QString &wrongWord,
	std::vector<QString> *optionalSuggestions) {
	const auto stdWord = _converter->fromUnicode(wrongWord);

	for (const auto &guess : _hunspell->suggest(stdWord)) {
		if (optionalSuggestions->size()	== kMaxSuggestions) {
			return;
		}
		const auto qguess = _converter->toUnicode(guess);
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

HunspellService::HunspellService()
: _customDict(std::make_unique<Hunspell>("", "")) {
	// Remove the helper files of the old UTF table workaround.
	if (const auto dir = ::Spellchecker::WorkingDirPath(); !dir.isEmpty()) {
		QFile::remove(dir + u"/utf_helper.aff"_q);
		QFile::remove(dir + u"/utf_helper.dic"_q);
	}

	readFile();
}

std::vector<QString> &HunspellService::addedWords(const QString &word) {
	return _addedWords[::Spellchecker::WordScript(word)];
}

void HunspellService::updateLanguages(const std::vector<QString> &langs) {
	_engines = ranges::views::all(
		_engines
	) | ranges::views::filter([&](const auto &engine) {
		return ranges::contains(langs, engine->lang());
	}) | ranges::views::transform([](auto &engine) {
		return std::move(engine);
	}) | ranges::to_vector;

	for (const auto &lang : langs) {
		const auto engineLang = [](const auto &engine) {
			return engine->lang();
		};
		if (ranges::contains(_engines, lang, engineLang)) {
			continue;
		}
		auto engine = std::make_unique<HunspellEngine>(lang);
		if (engine->isValid()) {
			_engines.push_back(std::move(engine));
		}
	}

	auto loaded = ranges::views::all(
		_engines
	) | ranges::views::transform(
		&HunspellEngine::lang
	) | ranges::to_vector;

	crl::on_main([loaded = std::move(loaded)]() mutable {
		ActiveLanguagesMirror = loaded;
		::Spellchecker::UpdateSupportedScripts(std::move(loaded));
	});
}

bool HunspellService::checkSpelling(const QString &wordToCheck) {
	const auto wordScript = ::Spellchecker::WordScript(wordToCheck);
	const auto isCustomWord = [&](const WordsMap &words) {
		const auto i = words.find(wordScript);
		return (i != end(words))
			&& ranges::contains(i->second, wordToCheck);
	};
	if (isCustomWord(_ignoredWords) || isCustomWord(_addedWords)) {
		return true;
	}
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

MisspelledWords HunspellService::checkSpellingText(const QString &text) {
	return ::Spellchecker::RangesFromText(text, [&](const QString &word) {
		return !::Spellchecker::IsWordSkippable(word)
			&& checkSpelling(word);
	});
}

std::vector<QString> HunspellService::lookupSuggestions(
		const QString &wrongWord,
		int generation) {
	const auto wordScript = ::Spellchecker::WordScript(wrongWord);

	const auto customGuesses = _customDict->suggest(wrongWord.toStdString());
	auto sources = std::vector<std::vector<QString>>();
	sources.push_back(ranges::views::all(
		customGuesses
	) | ranges::views::take(
		kMaxSuggestions
	) | ranges::views::transform([](auto &guess) {
		return QString::fromStdString(guess);
	}) | ranges::to_vector);

	const auto startTime = crl::now();
	for (const auto &engine : _engines) {
		if (LookupGeneration.load() != generation) {
			// There is a newer request to fill the suggestion list,
			// so we should drop the current one.
			return {};
		}
		if ((crl::now() - startTime) > kTimeLimitSuggestion) {
			break;
		}
		if (wordScript != engine->script()) {
			continue;
		}
		auto list = std::vector<QString>();
		engine->suggest(wrongWord, &list);
		sources.push_back(std::move(list));
	}

	// Round-robin so one language doesn't starve the others.
	auto result = std::vector<QString>();
	for (auto index = 0;; ++index) {
		auto any = false;
		for (const auto &source : sources) {
			if (index >= int(source.size())) {
				continue;
			}
			any = true;
			if (!ranges::contains(result, source[index])) {
				result.push_back(source[index]);
				if (result.size() == kMaxSuggestions) {
					return result;
				}
			}
		}
		if (!any) {
			break;
		}
	}
	return result;
}

void HunspellService::ignoreWord(const QString &word) {
	const auto wordScript = ::Spellchecker::WordScript(word);
	if (ranges::contains(_ignoredWords[wordScript], word)) {
		return;
	}
	_customDict->add(word.toStdString());
	_ignoredWords[wordScript].push_back(word);
}

void HunspellService::addWord(const QString &word) {
	auto &vector = addedWords(word);
	if (ranges::contains(vector, word)) {
		return;
	}
	const auto count = ranges::accumulate(
		ranges::views::values(_addedWords),
		0,
		ranges::plus(),
		&std::vector<QString>::size);
	if (count > kMaxSyncableDictionaryWords) {
		return;
	}
	_customDict->add(word.toStdString());
	vector.push_back(word);
	writeToFile();
	crl::on_main([word] {
		AddedWordsMirror.push_back(word);
	});
}

void HunspellService::removeWord(const QString &word) {
	_customDict->remove(word.toStdString());
	auto &vector = addedWords(word);
	vector.erase(ranges::remove(vector, word), end(vector));
	writeToFile();
	crl::on_main([word] {
		auto &mirror = AddedWordsMirror;
		mirror.erase(ranges::remove(mirror, word), end(mirror));
	});
}

void HunspellService::writeToFile() {
	auto body = QByteArray();
	for (const auto &[script, words] : _addedWords) {
		for (const auto &word : words) {
			body += word.toUtf8() + kLineBreak;
		}
	}
	const auto full = body
		+ kChecksumPrefix
		+ QByteArray::number(qulonglong(CountChecksum(body)), 16)
		+ kLineBreak;
	{
		auto backup = QFile(CustomDictionaryPath() + u".backup"_q);
		if (backup.open(QIODevice::WriteOnly)) {
			backup.write(full);
		}
	}
	auto f = QSaveFile(CustomDictionaryPath());
	if (!f.open(QIODevice::WriteOnly)) {
		return;
	}
	f.write(full);
	f.commit();
}

void HunspellService::readFile() {
	using namespace ::Spellchecker;

	const auto path = CustomDictionaryPath();
	if (const auto info = QFileInfo(path); info.isDir()) {
		QDir(info.absoluteFilePath()).removeRecursively();
		return;
	}
	auto dirty = false;
	auto data = QByteArray();
	if (auto verified = ReadVerifiedBody(path)) {
		data = std::move(*verified);
	} else if (auto backup = ReadVerifiedBody(path + u".backup"_q)) {
		data = std::move(*backup);
		dirty = true;
	} else {
		data = ReadRawFile(path);
		dirty = !data.isEmpty();
	}
	if (data.isEmpty()) {
		return;
	}

	auto splitedWords = QString::fromUtf8(data).replace(
		QChar('\r'),
		QChar('\n')
	).split(QChar('\n'), Qt::SkipEmptyParts)
		| ranges::to_vector
		| ranges::actions::sort
		| ranges::actions::unique;

	auto count = 0;
	for (auto &word : splitedWords) {
		// Ignore words with mixed scripts or non-words characters.
		if (IsWordSkippable(word, false)) {
			continue;
		}
		if (++count > kMaxSyncableDictionaryWords) {
			break;
		}
		_customDict->add(word.toStdString());
		_addedWords[WordScript(word)].push_back(std::move(word));
	}
	if (dirty) {
		writeToFile();
	}

	auto loaded = ranges::views::all(
		ranges::views::join(ranges::views::values(_addedWords))
	) | ranges::to_vector;
	crl::on_main([loaded = std::move(loaded)]() mutable {
		AddedWordsMirror = std::move(loaded);
	});
}

////// End of HunspellService class.

[[nodiscard]] crl::object_on_queue<HunspellService> &Queue() {
	static auto queue = crl::object_on_queue<HunspellService>();
	return queue;
}

} // namespace

void CheckSpelling(QString word, FnMut<void(bool correct)> callback) {
	Queue().with([
		word = std::move(word),
		callback = std::move(callback)
	](HunspellService &instance) mutable {
		const auto correct = instance.checkSpelling(word);
		crl::on_main([correct, callback = std::move(callback)]() mutable {
			callback(correct);
		});
	});
}

void CheckSpellingText(
		QString text,
		FnMut<void(MisspelledWords &&)> callback) {
	Queue().with([
		text = std::move(text),
		callback = std::move(callback)
	](HunspellService &instance) mutable {
		auto misspelled = instance.checkSpellingText(text);
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
	const auto generation = ++LookupGeneration;
	Queue().with([
		generation,
		word = std::move(word),
		callback = std::move(callback)
	](HunspellService &instance) mutable {
		const auto correct = instance.checkSpelling(word);
		auto suggestions = correct
			? std::vector<QString>()
			: instance.lookupSuggestions(word, generation);
		crl::on_main([
			correct,
			suggestions = std::move(suggestions),
			callback = std::move(callback)]() mutable {
			callback(correct, std::move(suggestions));
		});
	});
}

bool IsWordInDictionary(const QString &wordToCheck) {
	return ranges::contains(AddedWordsMirror, wordToCheck);
}

std::vector<QString> ActiveLanguages() {
	return ActiveLanguagesMirror;
}

void AddWord(const QString &word) {
	Queue().with([word](HunspellService &instance) {
		instance.addWord(word);
	});
}

void RemoveWord(const QString &word) {
	Queue().with([word](HunspellService &instance) {
		instance.removeWord(word);
	});
}

void IgnoreWord(const QString &word) {
	Queue().with([word](HunspellService &instance) {
		instance.ignoreWord(word);
	});
}

void UpdateLanguages(std::vector<int> languages) {
	auto languageCodes = ranges::views::all(
		languages
	) | ranges::views::transform(
		LocaleNameFromLangId
	) | ranges::to_vector;

	::Spellchecker::UpdateSupportedScripts(std::vector<QString>());
	Queue().with([codes = std::move(languageCodes)](
			HunspellService &instance) {
		instance.updateLanguages(codes);
	});
}

bool CheckSpelling(const QString &wordToCheck) {
	auto promise = std::promise<bool>();
	auto future = promise.get_future();
	Queue().with([&promise, word = wordToCheck](HunspellService &instance) {
		promise.set_value(instance.checkSpelling(word));
	});
	return future.get();
}

void FillSuggestionList(
		const QString &wrongWord,
		std::vector<QString> *optionalSuggestions) {
	const auto generation = ++LookupGeneration;
	auto promise = std::promise<std::vector<QString>>();
	auto future = promise.get_future();
	Queue().with([&promise, generation, word = wrongWord](
			HunspellService &instance) {
		promise.set_value(instance.lookupSuggestions(word, generation));
	});
	*optionalSuggestions = future.get();
}

void CheckSpellingText(
		const QString &text,
		MisspelledWords *misspelledWords) {
	auto promise = std::promise<MisspelledWords>();
	auto future = promise.get_future();
	Queue().with([&promise, text](HunspellService &instance) {
		promise.set_value(instance.checkSpellingText(text));
	});
	*misspelledWords = future.get();
}

} // namespace Platform::Spellchecker::ThirdParty
