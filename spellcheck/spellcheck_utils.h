// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "spellcheck/spellcheck_types.h"

namespace Spellchecker {

bool IsWordSkippable(const QStringRef &word);

MisspelledWords RangesFromText(
	const QString &text,
	Fn<bool(const QString &word)> filterCallback);

} // namespace Spellchecker