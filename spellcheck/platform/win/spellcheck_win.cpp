// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "spellcheck/platform/win/spellcheck_win.h"
#include "spellcheck/third_party/hunspell_controller.h"

#include <wrl/client.h>
#include <spellcheck.h>

#include <QtCore/QDir>
#include <QtCore/QLocale>
#include <QVector>

#include "base/platform/base_platform_info.h"

using namespace Microsoft::WRL;

namespace Platform::Spellchecker {

namespace {

// Seems like ISpellChecker API has bugs for Persian language (aka Farsi).
inline bool IsPersianLanguage(const QString &langTag) {
	return langTag.startsWith(QStringLiteral("fa"));
}

inline LPCWSTR Q2WString(QStringView string) {
	return (LPCWSTR)string.utf16();
}

inline auto SystemLanguages() {
	const auto appdata = qEnvironmentVariable("appdata");
	const auto dir = QDir(appdata + QString("\\Microsoft\\Spelling"));
	return (dir.exists()
		? dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)
		: QLocale::system().uiLanguages()).toVector().toStdVector();
}

// WindowsSpellChecker class is used to store all the COM objects and
// control their lifetime. The class also provides wrappers for
// ISpellCheckerFactory and ISpellChecker APIs. All COM calls are on the
// background thread.
class WindowsSpellChecker {
public:
	WindowsSpellChecker();

	void addWord(LPCWSTR word);
	void removeWord(LPCWSTR word);
	void ignoreWord(LPCWSTR word);
	bool checkSpelling(LPCWSTR word);
	void fillSuggestionList(
		LPCWSTR wrongWord,
		std::vector<QString> *optionalSuggestions);
	void checkSpellingText(
		LPCWSTR text,
		MisspelledWords *misspelledWordRanges);
	std::vector<QString> systemLanguages();

private:
	void createFactory();
	bool isLanguageSupported(const LPCWSTR& lang);
	void createSpellCheckers();

	ComPtr<ISpellCheckerFactory> _spellcheckerFactory;
	std::map<QString, ComPtr<ISpellChecker>> _spellcheckerMap;

};

WindowsSpellChecker::WindowsSpellChecker() {
	createFactory();
	createSpellCheckers();
}

void WindowsSpellChecker::createFactory() {
	if (FAILED(CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr,
		(CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER),
		IID_PPV_ARGS(&_spellcheckerFactory)))) {
		_spellcheckerFactory = nullptr;
	}
}

void WindowsSpellChecker::createSpellCheckers() {
	if (!_spellcheckerFactory) {
		return;
	}
	for (const auto &lang : SystemLanguages()) {
		const auto wlang = Q2WString(lang);
		if (!isLanguageSupported(wlang)) {
			continue;
		}
		if (_spellcheckerMap.find(lang) != _spellcheckerMap.end()) {
			continue;
		}
		ComPtr<ISpellChecker> spellchecker;
		HRESULT hr = _spellcheckerFactory->CreateSpellChecker(
			wlang,
			&spellchecker);
		if (SUCCEEDED(hr)) {
			_spellcheckerMap.insert({ lang, spellchecker });
		}
	}
}

bool WindowsSpellChecker::isLanguageSupported(const LPCWSTR& lang) {
	if (!_spellcheckerFactory) {
		return false;
	}

	BOOL isSupported = (BOOL)false;
	HRESULT hr = _spellcheckerFactory->IsSupported(lang, &isSupported);
	return SUCCEEDED(hr) && isSupported;
}

void WindowsSpellChecker::fillSuggestionList(
	LPCWSTR wrongWord,
	std::vector<QString> *optionalSuggestions) {
	auto i = 0;
	for (const auto &[langTag, spellchecker] : _spellcheckerMap) {
		if (IsPersianLanguage(langTag)) {
			continue;
		}
		ComPtr<IEnumString> suggestions;
		HRESULT hr = spellchecker->Suggest(wrongWord, &suggestions);
		if (hr != S_OK) {
			continue;
		}

		while (true) {
			wchar_t* suggestion = nullptr;
			hr = suggestions->Next(1, &suggestion, nullptr);
			if (hr != S_OK) {
				break;
			}
			const auto guess =
				QString::fromWCharArray(suggestion, wcslen(suggestion));
			CoTaskMemFree(suggestion);
			if (!guess.isEmpty()) {
				optionalSuggestions->push_back(guess);
				if (++i >= kMaxSuggestions) {
					return;
				}
			}
		}
	}
}

bool WindowsSpellChecker::checkSpelling(LPCWSTR word) {
	for (const auto &[_, spellchecker] : _spellcheckerMap) {
		ComPtr<IEnumSpellingError> spellingErrors;
		HRESULT hr = spellchecker->Check(word, &spellingErrors);

		if (SUCCEEDED(hr) && spellingErrors) {
			ComPtr<ISpellingError> spellingError;
			ULONG startIndex = 0;
			ULONG errorLength = 0;
			CORRECTIVE_ACTION action = CORRECTIVE_ACTION_NONE;
			hr = spellingErrors->Next(&spellingError);
			if (SUCCEEDED(hr) &&
				spellingError &&
				SUCCEEDED(spellingError->get_StartIndex(&startIndex)) &&
				SUCCEEDED(spellingError->get_Length(&errorLength)) &&
				SUCCEEDED(spellingError->get_CorrectiveAction(&action)) &&
				(action == CORRECTIVE_ACTION_GET_SUGGESTIONS ||
				 action == CORRECTIVE_ACTION_REPLACE)) {
			} else {
				return true;
			}
		}
	}
	return false;
}

void WindowsSpellChecker::checkSpellingText(
	LPCWSTR text,
	MisspelledWords *misspelledWordRanges) {

	// The spellchecker marks words not from its own language as misspelled.
	// So we only return words that are marked
	// as misspelled in all spellcheckers.
	MisspelledWords misspelledWords;

	constexpr auto isActionGood = [](auto action) {
		return action == CORRECTIVE_ACTION_GET_SUGGESTIONS
			|| action == CORRECTIVE_ACTION_REPLACE;
	};

	for (const auto &[langTag, spellchecker] : _spellcheckerMap) {
		ComPtr<IEnumSpellingError> spellingErrors;

		HRESULT hr = IsPersianLanguage(langTag)
			? spellchecker->Check(text, &spellingErrors)
			: spellchecker->ComprehensiveCheck(text, &spellingErrors);
		if (!(SUCCEEDED(hr) && spellingErrors)) {
			continue;
		}

		MisspelledWords tempMisspelled;
		ComPtr<ISpellingError> spellingError;
		for (; hr == S_OK; hr = spellingErrors->Next(&spellingError)) {
			ULONG startIndex = 0;
			ULONG errorLength = 0;
			CORRECTIVE_ACTION action = CORRECTIVE_ACTION_NONE;

			if (!(SUCCEEDED(hr)
				&& spellingError
				&& SUCCEEDED(spellingError->get_StartIndex(&startIndex))
				&& SUCCEEDED(spellingError->get_Length(&errorLength))
				&& SUCCEEDED(spellingError->get_CorrectiveAction(&action))
				&& isActionGood(action))) {
				continue;
			}
			const auto word = std::pair((int) startIndex, (int) errorLength);
			if (misspelledWords.empty()
				|| ranges::contains(misspelledWords, word)) {
				tempMisspelled.push_back(std::move(word));
			}
		}
		// If the tempMisspelled vector is empty at least once,
		// it means that the all words will be correct in the end
		// and it makes no sense to check other languages.
		if (tempMisspelled.empty()) {
			return;
		}
		misspelledWords = std::move(tempMisspelled);
	}
	*misspelledWordRanges = misspelledWords;
}

void WindowsSpellChecker::addWord(LPCWSTR word) {
	for (const auto &[_, spellchecker] : _spellcheckerMap) {
		spellchecker->Add(word);
	}
}

void WindowsSpellChecker::removeWord(LPCWSTR word) {
	for (const auto &[_, spellchecker] : _spellcheckerMap) {
		ComPtr<ISpellChecker2> spellchecker2;
		spellchecker->QueryInterface(IID_PPV_ARGS(&spellchecker2));
		if (spellchecker2) {
			spellchecker2->Remove(word);
		}
	}
}

void WindowsSpellChecker::ignoreWord(LPCWSTR word) {
	for (const auto &[_, spellchecker] : _spellcheckerMap) {
		spellchecker->Ignore(word);
	}
}

std::vector<QString> WindowsSpellChecker::systemLanguages() {
	return ranges::views::keys(_spellcheckerMap) | ranges::to_vector;
}

////// End of WindowsSpellChecker class.

WindowsSpellChecker &SharedSpellChecker() {
	static auto spellchecker = WindowsSpellChecker();
	return spellchecker;
}

} // namespace

// TODO: Add a better work with the Threading Models.
// All COM objects should be created asynchronously
// if we want to work with them asynchronously.
// Some calls can be made in the main thread before spellchecking
// (e.g. KnownLanguages), so we have to init it asynchronously first.
void Init() {
	if (IsSystemSpellchecker()) {
		crl::async(SharedSpellChecker);
	}
}

bool IsSystemSpellchecker() {
	// Windows 7 does not support spellchecking.
	// https://docs.microsoft.com/en-us/windows/win32/api/spellcheck/nn-spellcheck-ispellchecker
	return IsWindows8OrGreater();
}

std::vector<QString> ActiveLanguages() {
	if (IsSystemSpellchecker()) {
		return SharedSpellChecker().systemLanguages();
	}
	return ThirdParty::ActiveLanguages();
}

bool CheckSpelling(const QString &wordToCheck) {
	if (!IsSystemSpellchecker()) {
		return ThirdParty::CheckSpelling(wordToCheck);
	}
	return SharedSpellChecker().checkSpelling(Q2WString(wordToCheck));
}

void FillSuggestionList(
	const QString &wrongWord,
	std::vector<QString> *optionalSuggestions) {
	if (IsSystemSpellchecker()) {
		SharedSpellChecker().fillSuggestionList(
			Q2WString(wrongWord),
			optionalSuggestions);
		return;
	}
	ThirdParty::FillSuggestionList(
		wrongWord,
		optionalSuggestions);
}

void AddWord(const QString &word) {
	if (IsSystemSpellchecker()) {
		SharedSpellChecker().addWord(Q2WString(word));
	} else {
		ThirdParty::AddWord(word);
	}
}

void RemoveWord(const QString &word) {
	if (IsSystemSpellchecker()) {
		SharedSpellChecker().removeWord(Q2WString(word));
	} else {
		ThirdParty::RemoveWord(word);
	}
}

void IgnoreWord(const QString &word) {
	if (IsSystemSpellchecker()) {
		SharedSpellChecker().ignoreWord(Q2WString(word));
	} else {
		ThirdParty::IgnoreWord(word);
	}
}

bool IsWordInDictionary(const QString &wordToCheck) {
	if (IsSystemSpellchecker()) {
		// ISpellChecker can't check if a word is in the dictionary.
		return false;
	}
	return ThirdParty::IsWordInDictionary(wordToCheck);
}

void UpdateLanguages(std::vector<int> languages) {
	if (!IsSystemSpellchecker()) {
		ThirdParty::UpdateLanguages(languages);
		return;
	}
	crl::async([=] {
		const auto result = ActiveLanguages();
		crl::on_main([=] {
			::Spellchecker::UpdateSupportedScripts(result);
		});
	});
}

void CheckSpellingText(
	const QString &text,
	MisspelledWords *misspelledWords) {
	if (IsSystemSpellchecker()) {
		SharedSpellChecker().checkSpellingText(
			Q2WString(text),
			misspelledWords);
		return;
	}
	ThirdParty::CheckSpellingText(text, misspelledWords);
}


} // namespace Platform::Spellchecker
