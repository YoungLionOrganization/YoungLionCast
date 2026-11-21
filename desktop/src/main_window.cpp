#include "main_window.h"
#include "accounts.h"
#include "media_engine.h"
#include "preview.h"
#include "stream_session.h"
#include "theme.h"
#include <QActionGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyleHints>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
namespace Cast {
static QJsonObject lookup(const QJsonArray &a, const QString &id) {
    for (const auto &v : a)
        if (v.toObject()["id"].toString() == id)
            return v.toObject();
    return {};
}
MainWindow::MainWindow(MediaEngine *engine, QWidget *parent)
    : QMainWindow(parent), m_document(this), m_engine(engine) {
    setObjectName("mainWindow");
    resize(1440, 900);
    setMinimumSize(1000, 680);
    setDockOptions(AnimatedDocks | AllowNestedDocks | AllowTabbedDocks);
    setWindowTitle(tr("YoungLion Cast — Adsız[*]"));
    setWindowModified(false);
    auto *central = new QWidget(this);
    auto *v = new QVBoxLayout(central);
    v->setContentsMargins(12, 8, 12, 8);
    auto *top = new QHBoxLayout;
    auto *brand = new QLabel("YOUNGLION CAST");
    brand->setObjectName("brand");
    top->addWidget(brand);
    top->addStretch();
    auto *note = new QLabel(tr("SAHNE EDİTÖRÜ"));
    note->setObjectName("mutedLabel");
    top->addWidget(note);
    v->addLayout(top);
    m_preview = new Preview(engine, central);
    v->addWidget(m_preview, 1);
    auto *hint = new QLabel(
        tr("Seçili kaynağı taşımak için önizlemede sürükleyin · Ok tuşları: 1 px · Shift: 10 px"));
    hint->setObjectName("mutedLabel");
    v->addWidget(hint);
    setCentralWidget(central);
    auto dock = [this](const QString &title, const QString &name, Qt::DockWidgetArea area,
                       QWidget *widget) {
        auto *d = new QDockWidget(title, this);
        d->setObjectName(name);
        d->setWidget(widget);
        addDockWidget(area, d);
        return d;
    };
    auto panel = [](QListWidget *list, const QList<QPushButton *> &buttons) {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->setContentsMargins(6, 6, 6, 6);
        l->addWidget(list, 1);
        auto *row = new QHBoxLayout;
        for (auto *b : buttons)
            row->addWidget(b);
        row->addStretch();
        l->addLayout(row);
        return w;
    };
    m_scenes = new QListWidget;
    m_scenes->setObjectName("scenesList");
    auto *sceneAdd = new QPushButton("+"), *sceneRemove = new QPushButton("−");
    sceneAdd->setToolTip(tr("Sahne ekle"));
    sceneRemove->setToolTip(tr("Sahne sil"));
    auto *sd = dock(tr("Sahneler"), "scenesDock", Qt::BottomDockWidgetArea,
                    panel(m_scenes, {sceneAdd, sceneRemove}));
    m_sources = new QListWidget;
    m_sources->setObjectName("sourcesList");
    auto *sourceAdd = new QPushButton("+"), *sourceRemove = new QPushButton("−"),
         *up = new QPushButton("↑"), *down = new QPushButton("↓"),
         *props = new QPushButton(tr("Özellikler"));
    auto *sourcesDock = dock(tr("Kaynaklar"), "sourcesDock", Qt::BottomDockWidgetArea,
                             panel(m_sources, {sourceAdd, sourceRemove, up, down, props}));
    splitDockWidget(sd, sourcesDock, Qt::Horizontal);
    auto *content = new QWidget;
    m_form = new QFormLayout(content);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(250);
    scroll->setWidget(content);
    dock(tr("Dönüşüm"), "propertiesDock", Qt::RightDockWidgetArea, scroll);
    auto *account = new QWidget;
    auto *av = new QVBoxLayout(account);
    m_accountStatus = new QLabel(tr("YoungLion Account bağlantısı yapılandırılmadı."));
    m_accountStatus->setWordWrap(true);
    av->addWidget(m_accountStatus);
    m_connections = new QListWidget;
    m_connections->setObjectName("connectionsList");
    av->addWidget(m_connections);
    auto *login = new QPushButton(tr("YoungLion ile giriş yap"));
    login->setObjectName("loginButton");
    auto *sessionRow = new QHBoxLayout;
    sessionRow->addWidget(login);
    av->addLayout(sessionRow);
    auto *accountRow = new QHBoxLayout;
    auto *twitch = new QPushButton(tr("+ Twitch")), *kick = new QPushButton(tr("+ Kick"));
    accountRow->addWidget(twitch);
    accountRow->addWidget(kick);
    av->addLayout(accountRow);
    auto *manageRow = new QHBoxLayout;
    auto *reconnect = new QPushButton(tr("Yeniden bağla")),
         *disconnect = new QPushButton(tr("Bağlantıyı kaldır"));
    manageRow->addWidget(reconnect);
    manageRow->addWidget(disconnect);
    av->addLayout(manageRow);
    auto *logout = new QPushButton(tr("Çıkış yap")), *reload = new QPushButton(tr("Yenile")),
         *cancel = new QPushButton(tr("Bağlantıyı iptal et"));
    sessionRow->addWidget(logout);
    accountRow->addWidget(reload);
    manageRow->addWidget(cancel);
    QString accountError;
    m_accounts = new Accounts(AccountConfig::load(&accountError), this);
    auto updateAccounts = [this, login, twitch, kick, reconnect, disconnect, logout, reload, cancel,
                           accountError] {
        m_accountStatus->setText(m_accounts->configured() ? m_accounts->status() : accountError);
        login->setEnabled(m_accounts->configured() && !m_accounts->authenticated());
        for (auto *b : {twitch, kick, reconnect, disconnect, logout, reload, cancel})
            b->setEnabled(m_accounts->authenticated());
        m_connections->clear();
        for (const auto &v : m_accounts->connections()) {
            const auto c = v.toObject();
            auto *i = new QListWidgetItem(c["provider"].toString() + " · " +
                                              c["display_name"].toString() + " · " +
                                              c["status"].toString(),
                                          m_connections);
            i->setData(Qt::UserRole, c.toVariantMap());
        }
    };
    connect(m_accounts, &Accounts::changed, this, updateAccounts);
    connect(login, &QPushButton::clicked, m_accounts, &Accounts::login);
    connect(logout, &QPushButton::clicked, m_accounts, &Accounts::logout);
    connect(reload, &QPushButton::clicked, m_accounts, &Accounts::reload);
    connect(cancel, &QPushButton::clicked, m_accounts, &Accounts::cancelLink);
    connect(twitch, &QPushButton::clicked, this, [this] { m_accounts->link("twitch"); });
    connect(kick, &QPushButton::clicked, this, [this] { m_accounts->link("kick"); });
    connect(reconnect, &QPushButton::clicked, this, [this] {
        if (auto *i = m_connections->currentItem()) {
            const auto c = i->data(Qt::UserRole).toMap();
            m_accounts->link(c["provider"].toString(), c["id"].toString());
        }
    });
    connect(disconnect, &QPushButton::clicked, this, [this] {
        if (auto *i = m_connections->currentItem()) {
            const auto c = i->data(Qt::UserRole).toMap();
            if (QMessageBox::question(
                    this, tr("Bağlantıyı kaldır"),
                    tr("%1 hesabının yayın yetkisini kaldır?").arg(c["display_name"].toString())) ==
                QMessageBox::Yes)
                m_accounts->disconnectAccount(c["id"].toString());
        }
    });
    connect(m_accounts, &Accounts::pending, this, [this](const QJsonObject &account) {
        if (QMessageBox::question(this, tr("Yayın hesabını onayla"),
                                  tr("%1 hesabını YoungLion hesabınıza bağlamak istiyor musunuz?")
                                      .arg(account["name"].toString())) == QMessageBox::Yes)
            m_accounts->confirmLink();
        else
            m_accounts->cancelLink();
    });
    updateAccounts();
    QTimer::singleShot(0, m_accounts, &Accounts::restore);
    auto *accountsDock =
        dock(tr("Yayın hesapları"), "accountsDock", Qt::BottomDockWidgetArea, account);
    splitDockWidget(sourcesDock, accountsDock, Qt::Horizontal);
    m_stream = new StreamSession(this);
    auto *streamPanel = new QWidget;
    auto *streamLayout = new QVBoxLayout(streamPanel);
    auto *streamLabel = new QLabel(tr("RTMP hedefleri · her satıra bir adres"));
    auto *targets = new QPlainTextEdit;
    targets->setObjectName("streamTargets");
    targets->setPlaceholderText("rtmp://127.0.0.1:1935/live/cast");
    streamLabel->setBuddy(targets);
    targets->setAccessibleName(tr("Yerel RTMP hedefleri"));
    targets->setMaximumBlockCount(12);
    targets->setFixedHeight(76);
    auto *streamStatus = new QLabel;
    streamStatus->setWordWrap(true);
    auto *streamButton = new QPushButton(tr("Yerel yayını başlat"));
    streamButton->setObjectName("localStreamButton");
    streamButton->setEnabled(m_engine->ready() && StreamSession::available());
    streamLayout->addWidget(streamLabel);
    streamLayout->addWidget(targets);
    streamLayout->addWidget(streamStatus);
    streamLayout->addWidget(streamButton);
    streamLayout->addStretch();
    auto *streamDock =
        dock(tr("Yerel yayın"), "localStreamDock", Qt::BottomDockWidgetArea, streamPanel);
    tabifyDockWidget(accountsDock, streamDock);
    accountsDock->raise();
    if (!streamButton->isEnabled())
        streamStatus->setText(tr("Yerel RTMP için H.264 ve RTMP modülleri gerekli."));
    auto *streamTimer = new QTimer(this);
    streamTimer->setInterval(500);
    connect(streamTimer, &QTimer::timeout, this, [this, streamStatus] {
        QStringList lines;
        for (const auto &target : m_stream->status()) {
            QString state;
            switch (target.state) {
            case StreamSession::State::Connecting:
                state = tr("Bağlanıyor");
                break;
            case StreamSession::State::Live:
                state = tr("Yayında");
                break;
            case StreamSession::State::Reconnecting:
                state = tr("Yeniden bağlanıyor");
                break;
            case StreamSession::State::Stopped:
                state = tr("Durdu");
                break;
            case StreamSession::State::Failed:
                state = tr("Bağlantı başarısız");
                break;
            }
            lines.append(tr("Hedef %1 · %2 · %3 KiB · %4 düşen kare")
                             .arg(lines.size() + 1)
                             .arg(state)
                             .arg(target.bytes / 1024)
                             .arg(target.droppedFrames));
        }
        streamStatus->setText(lines.join("\n"));
    });
    connect(streamButton, &QPushButton::clicked, this,
            [this, targets, streamButton, streamStatus, streamTimer] {
                if (m_stream->running()) {
                    streamTimer->stop();
                    m_stream->stop();
                    targets->setReadOnly(false);
                    streamButton->setText(tr("Yerel yayını başlat"));
                    streamStatus->setText(tr("Yerel yayın durduruldu."));
                    m_engine->sync(m_document.json());
                    return;
                }
                QStringList urls;
                for (const auto &line : targets->toPlainText().split(u'\n')) {
                    if (!line.trimmed().isEmpty())
                        urls.append(line.trimmed());
                }
                QString error;
                if (!m_stream->start(urls, &error)) {
                    streamStatus->setText(error);
                    return;
                }
                targets->setReadOnly(true);
                streamButton->setText(tr("Yerel yayını durdur"));
                streamStatus->setText(tr("Bağlanıyor"));
                streamTimer->start();
            });
    auto *file = menuBar()->addMenu(tr("Dosya"));
    file->addAction(tr("Yeni proje"), QKeySequence::New, this, [this] {
        if (mayDiscard())
            m_document.reset();
    });
    file->addAction(tr("Aç…"), QKeySequence::Open, this, [this] {
        const auto f = QFileDialog::getOpenFileName(this, tr("Proje aç"), {},
                                                    tr("YoungLion projesi (*.ylcast)"));
        if (!f.isEmpty())
            openProject(f);
    });
    file->addAction(tr("Kaydet"), QKeySequence::Save, this, [this] { save(); });
    file->addAction(tr("Farklı kaydet…"), QKeySequence::SaveAs, this, [this] { save(true); });
    file->addSeparator();
    file->addAction(tr("Çıkış"), QKeySequence::Quit, this, &QWidget::close);
    auto *edit = menuBar()->addMenu(tr("Düzenle"));
    auto *undo = m_document.undoStack()->createUndoAction(this, tr("Geri al"));
    undo->setShortcut(QKeySequence::Undo);
    edit->addAction(undo);
    auto *redo = m_document.undoStack()->createRedoAction(this, tr("Yinele"));
    redo->setShortcuts(QKeySequence::Redo);
    edit->addAction(redo);
    auto *view = menuBar()->addMenu(tr("Görünüm"));
    for (auto *d : findChildren<QDockWidget *>())
        view->addAction(d->toggleViewAction());
    view->addAction(tr("Yerleşimi sıfırla"), this, [this] { restoreState(m_defaultLayout); });
    auto *themes = view->addMenu(tr("Tema"));
    auto *group = new QActionGroup(this);
    for (const auto &pair :
         {qMakePair(tr("Sistem"), QString("system")), qMakePair(tr("Koyu"), QString("dark")),
          qMakePair(tr("Açık"), QString("light"))}) {
        auto *a = themes->addAction(pair.first);
        a->setCheckable(true);
        a->setData(pair.second);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [=, this] { setTheme(pair.second); });
    }
    auto *canvas = menuBar()->addMenu(tr("Tuval"));
    for (const auto &size : {QSize(1920, 1080), QSize(1280, 720)})
        for (const int fps : {30, 60})
            canvas->addAction(
                QString("%1 × %2 / %3 FPS").arg(size.width()).arg(size.height()).arg(fps), this,
                [=, this] { m_document.setCanvas(size.width(), size.height(), fps); });
    auto *help = menuBar()->addMenu(tr("Yardım"));
    help->addAction(tr("Hakkında"), this, [this] {
        QMessageBox::about(
            this, tr("YoungLion Cast"),
            tr("Qt 6.12 · C++20 · libobs 33.0.1\nGPL-3.0-or-later\nBu sürüm sahne düzenleme ve "
               "yakalama içindir. Yayın, kayıt ve ses çıkışı içermez."));
    });
    auto *addMenu = new QMenu(sourceAdd);
    addMenu->setToolTipsVisible(true);
    for (const auto &pair :
         {qMakePair(tr("Ekran yakalama"), QString("display")),
          qMakePair(tr("Pencere yakalama"), QString("window")),
          qMakePair(tr("Uygulama yakalama (macOS)"), QString("application")),
          qMakePair(tr("Oyun yakalama (Windows)"), QString("game")),
          qMakePair(tr("Yazı"), QString("text")), qMakePair(tr("Görsel"), QString("image"))}) {
        auto *a = addMenu->addAction(pair.first);
        a->setEnabled(m_engine->supported(pair.second));
        if (!a->isEnabled())
            a->setToolTip(tr("Bu platform veya oturumda kullanılamıyor."));
        connect(a, &QAction::triggered, this, [=, this] { addSource(pair.second); });
    }
    sourceAdd->setMenu(addMenu);
    connect(sceneAdd, &QPushButton::clicked, this, &MainWindow::addScene);
    connect(sceneRemove, &QPushButton::clicked, this,
            [this] { m_document.removeScene(m_document.activeScene()); });
    connect(m_scenes, &QListWidget::itemDoubleClicked, this, &MainWindow::renameScene);
    connect(m_scenes, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *i) {
        if (i && !m_refreshing)
            m_document.selectScene(i->data(Qt::UserRole).toString());
    });
    connect(sourceRemove, &QPushButton::clicked, this,
            [this] { m_document.removeSource(selected()); });
    connect(up, &QPushButton::clicked, this, [this] { m_document.moveItem(selected(), 1); });
    connect(down, &QPushButton::clicked, this, [this] { m_document.moveItem(selected(), -1); });
    connect(props, &QPushButton::clicked, this, &MainWindow::editSource);
    connect(m_sources, &QListWidget::itemDoubleClicked, this, &MainWindow::editSource);
    connect(m_sources, &QListWidget::currentItemChanged, this, [this] {
        if (!m_refreshing)
            inspector();
    });
    connect(m_sources, &QListWidget::itemChanged, this, [this](QListWidgetItem *i) {
        if (!m_refreshing)
            m_document.setItem(i->data(Qt::UserRole).toString(),
                               {{"visible", i->checkState() == Qt::Checked}});
    });
    connect(&m_document, &SceneDocument::changed, this, [this] {
        m_engine->sync(m_document.json());
        refresh();
    });
    connect(&m_document, &SceneDocument::rejected, this,
            [this](const QString &s) { statusBar()->showMessage(s, 6000); });
    connect(m_document.undoStack(), &QUndoStack::cleanChanged, this,
            [this](bool clean) { setWindowModified(!clean); });
    connect(engine, &MediaEngine::error, this,
            [this](const QString &s) { statusBar()->showMessage(s, 10000); });
    connect(m_preview, &Preview::moved, this, [this](QPointF delta) {
        const auto item = lookup(m_document.items(), selected());
        if (!item.isEmpty())
            m_document.setItem(selected(), {{"x", item["x"].toDouble() + delta.x()},
                                            {"y", item["y"].toDouble() + delta.y()}});
    });
    for (const auto &key : {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down})
        for (bool shift : {false, true}) {
            auto *a = new QAction(this);
            a->setShortcut(QKeySequence(int(key) | (shift ? int(Qt::SHIFT) : 0)));
            a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            addAction(a);
            connect(a, &QAction::triggered, this, [=, this] {
                if (qobject_cast<QLineEdit *>(focusWidget()) ||
                    qobject_cast<QAbstractSpinBox *>(focusWidget()))
                    return;
                auto item = lookup(m_document.items(), selected());
                if (item.isEmpty())
                    return;
                const int step = shift ? 10 : 1;
                const bool x = key == Qt::Key_Left || key == Qt::Key_Right;
                const auto field = x ? "x" : "y";
                const int sign = key == Qt::Key_Left || key == Qt::Key_Up ? -1 : 1;
                m_document.setItem(selected(), {{field, item[field].toDouble() + step * sign}});
            });
        }
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        if (m_sourceStatus && !selected().isEmpty())
            m_sourceStatus->setText(
                m_engine->status(lookup(m_document.items(), selected())["sourceId"].toString()));
    });
    timer->start(1500);
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_theme == "system")
            applyTheme(m_theme);
    });
    resizeDocks({sd, sourcesDock, accountsDock}, {260, 260, 260}, Qt::Vertical);
    resizeDocks({sd, sourcesDock, accountsDock}, {250, 550, 450}, Qt::Horizontal);
    m_defaultLayout = saveState();
    restoreLayout();
    setTheme(QSettings().value("theme", "system").toString());
    for (auto *a : group->actions())
        a->setChecked(a->data().toString() == m_theme);
    m_engine->sync(m_document.json());
    refresh();
    statusBar()->showMessage(
        m_engine->ready() ? tr("Hazır · GPU önizlemesi")
                          : tr("Yakalama motoru kullanılamıyor · çevrimdışı sahne düzenleme"));
}
MainWindow::~MainWindow() {
    m_stream->stop();
    delete m_preview;
    m_preview = nullptr;
}
QString MainWindow::selected() const {
    return m_sources->currentItem() ? m_sources->currentItem()->data(Qt::UserRole).toString()
                                    : QString{};
}
void MainWindow::setTheme(const QString &mode) {
    m_theme = QStringList{"light", "dark", "system"}.contains(mode) ? mode : "system";
    applyTheme(m_theme);
    QSettings().setValue("theme", m_theme);
}
void MainWindow::restoreLayout() {
    QSettings s;
    if (s.contains("geometry"))
        restoreGeometry(s.value("geometry").toByteArray());
    if (s.contains("layout"))
        restoreState(s.value("layout").toByteArray());
}
void MainWindow::refresh() {
    m_refreshing = true;
    const auto selection = selected();
    m_scenes->clear();
    for (const auto &v : m_document.scenes()) {
        auto scene = v.toObject();
        auto *i = new QListWidgetItem(scene["name"].toString(), m_scenes);
        i->setData(Qt::UserRole, scene["id"].toString());
        if (scene["id"].toString() == m_document.activeScene())
            m_scenes->setCurrentItem(i);
    }
    m_sources->clear();
    auto items = m_document.items();
    for (qsizetype n = items.size(); n > 0; --n) {
        const auto item = items[n - 1].toObject(),
                   source = lookup(m_document.sources(), item["sourceId"].toString());
        auto *i =
            new QListWidgetItem((item["locked"].toBool() ? QString::fromUtf8("🔒 ") : QString{}) +
                                    source["name"].toString(),
                                m_sources);
        i->setData(Qt::UserRole, item["id"].toString());
        i->setFlags(i->flags() | Qt::ItemIsUserCheckable);
        i->setCheckState(item["visible"].toBool() ? Qt::Checked : Qt::Unchecked);
        i->setToolTip(m_engine->status(source["id"].toString()));
        if (item["id"].toString() == selection)
            m_sources->setCurrentItem(i);
    }
    if (!m_sources->currentItem() && m_sources->count())
        m_sources->setCurrentRow(0);
    m_refreshing = false;
    inspector();
    setWindowTitle(tr("YoungLion Cast — %1[*]")
                       .arg(m_document.path().isEmpty() ? tr("Adsız")
                                                        : QFileInfo(m_document.path()).fileName()));
}
void MainWindow::inspector() {
    while (m_form->rowCount())
        m_form->removeRow(0);
    m_sourceStatus = nullptr;
    const auto id = selected();
    const auto item = lookup(m_document.items(), id);
    m_engine->setSelection(item);
    if (item.isEmpty()) {
        m_form->addRow(new QLabel(tr("Düzenlemek için kaynak seçin.")));
        return;
    }
    auto *locked = new QCheckBox;
    locked->setChecked(item["locked"].toBool());
    m_form->addRow(tr("Kilitle"), locked);
    connect(locked, &QCheckBox::toggled, this,
            [=, this](bool b) { m_document.setItem(id, {{"locked", b}}); });
    for (const auto &pair :
         {qMakePair(tr("X"), QString("x")), qMakePair(tr("Y"), QString("y")),
          qMakePair(tr("Ölçek X"), QString("scaleX")), qMakePair(tr("Ölçek Y"), QString("scaleY")),
          qMakePair(tr("Döndürme °"), QString("rotation")),
          qMakePair(tr("Opaklık"), QString("opacity")),
          qMakePair(tr("Kırp sol"), QString("cropLeft")),
          qMakePair(tr("Kırp üst"), QString("cropTop")),
          qMakePair(tr("Kırp sağ"), QString("cropRight")),
          qMakePair(tr("Kırp alt"), QString("cropBottom"))}) {
        auto *spin = new QDoubleSpinBox;
        const bool scale = pair.second.startsWith("scale"), crop = pair.second.startsWith("crop");
        spin->setRange(scale                              ? 0.01
                       : crop || pair.second == "opacity" ? 0
                                                          : -100000,
                       pair.second == "opacity" ? 1 : 100000);
        spin->setDecimals(crop ? 0 : 2);
        spin->setSingleStep(scale || pair.second == "opacity" ? 0.05 : 1);
        spin->setValue(item[pair.second].toDouble());
        spin->setEnabled(!item["locked"].toBool());
        m_form->addRow(pair.first, spin);
        connect(spin, &QAbstractSpinBox::editingFinished, this,
                [=, this] { m_document.setItem(id, {{pair.second, spin->value()}}); });
    }
    auto *fit = new QPushButton(tr("Tuvale sığdır"));
    m_form->addRow(fit);
    connect(fit, &QPushButton::clicked, this, [=, this] {
        const auto size = m_engine->sourceSize(item["sourceId"].toString()),
                   canvas = m_engine->canvas();
        if (size.width() > 0 && size.height() > 0) {
            const double s = qMin(double(canvas.width()) / size.width(),
                                  double(canvas.height()) / size.height());
            m_document.setItem(id, {{"x", 0},
                                    {"y", 0},
                                    {"scaleX", s},
                                    {"scaleY", s},
                                    {"rotation", 0},
                                    {"cropLeft", 0},
                                    {"cropTop", 0},
                                    {"cropRight", 0},
                                    {"cropBottom", 0}});
        }
    });
    m_sourceStatus = new QLabel(m_engine->status(item["sourceId"].toString()));
    m_sourceStatus->setWordWrap(true);
    m_sourceStatus->setObjectName("mutedLabel");
    m_form->addRow(m_sourceStatus);
}
void MainWindow::addScene() {
    bool ok = false;
    auto name = QInputDialog::getText(this, tr("Sahne ekle"), tr("Sahne adı"), QLineEdit::Normal,
                                      tr("Yeni sahne"), &ok);
    if (ok)
        m_document.addScene(name);
}
void MainWindow::renameScene() {
    const auto id = m_document.activeScene();
    const auto scene = lookup(m_document.scenes(), id);
    bool ok = false;
    const auto name = QInputDialog::getText(this, tr("Sahneyi yeniden adlandır"), tr("Sahne adı"),
                                            QLineEdit::Normal, scene["name"].toString(), &ok);
    if (ok)
        m_document.renameScene(id, name);
}
void MainWindow::addSource(const QString &kind) {
    QJsonObject settings = m_engine->defaults(kind);
    if (kind == "image") {
        const auto f = QFileDialog::getOpenFileName(
            this, tr("Görsel seç"), {}, tr("Görseller (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"));
        if (f.isEmpty())
            return;
        settings["file"] = f;
    } else if (kind == "text") {
        settings = QJsonObject{{"text", tr("Yeni yazı")},
                               {"fontFace", "Sans Serif"},
                               {"fontSize", 48},
                               {"color", double(0xffffffff)}};
    }
    if (kind != "image" && kind != "text")
        settings = {{"needsSelection", true}};
    const auto name = kind == "image"         ? tr("Görsel")
                      : kind == "text"        ? tr("Yazı")
                      : kind == "game"        ? tr("Oyun")
                      : kind == "window"      ? tr("Pencere")
                      : kind == "application" ? tr("Uygulama")
                                              : tr("Ekran");
    const auto id = m_document.addSource(kind, name, settings);
    for (int i = 0; i < m_sources->count(); ++i)
        if (m_sources->item(i)->data(Qt::UserRole).toString() == id)
            m_sources->setCurrentRow(i);
    if (!id.isEmpty())
        editSource();
}
void MainWindow::editSource() {
    const auto item = lookup(m_document.items(), selected()),
               source = lookup(m_document.sources(), item["sourceId"].toString());
    if (source.isEmpty())
        return;
    const auto kind = source["kind"].toString(), sourceId = source["id"].toString();
    auto settings = source["settings"].toObject();
    const bool selectCapture = settings["needsSelection"].toBool();
    if (selectCapture) {
        if (!m_engine->supported(kind)) {
            QMessageBox::information(this, tr("Yakalama"),
                                     tr("Bu kaynak türü bu oturumda desteklenmiyor."));
            return;
        }
        settings = m_engine->defaults(kind);
        for (const auto &key : {"window", "capture_window", "screen", "monitor", "monitor_id",
                                "display_uuid", "RestoreToken", "application"})
            settings.remove(key);
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Kaynak özellikleri"));
    dialog.resize(520, 420);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    auto *name = new QLineEdit(source["name"].toString());
    name->setMaxLength(160);
    form->addRow(tr("Ad"), name);
    QPlainTextEdit *text = nullptr;
    QLineEdit *file = nullptr;
    QSpinBox *size = nullptr;
    QPushButton *font = nullptr, *color = nullptr;
    QList<std::pair<QString, QWidget *>> fields;
    if (kind == "text") {
        text = new QPlainTextEdit(settings["text"].toString());
        form->addRow(tr("Yazı"), text);
        size = new QSpinBox;
        size->setRange(8, 512);
        size->setValue(settings["fontSize"].toInt(48));
        form->addRow(tr("Boyut"), size);
        font = new QPushButton(settings["fontFace"].toString("Sans Serif"));
        form->addRow(tr("Yazı tipi"), font);
        connect(font, &QPushButton::clicked, &dialog, [&] {
            bool ok = false;
            auto chosen = QFontDialog::getFont(&ok, QFont(font->text()), &dialog);
            if (ok)
                font->setText(chosen.family());
        });
        color = new QPushButton(tr("Renk seç…"));
        form->addRow(tr("Renk"), color);
        connect(color, &QPushButton::clicked, &dialog, [&] {
            const auto bgr = quint32(settings["color"].toDouble(0xffffffff));
            auto c = QColorDialog::getColor(
                QColor(int(bgr & 255), int((bgr >> 8) & 255), int((bgr >> 16) & 255)), &dialog);
            if (c.isValid())
                settings["color"] = double(0xff000000u | uint(c.red()) | uint(c.green() << 8) |
                                           uint(c.blue() << 16));
        });
    } else if (kind == "image") {
        file = new QLineEdit(settings["file"].toString());
        form->addRow(tr("Dosya"), file);
        auto *browse = new QPushButton(tr("Gözat…"));
        form->addRow(browse);
        connect(browse, &QPushButton::clicked, &dialog, [&] {
            const auto f = QFileDialog::getOpenFileName(&dialog, tr("Görsel seç"));
            if (!f.isEmpty())
                file->setText(f);
        });
    } else {
        auto *warning = new QLabel(tr("Yalnız seçtiğiniz hedef yakalanır. Wayland/macOS ekran izni "
                                      "isteyebilir. Hedef kapanırsa özelliklerden yeniden seçin."));
        warning->setWordWrap(true);
        layout->addWidget(warning);
        for (const auto &p : m_engine->properties(sourceId, kind)) {
            QWidget *field = nullptr;
            if (p.type == OBS_PROPERTY_LIST) {
                auto *combo = new QComboBox;
                for (const auto &o : p.options)
                    combo->addItem(o.label, o.value.toVariant());
                const auto value = settings[p.key];
                int found = -1;
                for (int i = 0; i < combo->count(); ++i)
                    if (QJsonValue::fromVariant(combo->itemData(i)) == value)
                        found = i;
                combo->setCurrentIndex(found);
                field = combo;
            } else if (p.type == OBS_PROPERTY_BOOL) {
                auto *check = new QCheckBox;
                check->setChecked(settings[p.key].toBool());
                field = check;
            } else if (p.type == OBS_PROPERTY_INT) {
                auto *spin = new QSpinBox;
                spin->setRange(p.minimum, p.maximum);
                spin->setValue(settings[p.key].toInt());
                field = spin;
            } else if (p.type == OBS_PROPERTY_BUTTON) {
                auto *button = new QPushButton(p.label);
                connect(button, &QPushButton::clicked, &dialog,
                        [=, this] { m_engine->propertyButton(sourceId, p.key); });
                field = button;
            }
            if (field) {
                form->addRow(p.label, field);
                fields.append({p.key, field});
            }
        }
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (text) {
        settings["text"] = text->toPlainText();
        settings["fontSize"] = size->value();
        settings["fontFace"] = font->text();
    }
    if (file)
        settings["file"] = file->text();
    for (const auto &[key, field] : fields) {
        if (auto *c = qobject_cast<QComboBox *>(field)) {
            if (c->currentIndex() >= 0)
                settings[key] = QJsonValue::fromVariant(c->currentData());
        } else if (auto *b = qobject_cast<QCheckBox *>(field))
            settings[key] = b->isChecked();
        else if (auto *s = qobject_cast<QSpinBox *>(field))
            settings[key] = s->value();
    }
    if (selectCapture && !m_engine->captureTargetSelected(kind, settings)) {
        statusBar()->showMessage(tr("Önce bir yakalama hedefi seçin."), 6000);
        return;
    }
    m_document.setSource(sourceId, name->text(), settings);
}
bool MainWindow::mayDiscard() {
    if (m_document.undoStack()->isClean())
        return true;
    const auto choice = QMessageBox::warning(
        this, tr("Kaydedilmemiş değişiklikler"), tr("Projeyi kaydetmek ister misiniz?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    return choice == QMessageBox::Discard || (choice == QMessageBox::Save && save());
}
bool MainWindow::save(bool as) {
    auto file = as ? QString{} : m_document.path();
    if (file.isEmpty())
        file = QFileDialog::getSaveFileName(this, tr("Projeyi kaydet"), "YoungLion.ylcast",
                                            tr("YoungLion projesi (*.ylcast)"));
    if (file.isEmpty())
        return false;
    if (!file.endsWith(".ylcast"))
        file += ".ylcast";
    QString error;
    if (!m_document.save(file, &error)) {
        QMessageBox::warning(this, tr("Kaydetme hatası"), error);
        return false;
    }
    refresh();
    return true;
}
void MainWindow::openProject(const QString &file) {
    if (!mayDiscard())
        return;
    QString error;
    if (!m_document.load(file, &error))
        QMessageBox::warning(this, tr("Proje açılmadı"), error);
    else
        refresh();
}
void MainWindow::closeEvent(QCloseEvent *e) {
    if (!mayDiscard()) {
        e->ignore();
        return;
    }
    QSettings s;
    s.setValue("geometry", saveGeometry());
    s.setValue("layout", saveState());
    e->accept();
}
void MainWindow::changeEvent(QEvent *e) {
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange)
        m_preview->setSuspended(isMinimized());
}
} // namespace Cast
