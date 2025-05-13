// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "spellcheck/third_party/language_cld3.h"

#include <nnet_language_identifier.h>

namespace Platform::Language {

LanguageId Recognize(QStringView text) {
	using chrome_lang_id::NNetLanguageIdentifier;

	constexpr auto kMinNumBytes = 0;
	constexpr auto kMaxNumBytes = 1000;
	constexpr auto kMaxLangs = 3;

	auto lang_id = NNetLanguageIdentifier(kMinNumBytes, kMaxNumBytes);
	const auto string = text.toUtf8().toStdString();
	const auto results = lang_id.FindTopNMostFreqLangs(string, kMaxLangs);

	auto maxRatio = 0.;
	auto final = NNetLanguageIdentifier::Result();
	for (const auto &result : results) {
		const auto ratio = result.probability * result.proportion;
		if (ratio > maxRatio) {
			maxRatio = ratio;
			final = result;
		}
	}
	if (final.language == NNetLanguageIdentifier::kUnknown) {
		return {};
	}
	return { QLocale(QString::fromStdString(final.language)).language() };
}

} // namespace Platform::Language
