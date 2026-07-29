#pragma once

#include <QMainWindow>

namespace Ui
{
class LoginWindow;
}

class NetworkManager;

class LoginWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit LoginWindow(QWidget *parent = nullptr);
    ~LoginWindow();

private slots:
    void onLoginButtonClicked();
    void onRegisterButtonClicked();
    void onLoginSuccessful();
    void onLoginFailed(const QString &errorMessage);
    void onRegisterSuccessful();
    void onRegisterFailed(const QString &errorMessage);

private:
    void setRegisterMode(bool registerMode);

    Ui::LoginWindow *ui;
    NetworkManager *m_networkManager;
    bool m_isRegisterMode = false;
};
