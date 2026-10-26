import QtQuick 2.15
import QtQuick.Controls 2.15

/**
 * AudioVisualizer.qml - 音频可视化组件
 * 
 * 功能：
 * - 实时显示音频波形图
 * - 音量指示器（柱形图）
 * - 可选的频谱显示
 * 
 * 参考: ffplay.c 的 video_audio_display() 函数
 * 
 * 使用方式:
 * AudioVisualizer {
 *     audioSampler: recorder.audioSampler
 *     width: 80
 *     height: 80
 *     mode: "volume"  // "wave", "spectrum", "volume"
 * }
 */
Rectangle {
    id: audioVisualizer
    color: "#1a1a1a"
    border.color: "#40454F"
    border.width: 1
    
    // 外部传入的音频采样器对象
    property var audioSampler: null
    
    // 显示模式: "volume" (音量指示), "wave" (波形), "spectrum" (频谱)
    property string mode: "volume"
    
    // 调色板
    property color volumeBarColor: "#00FF00"
    property color waveformColor: "#00FF00"
    property color spectrumColor: "#00FF00"
    property color backgroundColor: "#1a1a1a"
    property color gridColor: "#333333"
    
    // 内部状态
    property int currentVolume: audioSampler ? audioSampler.volumeLevel : 0
    property var waveformData: audioSampler ? audioSampler.waveformData : []
    property var spectrumData: audioSampler ? audioSampler.spectrumData : []
    
    // 连接信号
    Connections {
        target: audioSampler
        onVolumeLevelChanged: {
            if (!audioSampler) return
            currentVolume = audioSampler.volumeLevel
            if (mode === "volume") canvas.requestPaint()
        }

        onWaveformDataChanged: {
            canvas.requestPaint()
        }
        onSpectrumDataChanged: {
            canvas.requestPaint()
        }
    }
    
    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.margins: 2
        
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            
            // 绘制背景
            ctx.fillStyle = backgroundColor;
            ctx.fillRect(0, 0, width, height);
            
            // 根据模式选择绘制方式
            if (mode === "volume") {
                drawVolumeIndicator(ctx);
            } else if (mode === "wave") {
                drawWaveform(ctx);
            } else if (mode === "spectrum") {
                drawSpectrum(ctx);
            }
        }
        
        /**
         * 绘制音量指示器（柱形图）
         * 显示范围: 0-100
         */
        function drawVolumeIndicator(ctx) {
            var volume = Math.max(0, Math.min(100, currentVolume));
            var barHeight = (volume / 100) * height;
            var remainingHeight = height - barHeight;
            
            // 绘制背景柱
            ctx.fillStyle = "#333333";
            ctx.fillRect(4, 0, width - 8, height);
            
            // 绘制音量柱
            var gradient = ctx.createLinearGradient(0, height, 0, 0);
            if (volume < 30) {
                gradient.addColorStop(0, "#00FF00");
                gradient.addColorStop(1, "#00AA00");
            } else if (volume < 70) {
                gradient.addColorStop(0, "#FFFF00");
                gradient.addColorStop(1, "#FFAA00");
            } else {
                gradient.addColorStop(0, "#FF0000");
                gradient.addColorStop(1, "#DD0000");
            }
            ctx.fillStyle = gradient;
            ctx.fillRect(4, remainingHeight, width - 8, barHeight);
            
            // 绘制中心线
            ctx.strokeStyle = gridColor;
            ctx.lineWidth = 0.5;
            ctx.beginPath();
            ctx.moveTo(0, height / 2);
            ctx.lineTo(width, height / 2);
            ctx.stroke();
            
            // 绘制刻度线
            ctx.strokeStyle = gridColor;
            ctx.lineWidth = 0.5;
            for (var i = 1; i < 4; i++) {
                var y = (height / 4) * i;
                ctx.beginPath();
                ctx.moveTo(2, y);
                ctx.lineTo(width - 2, y);
                ctx.stroke();
            }
        }
        
        /**
         * 绘制波形图
         * 参考: ffplay 的 SHOW_MODE_WAVES
         */
        function drawWaveform(ctx) {
            if (!waveformData || waveformData.length === 0) {
                return;
            }
            
            var centerY = height / 2;
            var scaleFactor = (height - 4) / 200;  // 样本范围 -100 ~ 100
            
            // 绘制中心线
            ctx.strokeStyle = gridColor;
            ctx.lineWidth = 0.5;
            ctx.beginPath();
            ctx.moveTo(0, centerY);
            ctx.lineTo(width, centerY);
            ctx.stroke();
            
            // 绘制波形
            ctx.strokeStyle = waveformColor;
            ctx.lineWidth = 1;
            ctx.beginPath();
            
            var pointSpacing = width / waveformData.length;
            
            for (var i = 0; i < waveformData.length; i++) {
                var x = i * pointSpacing;
                var sample = waveformData[i];
                var y = centerY - (sample * scaleFactor);
                
                if (i === 0) {
                    ctx.moveTo(x, y);
                } else {
                    ctx.lineTo(x, y);
                }
            }
            ctx.stroke();
        }
        
        /**
         * 绘制频谱图
         * 参考: ffplay 的 SHOW_MODE_RDFT
         */
        function drawSpectrum(ctx) {
            if (!spectrumData || spectrumData.length === 0) {
                return;
            }
            
            var barWidth = width / spectrumData.length;
            
            // 绘制频谱柱
            for (var i = 0; i < spectrumData.length; i++) {
                var magnitude = spectrumData[i];  // 0.0 ~ 1.0
                var barHeight = magnitude * (height - 2);
                var x = i * barWidth;
                var y = height - barHeight;
                
                // 颜色渐变
                var hue = (i / spectrumData.length) * 360;
                ctx.fillStyle = "hsl(" + hue + ", 100%, 50%)";
                ctx.fillRect(x + 0.5, y, barWidth - 1, barHeight);
            }
        }
    }
    
    // // 音量数值显示（可选）
    // Text {
    //     anchors {
    //         bottom: parent.bottom
    //         bottomMargin: 2
    //         horizontalCenter: parent.horizontalCenter
    //     }
    //     visible: mode === "volume"
    //     text: currentVolume + "%"
    //     color: currentVolume < 30 ? "#00FF00" : (currentVolume < 70 ? "#FFFF00" : "#FF0000")
    //     font.pixelSize: 10
    //     font.bold: true
    // }
}
