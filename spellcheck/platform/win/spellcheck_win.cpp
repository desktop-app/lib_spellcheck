// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "spellcheck/platform/win/spellcheck_win.h"

#include <wrl/client.h>
#include <spellcheck.h>

#include <QtCore/QLocale>

#include "base/platform/base_platform_info.h"

using namespace Microsoft::WRL;

namespace Platform {
namespace Spellchecker {


namespace {

inline LPCWSTR Q2WString(QString string) {
	return (LPCWSTR)string.utf16();
}

// WindowsSpellChecker class is used to store all the COM objects and
// control their lifetime. The class also provides wrappers for
// ISpellCheckerFactory and ISpellChecker APIs. All COM calls are on the
// background thread.
class WindowsSpellChecker {

public:

	WindowsSpellChecker();
	void createFactory();
	void createSpellChecker(const QString& lang);

	bool isLanguageSupported(const QString& lang);

	bool checkSpelling(const QString& word);
	void fillSuggestionList(
		const QString &wrongWord,
		std::vector<QString> *optionalSuggestions);

private:

	ComPtr<ISpellCheckerFactory> _spellcheckerFactory;
	std::map<QString, ComPtr<ISpellChecker>> _spellcheckerMap;

};

WindowsSpellChecker::WindowsSpellChecker() {
	createFactory();
}

void WindowsSpellChecker::createFactory() {
	if (FAILED(CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr,
		(CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER),
		IID_PPV_ARGS(&_spellcheckerFactory)))) {
		_spellcheckerFactory = nullptr;
	}
}

void WindowsSpellChecker::createSpellChecker(const QString& lang) {
	if (!_spellcheckerFactory) {
		return;
	}
	if (_spellcheckerMap.find(lang) != _spellcheckerMap.end()) {
		return;
	}

	if (isLanguageSupported(lang)) {
		ComPtr<ISpellChecker> spellchecker;
		HRESULT hr = _spellcheckerFactory->CreateSpellChecker(
		Q2WString(lang), &spellchecker);
		if (SUCCEEDED(hr)) {
			_spellcheckerMap.insert({lang, spellchecker});
		}
	}
}

bool WindowsSpellChecker::isLanguageSupported(const QString& lang) {
	if (!_spellcheckerFactory) {
		return false;
	}

	BOOL isSupported = (BOOL)false;
	HRESULT hr = _spellcheckerFactory->IsSupported(
		Q2WString(lang),
		&isSupported);
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
				return false;
			}
		}
	}
	return true;
}

////// End of WindowsSpellChecker class.

std::unique_ptr<WindowsSpellChecker>& SharedSpellChecker() {
	static auto spellchecker = std::make_unique<WindowsSpellChecker>();
	spellchecker->createSpellChecker(QString("en-US"));
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

} // namespace Spellchecker
} // namespace Platform
