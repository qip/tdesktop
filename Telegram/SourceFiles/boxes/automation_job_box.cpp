/*
This file is part of 64Gram Desktop,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LEGAL
*/
#include "stdafx.h"
#include "boxes/automation_job_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/boxes/confirm_box.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "data/automation/cron_parser.h"
#include "main/main_session.h"
#include "core/enhanced_settings.h"
#include "window/window_session_controller.h"
#include "boxes/peer_list_box.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"
#include "styles/style_chat_helpers.h"
#include "history/history.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_indexed_list.h"
#include "data/data_chat_filters.h"

#include <QtCore/QUuid>

namespace {

constexpr auto kBoxWidth = 480;

QString ActionTypeLabel(Automation::ActionType type) {
	switch (type) {
	case Automation::ActionType::SendMessage:
		return tr::lng_automation_action_send(tr::now);
	case Automation::ActionType::ClickButton:
		return tr::lng_automation_action_click(tr::now);
	}
	return QString();
}

QString RunModeLabel(Automation::RunMode mode) {
	switch (mode) {
	case Automation::RunMode::ExactTime:
		return tr::lng_automation_run_exact(tr::now);
	case Automation::RunMode::RunExpiredOnOpen:
		return tr::lng_automation_run_expired(tr::now);
	}
	return QString();
}

} // namespace

// --- AutomationJobListBox ---

AutomationJobListBox::AutomationJobListBox(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: _controller(controller) {
}

void AutomationJobListBox::prepare() {
	setTitle(tr::lng_automation_manage_title());

	addButton(tr::lng_automation_add(), [=] { addJob(); });
	addButton(tr::lng_close(), [=] { closeBox(); });

	_list.create(this);
	_list->resizeToWidth(kBoxWidth);
	_list->show();

	refreshList();
	_list->resizeToWidth(kBoxWidth);
	_prepared = true;

	const auto height = std::min(
		600,
		_list->height() + st::boxPadding.top() + st::boxPadding.bottom());
	setDimensions(kBoxWidth, std::max(
		height,
		st::boxPadding.top() + st::boxPadding.bottom() + 50));
}

void AutomationJobListBox::showEvent(QShowEvent *e) {
	Ui::BoxContent::showEvent(e);
	if (_prepared) {
		refreshList();
	}
}

void AutomationJobListBox::refreshList() {
	_list->clear();

	const auto jobs = EnhancedSettings::GetAutomationJobs();

	if (jobs.isEmpty()) {
		_list->add(object_ptr<Ui::FlatLabel>(
			_list,
			tr::lng_automation_no_jobs(tr::now),
			st::boxLabel));
		return;
	}

	static const auto rowSt = [] {
		auto st = st::settingsButton;
		st.padding.setRight(st.padding.right()
			+ st::filtersRemove.width
			+ st::boxLittleSkip);
		return st;
	}();

	for (const auto &job : jobs) {
		const auto label = job.name
			+ QString::fromUtf8(" \xe2\x80\x94 ")
			+ job.cronExpr
			+ QString::fromUtf8(" \xe2\x80\x94 ")
			+ ActionTypeLabel(job.actionType)
			+ (job.enabled ? QString() : QString::fromUtf8(" [OFF]"));

		const auto row = _list->add(object_ptr<Ui::SettingsButton>(
			_list,
			rpl::single(label),
			rowSt));

		row->setClickedCallback([=, id = job.id] {
			editJob(id);
		});

		const auto deleteBtn = Ui::CreateChild<Ui::IconButton>(
			row,
			st::filtersRemove);
		deleteBtn->show();
		deleteBtn->setClickedCallback([=, id = job.id] {
			deleteJob(id);
		});

		row->sizeValue(
		) | rpl::on_next([=](QSize size) {
			const auto top = (size.height() - deleteBtn->height()) / 2;
			deleteBtn->moveToRight(
				st::settingsButton.padding.right(), top);
			deleteBtn->raise();
		}, deleteBtn->lifetime());
	}
}

void AutomationJobListBox::addJob() {
	Automation::AutomationJob newJob;
	newJob.id = QUuid::createUuid().toString();
	newJob.name = "New Job";
	newJob.cronExpr = "0 * * * *";
	newJob.runMode = Automation::RunMode::ExactTime;
	newJob.actionType = Automation::ActionType::SendMessage;
	newJob.enabled = true;

	getDelegate()->show(
		Box<AutomationJobEditBox>(_controller, newJob, true),
		Ui::LayerOption::KeepOther);
}

void AutomationJobListBox::editJob(const QString &jobId) {
	const auto jobs = EnhancedSettings::GetAutomationJobs();
	for (const auto &job : jobs) {
		if (job.id == jobId) {
			getDelegate()->show(
				Box<AutomationJobEditBox>(_controller, job, false),
				Ui::LayerOption::KeepOther);
			return;
		}
	}
}

void AutomationJobListBox::deleteJob(const QString &jobId) {
	getDelegate()->show(Ui::MakeConfirmBox({
		.text = tr::lng_automation_delete_confirm(tr::now),
		.confirmed = [=, this](Fn<void()> close) {
			EnhancedSettings::DeleteAutomationJob(jobId);
			refreshList();
			_list->resizeToWidth(kBoxWidth);
			const auto height = std::min(
				600,
				_list->height()
					+ st::boxPadding.top()
					+ st::boxPadding.bottom());
			setDimensions(kBoxWidth, std::max(
				height,
				st::boxPadding.top() + st::boxPadding.bottom() + 50));
			close();
		},
		.confirmText = tr::lng_box_delete(tr::now),
	}), Ui::LayerOption::KeepOther);
}

// --- AutomationJobEditBox ---

AutomationJobEditBox::AutomationJobEditBox(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	const Automation::AutomationJob &job,
	bool isNew)
: _controller(controller)
, _job(job)
, _isNew(isNew)
, _name(this, st::defaultInputField, nullptr, job.name)
, _cronExpr(this, st::defaultInputField, nullptr, job.cronExpr)
, _messageText(this, st::defaultInputField,
	tr::lng_automation_message_placeholder(), job.messageText)
, _buttonIndex(this, st::defaultInputField, nullptr, job.buttonIndex > 0
		? QString::number(job.buttonIndex + 1)
		: QString("1"))
, _delayBetween(this, st::defaultInputField, nullptr, QString::number(job.delayBetweenSecs, 'g', 10)) {
}

void AutomationJobEditBox::prepare() {
	setTitle(_isNew
		? tr::lng_automation_add()
		: tr::lng_automation_edit());

	addButton(tr::lng_settings_save(), [=] { save(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	const auto w = kBoxWidth - 2 * st::boxPadding.left();

	// Name
	_nameLabel = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::lng_automation_name(tr::now),
		st::boxLabel);
	_name->resize(w, _name->height());

	// Cron expression
	_cronLabel = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::lng_automation_cron(tr::now),
		st::boxLabel);
	_cronExpr->resize(w, _cronExpr->height());

	_cronHint = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::lng_automation_cron_hint(tr::now),
		st::boxLabel);
	_cronHint->resizeToWidth(w);

	// Run mode
	_runModeLabel = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::lng_automation_run_mode(tr::now),
		st::boxLabel);

	_runModeGroup = std::make_shared<Ui::RadiobuttonGroup>(
		static_cast<int>(_job.runMode));

	_exactBtn = Ui::CreateChild<Ui::Radiobutton>(
		this,
		_runModeGroup,
		static_cast<int>(Automation::RunMode::ExactTime),
		tr::lng_automation_run_exact(tr::now),
		st::defaultCheckbox);

	_expiredBtn = Ui::CreateChild<Ui::Radiobutton>(
		this,
		_runModeGroup,
		static_cast<int>(Automation::RunMode::RunExpiredOnOpen),
		tr::lng_automation_run_expired(tr::now),
		st::defaultCheckbox);

	// Action type
	_actionLabel = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::lng_automation_action_type(tr::now),
		st::boxLabel);

	_actionTypeGroup = std::make_shared<Ui::RadiobuttonGroup>(
		static_cast<int>(_job.actionType));

	_sendBtn = Ui::CreateChild<Ui::Radiobutton>(
		this,
		_actionTypeGroup,
		static_cast<int>(Automation::ActionType::SendMessage),
		tr::lng_automation_action_send(tr::now),
		st::defaultCheckbox);

	_clickBtn = Ui::CreateChild<Ui::Radiobutton>(
		this,
		_actionTypeGroup,
		static_cast<int>(Automation::ActionType::ClickButton),
		tr::lng_automation_action_click(tr::now),
		st::defaultCheckbox);

	_actionTypeGroup->setChangedCallback([=](int) {
		updateActionFields();
	});

	// Target chat
	_chatSelectLabel = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::lng_automation_target_chat(tr::now),
		st::boxLabel);

	_chatSelectBtn = Ui::CreateChild<Ui::LinkButton>(
		this,
		tr::lng_automation_select_chat(tr::now));
	_chatSelectBtn->setClickedCallback([=] { selectChat(); });

	_chatLabel = Ui::CreateChild<Ui::FlatLabel>(
		this,
		QString(),
		st::boxLabel);
	_chatLabel->resizeToWidth(w);
	updateChatLabel();

	// Delay between chats
	_delayLabel = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::lng_automation_delay_between(tr::now),
		st::boxLabel);
	_delayBetween->resize(w, _delayBetween->height());

	// Message text (for SendMessage)
	_msgLabel = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::lng_automation_message_text(tr::now),
		st::boxLabel);
	_messageText->resize(w, _messageText->height());

	// Button index (for ClickButton)
	_btnIdxLabel = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::lng_automation_button_index(tr::now),
		st::boxLabel);
	_buttonIndex->resize(w, _buttonIndex->height());

	// Dismiss popup
	_dismissPopup.create(
		this,
		tr::lng_automation_dismiss_popup(tr::now),
		_job.dismissPopup,
		st::defaultCheckbox);

	// Enabled
	_enabled.create(
		this,
		tr::lng_automation_enabled(tr::now),
		_job.enabled,
		st::defaultCheckbox);

	updateActionFields();
	relayout();
}

void AutomationJobEditBox::setInnerFocus() {
	_name->setFocusFast();
}

void AutomationJobEditBox::updateActionFields() {
	const auto action = static_cast<Automation::ActionType>(
		_actionTypeGroup->current());
	const auto isSend = (action == Automation::ActionType::SendMessage);
	_msgLabel->setVisible(isSend);
	_messageText->setVisible(isSend);
	_btnIdxLabel->setVisible(!isSend);
	_buttonIndex->setVisible(!isSend);
	relayout();
}

void AutomationJobEditBox::relayout() {
	const auto left = st::boxPadding.left();
	auto y = st::boxPadding.top();

	const auto place = [&](QWidget *widget) {
		if (widget && !widget->isHidden()) {
			widget->move(left, y);
			y += widget->height() + st::boxLittleSkip;
		}
	};
	const auto placeMedium = [&](QWidget *widget) {
		if (widget && !widget->isHidden()) {
			widget->move(left, y);
			y += widget->height() + st::boxMediumSkip;
		}
	};

	place(_nameLabel);
	placeMedium(_name.data());

	place(_cronLabel);
	_cronExpr->move(left, y);
	y += _cronExpr->height() + st::boxLittleSkip;
	placeMedium(_cronHint);

	place(_runModeLabel);
	_exactBtn->move(left, y);
	y += _exactBtn->heightNoMargins() + st::boxLittleSkip;
	_expiredBtn->move(left, y);
	y += _expiredBtn->heightNoMargins() + st::boxMediumSkip;

	place(_actionLabel);
	_sendBtn->move(left, y);
	y += _sendBtn->heightNoMargins() + st::boxLittleSkip;
	_clickBtn->move(left, y);
	y += _clickBtn->heightNoMargins() + st::boxMediumSkip;

	place(_chatSelectLabel);
	_chatSelectBtn->move(left, y);
	y += _chatSelectBtn->height() + st::boxLittleSkip;
	placeMedium(_chatLabel);

	place(_delayLabel);
	placeMedium(_delayBetween.data());

	place(_msgLabel);
	placeMedium(_messageText.data());

	place(_btnIdxLabel);
	placeMedium(_buttonIndex.data());

	_dismissPopup->move(left, y);
	y += _dismissPopup->heightNoMargins() + st::boxMediumSkip;

	_enabled->move(left, y);
	y += _enabled->heightNoMargins() + st::boxMediumSkip;

	setDimensions(kBoxWidth, y);
}

void AutomationJobEditBox::updateChatLabel() {
	if (_job.peerIds.isEmpty()) {
		_chatLabel->setText(tr::lng_automation_no_chat(tr::now));
	} else {
		const auto session = &_controller->session();
		QStringList names;
		for (const auto &pid : _job.peerIds) {
			const auto peer = session->data().peerLoaded(PeerId(pid));
			if (peer) {
				names.append(peer->name());
			} else {
				names.append(tr::lng_automation_chat_id(tr::now)
					+ QString::number(pid));
			}
		}
		_chatLabel->setText(names.join(QString::fromUtf8(", ")));
	}
	_chatLabel->resizeToWidth(kBoxWidth - 2 * st::boxPadding.left());
}

void AutomationJobEditBox::selectChat() {
	class ChatSelectController final : public PeerListController {
	public:
		ChatSelectController(
			not_null<Main::Session*> session,
			const QVector<uint64> &selectedIds)
		: _session(session)
		, _selectedIds(selectedIds) {
		}

		void prepare() override {
			delegate()->peerListSetSearchMode(
				PeerListSearchMode::Enabled);
			delegate()->peerListSetTitle(
				tr::lng_automation_select_chat());
			const auto &list = _session->data().chatsList()->indexed();
			for (const auto &row : list->all()) {
				if (const auto history = row->history()) {
					auto peerRow = std::make_unique<PeerListRow>(
						history->peer);
					const auto raw = peerRow.get();
					delegate()->peerListAppendRow(std::move(peerRow));
					for (const auto &id : _selectedIds) {
						if (history->peer->id.value == id) {
							delegate()->peerListSetRowChecked(raw, true);
							break;
						}
					}
				}
			}
			delegate()->peerListRefreshRows();
		}

		void rowClicked(not_null<PeerListRow*> row) override {
			delegate()->peerListSetRowChecked(row, !row->checked());
		}

		Main::Session &session() const override {
			return *_session;
		}

	private:
		const not_null<Main::Session*> _session;
		QVector<uint64> _selectedIds;
	};

	auto controller = std::make_unique<ChatSelectController>(
		&_controller->session(),
		_job.peerIds);

	auto initBox = [=, this](not_null<PeerListBox*> box) {
		box->setCloseByOutsideClick(false);
		box->addButton(tr::lng_settings_save(), crl::guard(this, [=, this] {
			const auto peers = box->collectSelectedRows();
			_job.peerIds.clear();
			for (const auto &peer : peers) {
				_job.peerIds.append(peer->id.value);
			}
			updateChatLabel();
			relayout();
			box->closeBox();
		}));
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};

	getDelegate()->show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)),
		Ui::LayerOption::KeepOther);
}

void AutomationJobEditBox::save() {
	_job.name = _name->getLastText().trimmed();
	_job.cronExpr = _cronExpr->getLastText().trimmed();
	_job.runMode = static_cast<Automation::RunMode>(
		_runModeGroup->current());
	_job.actionType = static_cast<Automation::ActionType>(
		_actionTypeGroup->current());
	_job.messageText = _messageText->getLastText();
	_job.dismissPopup = _dismissPopup->checked();
	_job.enabled = _enabled->checked();

	bool btnOk = false;
	const int btnVal = _buttonIndex->getLastText().trimmed().toInt(&btnOk);
	_job.buttonIndex = (btnOk && btnVal > 0) ? (btnVal - 1) : 0;

	bool delayOk = false;
	const double delayVal = _delayBetween->getLastText().trimmed().toDouble(&delayOk);
	_job.delayBetweenSecs = (delayOk && delayVal >= 0) ? delayVal : 0.0;

	if (_job.name.isEmpty()) {
		_name->showError();
		return;
	}
	if (!Automation::CronIsValid(_job.cronExpr)) {
		_cronExpr->showError();
		return;
	}

	if (_isNew) {
		EnhancedSettings::AddAutomationJob(_job);
	} else {
		EnhancedSettings::UpdateAutomationJob(_job);
	}

	closeBox();
}
