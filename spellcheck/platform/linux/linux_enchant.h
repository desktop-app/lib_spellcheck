// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

namespace Platform::Spellchecker::Enchant {

struct EnchantDict;

class Dictionary final {
public:
	explicit Dictionary(EnchantDict *dict);
	~Dictionary();

	[[nodiscard]] bool check(const QString &word) const;
	[[nodiscard]] std::vector<QString> suggest(const QString &word) const;

	Dictionary(const Dictionary &) = delete;
	Dictionary &operator=(const Dictionary &) = delete;

private:
	EnchantDict *_dict = nullptr;

};

[[nodiscard]] std::unique_ptr<Dictionary> RequestDictionary(
	const QString &lang);
[[nodiscard]] std::vector<QString> ListDictionaries();

} // namespace Platform::Spellchecker::Enchant
