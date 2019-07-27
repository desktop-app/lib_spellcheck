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
NSSpellChecker* SharedSpellChecker() {
	@try {
		return [NSSpellChecker sharedSpellChecker];
	} @catch (id exception) {
		return nil;
	}
}

} // namespace

namespace Platform {
namespace Spellcheck {

bool CheckSpelling(const QString &wordToCheck) {
	// -[NSSpellChecker checkSpellingOfString] returns an NSRange that
	// we can look at to determine if a word is misspelled.
	NSRange spellRange = {0,0};

	// Check the spelling, starting at the beginning of the word.
	spellRange = [SharedSpellChecker()
					  checkSpellingOfString:Q2NSString(wordToCheck)
					  startingAt:0
					  language:nil
					  wrap:false
					  inSpellDocumentWithTag:tag
					  wordCount:nil];

	// If the length of the misspelled word == 0,
	// then there is no misspelled word.
	return (spellRange.length == 0);
}

void FillSuggestionList(
	const QString &wrongWord,
	std::vector<QString> *optionalSuggestions) {

	NSString* NSWrongWord = Q2NSString(wrongWord);
	NSSpellChecker* checker = SharedSpellChecker();
	NSRange wordRange = NSMakeRange(0, wrongWord.length());
	NSArray* guesses = [checker guessesForWordRange:wordRange
		inString:NSWrongWord
		language:nil
		inSpellDocumentWithTag:0];

	int i = 0;
	for (NSString* guess in guesses) {
		optionalSuggestions->push_back(NS2QString(guess));
		if (++i >= kMaxSuggestions) {
			break;
		}
	}
}

} // namespace Spellcheck
} // namespace Platform
