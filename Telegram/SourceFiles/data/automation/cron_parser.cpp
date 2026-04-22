/*
This file is part of 64Gram Desktop,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LEGAL
*/
#include "stdafx.h"
#include "data/automation/cron_parser.h"

#include <QtCore/QStringList>

namespace Automation {
namespace {

struct CronField {
	int min = 0;
	int max = 0;
	QVector<int> values;
};

bool ParseField(const QString &field, int min, int max, QVector<int> &out) {
	out.clear();
	const auto parts = field.split(',');
	for (const auto &part : parts) {
		if (part == "*") {
			for (int i = min; i <= max; ++i) {
				out.append(i);
			}
		} else if (part.contains('/')) {
			const auto stepParts = part.split('/');
			if (stepParts.size() != 2) return false;
			bool stepOk = false;
			const int step = stepParts[1].toInt(&stepOk);
			if (!stepOk || step <= 0) return false;

			int start = min;
			int end = max;
			if (stepParts[0] != "*") {
				if (stepParts[0].contains('-')) {
					const auto range = stepParts[0].split('-');
					if (range.size() != 2) return false;
					bool ok1 = false, ok2 = false;
					start = range[0].toInt(&ok1);
					end = range[1].toInt(&ok2);
					if (!ok1 || !ok2) return false;
				} else {
					bool ok = false;
					start = stepParts[0].toInt(&ok);
					if (!ok) return false;
				}
			}
			if (start < min || end > max || start > end) return false;
			for (int i = start; i <= end; i += step) {
				out.append(i);
			}
		} else if (part.contains('-')) {
			const auto range = part.split('-');
			if (range.size() != 2) return false;
			bool ok1 = false, ok2 = false;
			const int a = range[0].toInt(&ok1);
			const int b = range[1].toInt(&ok2);
			if (!ok1 || !ok2 || a < min || b > max || a > b) return false;
			for (int i = a; i <= b; ++i) {
				out.append(i);
			}
		} else {
			bool ok = false;
			const int v = part.toInt(&ok);
			if (!ok || v < min || v > max) return false;
			out.append(v);
		}
	}
	return !out.isEmpty();
}

struct ParsedCron {
	QVector<int> minutes;
	QVector<int> hours;
	QVector<int> daysOfMonth;
	QVector<int> months;
	QVector<int> daysOfWeek;
};

bool ParseCron(const QString &expr, ParsedCron &out) {
	const auto fields = expr.simplified().split(' ');
	if (fields.size() != 5) return false;
	if (!ParseField(fields[0], 0, 59, out.minutes)) return false;
	if (!ParseField(fields[1], 0, 23, out.hours)) return false;
	if (!ParseField(fields[2], 1, 31, out.daysOfMonth)) return false;
	if (!ParseField(fields[3], 1, 12, out.months)) return false;
	if (!ParseField(fields[4], 0, 6, out.daysOfWeek)) return false;
	return true;
}

} // namespace

bool CronIsValid(const QString &expr) {
	ParsedCron parsed;
	return ParseCron(expr, parsed);
}

bool CronMatches(const QString &expr, const QDateTime &dt) {
	ParsedCron parsed;
	if (!ParseCron(expr, parsed)) return false;

	const int minute = dt.time().minute();
	const int hour = dt.time().hour();
	const int dom = dt.date().day();
	const int month = dt.date().month();
	const int dow = dt.date().dayOfWeek() % 7; // Qt: Mon=1..Sun=7 -> 0=Sun

	return parsed.minutes.contains(minute)
		&& parsed.hours.contains(hour)
		&& parsed.daysOfMonth.contains(dom)
		&& parsed.months.contains(month)
		&& parsed.daysOfWeek.contains(dow);
}

QDateTime CronNextMatch(const QString &expr, const QDateTime &after) {
	ParsedCron parsed;
	if (!ParseCron(expr, parsed)) return QDateTime();

	auto candidate = after.addSecs(60);
	candidate.setTime(QTime(candidate.time().hour(), candidate.time().minute(), 0));

	constexpr int kMaxIterations = 525600; // one year of minutes
	for (int i = 0; i < kMaxIterations; ++i) {
		const int minute = candidate.time().minute();
		const int hour = candidate.time().hour();
		const int dom = candidate.date().day();
		const int month = candidate.date().month();
		const int dow = candidate.date().dayOfWeek() % 7;

		if (parsed.months.contains(month)
			&& parsed.daysOfMonth.contains(dom)
			&& parsed.daysOfWeek.contains(dow)
			&& parsed.hours.contains(hour)
			&& parsed.minutes.contains(minute)) {
			return candidate;
		}

		candidate = candidate.addSecs(60);
	}
	return QDateTime();
}

} // namespace Automation
