// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//

#include "chat_helpers/spellchecker_helper.h"

#include "base/timer.h"
#include "spellcheck/platform/platform_spellcheck.h"

#include <QtGui/QSyntaxHighlighter>
#include <QtWidgets/QTextEdit>

#include <rpl/event_stream.h>

namespace Spellchecker {

using MisspelledWords = Platform::Spellchecker::MisspelledWords;
using MisspelledWord = Platform::Spellchecker::MisspelledWord;
using UncheckableCallback = Fn<bool(const QString &tag)>;

class SpellingHighlighter final : public QSyntaxHighlighter {
	Q_OBJECT

public:
	SpellingHighlighter(QTextEdit *textEdit, UncheckableCallback callback);
	~SpellingHighlighter() override {
	}

protected:
	void highlightBlock(const QString &text) override;
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	void contentsChange(int pos, int removed, int added);
	void checkText(const QString &text);

	void invokeCheck(const QString &text);

	void checkCurrentText();
	void checkChangedText();
	bool checkSingleWord(const MisspelledWord &range);

	QString getTagFromRange(int begin, int length);
	MisspelledWord getWordUnderPosition(int position);

	QTextCharFormat misspelledFormat;

	QTextCursor _cursor;
	std::unique_ptr<SpellCheckerHelper> _spellCheckerHelper;

	UncheckableCallback _unspellcheckableCallback;

	MisspelledWords _cachedRanges;

	int _addedSymbols = 0;
	int _removedSymbols = 0;
	int _lastPosition = 0;

	base::Timer _coldSpellcheckingTimer;

	QTextEdit *_textEdit;

};

} // namespace Spellchecker