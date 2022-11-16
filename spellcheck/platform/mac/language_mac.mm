// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "spellcheck/platform/mac/language_mac.h"

namespace Platform::Language {

RecognitionResult Recognize(QStringView text) {
	return { .unknown = true };
}

} // namespace Platform::Language
