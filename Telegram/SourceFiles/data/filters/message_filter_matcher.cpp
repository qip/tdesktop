/*
This file is part of 64Gram Desktop,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/TDesktop-x64/tdesktop/blob/dev/LEGAL
*/
#include "data/filters/message_filter_matcher.h"

#include "history/history_item.h"
#include "history/history.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "core/enhanced_settings.h"
#include "core/application.h"
#include "main/main_session.h"

#include <QtCore/QRegularExpression>

namespace MessageFilters {

FilterResult CheckMessageAgainstFilters(not_null<HistoryItem*> item) {
	// Skip service messages - they don't have regular text
	if (item->isService()) {
		return { false, FilterDisplayMode::Hide };
	}
	
	const auto filters = EnhancedSettings::GetMessageFilters();
	
	if (filters.isEmpty()) {
		item->clearFilterReplacement();
		return { false, FilterDisplayMode::Hide };
	}
	
	// Sort by order (only if needed)
	auto sortedFilters = filters;
	std::sort(sortedFilters.begin(), sortedFilters.end(), [](const auto &a, const auto &b) {
		return a.order < b.order;
	});

	const auto chatId = item->history()->peer->id.value;

	for (const auto &filter : sortedFilters) {
		if (!filter.enabled) {
			continue;
		}

		// Check if chat matches (if filter specifies chats)
		if (!filter.chatIds.isEmpty() && !filter.chatIds.contains(chatId)) {
			continue;
		}

		// Now check all conditions that must match (AND logic)
		bool userMatches = true;
		bool regexMatches = true;

		// Check if user ID matches (if filter specifies user IDs)
		if (!filter.userIds.isEmpty()) {
			userMatches = false; // Default to false if userIds are specified
			const auto from = item->from();
			if (from) {
				const auto userId = from->id.value;
				if (filter.userIds.contains(userId)) {
					userMatches = true;
				}
			}
		}

		// Check if text matches regex (if filter specifies regex)
		QString replacedText;
		if (!filter.regex.isEmpty()) {
			regexMatches = false;
			const auto text = item->originalText().text;
			QRegularExpression regex(filter.regex);
			if (regex.isValid()) {
				const auto match = regex.match(text);
				if (match.hasMatch()) {
					regexMatches = true;
					if (filter.mode == FilterMode::Replace) {
						// Prepare replacement text
						replacedText = text;
						replacedText.replace(regex, filter.replacementText);
					}
				}
			}
		}

		// All specified conditions must match (AND logic)
		const bool matches = userMatches && regexMatches;

		// Apply filter based on mode
		if (matches) {
			if (filter.mode == FilterMode::Blacklist) {
				item->clearFilterReplacement();
				return { true, filter.displayMode };
			} else if (filter.mode == FilterMode::Replace) {
				// Apply the replacement now that all conditions are confirmed
				if (!replacedText.isEmpty()) {
					item->setFilterReplacement({ replacedText, {} });
				}
				return { false, FilterDisplayMode::Hide };
			} else {
				// Whitelist: show this message
				item->clearFilterReplacement();
				return { false, FilterDisplayMode::Hide };
			}
		} else if (filter.mode == FilterMode::Whitelist) {
			// Whitelist: message doesn't match, hide it
			item->clearFilterReplacement();
			return { true, FilterDisplayMode::Hide };
		}
	}

	// No filters matched, clear any previous replacement and show the message
	item->clearFilterReplacement();
	return { false, FilterDisplayMode::Hide };
}

bool ShouldSuppressNotification(not_null<HistoryItem*> item) {
	const auto result = CheckMessageAgainstFilters(item);
	// Suppress notification if message is filtered by a blacklist
	if (result.filtered) {
		const auto filters = EnhancedSettings::GetMessageFilters();
		for (const auto &filter : filters) {
			if (!filter.enabled) continue;
			
			// Quick check if this could be the filter that matched
			if (filter.mode == FilterMode::Blacklist) {
				return true;
			}
		}
	}
	return false;
}

} // namespace MessageFilters

