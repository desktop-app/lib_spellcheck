// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QtCore/QLocale>

using MisspelledWord = std::pair<int, int>;
using MisspelledWords = std::vector<MisspelledWord>;

struct LanguageId {
	QLocale::Language value = QLocale::AnyLanguage;

	[[nodiscard]] static LanguageId FromName(const QString &name) {
		auto exact = QLocale(name);
		return {
			((exact.language() == QLocale::C)
				? QLocale(name.mid(0, 2))
				: exact).language()
		};
	}

	[[nodiscard]] QLocale locale() const {
		if (value == QLocale::C) {
			return QLocale(QLocale::English);
		}
		auto result = QLocale(value);
		return (result.language() == QLocale::C)
			? QLocale(QLocale::English)
			: result;
	}

	[[nodiscard]] bool known() const noexcept {
		return (value != QLocale::AnyLanguage);
	}
	explicit operator bool() const noexcept {
		return known();
	}

	friend inline constexpr auto operator<=>(
			LanguageId a,
			LanguageId b) noexcept {
		return (a.value == QLocale::C ? QLocale::English : a.value)
			<=> (b.value == QLocale::C ? QLocale::English : b.value);
	}
	friend inline constexpr bool operator==(
			LanguageId a,
			LanguageId b) noexcept {
		return (a <=> b) == 0;
	}
};
