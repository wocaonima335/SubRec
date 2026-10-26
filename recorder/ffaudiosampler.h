#ifndef FFAUDIOSAMPLER_H
#define FFAUDIOSAMPLER_H

#include <QObject>
#include <vector>
#include <queue>
#include <mutex>
#include <cstdint>

/**
 * @brief FFAudioSampler - 音频采样和可视化数据提供器
 *
 * 功能：
 * - 从解码后的PCM数据采集音频样本
 * - 维护环形缓冲区存储原始音频波形
 * - 计算实时RMS（均方根）音量大小
 * - 通过Qt信号暴露数据给QML前端
 *
 * 设计原理参考: ffplay.c 中的 update_sample_display() 和 video_audio_display()
 */
class FFAudioSampler : public QObject
{
    Q_OBJECT

    // QML 绑定属性
    Q_PROPERTY(int volumeLevel READ getVolumeLevel NOTIFY volumeLevelChanged)
    Q_PROPERTY(QVector<int> waveformData READ getWaveformData NOTIFY waveformDataChanged)
    Q_PROPERTY(QVector<float> spectrumData READ getSpectrumData NOTIFY spectrumDataChanged)
    Q_PROPERTY(bool isActive READ isActive NOTIFY activeStateChanged)

public:
    explicit FFAudioSampler(QObject *parent = nullptr);
    ~FFAudioSampler();

    // 初始化采样器
    void initialize(int sampleRate, int channels, int format);

    // 采集音频样本（从解码后的音频帧调用）
    // samples: 原始PCM数据指针
    // sampleCount: 样本数量
    void collectSamples(const float *samples, int sampleCount);
    void collectSamples(const int16_t *samples, int sampleCount);

    // 获取当前音量等级 (0-100)
    int getVolumeLevel() const { return m_volumeLevel; }

    // 获取波形数据（用于绘制波形图）
    QVector<int> getWaveformData() const;

    // 获取频谱数据（用于绘制频谱图）
    QVector<float> getSpectrumData() const;

    // 查询采样器是否活跃
    bool isActive() const { return m_isActive; }

    // 启动/停止采样
    void start();
    void stop();

    // 清空缓冲区
    void clear();

signals:
    // 音量变化信号
    Q_SIGNAL void volumeLevelChanged(int newLevel);

    // 波形数据更新信号
    Q_SIGNAL void waveformDataChanged();

    // 频谱数据更新信号
    Q_SIGNAL void spectrumDataChanged();

    // 采样器活跃状态变化
    Q_SIGNAL void activeStateChanged(bool active);

private:
    // 计算RMS（均方根）音量
    void updateVolumeLevel();

    // 执行FFT变换获取频谱（可选，初期简化实现）
    void computeSpectrum();

    // 环形缓冲区的读取方法
    void updateWaveformData();

    // ===== 音频参数 =====
    int m_sampleRate; // 采样率 (Hz)
    int m_channels;   // 声道数
    int m_format;     // 音频格式 (PCM16, PCM32, FLOAT等)

    // ===== 采样缓冲区 =====
    // 环形缓冲区大小: 采样率 * 缓冲时长(秒)
    // 例如: 48kHz * 2秒 = 96000 样本
    static constexpr int SAMPLE_BUFFER_SIZE = 48000 * 2; // 96000 samples @ 48kHz
    std::vector<float> m_sampleBuffer;
    int m_bufferIndex = 0;

    // ===== 波形显示数据 =====
    // 采样点数（QML显示的波形分辨率）
    static constexpr int WAVEFORM_DISPLAY_POINTS = 256;
    std::vector<int> m_waveformData;

    // ===== 频谱显示数据 =====
    // FFT bin数量
    static constexpr int SPECTRUM_BINS = 64;
    std::vector<float> m_spectrumData;

    // ===== 音量数据 =====
    // 当前RMS音量 (0-100)
    int m_volumeLevel = 0;

    // 音量平滑系数（防止音量抖动）
    // 新音量 = 旧音量 * 0.7 + 新计算值 * 0.3
    static constexpr float VOLUME_SMOOTH_FACTOR = 0.7f;

    // ===== 状态标志 =====
    bool m_isActive = false;

    // ===== 线程安全 =====
    mutable std::mutex m_dataMutex;

    // ===== 性能优化 =====
    // 更新计数器（控制信号发送频率，避免过于频繁）
    int m_updateCounter = 0;
    static constexpr int SIGNAL_UPDATE_INTERVAL = 3; // 每3次采样更新一次信号
};

#endif // FFAUDIOSAMPLER_H
