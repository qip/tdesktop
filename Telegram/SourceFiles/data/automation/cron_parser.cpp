/*
This file is part of 64Gram Desktop,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LEGAL
*/
#include "stdafx.h"
#include "data/automation/cron_parser.h"

#include <QtCore/QStringList>

#include <array>
#include <map>
#include <optional>

namespace Automation {
namespace {

// Four years covers every Feb 29 case; an expression with no match in that
// window (for example "0 0 30 2 *") can never fire.
constexpr auto kMaxSearchDays = 366 * 4;
constexpr auto kMaxCachedExpressions = 64;

struct ParsedCron {
	std::array<bool, 60> minutes = {};
	std::array<bool, 24> hours = {};
	std::array<bool, 32> daysOfMonth = {}; // 1..31
	std::array<bool, 13> months = {}; // 1..12
	std::array<bool, 7> daysOfWeek = {}; // 0..6, 0 = Sunday
	// POSIX cron ORs day-of-month with day-of-week when either field is
	// restricted, so we have to remember whether they were plain "*".
	bool dayOfMonthRestricted = false;
	bool dayOfWeekRestricted = false;
};

bool ParseField(const QString &field, int min, int max, bool *out) {
	auto any = false;
	const auto set = [&](int value) {
		if (value >= min && value <= max) {
			out[value] = true;
			any = true;
		}
	};
	const auto parts = field.split(',');
	for (const auto &part : parts) {
		if (part == "*") {
			for (auto i = min; i <= max; ++i) {
				set(i);
			}
		} else if (part.contains('/')) {
			const auto stepParts = part.split('/');
			if (stepParts.size() != 2) return false;
			auto stepOk = false;
			const auto step = stepParts[1].toInt(&stepOk);
			if (!stepOk || step <= 0) return false;

			auto start = min;
			auto end = max;
			if (stepParts[0] != "*") {
				if (stepParts[0].contains('-')) {
					const auto range = stepParts[0].split('-');
					if (range.size() != 2) return false;
					auto ok1 = false, ok2 = false;
					start = range[0].toInt(&ok1);
					end = range[1].toInt(&ok2);
					if (!ok1 || !ok2) return false;
				} else {
					auto ok = false;
					start = stepParts[0].toInt(&ok);
					if (!ok) return false;
				}
			}
			if (start < min || end > max || start > end) return false;
			for (auto i = start; i <= end; i += step) {
				set(i);
			}
		} else if (part.contains('-')) {
			const auto range = part.split('-');
			if (range.size() != 2) return false;
			auto ok1 = false, ok2 = false;
			const auto a = range[0].toInt(&ok1);
			const auto b = range[1].toInt(&ok2);
			if (!ok1 || !ok2 || a < min || b > max || a > b) return false;
			for (auto i = a; i <= b; ++i) {
				set(i);
			}
		} else {
			auto ok = false;
			const auto v = part.toInt(&ok);
			if (!ok || v < min || v > max) return false;
			set(v);
		}
	}
	return any;
}

bool ParseCron(const QString &expr, ParsedCron &out) {
	const auto fields = expr.simplified().split(' ');
	if (fields.size() != 5) return false;
	if (!ParseField(fields[0], 0, 59, out.minutes.data())) return false;
	if (!ParseField(fields[1], 0, 23, out.hours.data())) return false;
	if (!ParseField(fields[2], 1, 31, out.daysOfMonth.data())) return false;
	if (!ParseField(fields[3], 1, 12, out.months.data())) return false;
	if (!ParseField(fields[4], 0, 6, out.daysOfWeek.data())) return false;
	out.dayOfMonthRestricted = (fields[2] != "*");
	out.dayOfWeekRestricted = (fields[4] != "*");
	return true;
}

// Parsing allocated and scanned five vectors per call, and the engine calls
// this once a minute per job - and once per minute-step while searching for
// the next match. Cache the parse instead.
[[nodiscard]] const ParsedCron *Parsed(const QString &expr) {
	static auto cache = std::map<QString, std::optional<ParsedCron>>();
	auto i = cache.find(expr);
	if (i == cache.end()) {
		if (cache.size() >= kMaxCachedExpressions) {
			cache.clear();
		}
		auto parsed = ParsedCron();
		i = cache.emplace(
			expr,
			ParseCron(expr, parsed)
				? std::optional<ParsedCron>(std::move(parsed))
				: std::nullopt).first;
	}
	return i->second ? &*i->second : nullptr;
}

[[nodiscard]] bool DayMatches(const ParsedCron &cron, const QDate &date) {
	const auto dom = date.day();
	const auto dow = date.dayOfWeek() % 7; // Qt: Mon=1..Sun=7 -> 0=Sun
	return (cron.dayOfMonthRestricted && cron.dayOfWeekRestricted)
		? (cron.daysOfMonth[dom] || cron.daysOfWeek[dow])
		: (cron.daysOfMonth[dom] && cron.daysOfWeek[dow]);
}

} // namespace

bool CronIsValid(const QString &expr) {
	return (Parsed(expr) != nullptr);
}

bool CronMatches(const QString &expr, const QDateTime &dt) {
	const auto cron = Parsed(expr);
	if (!cron) {
		return false;
	}
	const auto date = dt.date();
	const auto time = dt.time();
	return cron->months[date.month()]
		&& DayMatches(*cron, date)
		&& cron->hours[time.hour()]
		&& cron->minutes[time.minute()];
}

QDateTime CronNextMatch(const QString &expr, const QDateTime &after) {
	const auto cron = Parsed(expr);
	if (!cron) {
		return QDateTime();
	}
	const auto from = after.addSecs(60);
	auto date = from.date();
	auto startHour = from.time().hour();
	auto startMinute = from.time().minute();

	// Walk whole days first, so an expression that matches no day at all
	// costs a few hundred date increments instead of a year of minutes.
	for (auto day = 0; day != kMaxSearchDays; ++day) {
		if (cron->months[date.month()] && DayMatches(*cron, date)) {
			for (auto hour = startHour; hour != 24; ++hour) {
				if (!cron->hours[hour]) {
					continue;
				}
				const auto first = (hour == startHour) ? startMinute : 0;
				for (auto minute = first; minute != 60; ++minute) {
					if (cron->minutes[minute]) {
						auto result = from;
						result.setDate(date);
						result.setTime(QTime(hour, minute));
						return result;
					}
				}
			}
		}
		date = date.addDays(1);
		startHour = 0;
		startMinute = 0;
	}
	return QDateTime();
}

} // namespace Automation
