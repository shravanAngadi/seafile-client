extern "C" {

#include <ccnet/peer.h>

}

#include <vector>
#include <QtGlobal>

#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include <QWidget>
#include <QImage>

#include "QtAwesome.h"
#include "utils/utils.h"
#include "utils/utils-mac.h"
#include "seafile-applet.h"
#include "rpc/rpc-client.h"
#include "account-mgr.h"
#include "create-repo-dialog.h"
#include "clone-tasks-dialog.h"
#include "server-status-dialog.h"
#include "main-window.h"
#include "repos-tab.h"
#include "starred-files-tab.h"
#include "activities-tab.h"
#include "search-tab.h"
#include "account-view.h"
#include "seafile-tab-widget.h"
#include "utils/paint-utils.h"
#include "server-status-service.h"
#include "customization-service.h"

#include "cloud-view.h"

namespace {

const int kRefreshStatusInterval = 1000;

const int kIndexOfAccountView = 1;
const int kIndexOfToolBar = 2;
const int kIndexOfTabWidget = 3;

enum {
    TAB_INDEX_REPOS = 0,
    TAB_INDEX_STARRED_FILES,
    TAB_INDEX_ACTIVITIES,
    TAB_INDEX_SEARCH,
};


}

CloudView::CloudView(QWidget *parent)
    : QWidget(parent),
      clone_task_dialog_(NULL)

{
    setupUi(this);

    // seahub_messages_monitor_ = new SeahubMessagesMonitor(this);
    // mSeahubMessagesBtn->setVisible(false);

    layout()->setContentsMargins(1, 0, 1, 0);

    // Setup widgets from top down
    setupHeader();

    createAccountView();

    createTabs();

    // tool bar have to be created after tabs, since some of the toolbar
    // actions are provided by the tabs
    createToolBar();

    setupDropArea();

    setupFooter();

    QVBoxLayout *vlayout = (QVBoxLayout *)layout();
    vlayout->insertWidget(kIndexOfAccountView, account_view_);
    vlayout->insertWidget(kIndexOfToolBar, tool_bar_);
    vlayout->insertWidget(kIndexOfTabWidget, tabs_);

    resizer_ = new QSizeGrip(this);
    resizer_->resize(resizer_->sizeHint());

    refresh_status_bar_timer_ = new QTimer(this);
    connect(refresh_status_bar_timer_, SIGNAL(timeout()), this, SLOT(refreshStatusBar()));

    AccountManager *account_mgr = seafApplet->accountManager();
    connect(account_mgr, SIGNAL(accountsChanged()),
            this, SLOT(onAccountChanged()));

#if defined(Q_OS_MAC)
    mHeader->setVisible(false);
#endif

    connect(ServerStatusService::instance(), SIGNAL(serverStatusChanged()),
            this, SLOT(refreshServerStatus()));
    connect(CustomizationService::instance(), SIGNAL(serverLogoFetched(const QUrl&)),
            this, SLOT(onServerLogoFetched(const QUrl&)));

    QTimer::singleShot(0, this, SLOT(onAccountChanged()));
}

void CloudView::setupHeader()
{
    setupLogoAndBrand();

    mMinimizeBtn->setText("");
    mMinimizeBtn->setToolTip(tr("Minimize"));
    mMinimizeBtn->setIcon(awesome->icon(icon_minus, QColor("#808081")));
    connect(mMinimizeBtn, SIGNAL(clicked()), this, SLOT(onMinimizeBtnClicked()));

    mCloseBtn->setText("");
    mCloseBtn->setToolTip(tr("Close"));
    mCloseBtn->setIcon(awesome->icon(icon_remove, QColor("#808081")));
    connect(mCloseBtn, SIGNAL(clicked()), this, SLOT(onCloseBtnClicked()));

    // Handle mouse move event
    mHeader->installEventFilter(this);
}


void CloudView::createAccountView()
{
    account_view_ = new AccountView;
#ifdef Q_OS_MAC
    account_view_->setContentsMargins(0, 0, 0, -8);
#else
    account_view_->setContentsMargins(0, -8, 0, -8);
#endif
}

void CloudView::createTabs()
{
    tabs_ = new SeafileTabWidget;
    // tabs_ = new QTabWidget;

    repos_tab_ = new ReposTab;

    QString base_icon_path = ":/images/tabs/";
    tabs_->addTab(repos_tab_, tr("Libraries"), base_icon_path + "files.png");

    starred_files_tab_ = new StarredFilesTab;
    tabs_->addTab(starred_files_tab_, tr("Starred"), base_icon_path + "starred.png");

    activities_tab_ = new ActivitiesTab;

    search_tab_ = new SearchTab;

    connect(tabs_, SIGNAL(currentTabChanged(int)),
            this, SLOT(onTabChanged(int)));

    showProperTabs();
    // bool has_pro_account = hasAccount() && seafApplet->accountManager()->accounts().front().isPro();
    // if (has_pro_account) {
    //     addActivitiesTab();
    // }
}

void CloudView::setupDropArea()
{
    mDropArea->setAcceptDrops(true);
    mDropArea->installEventFilter(this);
    connect(mSelectFolderBtn, SIGNAL(clicked()), this, SLOT(chooseFolderToSync()));
}

void CloudView::setupFooter()
{
    // mDownloadTasksInfo->setText("0");
    // mDownloadTasksBtn->setIcon(QIcon(":/images/toobar/download-gray.png"));
    // mDownloadTasksBtn->setToolTip(tr("Show download tasks"));
    // connect(mDownloadTasksBtn, SIGNAL(clicked()), this, SLOT(showCloneTasksDialog()));

    mServerStatusBtn->setIcon(QIcon(":/images/main-panel/connected.png"));
    mServerStatusBtn->setIconSize(QSize(18, 18));
    connect(mServerStatusBtn, SIGNAL(clicked()), this, SLOT(showServerStatusDialog()));

    // mDownloadRateArrow->setText(QChar(icon_arrow_down));
    // mDownloadRateArrow->setFont(awesome->font(16));
    mDownloadRateArrow->setPixmap(QPixmap(":/images/main-panel/down.png"));
    mDownloadRate->setText("0 kB/s");
    mDownloadRate->setToolTip(tr("current download rate"));

    // mUploadRateArrow->setText(QChar(icon_arrow_up));
    // mUploadRateArrow->setFont(awesome->font(16));
    mUploadRateArrow->setPixmap(QPixmap(":/images/main-panel/up.png"));
    mUploadRate->setText("0 kB/s");
    mUploadRate->setToolTip(tr("current upload rate"));
}

void CloudView::chooseFolderToSync()
{
    QString msg = tr("Please Choose a folder to sync");
#if defined(Q_OS_WIN32)
    QString parent_dir = "C:\\";
#else
    QString parent_dir = QDir::homePath();
#endif
    QString dir = QFileDialog::getExistingDirectory(this, msg, parent_dir,
                                                    QFileDialog::ShowDirsOnly
                                                    | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) {
        return;
    }

    showCreateRepoDialog(dir);
}

void CloudView::showCreateRepoDialog(const QString& path)
{
    const std::vector<Account>& accounts = seafApplet->accountManager()->accounts();
    if (accounts.empty()) {
        return;
    }

    CreateRepoDialog *dialog = new CreateRepoDialog(accounts[0], path, repos_tab_, this);

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void CloudView::onMinimizeBtnClicked()
{
    seafApplet->mainWindow()->showMinimized();
}

void CloudView::onCloseBtnClicked()
{
    seafApplet->mainWindow()->hide();
}

bool CloudView::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == mHeader) {
        static QPoint oldPos;
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *ev = (QMouseEvent *)event;
            oldPos = ev->globalPos();

            return true;

        } else if (event->type() == QEvent::MouseMove) {
            QMouseEvent *ev = (QMouseEvent *)event;
            const QPoint delta = ev->globalPos() - oldPos;

            MainWindow *win = seafApplet->mainWindow();
            win->move(win->x() + delta.x(), win->y() + delta.y());

            oldPos = ev->globalPos();
            return true;
        }

    } else if (obj == mDropArea) {
        if (event->type() == QEvent::DragEnter) {
            QDragEnterEvent *ev = (QDragEnterEvent *)event;
            if (ev->mimeData()->hasUrls() && ev->mimeData()->urls().size() == 1) {
                const QUrl url = ev->mimeData()->urls().at(0);
                if (url.scheme() == "file") {
                    QString path = url.toLocalFile();
#if defined(Q_OS_MAC) && (QT_VERSION <= QT_VERSION_CHECK(5, 4, 0))
                    path = utils::mac::fix_file_id_url(path);
#endif
                    if (QFileInfo(path).isDir()) {
                        ev->acceptProposedAction();
                    }
                }
            }
            return true;
        } else if (event->type() == QEvent::Drop) {
            QDropEvent *ev = (QDropEvent *)event;
            const QUrl url = ev->mimeData()->urls().at(0);
            QString path = url.toLocalFile();
#if defined(Q_OS_MAC) && (QT_VERSION <= QT_VERSION_CHECK(5, 4, 0))
            path = utils::mac::fix_file_id_url(path);
#endif
            ev->setDropAction(Qt::CopyAction);
            ev->accept();
            showCreateRepoDialog(path);
            return true;
        }
    }

    return QWidget::eventFilter(obj, event);
}

CloneTasksDialog *CloudView::cloneTasksDialog()
{
    if (clone_task_dialog_ == NULL) {
        clone_task_dialog_ = new CloneTasksDialog;
    }

    return clone_task_dialog_;
}

bool CloudView::hasAccount()
{
    return seafApplet->accountManager()->hasAccount();
}

void CloudView::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);

    refresh_status_bar_timer_->start(kRefreshStatusInterval);
}

void CloudView::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    refresh_status_bar_timer_->stop();
}


void CloudView::refreshTasksInfo()
{
    int count = 0;
    if (seafApplet->rpcClient()->getCloneTasksCount(&count) < 0) {
        return;
    }

    // mDownloadTasksInfo->setText(QString::number(count));
}

void CloudView::refreshServerStatus()
{
    ServerStatusService *service = ServerStatusService::instance();
    QString tool_tip;
    if (service->allServersConnected()) {
        tool_tip = tr("all servers connected");
    } else if (service->allServersDisconnected()) {
        tool_tip = tr("no server connected");
    } else {
        tool_tip = tr("some servers not connected");
    }
    mServerStatusBtn->setIcon(QIcon(service->allServersConnected()
                                    ? ":/images/main-panel/connected.png"
                                    : ":/images/main-panel/unconnected.png"));
    mServerStatusBtn->setToolTip(tool_tip);
}

void CloudView::refreshTransferRate()
{
    int up_rate, down_rate;
    if (seafApplet->rpcClient()->getUploadRate(&up_rate) < 0) {
        return;
    }

    if (seafApplet->rpcClient()->getDownloadRate(&down_rate) < 0) {
        return;
    }

    mUploadRate->setText(tr("%1 kB/s").arg(up_rate / 1024));
    mDownloadRate->setText(tr("%1 kB/s").arg(down_rate / 1024));
}

void CloudView::refreshStatusBar()
{
    if (!seafApplet->mainWindow()->isVisible()) {
        return;
    }
    refreshTasksInfo();
    refreshTransferRate();
}

void CloudView::showCloneTasksDialog()
{
    //CloneTasksDialog dialog(this);
    if (clone_task_dialog_ == NULL) {
        clone_task_dialog_ = new CloneTasksDialog;
    }

    clone_task_dialog_->updateTasks();
    clone_task_dialog_->show();
    clone_task_dialog_->raise();
    clone_task_dialog_->activateWindow();
}

void CloudView::showServerStatusDialog()
{
    ServerStatusDialog dialog(this);
    dialog.exec();
}

void CloudView::createToolBar()
{
    tool_bar_ = new QToolBar;
    tool_bar_->setIconSize(QSize(24, 24));

    std::vector<QAction*> repo_actions = repos_tab_->getToolBarActions();
    for (size_t i = 0, n = repo_actions.size(); i < n; i++) {
        QAction *action = repo_actions[i];
        tool_bar_->addAction(action);
    }

    QWidget *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tool_bar_->addWidget(spacer);

    refresh_action_ = new QAction(tr("Refresh"), this);
    refresh_action_->setIcon(QIcon(":/images/toolbar/refresh.png"));
    refresh_action_->setEnabled(hasAccount());
    connect(refresh_action_, SIGNAL(triggered()), this, SLOT(onRefreshClicked()));
    tool_bar_->addAction(refresh_action_);

    QWidget *spacer_right = new QWidget;
    spacer_right->setObjectName("spacerWidget");
    spacer_right->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    tool_bar_->addWidget(spacer_right);
}

void CloudView::onRefreshClicked()
{
    if (tabs_->currentIndex() == TAB_INDEX_REPOS) {
        repos_tab_->refresh();
    } else if (tabs_->currentIndex() == TAB_INDEX_STARRED_FILES) {
        starred_files_tab_->refresh();
    } else if (tabs_->currentIndex() == TAB_INDEX_ACTIVITIES) {
        activities_tab_->refresh();
    } else if (tabs_->currentIndex() == TAB_INDEX_SEARCH) {
        search_tab_->refresh();
    }
}

void CloudView::resizeEvent(QResizeEvent *event)
{
    resizer_->move(rect().bottomRight() - resizer_->rect().bottomRight());
    resizer_->raise();
    tabs_->adjustTabsWidth(rect().width());
}

void CloudView::onServerLogoFetched(const QUrl& url)
{
    const Account& account = seafApplet->accountManager()->currentAccount();
    if (account.isValid() && url.host() == account.serverUrl.host()) {
        mLogo->setPixmap(CustomizationService::instance()->getServerLogo(account));
    }
}

void CloudView::setupLogoAndBrand()
{
    mLogo->setText("");
    mLogo->setToolTip(getBrand());
    QPixmap logo;
    // We must get the pixmap from a QImage, otherwise the logo won't be
    // updated when we switch between two accounts, one of which has custom
    // logo and the other doesn't.
    logo.convertFromImage(QImage(":/images/seafile-24.png"));
    mLogo->setPixmap(logo);

    mBrand->setText(getBrand());
    mBrand->setToolTip(getBrand());
}

void CloudView::onAccountChanged()
{
    setupLogoAndBrand();
    const Account& account = seafApplet->accountManager()->currentAccount();
    if (account.isValid() && account.isPro()) {
        if (!account.serverInfo.customBrand.isEmpty()) {
            QString title = account.serverInfo.customBrand;
            mBrand->setText(title);
            mBrand->setToolTip(title);
            mLogo->setToolTip(title);
            if (seafApplet->mainWindow()) {
                seafApplet->mainWindow()->setWindowTitle(title);
            }
        }

        if (!account.serverInfo.customLogo.isEmpty()) {
            mLogo->setPixmap(
                CustomizationService::instance()->getServerLogo(account));
        }
    }

    refresh_action_->setEnabled(account.isValid());

    showProperTabs();

    repos_tab_->refresh();
    starred_files_tab_->refresh();
    activities_tab_->refresh();
    search_tab_->reset();

    account_view_->onAccountChanged();
    // we need update tab manually
    onTabChanged(tabs_->currentIndex());
}

void CloudView::showProperTabs()
{
    const Account& account = seafApplet->accountManager()->currentAccount();
    bool show_activities_tab = false;
    bool show_search_tab = false;
    if (account.isPro()) {
        show_activities_tab = true;
        if (account.hasFileSearch()) {
            show_search_tab = true;
        }
    }
    if (show_activities_tab && tabs_->count() < 3) {
        tabs_->addTab(activities_tab_, tr("Activities"), ":/images/tabs/history.png");
    } else if (!show_activities_tab && tabs_->count() >= 3) {
        tabs_->removeTab(TAB_INDEX_ACTIVITIES, activities_tab_);
    }
    if (show_search_tab && tabs_->count() < 4) {
        tabs_->addTab(search_tab_, tr("Search"), ":/images/tabs/search.png");
    } else if (!show_search_tab && tabs_->count() >= 4) {
        tabs_->removeTab(TAB_INDEX_SEARCH, search_tab_);
    }
    tabs_->adjustTabsWidth(rect().width());
}

void CloudView::onTabChanged(int index)
{
    bool enable_sync_with_any_folder = hasAccount() && !seafApplet->accountManager()->accounts().front().hasDisableSyncWithAnyFolder();
    bool drop_area_visible = index == 0;
    if (enable_sync_with_any_folder && drop_area_visible) {
        mDropArea->setVisible(true);
        mFooter->setStyleSheet("");
    } else {
        mDropArea->setVisible(false);
        mFooter->setStyleSheet("QFrame#mFooter { border-top: 1px solid #DCDCDE; }");
    }
}
