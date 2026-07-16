/*
This file is part of 64Gram Desktop,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "data/automation/automation_job.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Automation {

class AutomationEngine final {
public:
	AutomationEngine();
	~AutomationEngine();

	void start();
	void stop();
	void reload();
	void checkExpiredIfNeeded();
	void runJobNow(const QString &jobId);

private:
	void tick();
	void checkExpired();
	void executeJob(AutomationJob &job);
	void executeSendMessage(AutomationJob &job);
	void executeClickButton(AutomationJob &job);
	void dismissPopupIfNeeded(const AutomationJob &job);

	[[nodiscard]] Window::SessionController *findSessionController() const;

	base::Timer _tickTimer;
	bool _running = false;
	int64 _lastTickTime = 0;
	int64 _lastCheckExpiredTime = 0;

};

} // namespace Automation
