// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "spellcheck/spellcheck_highlight_syntax.h"

#include "base/base_file_utilities.h"
#include "base/debug_log.h"
#include "base/flat_map.h"
#include "crl/crl_object_on_queue.h"

#include "SyntaxHighlighter.h"

#include <QtCore/QFile>

#include <xxhash.h>
#include <variant>
#include <string>

void spellchecker_InitHighlightingResource() {
#ifdef Q_OS_MAC // Use resources from the .app bundle on macOS.

	base::RegisterBundledResources(u"lib_spellcheck.rcc"_q);

#else // Q_OS_MAC

	Q_INIT_RESOURCE(highlighting);

#endif // Q_OS_MAC
}

namespace Spellchecker {
namespace {

base::flat_map<XXH64_hash_t, EntitiesInText> Cache;
HighlightProcessId ProcessIdAutoIncrement/* = 0*/;
rpl::event_stream<HighlightProcessId> ReadyStream;

class QueuedHighlighter final {
public:
	QueuedHighlighter();

	struct Request {
		uint64 hash = 0;
		QString text;
		QString language;
	};
	void process(Request request);
	void notify(HighlightProcessId id);

private:
	using Task = std::variant<Request, HighlightProcessId>;

	std::vector<Task> _tasks;

	std::unique_ptr<SyntaxHighlighter> _highlighter;

};

[[nodiscard]] crl::object_on_queue<QueuedHighlighter> &Highlighter() {
	static auto result = crl::object_on_queue<QueuedHighlighter>();
	return result;
}

[[nodiscard]] const QString &LookupAlias(const QString &language) {
	static const auto kAliases = base::flat_map<QString, QString>{
		{ u"diff"_q, u"git"_q },
		{ u"patch"_q, u"git"_q },
	};
	const auto i = kAliases.find(language);
	return (i != end(kAliases)) ? i->second : language;
}

QueuedHighlighter::QueuedHighlighter() {
	spellchecker_InitHighlightingResource();
}

void QueuedHighlighter::process(Request request) {
	if (!_highlighter) {
		auto file = QFile(":/misc/grammars.dat");
		const auto size = file.size();
		const auto ok1 = file.open(QIODevice::ReadOnly);
		auto grammars = std::string();
		grammars.resize(size);
		const auto ok2 = (file.read(grammars.data(), size) == size);
		Assert(ok1 && ok2);

		_highlighter = std::make_unique<SyntaxHighlighter>(grammars);
	}

	const auto text = request.text.toStdString();
	const auto language = LookupAlias(request.language.toLower());
	const auto tokens = _highlighter->tokenize(text, language.toStdString());

	static const auto colors = base::flat_map<std::string, int>{
		{ "comment"      , 1 },
		{ "block-comment", 1 },
		{ "prolog"       , 1 },
		{ "doctype"      , 1 },
		{ "cdata"        , 1 },
		{ "punctuation"  , 2 },
		{ "property"     , 3 },
		{ "tag"          , 3 },
		{ "boolean"      , 3 },
		{ "number"       , 3 },
		{ "constant"     , 3 },
		{ "symbol"       , 3 },
		{ "deleted"      , 3 },
		{ "selector"     , 4 },
		{ "attr-name"    , 4 },
		{ "string"       , 4 },
		{ "char"         , 4 },
		{ "builtin"      , 4 },
		{ "operator"     , 5 },
		{ "entity"       , 5 },
		{ "url"          , 5 },
		{ "atrule"       , 6 },
		{ "attr-value"   , 6 },
		{ "keyword"      , 6 },
		{ "function"     , 6 },
		{ "class-name"   , 7 },
		{ "inserted"     , 8 },
	};

	auto offset = 0;
	auto entities = EntitiesInText();
	auto rebuilt = QString();
	rebuilt.reserve(request.text.size());
	const auto enumerate = [&](
			const TokenList &list,
			const std::string &type,
			auto &&self) -> void {
		for (const auto &node : list) {
			if (node.isSyntax()) {
				const auto &syntax = static_cast<const Syntax&>(node);
				self(syntax.children(), syntax.type(), self);
			} else {
				const auto text = static_cast<const Text&>(node).value();
				const auto utf16 = QString::fromUtf8(
					text.data(),
					text.size());
				const auto length = utf16.size();
				rebuilt.append(utf16);
				if (!type.empty()) {
					const auto i = colors.find(type);
					if (i != end(colors)) {
						entities.push_back(EntityInText(
							EntityType::Colorized,
							offset,
							length,
							QChar(ushort(i->second))));
					}
				}
				offset += length;
			}
		}
	};
	enumerate(tokens, std::string(), enumerate);
	const auto hash = request.hash;
	if (offset != request.text.size()) {
		// Something went wrong.
		LOG(("Highlighting Error: for language '%1', text: %2"
			).arg(request.language, request.text));
		entities.clear();
	}
	crl::on_main([hash, entities = std::move(entities)]() mutable {
		Cache.emplace(hash, std::move(entities));
	});
}

void QueuedHighlighter::notify(HighlightProcessId id) {
	crl::on_main([=] {
		ReadyStream.fire_copy(id);
	});
}

struct CacheResult {
	uint64 hash = 0;
	const EntitiesInText *list = nullptr;

	explicit operator bool() const {
		return list != nullptr;
	}
};
[[nodiscard]] CacheResult FindInCache(
		const TextWithEntities &text,
		EntitiesInText::const_iterator i) {
	const auto view = QStringView(text.text).mid(i->offset(), i->length());
	const auto language = i->data();

	struct Destroyer {
		void operator()(XXH64_state_t *state) {
			if (state) {
				XXH64_freeState(state);
			}
		}
	};
	static const auto S = std::unique_ptr<XXH64_state_t, Destroyer>(
		XXH64_createState());

	const auto state = S.get();
	XXH64_reset(state, 0);
	XXH64_update(state, view.data(), view.size() * sizeof(ushort));
	XXH64_update(state, language.data(), language.size() * sizeof(ushort));
	const auto hash = XXH64_digest(state);

	const auto j = Cache.find(hash);
	return { hash, (j != Cache.cend()) ? &j->second : nullptr };
}

EntitiesInText::iterator Insert(
		TextWithEntities &text,
		EntitiesInText::iterator i,
		const EntitiesInText &entities) {
	auto next = i + 1;
	if (entities.empty()) {
		return next;
	}
	const auto offset = i->offset();
	if (next != text.entities.cend()
		&& next->type() == entities.front().type()
		&& next->offset() == offset + entities.front().offset()) {
		return next;
	}
	const auto length = i->length();
	for (const auto &entity : entities) {
		if (entity.offset() + entity.length() > length) {
			break;
		}
		auto j = text.entities.insert(next, entity);
		j->shiftRight(offset);
		next = j + 1;
	}
	return next;
}

void Schedule(
		uint64 hash,
		const TextWithEntities &text,
		EntitiesInText::const_iterator i) {
	Highlighter().with([
		hash,
		text = text.text.mid(i->offset(), i->length()),
		language = i->data()
	](QueuedHighlighter &instance) mutable {
		instance.process({ hash, std::move(text), std::move(language) });
	});
}

void Notify(uint64 processId) {
	Highlighter().with([processId](QueuedHighlighter &instance) {
		instance.notify(processId);
	});
}

} // namespace

HighlightProcessId TryHighlightSyntax(TextWithEntities &text) {
	auto b = text.entities.begin();
	auto i = b;
	auto e = text.entities.end();
	const auto checking = [](const EntityInText &entity) {
		return (entity.type() == EntityType::Pre)
			&& !entity.data().isEmpty();
	};
	auto processId = HighlightProcessId();
	while (true) {
		i = std::find_if(i, e, checking);
		if (i == e) {
			break;
		} else if (const auto already = FindInCache(text, i)) {
			i = Insert(text, i, *already.list);
			b = text.entities.begin();
			e = text.entities.end();
		} else {
			Schedule(already.hash, text, i);
			if (!processId) {
				processId = ++ProcessIdAutoIncrement;
			}
			++i;
		}
	}
	if (processId) {
		Notify(processId);
	}
	return processId;
}

rpl::producer<HighlightProcessId> HighlightReady() {
	return ReadyStream.events();
}

} // namespace Spellchecker
