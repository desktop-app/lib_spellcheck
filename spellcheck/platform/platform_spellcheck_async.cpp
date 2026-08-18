// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "spellcheck/platform/platform_spellcheck.h"

#include <crl/crl_async.h>
#include <crl/crl_on_main.h>

namespace Platform::Spellchecker {

// A generic adapter over the synchronous backends: the work runs on the

void CheckSpelling(QString word, FnMut<void(bool correct)> callback) {
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
