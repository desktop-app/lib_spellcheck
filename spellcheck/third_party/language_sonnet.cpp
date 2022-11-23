// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "spellcheck/third_party/language_sonnet.h"

#include <guesslanguage.h>

inline void InitResources() {
	Q_INIT_RESOURCE(trigrams);
}

namespace Platform::Language {

RecognitionResult Recognize(QStringView text) {
	[[maybe_unused]] static const auto Inited = [] {
		InitResources();
		return true;
	}();

	const auto language = Sonnet::GuessLanguage().identify(text.toString());
	if (!language.isEmpty()) {
		return { .locale = QLocale(language) };
	}
	return { .unknown = true };
}

} // namespace Platform::Language
