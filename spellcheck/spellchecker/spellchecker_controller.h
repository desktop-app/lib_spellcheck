// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//

#include "platform/platform_spellcheck.h"

namespace Spellchecker {

using MisspelledWords = Platform::Spellchecker::MisspelledWords;

class Controller {

public:
	Controller() = default;

	void requestTextCheck(
		QTextDocument &doc,
		MisspelledWords *misspelledWords,
		MisspelledWords *correctWords);
	bool isWordSkippable(const QStringRef &word);

	void fillSuggestionList(const QString &wrongWord,
		std::vector<QString> *optionalSuggestions);

	bool checkSingleWord(const QString &word);
	bool isWordInDictionary(const QString &word);

	void addWord(const QString &word);
	void removeWord(const QString &word);
	void ignoreWord(const QString &word);

};

} // namespace Spellchecker