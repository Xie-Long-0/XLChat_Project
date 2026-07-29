#pragma once

#include <QMainWindow>
#include <QJsonObject>
#include <QJsonArray>

namespace Ui { class MainWindow; }
class NetworkManager;
class QListWidgetItem;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void setNetworkManager(NetworkManager *netMgr);

private slots:
    void onConversationsLoaded(const QJsonArray &conversations);
    void onConversationClicked(QListWidgetItem *item);
    void onMessagesSynced(qint64 conversationId, const QJsonArray &messages, bool hasMore);
    void onNewMessageReceived(const QJsonObject &message);
    void onMessageSent(qint64 messageId, qint64 conversationId);
    void onMessageSendFailed(const QString &error);
    void onSendMessageClicked();
    void onSearchButtonClicked();
    void onRefreshConversations();

private:
    void loadConversations();
    void loadMessages(qint64 conversationId, qint64 afterId = 0);
    void addMessageBubble(const QJsonObject &msg, bool isMine);
    void refreshConversationItem(int row, const QJsonObject &conv);

    Ui::MainWindow *ui;
    NetworkManager *m_netMgr = nullptr;

    // 当前选中的会话
    qint64 m_currentConversationId = 0;
    qint64 m_currentPeerUserId = 0;
    QString m_currentPeerUsername;

    // 会话列表数据缓存: row -> conversationId
    struct ConvData {
        qint64 conversationId = 0;
        qint64 peerUserId = 0;
        QString peerUsername;
    };
    QList<ConvData> m_convList;
};
