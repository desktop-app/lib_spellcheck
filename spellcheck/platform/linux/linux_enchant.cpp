// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "spellcheck/platform/linux/linux_enchant.h"

#include "base/platform/linux/base_linux_library.h"

#include <sys/types.h>

namespace Platform::Spellchecker::Enchant {

struct EnchantBroker;

namespace {

using EnchantDictDescribeFn = void(*)(
	const char *tag,
	const char *providerName,
	const char *providerDescription,
	const char *providerFile,
	void *userData);

EnchantBroker *(*enchant_broker_init)();
void (*enchant_broker_list_dicts)(
	EnchantBroker *broker,
	EnchantDictDescribeFn callback,
	void *userData);
EnchantDict *(*enchant_broker_request_dict)(
	EnchantBroker *broker,
	const char *tag);
void (*enchant_broker_free_dict)(EnchantBroker *broker, EnchantDict *dict);
int (*enchant_dict_check)(EnchantDict *dict, const char *word, ssize_t len);
char **(*enchant_dict_suggest)(
	EnchantDict *dict,
	const char *word,
	ssize_t len,
	size_t *count);
void (*enchant_dict_free_string_list)(EnchantDict *dict, char **list);

[[nodiscard]] bool Resolve() {
	const auto lib = base::Platform::LoadLibrary(
		"libenchant-2.so.2",
		RTLD_NODELETE);
	return lib
		&& LOAD_LIBRARY_SYMBOL(lib, enchant_broker_init)
		&& LOAD_LIBRARY_SYMBOL(lib, enchant_broker_list_dicts)
		&& LOAD_LIBRARY_SYMBOL(lib, enchant_broker_request_dict)
		&& LOAD_LIBRARY_SYMBOL(lib, enchant_broker_free_dict)
		&& LOAD_LIBRARY_SYMBOL(lib, enchant_dict_check)
		&& LOAD_LIBRARY_SYMBOL(lib, enchant_dict_suggest)
		&& LOAD_LIBRARY_SYMBOL(lib, enchant_dict_free_string_list);
}

[[nodiscard]] EnchantBroker *Broker() {
	// Never freed, dictionaries may be freed through it on exit
	static const auto result = Resolve() ? enchant_broker_init() : nullptr;
	return result;
}

} // namespace

Dictionary::Dictionary(EnchantDict *dict)
: _dict(dict) {
}

Dictionary::~Dictionary() {
	enchant_broker_free_dict(Broker(), _dict);
}

bool Dictionary::check(const QString &word) const {
	const auto utf8 = word.toUtf8();
	// An error is a word the provider can't take, like hspell with points
	return enchant_dict_check(_dict, utf8.constData(), utf8.size()) <= 0;
}

std::vector<QString> Dictionary::suggest(const QString &word) const {
	const auto utf8 = word.toUtf8();
	auto count = size_t();
	const auto list = enchant_dict_suggest(
		_dict,
		utf8.constData(),
		utf8.size(),
		&count);
	if (!list) {
		return {};
	}
	auto result = std::vector<QString>();
	result.reserve(count);
	for (auto i = size_t(); i != count; ++i) {
		result.push_back(QString::fromUtf8(list[i]));
	}
	enchant_dict_free_string_list(_dict, list);
	return result;
}

std::unique_ptr<Dictionary> RequestDictionary(const QString &lang) {
	const auto broker = Broker();
	if (!broker) {
		return nullptr;
	}
	const auto dict = enchant_broker_request_dict(
		broker,
		lang.toUtf8().constData());
	if (!dict) {
		return nullptr;
	}
	return std::make_unique<Dictionary>(dict);
}

std::vector<QString> ListDictionaries() {
	const auto broker = Broker();
	if (!broker) {
		return {};
	}
	auto result = std::vector<QString>();
	enchant_broker_list_dicts(broker, [](
			const char *tag,
			const char *,
			const char *,
			const char *,
			void *userData) {
		static_cast<std::vector<QString>*>(userData)->push_back(
			QString::fromUtf8(tag));
	}, &result);
	return result;
}

} // namespace Platform::Spellchecker::Enchant
