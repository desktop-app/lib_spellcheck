// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//

#include "spellcheck/spellchecker/spelling_highlighter.h"

#include "lang/lang_keys.h"
#include "spellcheck/spellchecker/spellchecker_locale_script.h"
#include "styles/palette.h"
#include "ui/ui_utility.h"

#include <QTextBoundaryFinder>

namespace Spellchecker {

namespace {

constexpr auto kTagProperty = QTextFormat::UserProperty + 4;

constexpr auto kColdSpellcheckingTimeout = crl::time(1000);

const auto kKeysToCheck = {
	Qt::Key_Up,
	Qt::Key_Down,
	Qt::Key_Left,
	Qt::Key_Right,
	Qt::Key_PageUp,
	Qt::Key_PageDown,
	Qt::Key_Home,
	Qt::Key_End,
};

MisspelledWords GetRanges(const QString &text) {
	MisspelledWords ranges;

	if (text.isEmpty()) {
		return ranges;
	}

	auto finder = QTextBoundaryFinder(QTextBoundaryFinder::Word, text);

	const auto isEnd = [&] {
		return (finder.toNextBoundary() == -1);
	};

	while (finder.position() < text.length()) {
		if (!(finder.boundaryReasons().testFlag(
				QTextBoundaryFinder::StartOfItem))) {
			if (isEnd()) {
				break;
			}
			continue;
		}

		const auto start = finder.position();
		const auto end = finder.toNextBoundary();
		if (end == -1) {
			break;
		}
		const auto length = end - start;
		if (length < 1) {
			continue;
		}
		ranges.push_back(std::make_pair(start, length));

		if (isEnd()) {
			break;
		}
	}
	return ranges;
}

} // namespace

SpellingHighlighter::SpellingHighlighter(
	QTextEdit *textEdit,
	UncheckableCallback callback)
: QSyntaxHighlighter(textEdit->document())
, _cursor(QTextCursor(document()->docHandle(), 0))
, _spellCheckerHelper(std::make_unique<SpellCheckerHelper>())
, _unspellcheckableCallback(std::move(callback))
, _coldSpellcheckingTimer([=] { checkCurrentText(); })
, _textEdit(textEdit) {

	_textEdit->installEventFilter(this);
	_textEdit->viewport()->installEventFilter(this);

	_cachedRanges = MisspelledWords();

#ifdef Q_OS_MAC
	misspelledFormat.setUnderlineStyle(QTextCharFormat::DotLine);
#else
	misspelledFormat.setUnderlineStyle(QTextCharFormat::WaveUnderline);
#endif
	misspelledFormat.setUnderlineColor(st::spellUnderline->c);

	connect(
		document(),
		&QTextDocument::contentsChange,
		[=](int p, int r, int a) { contentsChange(p, r, a); });
}

void SpellingHighlighter::contentsChange(int pos, int removed, int added) {
	if ((removed == 1) && (document()->toPlainText().length() != pos)) {
		const auto handleSymbolRemoving = [&](auto &&range) {
			if (range.first < pos) {
				return;
			}
			// Reduce the length of the word with which the symbol has been
			// removed and reduce the length of all words that stand after it.
			(pos > range.first && pos < range.first + range.second
				? range.second
				: range.first)--;
		};
		ranges::for_each(_cachedRanges, handleSymbolRemoving);
		rehighlight();
	} else {
		if (_coldSpellcheckingTimer.isActive()) {
			_coldSpellcheckingTimer.cancel();
		}
		_coldSpellcheckingTimer.callOnce(kColdSpellcheckingTimeout);
	}
}

void SpellingHighlighter::checkCurrentText() {
	invokeCheck(document()->toPlainText());
}

void SpellingHighlighter::invokeCheck(const QString &text) {
	const auto weak = make_weak(this);
	crl::async([=, text = std::move(text)]() mutable {
		MisspelledWords misspelledWordRanges;
		Platform::Spellchecker::CheckSpellingText(text, &misspelledWordRanges);
		if (!misspelledWordRanges.empty()) {
			crl::on_main(weak, [=, ranges = std::move(misspelledWordRanges)]() mutable {
				_cachedRanges = std::move(ranges);
				rehighlight();
			});
		}
	});
}

QString SpellingHighlighter::getTagFromRange(int begin, int length) {
	_cursor.setPosition(begin);
	_cursor.setPosition(begin + length, QTextCursor::KeepAnchor);
	return _cursor.charFormat().property(kTagProperty).toString();
}

void SpellingHighlighter::highlightBlock(const QString &text) {
	if (_cachedRanges.empty()) {
		invokeCheck(std::move(text));
		return;
	}

	for (const auto &[position, length] : _cachedRanges) {
		if (_unspellcheckableCallback(getTagFromRange(position, length))) {
			continue;
		}
		setFormat(position, length, misspelledFormat);
	}

	setCurrentBlockState(0);
}

bool SpellingHighlighter::eventFilter(QObject *o, QEvent *e) {
	if (_textEdit && (e->type() == QEvent::KeyPress)) {
		const auto *k = static_cast<QKeyEvent *>(e);

		if (ranges::find(kKeysToCheck, k->key()) != kKeysToCheck.end()) {
			checkCurrentText();
		}
	}
	return false;
}

} // namespace Spellchecker
