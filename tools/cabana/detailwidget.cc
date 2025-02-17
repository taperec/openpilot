#include "tools/cabana/detailwidget.h"

#include <QFormLayout>
#include <QMenu>
#include <QMessageBox>

#include "tools/cabana/commands.h"

// DetailWidget

DetailWidget::DetailWidget(ChartsWidget *charts, QWidget *parent) : charts(charts), QWidget(parent) {
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);

  // tabbar
  tabbar = new TabBar(this);
  tabbar->setUsesScrollButtons(true);
  tabbar->setAutoHide(true);
  tabbar->setContextMenuPolicy(Qt::CustomContextMenu);
  main_layout->addWidget(tabbar);

  // message title
  QHBoxLayout *title_layout = new QHBoxLayout();
  title_layout->setContentsMargins(3, 6, 3, 0);
  time_label = new QLabel(this);
  time_label->setToolTip(tr("Current time"));
  time_label->setStyleSheet("QLabel{font-weight:bold;}");
  title_layout->addWidget(time_label);
  name_label = new ElidedLabel(this);
  name_label->setStyleSheet("QLabel{font-weight:bold;}");
  name_label->setAlignment(Qt::AlignCenter);
  name_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  title_layout->addWidget(name_label);
  auto edit_btn = new ToolButton("pencil", tr("Edit Message"));
  title_layout->addWidget(edit_btn);
  remove_btn = new ToolButton("x-lg", tr("Remove Message"));
  title_layout->addWidget(remove_btn);
  main_layout->addLayout(title_layout);

  // warning
  warning_widget = new QWidget(this);
  QHBoxLayout *warning_hlayout = new QHBoxLayout(warning_widget);
  warning_hlayout->addWidget(warning_icon = new QLabel(this), 0, Qt::AlignTop);
  warning_hlayout->addWidget(warning_label = new QLabel(this), 1, Qt::AlignLeft);
  warning_widget->hide();
  main_layout->addWidget(warning_widget);

  // msg widget
  splitter = new QSplitter(Qt::Vertical, this);
  splitter->addWidget(binary_view = new BinaryView(this));
  splitter->addWidget(signal_view = new SignalView(charts, this));
  binary_view->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
  signal_view->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);

  tab_widget = new QTabWidget(this);
  tab_widget->setStyleSheet("QTabWidget::pane {border: none; margin-bottom: -2px;}");
  tab_widget->setTabPosition(QTabWidget::South);
  tab_widget->addTab(splitter, utils::icon("file-earmark-ruled"), "&Msg");
  tab_widget->addTab(history_log = new LogsWidget(this), utils::icon("stopwatch"), "&Logs");
  main_layout->addWidget(tab_widget);

  QObject::connect(edit_btn, &QToolButton::clicked, this, &DetailWidget::editMsg);
  QObject::connect(remove_btn, &QToolButton::clicked, this, &DetailWidget::removeMsg);
  QObject::connect(binary_view, &BinaryView::resizeSignal, signal_view->model, &SignalModel::resizeSignal);
  QObject::connect(binary_view, &BinaryView::addSignal, signal_view->model, &SignalModel::addSignal);
  QObject::connect(binary_view, &BinaryView::signalHovered, signal_view, &SignalView::signalHovered);
  QObject::connect(binary_view, &BinaryView::signalClicked, [this](const cabana::Signal *s) { signal_view->selectSignal(s, true); });
  QObject::connect(binary_view, &BinaryView::editSignal, signal_view->model, &SignalModel::saveSignal);
  QObject::connect(binary_view, &BinaryView::removeSignal, signal_view->model, &SignalModel::removeSignal);
  QObject::connect(binary_view, &BinaryView::showChart, charts, &ChartsWidget::showChart);
  QObject::connect(signal_view, &SignalView::showChart, charts, &ChartsWidget::showChart);
  QObject::connect(signal_view, &SignalView::highlight, binary_view, &BinaryView::highlight);
  QObject::connect(tab_widget, &QTabWidget::currentChanged, [this]() { updateState(); });
  QObject::connect(can, &AbstractStream::msgsReceived, this, &DetailWidget::updateState);
  QObject::connect(dbc(), &DBCManager::DBCFileChanged, this, &DetailWidget::refresh);
  QObject::connect(UndoStack::instance(), &QUndoStack::indexChanged, this, &DetailWidget::refresh);
  QObject::connect(tabbar, &QTabBar::customContextMenuRequested, this, &DetailWidget::showTabBarContextMenu);
  QObject::connect(tabbar, &QTabBar::currentChanged, [this](int index) {
    if (index != -1) {
      setMessage(tabbar->tabData(index).value<MessageId>());
    }
  });
  QObject::connect(tabbar, &QTabBar::tabCloseRequested, tabbar, &QTabBar::removeTab);
  QObject::connect(charts, &ChartsWidget::seriesChanged, signal_view, &SignalView::updateChartState);
}

void DetailWidget::showTabBarContextMenu(const QPoint &pt) {
  int index = tabbar->tabAt(pt);
  if (index >= 0) {
    QMenu menu(this);
    menu.addAction(tr("Close Other Tabs"));
    if (menu.exec(tabbar->mapToGlobal(pt))) {
      tabbar->moveTab(index, 0);
      tabbar->setCurrentIndex(0);
      while (tabbar->count() > 1) {
        tabbar->removeTab(1);
      }
    }
  }
}

void DetailWidget::setMessage(const MessageId &message_id) {
  if (std::exchange(msg_id, message_id) == message_id) return;

  tabbar->blockSignals(true);
  int index = tabbar->count() - 1;
  for (/**/; index >= 0; --index) {
    if (tabbar->tabData(index).value<MessageId>() == message_id) break;
  }
  if (index == -1) {
    index = tabbar->addTab(message_id.toString());
    tabbar->setTabData(index, QVariant::fromValue(message_id));
    tabbar->setTabToolTip(index, msgName(message_id));
  }
  tabbar->setCurrentIndex(index);
  tabbar->blockSignals(false);

  setUpdatesEnabled(false);
  signal_view->setMessage(msg_id);
  binary_view->setMessage(msg_id);
  history_log->setMessage(msg_id);
  refresh();
  setUpdatesEnabled(true);
}

void DetailWidget::refresh() {
  QStringList warnings;
  auto msg = dbc()->msg(msg_id);
  if (msg) {
    if (msg->size != can->lastMessage(msg_id).dat.size()) {
      warnings.push_back(tr("Message size (%1) is incorrect.").arg(msg->size));
    }
    for (auto s : binary_view->getOverlappingSignals()) {
      warnings.push_back(tr("%1 has overlapping bits.").arg(s->name));
    }
  } else {
    warnings.push_back(tr("Drag-Select in binary view to create new signal."));
  }
  remove_btn->setEnabled(msg != nullptr);
  name_label->setText(msgName(msg_id));

  if (!warnings.isEmpty()) {
    warning_label->setText(warnings.join('\n'));
    warning_icon->setPixmap(utils::icon(msg ? "exclamation-triangle" : "info-circle"));
  }
  warning_widget->setVisible(!warnings.isEmpty());
}

void DetailWidget::updateState(const QHash<MessageId, CanData> *msgs) {
  time_label->setText(QString::number(can->currentSec(), 'f', 3));
  if ((msgs && !msgs->contains(msg_id)))
    return;

  if (tab_widget->currentIndex() == 0)
    binary_view->updateState();
  else
    history_log->updateState();
}

void DetailWidget::editMsg() {
  auto msg = dbc()->msg(msg_id);
  int size = msg ? msg->size : can->lastMessage(msg_id).dat.size();
  EditMessageDialog dlg(msg_id, msgName(msg_id), size, this);
  if (dlg.exec()) {
    UndoStack::push(new EditMsgCommand(msg_id, dlg.name_edit->text(), dlg.size_spin->value()));
  }
}

void DetailWidget::removeMsg() {
  UndoStack::push(new RemoveMsgCommand(msg_id));
}

// EditMessageDialog

EditMessageDialog::EditMessageDialog(const MessageId &msg_id, const QString &title, int size, QWidget *parent)
    : original_name(title), msg_id(msg_id), QDialog(parent) {
  setWindowTitle(tr("Edit message: %1").arg(msg_id.toString()));
  QFormLayout *form_layout = new QFormLayout(this);

  form_layout->addRow("", error_label = new QLabel);
  error_label->setVisible(false);
  name_edit = new QLineEdit(title, this);
  name_edit->setValidator(new NameValidator(name_edit));
  form_layout->addRow(tr("Name"), name_edit);

  size_spin = new QSpinBox(this);
  // TODO: limit the maximum?
  size_spin->setMinimum(1);
  size_spin->setValue(size);
  form_layout->addRow(tr("Size"), size_spin);

  btn_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  validateName(name_edit->text());
  form_layout->addRow(btn_box);

  setFixedWidth(parent->width() * 0.9);
  connect(name_edit, &QLineEdit::textEdited, this, &EditMessageDialog::validateName);
  connect(btn_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(btn_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void EditMessageDialog::validateName(const QString &text) {
  bool valid = text.compare(UNTITLED, Qt::CaseInsensitive) != 0;
  error_label->setVisible(false);
  if (!text.isEmpty() && valid && text != original_name) {
    valid = dbc()->msg(msg_id.source, text) == nullptr;
    if (!valid) {
      error_label->setText(tr("Name already exists"));
      error_label->setVisible(true);
    }
  }
  btn_box->button(QDialogButtonBox::Ok)->setEnabled(valid);
}

// CenterWidget

CenterWidget::CenterWidget(ChartsWidget *charts, QWidget *parent) : charts(charts), QWidget(parent) {
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->addWidget(welcome_widget = createWelcomeWidget());
}

void CenterWidget::setMessage(const MessageId &msg_id) {
  if (!detail_widget) {
    delete welcome_widget;
    welcome_widget = nullptr;
    layout()->addWidget(detail_widget = new DetailWidget(charts, this));
  }
  detail_widget->setMessage(msg_id);
}

void CenterWidget::clear() {
  delete detail_widget;
  detail_widget = nullptr;
  if (!welcome_widget) {
    layout()->addWidget(welcome_widget = createWelcomeWidget());
  }
}

QWidget *CenterWidget::createWelcomeWidget() {
  QWidget *w = new QWidget(this);
  QVBoxLayout *main_layout = new QVBoxLayout(w);
  main_layout->addStretch(0);
  QLabel *logo = new QLabel("CABANA");
  logo->setAlignment(Qt::AlignCenter);
  logo->setStyleSheet("font-size:50px;font-weight:bold;");
  main_layout->addWidget(logo);

  auto newShortcutRow = [](const QString &title, const QString &key) {
    QHBoxLayout *hlayout = new QHBoxLayout();
    auto btn = new QToolButton();
    btn->setText(key);
    btn->setEnabled(false);
    hlayout->addWidget(new QLabel(title), 0, Qt::AlignRight);
    hlayout->addWidget(btn, 0, Qt::AlignLeft);
    return hlayout;
  };

  auto lb = new QLabel(tr("<-Select a message to view details"));
  lb->setAlignment(Qt::AlignHCenter);
  main_layout->addWidget(lb);
  main_layout->addLayout(newShortcutRow("Pause", "Space"));
  main_layout->addLayout(newShortcutRow("Help", "F1"));
  main_layout->addLayout(newShortcutRow("WhatsThis", "Shift+F1"));
  main_layout->addStretch(0);

  w->setStyleSheet("QLabel{color:darkGray;}");
  w->setBackgroundRole(QPalette::Base);
  w->setAutoFillBackground(true);
  return w;
}
