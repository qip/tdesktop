/*
This file is part of 64Gram Desktop,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include <QtCore/QString>
#include <QtCore/QDateTime>

namespace Automation {

[[nodiscard]] bool CronIsValid(const QString &expr);
[[nodiscard]] bool CronMatches(const QString &expr, const QDateTime &dt);
[[nodiscard]] QDateTime CronNextMatch(const QString &expr, const QDateTime &after);

} // namespace Automation
