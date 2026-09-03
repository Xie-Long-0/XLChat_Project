#pragma once

#include <QtGlobal>

namespace XYChat::Server
{

// 连接级固定窗口限流计数器
// 语义与既有 fetch_keys 连接级滑动窗口一致：窗口内累计放行次数达到上限即拒绝，
// 窗口过期后计数重置。nowSecs 由调用方传入（Unix 秒），便于确定性单元测试
// （无需真实等待），生产调用点传入 QDateTime::currentSecsSinceEpoch()。
class RateWindow
{
public:
    RateWindow(int maxCount, int windowSeconds)
        : m_maxCount(maxCount)
        , m_windowSeconds(windowSeconds)
    {
    }

    // 放行返回 true；窗口内超过上限返回 false（不递增计数）
    bool allow(qint64 nowSecs)
    {
        if (!m_initialized || nowSecs - m_windowStart >= m_windowSeconds) {
            m_windowStart = nowSecs;
            m_count = 0;
            m_initialized = true;
        }
        if (m_count >= m_maxCount) {
            return false;
        }
        ++m_count;
        return true;
    }

    // 当前窗口内已放行次数（观测/测试用）
    int count() const { return m_count; }

    // 重置窗口（例如连接复用于新会话时）
    void reset()
    {
        m_windowStart = 0;
        m_count = 0;
        m_initialized = false;
    }

private:
    int m_maxCount;
    int m_windowSeconds;
    qint64 m_windowStart = 0;
    int m_count = 0;
    bool m_initialized = false;
};

} // namespace XYChat::Server
