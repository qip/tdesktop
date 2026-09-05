/*
This file is part of 64Gram Desktop,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/TDesktop-x64/tdesktop/blob/dev/LEGAL
*/
#pragma once

#include <QtCore/QTimer>
#include "data/filters/message_filter.h"
#include "data/automation/automation_job.h"

namespace EnhancedSettings {

	// Soft mute data structure
	struct SoftMuteState {
		bool enabled = false;
		int period = 0; // in seconds
		int64 lastNotificationTime = 0; // unix timestamp
		int suppressionMode = 0; // 0 = silent (badge only), 1 = totally hidden
	};

	class Manager : public QObject {
	Q_OBJECT

	public:
		Manager();

		void fill();

		void write(bool force = false);

		void addIdToBlocklist(int64 userId);

		void removeIdFromBlocklist(int64 userId);

	public Q_SLOTS:

		void writeTimeout();

	private:
		void writeDefaultFile();

		void writeCurrentSettings();

		bool readCustomFile();

		void readBlocklist();

		QTimer _jsonWriteTimer;

		// Set when the custom settings file exists but could not be parsed.
		// While it is set we refuse to write, so a corrupt (or newer) file is
		// never replaced by the empty in-memory state.
		bool _customFileBroken = false;

	};

	void Start();

	void Write();

	void Finish();

	// Message filter management
	[[nodiscard]] QVector<MessageFilters::MessageFilter> GetMessageFilters();
	void AddMessageFilter(const MessageFilters::MessageFilter &filter);
	void UpdateMessageFilter(const MessageFilters::MessageFilter &filter);
	void DeleteMessageFilter(const QString &filterId);
	void ReorderFilters(const QVector<QString> &filterIds);

	// Soft mute management
	[[nodiscard]] SoftMuteState GetSoftMuteState(uint64 peerId);
	void SetSoftMuteState(uint64 peerId, const SoftMuteState &state);
	void UpdateSoftMuteLastNotification(uint64 peerId, int64 timestamp);
	void RemoveSoftMute(uint64 peerId);

	// Automation job management
	[[nodiscard]] QVector<Automation::AutomationJob> GetAutomationJobs();
	void AddAutomationJob(const Automation::AutomationJob &job);
	void UpdateAutomationJob(const Automation::AutomationJob &job);
	void DeleteAutomationJob(const QString &jobId);
	void UpdateAutomationJobLastRun(const QString &jobId, int64 timestamp);

	// Local pinned chats persistence
	[[nodiscard]] QVector<uint64> GetLocalPinnedPeers();
	void SetLocalPinnedPeers(const QVector<uint64> &peerIds);
	void AddLocalPinnedPeer(uint64 peerId);
	void RemoveLocalPinnedPeer(uint64 peerId);

} // namespace EnhancedSettings
