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
	const auto findWord = [&](int position) {
		return ranges::find_if(_cachedRanges, [&](MisspelledWord w) {
			return w.first >= position;
		});
	};

	const auto getWordUnderPosition = [&](int position) {
		_cursor.setPosition(position);
		_cursor.select(QTextCursor::WordUnderCursor);
		const auto start = _cursor.selectionStart();
		return std::make_pair(start, _cursor.selectionEnd() - start);
	};

	const auto checkAndAddWordUnderCursos = [&](int position) {
		const auto w = getWordUnderPosition(position);
		if (!checkSingleWord(w)) {
			_cachedRanges.insert(findWord(w.first), std::move(w));
		}
	};

	const auto isPositionInsideWord = [](auto p, auto &&range) {
		return p >= range.first && p < range.first + range.second;
	};

	bool isEndOfDoc = (document()->toPlainText().length() == pos);
	auto indexOfRange = _cachedRanges.begin() - 1;
	Fn<void()> callbackOnFinish;
	auto isWordUnderCursorMisspelled = false;
	if ((removed > 0) && !isEndOfDoc) {
		// Remove the all words that was in the selection.
		_cachedRanges = (
			_cachedRanges
		) | ranges::view::filter([&](const auto &range) {
			const auto word = QRect(range.first, 0, range.second, 1);
			const auto selection = QRect(pos, 0, removed, 1);
			return !word.intersects(selection);
		}) | ranges::to_vector;

		// Skip the all words to the left of the cursor.
		auto &&filteredRanges = (
			_cachedRanges
		) | ranges::view::filter([&](const auto &range) {
			return range.first + range.second > pos;
		});

		// Move the all words to the right of the cursor.
		ranges::for_each(filteredRanges, [&](auto &&range) {
			if (!isPositionInsideWord(pos, range)) {
				range.first -= removed;
			}
		});

		checkAndAddWordUnderCursos(pos);
		rehighlight();
	} else if ((added == 1) && !isEndOfDoc) {
		const auto addedSymbol =
			document()->toPlainText().midRef(pos, added).at(0);

		const auto handleSymbolInsertion = [&](auto &&range) {
			indexOfRange++;
			const auto wordEnd = range.first + range.second;
			if (wordEnd < pos) {
				return;
			}
			if (wordEnd > pos && range.first < pos) {
				// Cursor is inside of the word.
				isWordUnderCursorMisspelled = true;
				const auto w = getWordUnderPosition(pos + added);
				if (range.first == w.first &&
					wordEnd + 1 == w.first + w.second) {
					// The word under the cursor has increased.
					range.second++;

					if (checkSingleWord(range)) {
						callbackOnFinish = [=] {
							_cachedRanges.erase(indexOfRange);
						};
					}
				} else {
					// The word under the cursor is separated into two words.
					const auto firstWordLen = pos - range.first;
					const auto secondWordLen = range.second - firstWordLen;

					const auto firstWordPos = range.first;
					const auto secondWordPos = range.first + firstWordLen + 1;

					// 2 correct - erase the current range.
					// 2 misspelled - add a new range
					// and update the current range.
					// 1 correct and 1 misspelled - update the current range
					// with the misspelled word.

					// We should add a new range after the current word.
					const auto firstWord = std::make_pair(
						firstWordPos,
						firstWordLen);
					const auto secondWord = std::make_pair(
						secondWordPos,
						secondWordLen);

					const auto isFirstCorrect = checkSingleWord(firstWord);
					const auto isSecondCorrect = checkSingleWord(secondWord);

					if (isFirstCorrect && isSecondCorrect) {
						callbackOnFinish = [=] {
							_cachedRanges.erase(indexOfRange);
						};
					} else if (!isFirstCorrect && !isSecondCorrect) {
						if (indexOfRange != _cachedRanges.begin()) {
							callbackOnFinish = [=] {
								_cachedRanges.insert(
									indexOfRange + 1,
									std::move(secondWord));
							};
						}
						range = std::move(firstWord);
					} else if (isFirstCorrect && !isSecondCorrect) {
						range = std::move(secondWord);
					} else if (!isFirstCorrect && isSecondCorrect) {
						range = std::move(firstWord);
					}
				}
			} else if (range.first >= pos) {
				range.first++;
			}
		};
		ranges::for_each(_cachedRanges, handleSymbolInsertion);

		if (!isWordUnderCursorMisspelled) {
			//// If the word under cursor was correct.
			// But it was separated into two words.
			if (!addedSymbol.isLetter()) {
				checkAndAddWordUnderCursos(pos);
			}
			// But it was correct and became misspelled.
			checkAndAddWordUnderCursos(pos + added);
			////
		}

		if (callbackOnFinish) {
			callbackOnFinish();
		}
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

bool SpellingHighlighter::checkSingleWord(const MisspelledWord &range) {
	const auto w = document()->toPlainText().mid(range.first, range.second);
	return _spellCheckerHelper->checkSingleWord(std::move(w));
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
