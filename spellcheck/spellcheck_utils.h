// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "spellcheck/spellcheck_types.h"

#include <QLocale>

namespace Spellchecker {

constexpr auto kMaxWordSize = 99;

QChar::Script LocaleToScriptCode(const QString &locale);
QChar::Script WordScript(QStringView word);
bool IsWordSkippable(
	QStringView word,
	bool checkSupportedScripts = true);

[[nodiscard]] QString NormalizeApostrophes(const QString &word);

MisspelledWords RangesFromText(
	const QString &text,
	Fn<bool(const QString &word)> filterCallback);

// The word the position is inside of, or the one to the left of it when the
// position is between two words - which is what QTextCursor::WordUnderCursor
// answers. Words are told apart by Ui::Text::IsWordSeparator(), so that a word
// with an apostrophe in it stays one word here as well. The text is read a
// character at a time, so that a block of a document needs no copy.
[[nodiscard]] MisspelledWord WordAtPosition(
	Fn<QChar(int)> at,
	int length,
	int position);

// For Linux and macOS, which use RangesFromText.
bool CheckSkipAndSpell(const QString &word);

QLocale LocaleFromLangId(int langId);

void UpdateSupportedScripts(std::vector<QString> languages);
rpl::producer<> SupportedScriptsChanged();

} // namespace Spellchecker
