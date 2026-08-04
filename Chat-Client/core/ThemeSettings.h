#pragma once

#include <QObject>
#include <QSettings>

// M4.5: 主题偏好持久化（QSettings），暴露给 QML 作为主题切换的数据源
class ThemeSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)

public:
    explicit ThemeSettings(QObject *parent = nullptr) : QObject(parent)
    {
        m_darkMode = m_settings.value("theme/darkMode", false).toBool();
    }

    bool darkMode() const { return m_darkMode; }

    void setDarkMode(bool dark)
    {
        if (m_darkMode == dark) {
            return;
        }
        m_darkMode = dark;
        m_settings.setValue("theme/darkMode", dark);
        emit darkModeChanged();
    }

signals:
    void darkModeChanged();

private:
    QSettings m_settings;
    bool m_darkMode = false;
};
