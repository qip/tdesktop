/*
This file is part of 64Gram Desktop,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "base/flat_set.h"
#include "data/automation/automation_job.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Automation {

// has_weak_ptr so the delayed callbacks below (which can be scheduled
// minutes ahead) cannot outlive the engine.
class AutomationEngine final : public base::has_weak_ptr {
public:
	AutomationEngine();
	~AutomationEngine();

	void start();
	void stop();
	void checkExpiredIfNeeded();
	void runJobNow(const QString &jobId);

private:
	void tick();
	void checkExpired();
	// Return whether the job was actually dispatched, so a job that could
	// not run (no session controller yet) is not marked as run.
	bool executeJob(const AutomationJob &job);
	bool executeSendMessage(const AutomationJob &job);
	bool executeClickButton(const AutomationJob &job);
	void dismissPopupIfNeeded(const AutomationJob &job);

	[[nodiscard]] Window::SessionController *findSessionController() const;

	base::Timer _tickTimer;
	bool _running = false;
	// Monotonic, so a backwards wall-clock jump cannot suppress checks.
	crl::time _lastCheckExpired = 0;
	// Jobs with a delayed run already scheduled, so a tick, an expiry check
	// and "Run Now" cannot stack duplicate sends.
	base::flat_set<QString> _scheduled;

};

} // namespace Automation
