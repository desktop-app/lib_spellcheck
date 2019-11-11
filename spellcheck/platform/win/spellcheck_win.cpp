// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "spellcheck/platform/win/spellcheck_win.h"

#include <wrl/client.h>
#include <spellcheck.h>

#include <QtCore/QDir>
#include <QtCore/QLocale>

#include "base/platform/base_platform_info.h"

using namespace Microsoft::WRL;

namespace Platform {
namespace Spellchecker {


namespace {

inline LPCWSTR Q2WString(QString string) {
	return (LPCWSTR)string.utf16();
}

inline auto SystemLanguages() {
	const auto appdata = qEnvironmentVariable("appdata");
	// Probably never gonna be empty.
	if (appdata.isEmpty()) {
		return QLocale::system().uiLanguages();
	}
	const auto dir = QDir(appdata + QString("\\Microsoft\\Spelling"));
	if (!dir.exists()) {
		return QLocale::system().uiLanguages();
	}
	return dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
}

// WindowsSpellChecker class is used to store all the COM objects and
// control their lifetime. The class also provides wrappers for
// ISpellCheckerFactory and ISpellChecker APIs. All COM calls are on the
// background thread.
class WindowsSpellChecker {

public:

	WindowsSpellChecker();
	void createFactory();
	bool isLanguageSupported(const LPCWSTR& lang);

	void addWord(const QString& word);
	void removeWord(const QString& word);
	void ignoreWord(const QString& word);
	bool checkSpelling(const QString& word);
	void fillSuggestionList(
		const QString &wrongWord,
		std::vector<QString> *optionalSuggestions);
	void checkSpellingText(
		const QString &text,
		MisspelledWords *misspelledWordRanges);

private:
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
			_spellcheckerMap.insert({lang, spellchecker});
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
	const QString &wrongWord,
	std::vector<QString> *optionalSuggestions) {
	auto i = 0;
	for (const auto &[_, spellchecker] : _spellcheckerMap) {
		ComPtr<IEnumString> suggestions;
		HRESULT hr = spellchecker->Suggest(
			Q2WString(wrongWord),
			&suggestions);

		while (true) {
			wchar_t* suggestion = nullptr;
			hr = suggestions->Next(1, &suggestion, nullptr);
			if (hr != S_OK) {
				break;
			}
			const auto guess =
				QString::fromWCharArray(suggestion, wcslen(suggestion));
			if (!guess.isEmpty()) {
				optionalSuggestions->push_back(guess);
				if (++i >= kMaxSuggestions) {
					return;
				}
			}
			CoTaskMemFree(suggestion);
		}
	}

}

bool WindowsSpellChecker::checkSpelling(const QString& word) {
	for (const auto &[_, spellchecker] : _spellcheckerMap) {
		ComPtr<IEnumSpellingError> spellingErrors;
		HRESULT hr = spellchecker->Check(Q2WString(word), &spellingErrors);

		if (SUCCEEDED(hr) && spellingErrors) {
			ComPtr<ISpellingError> spellingError;
			ULONG startIndex = 0;
			ULONG errorLength = 0;
			CORRECTIVE_ACTION action = CORRECTIVE_ACTION_NONE;
			hr = spellingErrors->Next(&spellingError);
			if (SUCCEEDED(hr) && spellingError &&
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
	const QString &text,
	MisspelledWords *misspelledWordRanges) {

	// The spellchecker marks words not from its own language as misspelled.
	// So we only return words that are marked
	// as misspelled in all spellcheckers.
	MisspelledWords misspelledWords;

	constexpr auto isActionGood = [](auto action) {
		return action == CORRECTIVE_ACTION_GET_SUGGESTIONS
			|| action == CORRECTIVE_ACTION_REPLACE;
	};

	for (const auto &[_, spellchecker] : _spellcheckerMap) {
		ComPtr<IEnumSpellingError> spellingErrors;

		HRESULT hr = spellchecker->ComprehensiveCheck(
			Q2WString(text),
			&spellingErrors);
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
			const auto word = std::make_pair(
				(int) startIndex,
				(int) errorLength);
			if (misspelledWords.empty()
				|| ranges::find_if(misspelledWords, [&](auto &range) {
					return (word == range);
				}) != misspelledWords.end()) {
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
	misspelledWordRanges->insert(
		misspelledWordRanges->end(),
		misspelledWords.begin(),
		misspelledWords.end());
}

void WindowsSpellChecker::addWord(const QString& word) {
	for (const auto &[_, spellchecker] : _spellcheckerMap) {
		spellchecker->Add(Q2WString(word));
	}
}

void WindowsSpellChecker::removeWord(const QString& word) {
	for (const auto &[_, spellchecker] : _spellcheckerMap) {
		ComPtr<ISpellChecker2> spellchecker2;
		spellchecker->QueryInterface(IID_PPV_ARGS(&spellchecker2));
		if (spellchecker2) {
			spellchecker2->Remove(Q2WString(word));
		}
	}
}

void WindowsSpellChecker::ignoreWord(const QString& word) {
	for (const auto &[_, spellchecker] : _spellcheckerMap) {
		spellchecker->Ignore(Q2WString(word));
	}
}

////// End of WindowsSpellChecker class.

std::unique_ptr<WindowsSpellChecker>& SharedSpellChecker() {
	static auto spellchecker = std::make_unique<WindowsSpellChecker>();
	return spellchecker;
}

} // namespace

bool CheckSpelling(const QString &wordToCheck) {
	// Windows 7 does not support spellchecking.
	// https://docs.microsoft.com/en-us/windows/win32/api/spellcheck/nn-spellcheck-ispellchecker
	if (!IsWindows8OrGreater()) {
		return true;
	}
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
	// ISpellChecker can't check if a word is in the dictionary.
	return false;
}

void CheckSpellingText(
	const QString &text,
	MisspelledWords *misspelledWordRanges) {
	SharedSpellChecker()->checkSpellingText(text, misspelledWordRanges);
}


} // namespace Spellchecker
} // namespace Platform
