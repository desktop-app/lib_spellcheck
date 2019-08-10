// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//

using RangeWord = std::pair<int, int>;

class SpellCheckerHelper : public QObject {

public:
	SpellCheckerHelper() = default;

	void requestTextCheck(
		QTextDocument &doc,
		std::vector<RangeWord> *misspelledWords,
		std::vector<RangeWord> *correctWords);
	bool isWordSkippable(const QStringRef &word);

	void fillSuggestionList(const QString &wrongWord,
		std::vector<QString>* optionalSuggestions);

	bool checkSingleWord(const QString &word);
	bool isWordInDictionary(const QString &word);

	void addWord(const QString &word);
	void removeWord(const QString &word);
	void ignoreWord(const QString &word);

};