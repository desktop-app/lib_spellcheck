// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "spellcheck/platform/mac/spellcheck_mac.h"

#include "base/platform/mac/base_utilities_mac.h"

#import <QuartzCore/QuartzCore.h>

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

namespace Platform {
namespace Spellchecker {

// Known Issue: Despite the explicitly defined parameter,
// the correctness of a single word depends on the rest of the text.
// For example, "testt testtttyy" - this string will be marked as correct.
// But at the same time "testtttyy" will be marked as misspelled word.

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
}

void FillSuggestionList(
	const QString &wrongWord,
	std::vector<QString> *optionalSuggestions) {

	NSString *NSWrongWord = Q2NSString(wrongWord);
	NSSpellChecker *checker = SharedSpellChecker();
	NSRange wordRange = NSMakeRange(0, wrongWord.length());
	NSArray *guesses = [checker guessesForWordRange:wordRange
		inString:NSWrongWord
		language:nil
		inSpellDocumentWithTag:0];

	int i = 0;
	for (NSString *guess in guesses) {
		optionalSuggestions->push_back(NS2QString(guess));
		if (++i >= kMaxSuggestions) {
			break;
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

} // namespace Spellchecker
} // namespace Platform
