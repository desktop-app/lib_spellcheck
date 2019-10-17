// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//

#include "base/timer.h"
#include "spellcheck/platform/platform_spellcheck.h"

#include <QtGui/QSyntaxHighlighter>
#include <QtWidgets/QMenu>
#include <QtWidgets/QTextEdit>

#include <rpl/event_stream.h>

namespace Spellchecker {

using MisspelledWords = Platform::Spellchecker::MisspelledWords;
using MisspelledWord = Platform::Spellchecker::MisspelledWord;
using UncheckableCallback = Fn<bool(const QString &tag)>;

class SpellingHighlighter final : public QSyntaxHighlighter {
	Q_OBJECT

public:
	SpellingHighlighter(
		QTextEdit *textEdit,
		const std::initializer_list<const QString *> unspellcheckableTags,
		rpl::producer<bool> enabled,
		rpl::producer<std::tuple<int, int, int>> documentChanges);
	~SpellingHighlighter() override {
	}

	void contentsChange(int pos, int removed, int added);
	void checkCurrentText();
	bool enabled();

	// Windows system spellchecker forces us to perform spell operations
	// In another thread, so the word check and getting a list of suggestions
	// Are run asynchronously.
	// And then the context menu is filled in the main thread.
	void addSpellcheckerActions(
		not_null<QMenu*> menu,
		QTextCursor cursorForPosition,
		Fn<void()> showMenuCallback);

protected:
	void highlightBlock(const QString &text) override;
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	void setEnabled(bool enabled);
	void checkText(const QString &text);

	void invokeCheckText(
		const QString &text,
		Fn<void(const MisspelledWords &ranges)> callback,
		int rangesOffset = 0);

	void checkChangedText();
	bool checkSingleWord(const MisspelledWord &range);
	MisspelledWords filterSkippableWords(MisspelledWords &ranges);

	bool isTagUnspellcheckable(int begin, int length);
	MisspelledWord getWordUnderPosition(int position);
	QString getDocumentText();

	QTextCharFormat misspelledFormat;

	QTextCursor _cursor;

	UncheckableCallback _unspellcheckableCallback;

	MisspelledWords _cachedRanges;

	int _addedSymbols = 0;
	int _removedSymbols = 0;
	int _lastPosition = 0;
	bool _enabled = true;

	base::Timer _coldSpellcheckingTimer;

	const std::initializer_list<const QString *> _unspellcheckableTags;

	QTextEdit *_textEdit;

	rpl::lifetime _lifetime;

};

} // namespace Spellchecker