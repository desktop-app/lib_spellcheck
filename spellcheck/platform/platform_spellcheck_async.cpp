// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "spellcheck/platform/platform_spellcheck.h"

#ifdef SPELLCHECK_HAVE_HUNSPELL
#include "spellcheck/third_party/hunspell_controller.h"
#endif // SPELLCHECK_HAVE_HUNSPELL

#include <crl/crl_async.h>
#include <crl/crl_on_main.h>

namespace Platform::Spellchecker {
namespace {

[[nodiscard]] bool UseThirdParty() {
#ifdef SPELLCHECK_HAVE_HUNSPELL
	return !IsSystemSpellchecker();
#else // SPELLCHECK_HAVE_HUNSPELL
	return false;
#endif // !SPELLCHECK_HAVE_HUNSPELL
}

} // namespace

void CheckSpelling(QString word, FnMut<void(bool correct)> callback) {
#ifdef SPELLCHECK_HAVE_HUNSPELL
	if (UseThirdParty()) {
		ThirdParty::CheckSpelling(std::move(word), std::move(callback));
		return;
	}
#endif // SPELLCHECK_HAVE_HUNSPELL
	crl::async([word = std::move(word), callback = std::move(callback)]() mutable {
		const auto correct = CheckSpelling(word);
		crl::on_main([correct, callback = std::move(callback)]() mutable {
			callback(correct);
		});
	});
}

void CheckSpellingText(
		QString text,
		FnMut<void(MisspelledWords &&)> callback) {
#ifdef SPELLCHECK_HAVE_HUNSPELL
	if (UseThirdParty()) {
		ThirdParty::CheckSpellingText(std::move(text), std::move(callback));
		return;
	}
#endif // SPELLCHECK_HAVE_HUNSPELL
	crl::async([text = std::move(text), callback = std::move(callback)]() mutable {
		auto misspelled = MisspelledWords();
		CheckSpellingText(text, &misspelled);
		crl::on_main([
			misspelled = std::move(misspelled),
			callback = std::move(callback)]() mutable {
			callback(std::move(misspelled));
		});
	});
}

void LookupWord(
		QString word,
		FnMut<void(bool correct, std::vector<QString> &&suggestions)> callback) {
#ifdef SPELLCHECK_HAVE_HUNSPELL
	if (UseThirdParty()) {
		ThirdParty::LookupWord(std::move(word), std::move(callback));
		return;
	}
#endif // SPELLCHECK_HAVE_HUNSPELL
	crl::async([word = std::move(word), callback = std::move(callback)]() mutable {
		const auto correct = CheckSpelling(word);
		auto suggestions = std::vector<QString>();
		if (!correct) {
			FillSuggestionList(word, &suggestions);
		}
		crl::on_main([
			correct,
			suggestions = std::move(suggestions),
			callback = std::move(callback)]() mutable {
			callback(correct, std::move(suggestions));
		});
	});
}

} // namespace Platform::Spellchecker
