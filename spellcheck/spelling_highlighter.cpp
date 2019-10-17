// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//

#include "spellcheck/spelling_highlighter.h"

#include "lang/lang_keys.h"
#include "spellcheck/spellchecker_locale_script.h"
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
	const std::initializer_list<const QString *> unspellcheckableTags,
	rpl::producer<bool> enabled,
	rpl::producer<std::tuple<int, int, int>> documentChanges)
: QSyntaxHighlighter(textEdit->document())
, _cursor(QTextCursor(document()->docHandle(), 0))
, _coldSpellcheckingTimer([=] { checkChangedText(); })
, _unspellcheckableTags(std::move(unspellcheckableTags))
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

	std::move(
		documentChanges
	) | rpl::start_with_next([=](const auto &value) {
		const auto &[pos, removed, added] = value;
		contentsChange(pos, removed, added);
	}, _lifetime);

	std::move(
		enabled
	) | rpl::start_with_next([=](bool value) {
		setEnabled(value);
	}, _lifetime);

	checkCurrentText();
}

void SpellingHighlighter::contentsChange(int pos, int removed, int added) {
	if (getDocumentText().isEmpty()) {
		_cachedRanges.clear();
		return;
	}

	const auto shift = [&](auto chars) {
		ranges::for_each(_cachedRanges, [&](auto &range) {
			if (range.first >= pos + removed) {
				range.first += chars;
			}
		});
	};

	// Shift to the right all words after the cursor, when adding text.
	if (added > 0) {
		shift(added);
	}

	// Remove all words that are in the selection.
	// Remove the word that is under the cursor.
	const auto wordUnderPos = getWordUnderPosition(pos);

	// If the cursor is between spaces,
	// QTextCursor::WordUnderCursor highlights the word on the left
	// even if the word is not under the cursor.
	// Example: "super  |  test", where | is the cursor position.
	// In this example QTextCursor::WordUnderCursor will select "super".
	const auto isPosNotInWord = pos > EndOfWord(wordUnderPos);

	_cachedRanges = (
		_cachedRanges
	) | ranges::view::filter([&](const auto &range) {
		if (IntersectsWordRanges(range, wordUnderPos)) {
			return isPosNotInWord;
		}
		return !(IntersectsWordRanges(range, wordUnderPos)
			|| (removed > 0 && IntersectsWordRanges(range, pos, removed)));
	}) | ranges::to_vector;

	// Shift to the left all words after the cursor, when deleting text.
	if (removed > 0) {
		shift(-removed);
	}

	rehighlight();

	_addedSymbols += added;
	_removedSymbols += removed;

	// The typing of text character by character should produce
	// the same _lastPosition, _addedSymbols and _removedSymbols values
	// as removing and pasting several characters at a time.
	if (!_lastPosition || (removed == 1)) {
		_lastPosition = pos;
	}

	const auto addedSymbol = (added == 1)
		? getDocumentText().midRef(pos, added).at(0)
		: QChar();

	if ((removed == 1) || addedSymbol.isLetterOrNumber()) {
		if (_coldSpellcheckingTimer.isActive()) {
			_coldSpellcheckingTimer.cancel();
		}
		_coldSpellcheckingTimer.callOnce(kColdSpellcheckingTimeout);
	} else {
		// We forcefully increase the range of check
		// when inserting a non-char. This can help when the user inserts
		// a non-char in the middle of a word.
		if (!(addedSymbol.isNull()
			|| addedSymbol.isSpace()
			|| addedSymbol.isLetterOrNumber())) {
			_lastPosition--;
			_addedSymbols++;
		}

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
		auto w = getDocumentText().mid(
			wordUnderCursor.first,
			wordUnderCursor.second);
		if (IsWordSkippable(&w)) {
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

		const auto addedText = getDocumentText().mid(
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
		return !IsWordSkippable(getDocumentText().midRef(
			range.first,
			range.second));
	}) | ranges::to_vector;
}

void SpellingHighlighter::checkCurrentText() {
	if (const auto text = getDocumentText(); !text.isEmpty()) {
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
	const auto w = getDocumentText().mid(range.first, range.second);
	return IsWordSkippable(&w)
		|| Platform::Spellchecker::CheckSpelling(std::move(w));
}

bool SpellingHighlighter::isTagUnspellcheckable(int begin, int length) {
	_cursor.setPosition(begin);
	_cursor.setPosition(begin + length, QTextCursor::KeepAnchor);
	const auto tag = _cursor.charFormat().property(kTagProperty).toString();

	return (!tag.isEmpty())
		&& ranges::find_if(_unspellcheckableTags, [&](const QString *t) {
			return t == tag;
		}) != end(_unspellcheckableTags);
}

MisspelledWord SpellingHighlighter::getWordUnderPosition(int position) {
	_cursor.setPosition(position);
	_cursor.select(QTextCursor::WordUnderCursor);
	const auto start = _cursor.selectionStart();
	return std::make_pair(start, _cursor.selectionEnd() - start);
}

void SpellingHighlighter::highlightBlock(const QString &text) {
	if (_cachedRanges.empty() || !_enabled) {
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
		if (isTagUnspellcheckable(position, l)) {
			continue;
		}
		setFormat(position - block.position(), length, misspelledFormat);
	}

	setCurrentBlockState(0);
}

bool SpellingHighlighter::eventFilter(QObject *o, QEvent *e) {
	if (!_textEdit || !_enabled) {
		return false;
	}
	if (e->type() == QEvent::ContextMenu) {
		const auto c = static_cast<QContextMenuEvent *>(e);
		const auto menu = _textEdit->createStandardContextMenu();
		if (!menu || !c) {
			return false;
		}
		// Copy of QContextMenuEvent.
		const auto result = std::make_pair(
			menu,
			QContextMenuEvent(
				c->reason(),
				c->pos(),
				c->globalPos()));
		const auto showMenu = [=, result = std::move(result)] {
			_contextMenuCreated.fire(std::move(result));
		};
		addSpellcheckerActions(
			std::move(menu),
			_textEdit->cursorForPosition(c->pos()),
			std::move(showMenu));
		return true;
	} else if (e->type() == QEvent::KeyPress) {
		const auto k = static_cast<QKeyEvent *>(e);

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

bool SpellingHighlighter::enabled() {
	return _enabled;
}

void SpellingHighlighter::setEnabled(bool enabled) {
	_enabled = enabled;
	if (_enabled) {
		checkCurrentText();
	} else {
		_cachedRanges.clear();
	}
	rehighlight();
}

QString SpellingHighlighter::getDocumentText() {
	return document()
		? document()->toPlainText()
		: QString();
}

void SpellingHighlighter::addSpellcheckerActions(
		not_null<QMenu*> menu,
		QTextCursor cursorForPosition,
		Fn<void()> showMenuCallback) {

	const auto fillMenu = [=,
		showMenuCallback = std::move(showMenuCallback),
		menu = std::move(menu)](
			const auto isCorrect,
			const auto suggestions,
			const auto newTextCursor) {
		const auto word = newTextCursor.selectedText();
		if (isCorrect) {
			if (Platform::Spellchecker::IsWordInDictionary(word)) {
				menu->addSeparator();
				menu->addAction(
					tr::lng_spellchecker_remove(tr::now),
					[=] {
						Platform::Spellchecker::RemoveWord(word);
						checkCurrentText();
				});
			}
			showMenuCallback();
			return;
		}

		menu->addSeparator();

		menu->addAction(
			tr::lng_spellchecker_add(tr::now),
			[=] {
				Platform::Spellchecker::AddWord(word);
				checkCurrentText();
		});

		menu->addAction(
			tr::lng_spellchecker_ignore(tr::now),
			[=] {
				Platform::Spellchecker::IgnoreWord(word);
				checkCurrentText();
		});

		if (suggestions.empty()) {
			showMenuCallback();
			return;
		}

		menu->addSeparator();
		for (const auto &suggestion : suggestions) {
			const auto replaceWord = [=] {
				const auto oldTextCursor = _textEdit->textCursor();
				_textEdit->setTextCursor(newTextCursor);
				_textEdit->textCursor().insertText(suggestion);
				_textEdit->setTextCursor(oldTextCursor);
			};
			menu->addAction(suggestion, std::move(replaceWord));
		}
		showMenuCallback();
	};

	const auto weak = Ui::MakeWeak(this);
	crl::async([=,
		newTextCursor = std::move(cursorForPosition),
		fillMenu = std::move(fillMenu)]() mutable {

		newTextCursor.select(QTextCursor::WordUnderCursor);
		const auto word = newTextCursor.selectedText();

		const auto isCorrect = IsWordSkippable(&word)
			|| Platform::Spellchecker::CheckSpelling(word);
		std::vector<QString> suggestions;
		if (!isCorrect) {
			Platform::Spellchecker::FillSuggestionList(word, &suggestions);
		}

		crl::on_main(weak, [=,
				newTextCursor = std::move(newTextCursor),
				suggestions = std::move(suggestions),
				fillMenu = std::move(fillMenu)]() mutable {
			fillMenu(
				isCorrect,
				std::move(suggestions),
				std::move(newTextCursor));
		});
	});
}

} // namespace Spellchecker
