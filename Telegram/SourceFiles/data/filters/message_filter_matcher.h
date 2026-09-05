/*
This file is part of 64Gram Desktop,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/TDesktop-x64/tdesktop/blob/dev/LEGAL
*/
#pragma once

#include "data/filters/message_filter.h"
#include "base/basic_types.h"

class HistoryItem;

namespace MessageFilters {

struct FilterResult {
	bool filtered = false;
	FilterDisplayMode displayMode = FilterDisplayMode::Hide;
	// Mode of the filter that produced this verdict. Only meaningful when
	// `filtered` is true.
	FilterMode matchedMode = FilterMode::Blacklist;
};

// Pure query - never touches the item. Safe to call from const paint and
// geometry paths. Results are only valid for the current FiltersRevision().
[[nodiscard]] FilterResult CheckMessageAgainstFilters(
	not_null<HistoryItem*> item);

// Recomputes and stores the Replace-mode text on the item. This is the
// mutating half, and must only be called from non-const paths.
void ApplyFilterReplacement(not_null<HistoryItem*> item);

// Bumped whenever the filter set changes. Callers cache per-message verdicts
// against this value instead of re-running the patterns.
[[nodiscard]] uint32 FiltersRevision();

[[nodiscard]] bool ShouldSuppressNotification(
	not_null<HistoryItem*> item);

} // namespace MessageFilters

