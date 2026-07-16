/*
This file is part of 64Gram Desktop,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include <QtCore/QString>
#include <QtCore/QVector>

namespace Automation {

enum class ActionType {
	SendMessage,
	ClickButton,
};

enum class RunMode {
	ExactTime,
	RunExpiredOnOpen,
};

struct AutomationJob {
	QString id;
	QString name;
	QString cronExpr;
	RunMode runMode = RunMode::ExactTime;
	ActionType actionType = ActionType::SendMessage;
	QVector<uint64> peerIds;
	double delayBetweenSecs = 0.0;
	QString messageText;
	int buttonIndex = 0;
	bool dismissPopup = false;
	double startupDelaySecs = 0.0;
	bool enabled = true;
	int64 lastRunTime = 0;
};

} // namespace Automation
