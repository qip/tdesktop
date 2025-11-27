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
#include "ui/text/text_entity.h"

#include <QtCore/QRegularExpression>

namespace MessageFilters {

namespace {

// Replaces text while preserving and adjusting entities
TextWithEntities ReplaceTextWithEntities(
		const TextWithEntities &original,
		const QRegularExpression &regex,
		const QString &replacement) {
	auto result = original;
	auto &text = result.text;
	auto &entities = result.entities;
	
	// Find all matches first, then process from end to start
	// to preserve offset positions
	QVector<QRegularExpressionMatch> matches;
	auto it = regex.globalMatch(text);
	while (it.hasNext()) {
		matches.append(it.next());
	}
	
	// Process matches from end to start
	for (int i = matches.size() - 1; i >= 0; --i) {
		const auto &match = matches[i];
		const auto matchStart = match.capturedStart();
		const auto matchLength = match.capturedLength();
		const auto matchEnd = matchStart + matchLength;
		
		// Build the actual replacement string (with backreferences)
		auto actualReplacement = replacement;
		for (int g = match.lastCapturedIndex(); g >= 0; --g) {
			actualReplacement.replace(
				QString("\\%1").arg(g),
				match.captured(g));
		}
		
		const auto replacementLength = actualReplacement.length();
		const auto lengthDiff = replacementLength - matchLength;
		
		// Replace the text
		text.replace(matchStart, matchLength, actualReplacement);
		
		// Adjust entities
		for (auto &entity : entities) {
			const auto entityStart = entity.offset();
			const auto entityEnd = entityStart + entity.length();
			
			if (entityEnd <= matchStart) {
				// Entity is entirely before the match - no change needed
				continue;
			} else if (entityStart >= matchEnd) {
				// Entity is entirely after the match - shift by length difference
				entity = EntityInText(
					entity.type(),
					entity.offset() + lengthDiff,
					entity.length(),
					entity.data());
			} else if (entityStart <= matchStart && entityEnd >= matchEnd) {
				// Entity contains the entire match - adjust length
				entity = EntityInText(
					entity.type(),
					entity.offset(),
					entity.length() + lengthDiff,
					entity.data());
			} else if (entityStart >= matchStart && entityEnd <= matchEnd) {
				// Entity is entirely within the match - scale proportionally
				// or remove if match is being replaced with shorter/empty text
				if (replacementLength > 0 && matchLength > 0) {
					const auto relativeStart = entityStart - matchStart;
					const auto scale = static_cast<double>(replacementLength) / matchLength;
					const auto newStart = matchStart + static_cast<int>(relativeStart * scale);
					const auto newLength = std::max(1, static_cast<int>(entity.length() * scale));
					entity = EntityInText(
						entity.type(),
						newStart,
						newLength,
						entity.data());
				} else {
					// Mark for removal by setting length to 0
					entity = EntityInText(entity.type(), 0, 0, entity.data());
				}
			} else if (entityStart < matchStart && entityEnd > matchStart && entityEnd <= matchEnd) {
				// Entity starts before match but ends inside it - truncate
				entity = EntityInText(
					entity.type(),
					entity.offset(),
					matchStart - entityStart,
					entity.data());
			} else if (entityStart >= matchStart && entityStart < matchEnd && entityEnd > matchEnd) {
				// Entity starts inside match but ends after it - adjust start
				const auto newStart = matchStart + replacementLength;
				const auto newLength = entityEnd - matchEnd;
				entity = EntityInText(
					entity.type(),
					newStart,
					newLength,
					entity.data());
			}
		}
		
		// Remove entities with zero length
		entities.erase(
			std::remove_if(entities.begin(), entities.end(), [](const EntityInText &e) {
				return e.length() <= 0;
			}),
			entities.end());
	}
	
	return result;
}

} // namespace

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
		TextWithEntities replacedTextWithEntities;
		if (!filter.regex.isEmpty()) {
			regexMatches = false;
			const auto &original = item->originalText();
			QRegularExpression regex(filter.regex);
			if (regex.isValid()) {
				const auto match = regex.match(original.text);
				if (match.hasMatch()) {
					regexMatches = true;
					if (filter.mode == FilterMode::Replace) {
						// Prepare replacement text with entities preserved
						replacedTextWithEntities = ReplaceTextWithEntities(
							original,
							regex,
							filter.replacementText);
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
				if (!replacedTextWithEntities.text.isEmpty()) {
					item->setFilterReplacement(std::move(replacedTextWithEntities));
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

