// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "spellcheck/third_party/language_cld3.h"

#include "nnet_language_identifier.h"

namespace Platform::Language {

RecognitionResult Recognize(QStringView text) {
	using chrome_lang_id::NNetLanguageIdentifier;

	constexpr auto kMinNumBytes = 0;
	constexpr auto kMaxNumBytes = 1000;
	constexpr auto kMaxLangs = 3;

	auto lang_id = NNetLanguageIdentifier(kMinNumBytes, kMaxNumBytes);
	const auto string = std::string(text.toUtf8().constData());
	const auto results = lang_id.FindTopNMostFreqLangs(string, kMaxLangs);

	auto maxProbability = 0.;
	auto final = NNetLanguageIdentifier::Result();
	for (const auto &result : results) {
		if (result.probability > maxProbability) {
			maxProbability = result.probability;
			final = result;
		}
	}
	if (final.language == NNetLanguageIdentifier::kUnknown) {
		return { .unknown = true };
	} else {
		return { .locale = QLocale(QString::fromStdString(final.language)) };
	}
}

} // namespace Platform::Language