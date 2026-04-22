/*
This file is part of 64Gram Desktop,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/AyuGram/AyuGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "data/automation/automation_job.h"

namespace Ui {
class VerticalLayout;
class InputField;
class Checkbox;
class RadiobuttonGroup;
class Radiobutton;
class FlatLabel;
class SettingsButton;
class LinkButton;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

class AutomationJobListBox : public Ui::BoxContent {
public:
	AutomationJobListBox(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

protected:
	void prepare() override;
	void showEvent(QShowEvent *e) override;

private:
	void addJob();
	void editJob(const QString &jobId);
	void deleteJob(const QString &jobId);
	void refreshList();

	const not_null<Window::SessionController*> _controller;
	object_ptr<Ui::VerticalLayout> _list = {nullptr};
	bool _prepared = false;
};

class AutomationJobEditBox : public Ui::BoxContent {
public:
	AutomationJobEditBox(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		const Automation::AutomationJob &job,
		bool isNew);

protected:
	void prepare() override;
	void setInnerFocus() override;

private:
	void save();
	void selectChat();
	void updateChatLabel();
	void updateActionFields();
	void relayout();

	const not_null<Window::SessionController*> _controller;
	Automation::AutomationJob _job;
	bool _isNew = false;

	// Labels stored for relayout / visibility toggling.
	Ui::FlatLabel *_nameLabel = nullptr;
	Ui::FlatLabel *_cronLabel = nullptr;
	Ui::FlatLabel *_cronHint = nullptr;
	Ui::FlatLabel *_runModeLabel = nullptr;
	Ui::FlatLabel *_actionLabel = nullptr;
	Ui::FlatLabel *_chatSelectLabel = nullptr;
	Ui::FlatLabel *_chatLabel = nullptr;
	Ui::FlatLabel *_msgLabel = nullptr;
	Ui::FlatLabel *_btnIdxLabel = nullptr;
	Ui::FlatLabel *_delayLabel = nullptr;

	object_ptr<Ui::InputField> _name = {nullptr};
	object_ptr<Ui::InputField> _cronExpr = {nullptr};
	object_ptr<Ui::InputField> _messageText = {nullptr};
	object_ptr<Ui::InputField> _buttonIndex = {nullptr};
	object_ptr<Ui::InputField> _delayBetween = {nullptr};
	object_ptr<Ui::Checkbox> _dismissPopup = {nullptr};
	object_ptr<Ui::Checkbox> _enabled = {nullptr};
	Ui::LinkButton *_chatSelectBtn = nullptr;
	Ui::Radiobutton *_exactBtn = nullptr;
	Ui::Radiobutton *_expiredBtn = nullptr;
	Ui::Radiobutton *_sendBtn = nullptr;
	Ui::Radiobutton *_clickBtn = nullptr;
	std::shared_ptr<Ui::RadiobuttonGroup> _runModeGroup;
	std::shared_ptr<Ui::RadiobuttonGroup> _actionTypeGroup;
};
