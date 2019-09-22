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
, _coldSpellcheckingTimer([=] { checkChangedText(); })
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

	checkCurrentText();
}

void SpellingHighlighter::contentsChange(int pos, int removed, int added) {
	if (document()->toPlainText().isEmpty()) {
		_cachedRanges.clear();
		return;
	}

	// Skip the all words to the left of the cursor.
	auto &&filteredRanges = (
		_cachedRanges
	) | ranges::view::filter([&](const auto &range) {
		return range.first + range.second > pos;
	});

	// Move the all words to the right of the cursor.
	ranges::for_each(filteredRanges, [&](auto &range) {
		if (!IsPositionInsideWord(pos, range)) {
			range.first += added - removed;
		}
	});

	const auto wordUnderPos = getWordUnderPosition(pos);
	const auto rectUnderPos = QRect(
		wordUnderPos.first,
		0,
		wordUnderPos.second,
		1);

	_cachedRanges = (
		_cachedRanges
	) | ranges::view::filter([&](const auto &range) {
		const auto word = QRect(range.first, 0, range.second, 1);
		const auto selection = QRect(pos, 0, removed, 1);
		return !(word.intersects(selection) || word.intersects(rectUnderPos));
	}) | ranges::to_vector;

	rehighlight();

	_addedSymbols += added;
	_removedSymbols += removed;
	_lastPosition = pos;

	const auto isLetterOrNumber = (added == 1
		&& document()->toPlainText().midRef(
			pos,
			added).at(0).isLetterOrNumber());

	if ((removed == 1) || isLetterOrNumber) {
		if (_coldSpellcheckingTimer.isActive()) {
			_coldSpellcheckingTimer.cancel();
		}
		_coldSpellcheckingTimer.callOnce(kColdSpellcheckingTimeout);
	} else {
		checkChangedText();
	}
}

void SpellingHighlighter::checkChangedText() {
	const auto pos = _lastPosition;
	const auto added = _addedSymbols;
	const auto removed = _removedSymbols;

	const auto findWord = [&](int position) {
		return ranges::find_if(_cachedRanges, [&](MisspelledWord w) {
			return w.first >= position;
		});
	};

	const auto checkAndAddWordUnderCursos = [&](int position) {
		const auto w = getWordUnderPosition(position);
		if (!checkSingleWord(w)) {
			ranges::insert(_cachedRanges, findWord(w.first), std::move(w));
		}
	};

	if (removed > 0) {
		checkAndAddWordUnderCursos(pos);
		rehighlight();
	}

	if (added > 0) {

		// Remove the all words that was in the selection.
		_cachedRanges = (
			_cachedRanges
		) | ranges::view::filter([&](const auto &range) {
			const auto word = QRect(range.first, 0, range.second, 1);
			const auto selection = QRect(pos, 0, 1, 1);
			return !word.intersects(selection);
		}) | ranges::to_vector;

		const auto newBeginSelection = getWordUnderPosition(pos).first;
		const auto endWord = getWordUnderPosition(pos + added);

		const auto addedText = document()->toPlainText().mid(
			newBeginSelection,
			endWord.first + endWord.second - newBeginSelection);


		const auto weak = make_weak(this);
		crl::async([=, text = std::move(addedText)]() mutable {
			MisspelledWords misspelledWordRanges;
			Platform::Spellchecker::CheckSpellingText(std::move(text), &misspelledWordRanges);
			crl::on_main(weak, [=, ranges = std::move(misspelledWordRanges)]() mutable {
				if (!ranges.empty()) {
					ranges::for_each(ranges, [&](auto &&range) {
						range.first += newBeginSelection;
					});
					ranges::insert(_cachedRanges, findWord(newBeginSelection), std::move(ranges));
					// _cachedRanges = std::move(ranges);
				}
				rehighlight();
			});
		});
	}


	_lastPosition = 0;
	_removedSymbols = 0;
	_addedSymbols = 0;
}

void SpellingHighlighter::checkCurrentText() {
	if (const auto text = document()->toPlainText(); !text.isEmpty()) {
		invokeCheck(text);
	}
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

bool SpellingHighlighter::checkSingleWord(const MisspelledWord &range) {
	const auto w = document()->toPlainText().mid(range.first, range.second);
	return _spellCheckerHelper->checkSingleWord(std::move(w));
}

QString SpellingHighlighter::getTagFromRange(int begin, int length) {
	_cursor.setPosition(begin);
	_cursor.setPosition(begin + length, QTextCursor::KeepAnchor);
	return _cursor.charFormat().property(kTagProperty).toString();
}

MisspelledWord SpellingHighlighter::getWordUnderPosition(int position) {
	_cursor.setPosition(position);
	_cursor.select(QTextCursor::WordUnderCursor);
	const auto start = _cursor.selectionStart();
	return std::make_pair(start, _cursor.selectionEnd() - start);
}

void SpellingHighlighter::highlightBlock(const QString &text) {
	if (_cachedRanges.empty()) {
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
			if (_addedSymbols + _removedSymbols + _lastPosition) {
				_coldSpellcheckingTimer.cancel();
				checkCurrentText();
			}
		}
	}
	return false;
}

} // namespace Spellchecker
