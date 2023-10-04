// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text_entity.h"

namespace Spellchecker {

using HighlightProcessId = uint64;

// Returning zero means we highlighted everything already.
[[nodiscard]] HighlightProcessId TryHighlightSyntax(TextWithEntities &text);
[[nodiscard]] rpl::producer<HighlightProcessId> HighlightReady();

} // namespace Spellchecker
