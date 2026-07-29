#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "core/NetworkManager.h"

#include <QMessageBox>
#include <QInputDialog>
#include <QLabel>
#include <QDateTime>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    resize(800, 600);

    connect(ui->sendButton, &QPushButton::clicked, this, &MainWindow::onSendMessageClicked);
    connect(ui->searchButton, &QPushButton::clicked, this, &MainWindow::onSearchButtonClicked);
    connect(ui->refreshButton, &QPushButton::clicked, this, &MainWindow::onRefreshConversations);
    connect(ui->conversationListWidget, &QListWidget::itemClicked, this, &MainWindow::onConversationClicked);
    connect(ui->messageInput, &QLineEdit::returnPressed, this, &MainWindow::onSendMessageClicked);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setNetworkManager(NetworkManager *netMgr)
{
    m_netMgr = netMgr;
    if (!m_netMgr) return;

    connect(m_netMgr, &NetworkManager::conversationsResult, this, &MainWindow::onConversationsLoaded);
    connect(m_netMgr, &NetworkManager::messagesSynced, this, &MainWindow::onMessagesSynced);
    connect(m_netMgr, &NetworkManager::newMessageReceived, this, &MainWindow::onNewMessageReceived);
    connect(m_netMgr, &NetworkManager::messageSent, this, &MainWindow::onMessageSent);
    connect(m_netMgr, &NetworkManager::messageSendFailed, this, &MainWindow::onMessageSendFailed);

    loadConversations();
}

// ── 加载会话列表 ───────────────────────────────────────────────────────────
void MainWindow::loadConversations()
{
    if (m_netMgr) {
        m_netMgr->getConversations();
    }
}

void MainWindow::onConversationsLoaded(const QJsonArray &conversations)
{
    ui->conversationListWidget->clear();
    m_convList.clear();

    for (const auto &val : conversations) {
        const QJsonObject conv = val.toObject();
        ConvData cd;
        cd.conversationId = conv.value("conversationId").toVariant().toLongLong();
        cd.peerUserId = conv.value("peerUserId").toVariant().toLongLong();
        cd.peerUsername = conv.value("peerUsername").toString();
        m_convList.append(cd);

        const QString lastMsg = conv.value("lastMessage").toString();
        const int unread = conv.value("unreadCount").toInt();

        QString displayText = cd.peerUsername;
        if (!lastMsg.isEmpty()) {
            displayText += "\n" + (lastMsg.length() > 30 ? lastMsg.left(30) + "..." : lastMsg);
        }
        if (unread > 0) {
            displayText += QString(" [%1]").arg(unread);
        }

        auto *item = new QListWidgetItem(displayText);
        ui->conversationListWidget->addItem(item);
    }
}

// ── 点击会话 ─────────────────────────────────────────────────────────────
void MainWindow::onConversationClicked(QListWidgetItem *item)
{
    const int row = ui->conversationListWidget->row(item);
    if (row < 0 || row >= m_convList.size()) return;

    const ConvData &cd = m_convList.at(row);
    m_currentConversationId = cd.conversationId;
    m_currentPeerUserId = cd.peerUserId;
    m_currentPeerUsername = cd.peerUsername;

    ui->chatTitleLabel->setText(QString("与 %1 的对话").arg(cd.peerUsername));
    ui->messageListWidget->clear();

    loadMessages(m_currentConversationId);
}

// ── 加载消息 ─────────────────────────────────────────────────────────────
void MainWindow::loadMessages(qint64 conversationId, qint64 afterId)
{
    if (m_netMgr) {
        m_netMgr->syncMessages(conversationId, afterId);
    }
}

void MainWindow::onMessagesSynced(qint64 conversationId, const QJsonArray &messages, bool hasMore)
{
    Q_UNUSED(hasMore);
    if (conversationId != m_currentConversationId) return;

    ui->messageListWidget->clear();

    for (const auto &val : messages) {
        const QJsonObject msg = val.toObject();
        const qint64 senderId = msg.value("senderId").toVariant().toLongLong();
        const bool isMine = (m_netMgr && senderId == m_netMgr->userId());
        addMessageBubble(msg, isMine);
    }

    // 滚动到底部
    const int count = ui->messageListWidget->count();
    if (count > 0) {
        ui->messageListWidget->scrollToItem(ui->messageListWidget->item(count - 1));
    }
}

// ── 消息气泡 ─────────────────────────────────────────────────────────────
void MainWindow::addMessageBubble(const QJsonObject &msg, bool isMine)
{
    const QString content = msg.value("content").toString();
    const QString sender = msg.value("senderUsername").toString();
    const QString time = msg.value("createdAt").toString();

    auto *item = new QListWidgetItem();

    // 构造气泡 Widget
    auto *bubbleWidget = new QWidget();
    auto *bubbleLayout = new QVBoxLayout(bubbleWidget);
    bubbleLayout->setContentsMargins(8, 4, 8, 4);

    // 发送者和时间
    auto *metaLabel = new QLabel(QString("%1  %2").arg(sender, time));
    metaLabel->setStyleSheet("color: gray; font-size: 10px;");

    // 消息内容
    auto *contentLabel = new QLabel(content);
    contentLabel->setWordWrap(true);
    contentLabel->setStyleSheet(
        isMine
        ? "background-color: #dcf8c6; border-radius: 8px; padding: 6px 10px; max-width: 400px;"
        : "background-color: #ffffff; border-radius: 8px; padding: 6px 10px; max-width: 400px; border: 1px solid #e0e0e0;"
    );

    bubbleLayout->addWidget(metaLabel);
    bubbleLayout->addWidget(contentLabel);

    if (isMine) {
        bubbleLayout->setAlignment(Qt::AlignRight);
    } else {
        bubbleLayout->setAlignment(Qt::AlignLeft);
    }

    item->setSizeHint(bubbleWidget->sizeHint());
    ui->messageListWidget->addItem(item);
    ui->messageListWidget->setItemWidget(item, bubbleWidget);
}

// ── 发送消息 ─────────────────────────────────────────────────────────────
void MainWindow::onSendMessageClicked()
{
    const QString text = ui->messageInput->text().trimmed();
    if (text.isEmpty() || m_currentPeerUserId <= 0 || !m_netMgr) return;

    m_netMgr->sendMessage(m_currentPeerUserId, text);
    ui->messageInput->clear();
}

void MainWindow::onMessageSent(qint64 messageId, qint64 conversationId)
{
    Q_UNUSED(messageId);
    if (conversationId == m_currentConversationId) {
        // 重新加载消息列表
        loadMessages(m_currentConversationId);
    }
    // 刷新会话列表
    loadConversations();
}

void MainWindow::onMessageSendFailed(const QString &error)
{
    QMessageBox::warning(this, "发送失败", error);
}

// ── 收到新消息 ───────────────────────────────────────────────────────────
void MainWindow::onNewMessageReceived(const QJsonObject &message)
{
    const qint64 convId = message.value("conversationId").toVariant().toLongLong();

    if (convId == m_currentConversationId) {
        // 当前正在查看的会话，直接显示
        addMessageBubble(message, false);
        const int count = ui->messageListWidget->count();
        if (count > 0) {
            ui->messageListWidget->scrollToItem(ui->messageListWidget->item(count - 1));
        }
    }

    // 刷新会话列表以更新未读数
    loadConversations();
}

// ── 搜索用户 ─────────────────────────────────────────────────────────────
void MainWindow::onSearchButtonClicked()
{
    if (!m_netMgr) return;

    bool ok = false;
    const QString query = QInputDialog::getText(this, "搜索用户", "输入用户名:", QLineEdit::Normal, "", &ok);
    if (!ok || query.trimmed().isEmpty()) return;

    m_netMgr->searchUsers(query.trimmed());

    // 连接一次性结果处理
    connect(m_netMgr, &NetworkManager::searchUsersResult, this, [this](const QJsonArray &users) {
        if (users.isEmpty()) {
            QMessageBox::information(this, "搜索结果", "未找到匹配的用户");
            return;
        }

        QStringList names;
        for (const auto &val : users) {
            const QJsonObject u = val.toObject();
            names << QString("%1 (ID: %2)").arg(u.value("username").toString()).arg(u.value("userId").toVariant().toLongLong());
        }

        bool ok2 = false;
        const QString selected = QInputDialog::getItem(this, "选择用户", "找到以下用户:", names, 0, false, &ok2);
        if (!ok2 || selected.isEmpty()) return;

        // 提取 userId
        const int idx = names.indexOf(selected);
        if (idx >= 0) {
            const QJsonObject u = users.at(idx).toObject();
            const qint64 targetUserId = u.value("userId").toVariant().toLongLong();
            m_netMgr->addContact(targetUserId);
            // 刷新会话列表
            loadConversations();
        }
    }, Qt::SingleShotConnection);
}

void MainWindow::onRefreshConversations()
{
    loadConversations();
}

void MainWindow::refreshConversationItem(int row, const QJsonObject &conv)
{
    if (row < 0 || row >= ui->conversationListWidget->count()) return;

    const QString lastMsg = conv.value("lastMessage").toString();
    const int unread = conv.value("unreadCount").toInt();
    const QString peerUsername = conv.value("peerUsername").toString();

    QString displayText = peerUsername;
    if (!lastMsg.isEmpty()) {
        displayText += "\n" + (lastMsg.length() > 30 ? lastMsg.left(30) + "..." : lastMsg);
    }
    if (unread > 0) {
        displayText += QString(" [%1]").arg(unread);
    }

    ui->conversationListWidget->item(row)->setText(displayText);
}
