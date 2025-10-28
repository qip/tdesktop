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
};

[[nodiscard]] FilterResult CheckMessageAgainstFilters(
	not_null<HistoryItem*> item);

[[nodiscard]] bool ShouldSuppressNotification(
	not_null<HistoryItem*> item);

} // namespace MessageFilters

