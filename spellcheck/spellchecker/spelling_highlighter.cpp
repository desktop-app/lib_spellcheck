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

inline int EndOfWord(const MisspelledWord &range) {
	return range.first + range.second;
}

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

inline bool IntersectsWordRanges(
	const MisspelledWord &range,
	int pos2,
	int len2) {
	const auto l1 = range.first;
	const auto r1 = EndOfWord(range) - 1;
	const auto l2 = pos2;
	const auto r2 = pos2 + len2 - 1;
	return !(l1 > r2 || l2 > r1);
}

inline bool IntersectsWordRanges(
	const MisspelledWord &range,
	const MisspelledWord &range2) {
	const auto l1 = range.first;
	const auto r1 = EndOfWord(range) - 1;
	const auto l2 = range2.first;
	const auto r2 = EndOfWord(range2) - 1;
	return !(l1 > r2 || l2 > r1);
}

} // namespace

SpellingHighlighter::SpellingHighlighter(
	QTextEdit *textEdit,
	std::shared_ptr<Spellchecker::Controller> controller,
	UncheckableCallback callback)
: QSyntaxHighlighter(textEdit->document())
, _cursor(QTextCursor(document()->docHandle(), 0))
, _spellCheckerController(controller)
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

	checkCurrentText();
}

void SpellingHighlighter::contentsChange(int pos, int removed, int added) {
	if (document()->toPlainText().isEmpty()) {
		_cachedRanges.clear();
		return;
	}

	// Shift to the right all words after the cursor, when adding text.
	if (added > 0) {
		ranges::for_each(_cachedRanges, [&](auto &range) {
			if (range.first >= pos + removed) {
				range.first += added;
			}
		});
	}

	// Remove all words that are in the selection.
	// Remove the word that is under the cursor.
	const auto wordUnderPos = getWordUnderPosition(pos);
	_cachedRanges = (
		_cachedRanges
	) | ranges::view::filter([&](const auto &range) {
		return !(IntersectsWordRanges(range, wordUnderPos)
			|| (removed > 0 && IntersectsWordRanges(range, pos, removed)));
	}) | ranges::to_vector;

	// Shift to the left all words after the cursor, when deleting text.
	if (removed > 0) {
		ranges::for_each(_cachedRanges, [&](auto &range) {
			if (range.first > pos + removed) {
				range.first -= removed;
			}
		});
	}

	rehighlight();

	_addedSymbols += added;
	_removedSymbols += removed;
	if (!_lastPosition) {
		_lastPosition = pos;
	}

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

	_lastPosition = 0;
	_removedSymbols = 0;
	_addedSymbols = 0;

	if (_coldSpellcheckingTimer.isActive()) {
		_coldSpellcheckingTimer.cancel();
	}

	const auto wordUnderCursor = getWordUnderPosition(pos);
	const auto wordInCacheIt = [=] {
		return ranges::find_if(_cachedRanges, [&](auto &&w) {
			return w.first >= wordUnderCursor.first;
		});
	};

	const auto checkAndAddWordUnderCursos = [&] {
		const auto weak = Ui::MakeWeak(this);
		auto w = document()->toPlainText().mid(
			wordUnderCursor.first,
			wordUnderCursor.second);
		if (_spellCheckerController->isWordSkippable(&w)) {
			return;
		}
		crl::async([=,
			w = std::move(w),
			wordUnderCursor = std::move(wordUnderCursor)]() mutable {
			if (Platform::Spellchecker::CheckSpelling(std::move(w))) {
				return;
			}

			crl::on_main(weak, [=,
					wordUnderCursor = std::move(wordUnderCursor)]() mutable {
				ranges::insert(
					_cachedRanges,
					ranges::find_if(_cachedRanges, [&](auto &&w) {
						return w.first >= wordUnderCursor.first;
					}),
					std::move(wordUnderCursor));
				rehighlight();
			});
		});
	};

	if (added > 0) {
		const auto lastWordNewSelection = getWordUnderPosition(pos + added);

		// This is the same word.
		if (wordUnderCursor == lastWordNewSelection) {
			checkAndAddWordUnderCursos();
			rehighlight();
			return;
		}

		const auto beginNewSelection = wordUnderCursor.first;
		const auto endNewSelection = EndOfWord(lastWordNewSelection);

		const auto addedText = document()->toPlainText().mid(
			beginNewSelection,
			endNewSelection - beginNewSelection);

		invokeCheckText(std::move(addedText), [=](const MisspelledWords &r) {
			ranges::insert(_cachedRanges, wordInCacheIt(), std::move(r));
		}, beginNewSelection);
		return;
	}

	if (removed > 0) {
		checkAndAddWordUnderCursos();
		rehighlight();
	}
}

MisspelledWords SpellingHighlighter::filterSkippableWords(
	MisspelledWords &ranges) {
	return ranges | ranges::view::filter([&](const auto &range) {
		return !_spellCheckerController->isWordSkippable(document()
			->toPlainText().midRef(range.first, range.second));
	}) | ranges::to_vector;
}

void SpellingHighlighter::checkCurrentText() {
	if (const auto text = document()->toPlainText(); !text.isEmpty()) {
		invokeCheckText(text, [&](const MisspelledWords &ranges) {
			_cachedRanges = std::move(ranges);
		});
	}
}

void SpellingHighlighter::invokeCheckText(
	const QString &text,
	Fn<void(const MisspelledWords &ranges)> callback,
	int rangesOffset) {

	const auto weak = Ui::MakeWeak(this);
	crl::async([=,
		text = std::move(text),
		callback = std::move(callback)]() mutable {
		MisspelledWords misspelledWordRanges;
		Platform::Spellchecker::CheckSpellingText(
			std::move(text),
			&misspelledWordRanges);
		if (rangesOffset) {
			ranges::for_each(misspelledWordRanges, [&](auto &&range) {
				range.first += rangesOffset;
			});
		}
		crl::on_main(weak, [=,
				ranges = filterSkippableWords(misspelledWordRanges),
				callback = std::move(callback)]() mutable {
			if (!ranges.empty()) {
				callback(std::move(ranges));
			}
			rehighlight();
		});
	});
}

bool SpellingHighlighter::checkSingleWord(const MisspelledWord &range) {
	const auto w = document()->toPlainText().mid(range.first, range.second);
	return _spellCheckerController->checkSingleWord(std::move(w));
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
	const auto block = currentBlock();
	// Skip the all words outside the current block.
	auto &&rangesOfBlock = (
		_cachedRanges
	) | ranges::view::filter([&](const auto &range) {
		return IntersectsWordRanges(range, block.position(), block.length());
	});

	for (const auto &[position, length] : rangesOfBlock) {
		const auto endOfBlock = text.length() + block.position();
		const auto l = (endOfBlock < position + length)
			? endOfBlock - position
			: length;
		if (_unspellcheckableCallback(getTagFromRange(position, l))) {
			continue;
		}
		setFormat(position - block.position(), length, misspelledFormat);
	}

	setCurrentBlockState(0);
}

bool SpellingHighlighter::eventFilter(QObject *o, QEvent *e) {
	if (!_textEdit) {
		return false;
	}
	if (e->type() == QEvent::KeyPress) {
		const auto *k = static_cast<QKeyEvent *>(e);

		if (ranges::find(kKeysToCheck, k->key()) != kKeysToCheck.end()) {
			if (_addedSymbols + _removedSymbols + _lastPosition) {
				checkCurrentText();
			}
		}
	} else if ((o == _textEdit->viewport())
			&& (e->type() == QEvent::MouseButtonPress)) {
		if (_addedSymbols + _removedSymbols + _lastPosition) {
			checkCurrentText();
		}
	}
	return false;
}

} // namespace Spellchecker
