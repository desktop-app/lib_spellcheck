// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "spellcheck/platform/mac/spellcheck_mac.h"

#include "base/platform/mac/base_utilities_mac.h"
#include "spellcheck/spellcheck_utils.h"

#import <QuartzCore/QuartzCore.h>

#include <QtCore/QLocale>

using Platform::Q2NSString;
using Platform::NS2QString;

namespace {

// +[NSSpellChecker sharedSpellChecker] can throw exceptions depending
// on the state of the pasteboard, or possibly as a result of
// third-party code (when setting up services entries).  The following
// receives nil if an exception is thrown, in which case
// spell-checking will not work, but it also will not crash the
// browser.
NSSpellChecker *SharedSpellChecker() {
	@try {
		return [NSSpellChecker sharedSpellChecker];
	} @catch (id exception) {
		return nil;
	}
}

} // namespace

namespace Platform::Spellchecker {

bool IsAvailable() {
	return true;
}

void KnownLanguages(std::vector<QString> *langCodes) {
	QStringList result = QLocale::system().uiLanguages();
	langCodes->assign(result.begin(), result.end());
}

bool CheckSpelling(const QString &wordToCheck) {
	const auto wordLength = wordToCheck.length();
	NSArray<NSTextCheckingResult *> *spellRanges =
		[SharedSpellChecker()
			checkString:Q2NSString(std::move(wordToCheck))
			range:NSMakeRange(0, wordLength)
			types:NSTextCheckingTypeSpelling
			options:nil
			inSpellDocumentWithTag:0
			orthography:nil
			wordCount:nil];
	// If the length of the misspelled word == 0,
	// then there is no misspelled word.
	return (spellRanges.count == 0);
}


// There's no need to check the language on the Mac.
void CheckSpellingText(
	const QString &text,
	MisspelledWords *misspelledWordRanges) {
// Probably never gonna be defined.
#ifdef SPELLCHECKER_MAC_AUTO_CHECK_TEXT

	NSArray<NSTextCheckingResult *> *spellRanges =
		[SharedSpellChecker()
			checkString:Q2NSString(text)
			range:NSMakeRange(0, text.length())
			types:NSTextCheckingTypeSpelling
			options:nil
			inSpellDocumentWithTag:0
			orthography:nil
			wordCount:nil];

	misspelledWordRanges->reserve(spellRanges.count);
	for (NSTextCheckingResult *result in spellRanges) {
		if (result.resultType != NSTextCheckingTypeSpelling) {
			continue;
		}
		misspelledWordRanges->push_back({
			result.range.location,
			result.range.length});
	}

#else
// Known Issue: Despite the explicitly defined parameter,
// the correctness of a single word depends on the rest of the text.
// For example, "testt testtttyy" - this string will be marked as correct.
// But at the same time "testtttyy" will be marked as misspelled word.

// So we have to manually split the text into words and check them separately.
	const auto words = ::Spellchecker::RangesFromText(text, [&](auto &word) {
		return CheckSpelling(std::move(word));
	});

	if (words.empty()) {
		return;
	}

	misspelledWordRanges->insert(
		misspelledWordRanges->end(),
		words.begin(),
		words.end());

#endif
}

void FillSuggestionList(
	const QString &wrongWord,
	std::vector<QString> *optionalSuggestions) {

	const auto wordRange = NSMakeRange(0, wrongWord.length());
	auto *nsWord = Q2NSString(wrongWord);
	const auto guesses = [&](auto *lang) {
		return [SharedSpellChecker() guessesForWordRange:wordRange
			inString:nsWord
			language:lang
			inSpellDocumentWithTag:0];
	};

	auto wordCounter = 0;
	const auto wordScript = ::Spellchecker::WordScript(wrongWord.midRef(0));
	optionalSuggestions->reserve(kMaxSuggestions);
	auto hasEnglish = false;

	// for (NSString *lang in [SharedSpellChecker() availableLanguages]) {
	for (const auto &lang : QLocale::system().uiLanguages()) {
		const auto isEn = lang.startsWith("en");
		if (hasEnglish && isEn) {
			continue;
		} else {
			hasEnglish = isEn;
		}
		if (wordScript != ::Spellchecker::LocaleToScriptCode(lang)) {
			continue;
		}

		for (NSString *guess in guesses(Q2NSString(lang))) {
			optionalSuggestions->push_back(NS2QString(guess));
			if (++wordCounter >= kMaxSuggestions) {
				return;
			}
		}
	}
}

void AddWord(const QString &word) {
	[SharedSpellChecker() learnWord:Q2NSString(word)];
}

void RemoveWord(const QString &word) {
	[SharedSpellChecker() unlearnWord:Q2NSString(word)];
}

void IgnoreWord(const QString &word) {
	[SharedSpellChecker() ignoreWord:Q2NSString(word)
		inSpellDocumentWithTag:0];
}

bool IsWordInDictionary(const QString &wordToCheck) {
	return [SharedSpellChecker() hasLearnedWord:Q2NSString(wordToCheck)];
}

} // namespace Platform::Spellchecker
