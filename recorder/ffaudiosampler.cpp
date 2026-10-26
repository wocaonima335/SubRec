#include "ffaudiosampler.h"

#include <QDebug>
#include <algorithm>
#include <cmath>

FFAudioSampler::FFAudioSampler(QObject *parent)
    : QObject(parent), m_sampleRate(48000), m_channels(2), m_format(0)
{
    // 初始化缓冲区
    m_sampleBuffer.resize(SAMPLE_BUFFER_SIZE, 0.0f);
    m_waveformData.resize(WAVEFORM_DISPLAY_POINTS, 0);
    m_spectrumData.resize(SPECTRUM_BINS, 0.0f);
}

FFAudioSampler::~FFAudioSampler()
{
    stop();
}

void FFAudioSampler::initialize(int sampleRate, int channels, int format)
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_format = format;

    // 重新调整缓冲区大小
    int bufferSize = std::max(SAMPLE_BUFFER_SIZE, m_sampleRate * 2);
    m_sampleBuffer.resize(bufferSize, 0.0f);
    m_bufferIndex = 0;

    qDebug() << "FFAudioSampler initialized: sampleRate=" << sampleRate
             << "channels=" << channels << "bufferSize=" << bufferSize;
}

void FFAudioSampler::start()
{
    bool shouldEmit = false;
    {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        if (!m_isActive)
        {
            m_isActive = true;
            shouldEmit = true;
        }
    }
    if (shouldEmit)
    {
        QMetaObject::invokeMethod(this, "activeStateChanged", Qt::QueuedConnection, Q_ARG(bool, true));
    }
}

void FFAudioSampler::stop()
{
    bool shouldEmit = false;
    {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        if (m_isActive)
        {
            m_isActive = false;
            shouldEmit = true;
        }
    }
    if (shouldEmit)
    {
        QMetaObject::invokeMethod(this, "activeStateChanged", Qt::QueuedConnection, Q_ARG(bool, false));
    }
}

void FFAudioSampler::clear()
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    std::fill(m_sampleBuffer.begin(), m_sampleBuffer.end(), 0.0f);
    std::fill(m_waveformData.begin(), m_waveformData.end(), 0);
    std::fill(m_spectrumData.begin(), m_spectrumData.end(), 0.0f);
    m_bufferIndex = 0;
    m_volumeLevel = 0;
}

/**
 * @brief 采集float格式的音频样本
 * 参考ffplay.c的update_sample_display()函数设计
 */
void FFAudioSampler::collectSamples(const float *samples, int sampleCount)
{
    if (!m_isActive || !samples || sampleCount <= 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_dataMutex);

    // 将样本写入环形缓冲区
    for (int i = 0; i < sampleCount; ++i)
    {
        // 将float (-1.0 ~ 1.0) 转换为缓冲区格式
        m_sampleBuffer[m_bufferIndex] = samples[i];
        m_bufferIndex = (m_bufferIndex + 1) % m_sampleBuffer.size();
    }

    // 定期更新音量和波形数据
    m_updateCounter++;
    if (m_updateCounter >= SIGNAL_UPDATE_INTERVAL)
    {
        m_updateCounter = 0;
        updateVolumeLevel();
        // updateWaveformData();
        // computeSpectrum();  // 可选：初期简化，先不计算频谱
    }
}

/**
 * @brief 采集int16格式的音频样本
 */
void FFAudioSampler::collectSamples(const int16_t *samples, int sampleCount)
{
    if (!m_isActive || !samples || sampleCount <= 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_dataMutex);

    // 将int16样本转换为float并写入缓冲区
    constexpr float INT16_TO_FLOAT = 1.0f / 32768.0f;
    for (int i = 0; i < sampleCount; ++i)
    {
        float sample = samples[i] * INT16_TO_FLOAT;
        m_sampleBuffer[m_bufferIndex] = sample;
        m_bufferIndex = (m_bufferIndex + 1) % m_sampleBuffer.size();
    }

    // 定期更新音量和波形数据
    m_updateCounter++;
    if (m_updateCounter >= SIGNAL_UPDATE_INTERVAL)
    {
        m_updateCounter = 0;
        updateVolumeLevel();
        updateWaveformData();
    }
}

/**
 * @brief 计算RMS（均方根）音量
 *
 * 算法原理（参考ffplay分析文档）：
 * 1. 取缓冲区最近N个样本
 * 2. 计算每个样本的平方和
 * 3. RMS = sqrt(平方和 / 样本数)
 * 4. 音量等级 = RMS * 100（0-100范围）
 * 5. 应用平滑因子避免抖动
 */
void FFAudioSampler::updateVolumeLevel()
{
    // 优化：减少窗口至 ~100ms 提高响应性和性能
    int sampleWindow = std::min(m_sampleRate / 10, (int)m_sampleBuffer.size());
    int startIdx = (m_bufferIndex - sampleWindow + (int)m_sampleBuffer.size()) % m_sampleBuffer.size();

    // 计算RMS
    double sumSquares = 0.0;
    for (int i = 0; i < sampleWindow; ++i)
    {
        int idx = (startIdx + i) % m_sampleBuffer.size();
        float sample = m_sampleBuffer[idx];
        sumSquares += sample * sample;
    }

    float rms = std::sqrt(sumSquares / sampleWindow);

    // 转换为0-100的音量等级
    // 使用对数刻度（参考ffplay的分贝计算）
    int newLevel = 0;
    if (rms > 0.001f)
    {
        // dB = 20 * log10(rms)
        // 限制在 -40dB 到 0dB 范围
        float dB = 20.0f * std::log10(rms);
        dB = std::max(-40.0f, std::min(0.0f, dB));
        newLevel = (int)((dB + 40.0f) / 40.0f * 100.0f);
    }

    // 平滑处理，避免音量抖动
    m_volumeLevel = (int)(m_volumeLevel * VOLUME_SMOOTH_FACTOR + newLevel * (1.0f - VOLUME_SMOOTH_FACTOR));
    // 发送信号通知QML更新
    QMetaObject::invokeMethod(this, "volumeLevelChanged", Qt::QueuedConnection, Q_ARG(int, m_volumeLevel));
}

/**
 * @brief 更新波形数据用于显示
 * 从环形缓冲区采样WAVEFORM_DISPLAY_POINTS个点
 */
void FFAudioSampler::updateWaveformData()
{
    int stride = std::max(1, (int)m_sampleBuffer.size() / WAVEFORM_DISPLAY_POINTS);

    for (int i = 0; i < WAVEFORM_DISPLAY_POINTS; ++i)
    {
        int idx = (m_bufferIndex - (WAVEFORM_DISPLAY_POINTS - i) * stride + (int)m_sampleBuffer.size()) % m_sampleBuffer.size();

        float sample = m_sampleBuffer[idx];
        // 转换为显示范围 (-100 ~ 100)
        m_waveformData[i] = (int)(sample * 100.0f);
    }

    QMetaObject::invokeMethod(this, "waveformDataChanged", Qt::QueuedConnection);
}

/**
 * @brief 计算频谱数据（使用简化方法）
 * 注：完整的FFT实现需要引入FFmpeg库的avfft模块
 * 这里先提供简化版本，按频率带分析能量
 */
void FFAudioSampler::computeSpectrum()
{
    // 简化实现：将缓冲区分成SPECTRUM_BINS个频率段
    // 计算每个频率段的能量
    int samplesPerBin = m_sampleBuffer.size() / SPECTRUM_BINS;

    for (int bin = 0; bin < SPECTRUM_BINS; ++bin)
    {
        double energy = 0.0;
        for (int i = 0; i < samplesPerBin; ++i)
        {
            int idx = bin * samplesPerBin + i;
            float sample = m_sampleBuffer[idx];
            energy += sample * sample;
        }

        float magnitude = std::sqrt(energy / samplesPerBin);
        // 应用对数刻度，转换为dB
        float dB = magnitude > 0.001f ? 20.0f * std::log10(magnitude) : -100.0f;
        dB = std::max(-100.0f, std::min(0.0f, dB));
        m_spectrumData[bin] = (dB + 100.0f) / 100.0f; // 0-1 范围
    }

    QMetaObject::invokeMethod(this, "spectrumDataChanged", Qt::QueuedConnection);
}

QVector<int> FFAudioSampler::getWaveformData() const
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    return QVector<int>(m_waveformData.begin(), m_waveformData.end());
}

QVector<float> FFAudioSampler::getSpectrumData() const
{
    std::lock_guard<std::mutex> lock(m_dataMutex);
    return QVector<float>(m_spectrumData.begin(), m_spectrumData.end());
}
