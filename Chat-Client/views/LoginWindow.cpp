#include "LoginWindow.h"
#include "ui_LoginWindow.h"
#include "MainWindow.h"
#include <QMessageBox>
#include "core/NetworkManager.h"
#include "EncryptionManager.h"

LoginWindow::LoginWindow(QWidget *parent) :
    QMainWindow(parent)
    , ui(new Ui::LoginWindow)
    , m_networkManager(new NetworkManager(this))
{
    setAttribute(Qt::WA_DeleteOnClose);
    ui->setupUi(this);

    connect(ui->loginButton, &QPushButton::clicked, this, &LoginWindow::onLoginButtonClicked);
    connect(ui->registerButton, &QPushButton::clicked, this, &LoginWindow::onRegisterButtonClicked);
    connect(m_networkManager, &NetworkManager::loginSuccessful, this, &LoginWindow::onLoginSuccessful);
    connect(m_networkManager, &NetworkManager::loginFailed, this, &LoginWindow::onLoginFailed);
    connect(m_networkManager, &NetworkManager::registerSuccessful, this, &LoginWindow::onRegisterSuccessful);
    connect(m_networkManager, &NetworkManager::registerFailed, this, &LoginWindow::onRegisterFailed);

    // 初始隐藏邮箱和手机号字段（注册模式下显示）
    setRegisterMode(false);
}

LoginWindow::~LoginWindow()
{
    delete ui;
}

void LoginWindow::setRegisterMode(bool registerMode)
{
    m_isRegisterMode = registerMode;
    ui->emailLabel->setVisible(registerMode);
    ui->emailLineEdit->setVisible(registerMode);
    ui->phoneLabel->setVisible(registerMode);
    ui->phoneLineEdit->setVisible(registerMode);
    ui->loginButton->setVisible(!registerMode);
    ui->registerButton->setText(registerMode ? tr("返回登录") : tr("注册新账号"));
}

void LoginWindow::onLoginButtonClicked()
{
    QString username = ui->usernameLineEdit->text();
    QString password = ui->passwordLineEdit->text();

    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::critical(this, tr("错误"), tr("用户名或密码不能为空！"));
        return;
    }

    ui->loginButton->setEnabled(false);

    // 客户端对密码做 SHA-256，服务端再用 PBKDF2 验证
    QString encryptedPassword = EncryptionManager::encryptPassword(password);
    m_networkManager->login(username, encryptedPassword);
}

void LoginWindow::onRegisterButtonClicked()
{
    if (m_isRegisterMode) {
        // 返回登录模式
        setRegisterMode(false);
        return;
    }

    // 进入注册模式
    QString username = ui->usernameLineEdit->text();
    QString password = ui->passwordLineEdit->text();

    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::critical(this, tr("错误"), tr("用户名和密码不能为空！"));
        return;
    }

    // 先显示注册字段
    setRegisterMode(true);
    return;
}

void LoginWindow::onLoginSuccessful()
{
    auto mainWindow = new MainWindow();
    mainWindow->setNetworkManager(m_networkManager);
    mainWindow->show();
    close();
}

void LoginWindow::onLoginFailed(const QString &errorMessage)
{
    QMessageBox::critical(this, tr("登录失败"), errorMessage);
    ui->passwordLineEdit->clear();
    ui->loginButton->setEnabled(true);
}

void LoginWindow::onRegisterSuccessful()
{
    QMessageBox::information(this, tr("注册成功"),
                             tr("账号注册成功！请使用新账号登录。"));
    ui->passwordLineEdit->clear();
    ui->registerButton->setEnabled(true);
    setRegisterMode(false);
}

void LoginWindow::onRegisterFailed(const QString &errorMessage)
{
    QMessageBox::critical(this, tr("注册失败"), errorMessage);
    ui->registerButton->setEnabled(true);
}
