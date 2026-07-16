/*
This file is part of 64Gram Desktop,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LEGAL
*/
#include "stdafx.h"
#include "data/automation/automation_engine.h"

#include "core/application.h"
#include "core/enhanced_settings.h"
#include "data/automation/cron_parser.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_reply_markup.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "api/api_common.h"
#include "api/api_bot.h"
#include "apiwrap.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QDateTime>

#include "base/call_delayed.h"

namespace Automation {

AutomationEngine::AutomationEngine()
: _tickTimer([=] { tick(); }) {
}

AutomationEngine::~AutomationEngine() {
	stop();
}

void AutomationEngine::start() {
	if (_running) {
		return;
	}
	_running = true;
	_lastCheckExpiredTime = QDateTime::currentSecsSinceEpoch();
	checkExpired();
	_tickTimer.callEach(60 * 1000);
}

void AutomationEngine::stop() {
	_running = false;
	_tickTimer.cancel();
}

void AutomationEngine::reload() {
	// no-op: jobs are read from EnhancedSettings on each tick
}

void AutomationEngine::checkExpiredIfNeeded() {
	const auto now = QDateTime::currentSecsSinceEpoch();
	if (now - _lastCheckExpiredTime < 30) {
		return;
	}
	_lastCheckExpiredTime = now;
	checkExpired();
}

void AutomationEngine::tick() {
	const auto now = QDateTime::currentDateTime();
	auto jobs = EnhancedSettings::GetAutomationJobs();
	for (auto &job : jobs) {
		if (!job.enabled) {
			continue;
		}
		if (CronMatches(job.cronExpr, now)) {
			const auto nowUnix = now.toSecsSinceEpoch();
			// Avoid double-firing within the same minute.
			const auto lastRunDt = QDateTime::fromSecsSinceEpoch(job.lastRunTime);
			if (lastRunDt.date() == now.date()
				&& lastRunDt.time().hour() == now.time().hour()
				&& lastRunDt.time().minute() == now.time().minute()) {
				continue;
			}
			executeJob(job);
			EnhancedSettings::UpdateAutomationJobLastRun(job.id, nowUnix);
		}
	}
}

void AutomationEngine::checkExpired() {
	const auto now = QDateTime::currentDateTime();
	const auto nowUnix = now.toSecsSinceEpoch();
	auto jobs = EnhancedSettings::GetAutomationJobs();
	for (auto &job : jobs) {
		if (!job.enabled) {
			continue;
		}
		if (job.runMode != RunMode::RunExpiredOnOpen) {
			continue;
		}
		bool shouldRun = false;
		if (job.lastRunTime <= 0) {
			shouldRun = true;
		} else {
			const auto lastRun = QDateTime::fromSecsSinceEpoch(job.lastRunTime);
			const auto next = CronNextMatch(job.cronExpr, lastRun);
			if (next.isValid() && next <= now) {
				shouldRun = true;
			}
		}
		if (shouldRun) {
			const auto delayMs = static_cast<crl::time>(job.startupDelaySecs * 1000);
			const auto jobId = job.id;
			const auto run = [=, this, j = job]() mutable {
				executeJob(j);
				EnhancedSettings::UpdateAutomationJobLastRun(jobId, QDateTime::currentSecsSinceEpoch());
			};
			if (delayMs > 0) {
				base::call_delayed(delayMs, run);
			} else {
				run();
			}
		}
	}
}

void AutomationEngine::runJobNow(const QString &jobId) {
	auto jobs = EnhancedSettings::GetAutomationJobs();
	for (auto &job : jobs) {
		if (job.id == jobId) {
			executeJob(job);
			EnhancedSettings::UpdateAutomationJobLastRun(job.id, QDateTime::currentSecsSinceEpoch());
			return;
		}
	}
}

void AutomationEngine::executeJob(AutomationJob &job) {
	switch (job.actionType) {
	case ActionType::SendMessage:
		executeSendMessage(job);
		break;
	case ActionType::ClickButton:
		executeClickButton(job);
		break;
	}
}

Window::SessionController *AutomationEngine::findSessionController() const {
	if (!Core::IsAppLaunched()) {
		return nullptr;
	}
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return nullptr;
	}
	return window->sessionController();
}

void AutomationEngine::executeSendMessage(AutomationJob &job) {
	const auto controller = findSessionController();
	if (!controller) {
		return;
	}
	const auto delayMs = static_cast<crl::time>(job.delayBetweenSecs * 1000);
	for (int i = 0; i < job.peerIds.size(); ++i) {
		const auto peerId = job.peerIds[i];
		const auto messageText = job.messageText;
		const auto isLast = (i == job.peerIds.size() - 1);
		const auto jobCopy = job;
		const auto scheduleMs = static_cast<crl::time>(i) * delayMs;
		const auto action = [=] {
			const auto ctrl = findSessionController();
			if (!ctrl) {
				return;
			}
			auto &session = ctrl->session();
			const auto peer = session.data().peerLoaded(PeerId(peerId));
			if (!peer) {
				return;
			}
			const auto history = session.data().historyLoaded(peer);
			if (!history) {
				return;
			}
			auto message = Api::MessageToSend(Api::SendAction(history));
			message.textWithTags.text = messageText;
			message.action.clearDraft = false;
			session.api().sendMessage(std::move(message));
			if (isLast) {
				dismissPopupIfNeeded(jobCopy);
			}
		};
		if (scheduleMs > 0) {
			base::call_delayed(scheduleMs, action);
		} else {
			action();
		}
	}
}

void AutomationEngine::executeClickButton(AutomationJob &job) {
	const auto controller = findSessionController();
	if (!controller) {
		return;
	}
	const auto delayMs = static_cast<crl::time>(job.delayBetweenSecs * 1000);
	for (int i = 0; i < job.peerIds.size(); ++i) {
		const auto peerId = job.peerIds[i];
		const auto buttonIndex = job.buttonIndex;
		const auto isLast = (i == job.peerIds.size() - 1);
		const auto jobCopy = job;
		const auto scheduleMs = static_cast<crl::time>(i) * delayMs;
		const auto action = [=] {
			const auto ctrl = findSessionController();
			if (!ctrl) {
				return;
			}
			auto &session = ctrl->session();
			const auto peer = session.data().peerLoaded(PeerId(peerId));
			if (!peer) {
				return;
			}
			const auto history = session.data().historyLoaded(peer);
			if (!history) {
				return;
			}
			const auto item = history->lastMessage();
			if (!item) {
				return;
			}
			const auto markup = item->inlineReplyMarkup();
			if (!markup) {
				return;
			}
			const auto &rows = markup->data.rows;
			if (rows.empty()) {
				return;
			}
			int totalButtons = 0;
			for (const auto &row : rows) {
				totalButtons += int(row.size());
			}
			if (totalButtons == 0) {
				return;
			}
			int resolvedIndex = buttonIndex;
			if (resolvedIndex < 0) {
				resolvedIndex = totalButtons + resolvedIndex;
			}
			if (resolvedIndex < 0 || resolvedIndex >= totalButtons) {
				return;
			}
			int idx = 0;
			for (int r = 0; r < int(rows.size()); ++r) {
				for (int c = 0; c < int(rows[r].size()); ++c) {
					if (idx == resolvedIndex) {
						Api::SendBotCallbackData(ctrl, item, r, c);
						if (isLast) {
							dismissPopupIfNeeded(jobCopy);
						}
						return;
					}
					++idx;
				}
			}
		};
		if (scheduleMs > 0) {
			base::call_delayed(scheduleMs, action);
		} else {
			action();
		}
	}
}

void AutomationEngine::dismissPopupIfNeeded(const AutomationJob &job) {
	if (!job.dismissPopup) {
		return;
	}
	const auto controller = findSessionController();
	if (controller) {
		controller->hideLayer();
	}
}

} // namespace Automation
