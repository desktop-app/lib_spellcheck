// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "spellcheck/platform/mac/language_mac.h"

#include "base/platform/mac/base_utilities_mac.h"

#import <NaturalLanguage/NLLanguageRecognizer.h>

using Platform::Q2NSString;
using Platform::NS2QString;

namespace Platform::Language {

LanguageId Recognize(QStringView text) {
	if (@available(macOS 10.14, *)) {
		constexpr auto kMaxHypotheses = 3;
		static thread_local auto r = [] {
			return [[NLLanguageRecognizer alloc] init];
		}();

		[r processString:Q2NSString(text)];
		const auto hypotheses =
			[r languageHypothesesWithMaximum:kMaxHypotheses];
		[r reset];
		auto maxProbability = 0.;
		auto language = NLLanguage(nil);
		for (NLLanguage lang in hypotheses) {
			const auto probability = [hypotheses[lang] floatValue];
			if (probability > maxProbability) {
				maxProbability = probability;
				language = lang;
			}
		}
		if (language) {
			return { QLocale(NS2QString(language)).language() };
		}
	}

	return {};
}

} // namespace Platform::Language
