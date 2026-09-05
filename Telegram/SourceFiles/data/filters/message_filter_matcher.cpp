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
#include "logs.h"
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
	if (original.text.isEmpty()) {
		return original;
	}

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
	
	if (matches.isEmpty()) {
		return original;
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
		
		// Remove invalid entities (zero/negative length or out of bounds)
		const auto textLength = text.length();
		entities.erase(
			std::remove_if(entities.begin(), entities.end(), [textLength](const EntityInText &e) {
				return e.length() <= 0
					|| e.offset() < 0
					|| e.offset() >= textLength
					|| (e.offset() + e.length()) > textLength;
			}),
			entities.end());
	}
	
	return result;
}

// A filter plus its pattern, compiled once per filter-set revision instead of
// once per message per paint.
struct CompiledFilter {
	MessageFilter filter;
	QRegularExpression regex;
	bool regexUsable = false;
};

struct CompiledSet {
	uint32 revision = 0;
	std::vector<CompiledFilter> entries;
};

[[nodiscard]] const CompiledSet &Compiled() {
	static auto cache = CompiledSet();
	const auto revision = EnhancedSettings::MessageFiltersRevision();
	if (cache.revision == revision) {
		return cache;
	}
	cache.entries.clear();
	const auto &filters = EnhancedSettings::MessageFiltersRef();
	cache.entries.reserve(size_t(filters.size()));
	for (const auto &filter : filters) {
		if (!filter.enabled) {
			continue;
		}
		auto entry = CompiledFilter();
		entry.filter = filter;
		if (!filter.regex.isEmpty()) {
			entry.regex = QRegularExpression(filter.regex);
			entry.regexUsable = entry.regex.isValid();
			if (entry.regexUsable) {
				// Pay the JIT cost once, not once per message.
				entry.regex.optimize();
			} else {
				LOG(("Message Filters Error: filter '%1' has an invalid "
					"pattern, it will be ignored: %2").arg(
						filter.name,
						entry.regex.errorString()));
			}
		}
		cache.entries.push_back(std::move(entry));
	}
	std::sort(
		cache.entries.begin(),
		cache.entries.end(),
		[](const CompiledFilter &a, const CompiledFilter &b) {
			return a.filter.order < b.filter.order;
		});
	cache.revision = revision;
	return cache;
}

struct Evaluation {
	FilterResult result;
	bool hasReplacement = false;
	TextWithEntities replacement;
};

// Computes the verdict without touching the item. `wantReplacement` controls
// whether the (much more expensive) Replace-mode text is built as well.
[[nodiscard]] Evaluation Evaluate(
		not_null<HistoryItem*> item,
		bool wantReplacement) {
	auto evaluation = Evaluation();

	// Skip service messages - they don't have regular text.
	if (item->isService()) {
		return evaluation;
	}
	const auto &compiled = Compiled();
	if (compiled.entries.empty()) {
		return evaluation;
	}
	const auto chatId = item->history()->peer->id.value;

	for (const auto &entry : compiled.entries) {
		const auto &filter = entry.filter;

		// Check if chat matches (if filter specifies chats).
		if (!filter.chatIds.isEmpty() && !filter.chatIds.contains(chatId)) {
			continue;
		}

		// All specified conditions must match (AND logic).
		auto userMatches = true;
		auto regexMatches = true;

		if (!filter.userIds.isEmpty()) {
			userMatches = false;
			if (const auto from = item->from()) {
				userMatches = filter.userIds.contains(from->id.value);
			}
		}

		auto replaced = TextWithEntities();
		auto haveReplacement = false;
		if (!filter.regex.isEmpty()) {
			regexMatches = false;
			if (entry.regexUsable) {
				const auto &original = item->originalText();
				const auto match = entry.regex.match(original.text);
				if (match.hasMatch()) {
					regexMatches = true;
					if (wantReplacement
						&& filter.mode == FilterMode::Replace) {
						replaced = ReplaceTextWithEntities(
							original,
							entry.regex,
							filter.replacementText);
						haveReplacement = !replaced.text.isEmpty();
					}
				}
			}
		}

		if (userMatches && regexMatches) {
			if (filter.mode == FilterMode::Blacklist) {
				evaluation.result = {
					true,
					filter.displayMode,
					FilterMode::Blacklist,
				};
			} else if (filter.mode == FilterMode::Replace) {
				evaluation.result = {
					false,
					FilterDisplayMode::Hide,
					FilterMode::Replace,
				};
				evaluation.hasReplacement = haveReplacement;
				evaluation.replacement = std::move(replaced);
			} else {
				// Whitelist: this message matched, so show it.
				evaluation.result = {
					false,
					FilterDisplayMode::Hide,
					FilterMode::Whitelist,
				};
			}
			return evaluation;
		} else if (filter.mode == FilterMode::Whitelist) {
			// Whitelist: message doesn't match, hide it.
			evaluation.result = {
				true,
				FilterDisplayMode::Hide,
				FilterMode::Whitelist,
			};
			return evaluation;
		}
	}
	return evaluation;
}

} // namespace

FilterResult CheckMessageAgainstFilters(not_null<HistoryItem*> item) {
	return Evaluate(item, false).result;
}

void ApplyFilterReplacement(not_null<HistoryItem*> item) {
	if (item->isService()) {
		return;
	} else if (Compiled().entries.empty()) {
		item->clearFilterReplacement();
		return;
	}
	auto evaluation = Evaluate(item, true);
	if (evaluation.hasReplacement) {
		item->setFilterReplacement(std::move(evaluation.replacement));
	} else {
		item->clearFilterReplacement();
	}
}

uint32 FiltersRevision() {
	return EnhancedSettings::MessageFiltersRevision();
}

bool ShouldSuppressNotification(not_null<HistoryItem*> item) {
	// Suppress only when a blacklist filter is what hid this message. The
	// previous version returned true whenever any enabled blacklist filter
	// existed anywhere, regardless of which filter actually matched.
	const auto result = CheckMessageAgainstFilters(item);
	return result.filtered
		&& (result.matchedMode == FilterMode::Blacklist);
}


} // namespace MessageFilters

