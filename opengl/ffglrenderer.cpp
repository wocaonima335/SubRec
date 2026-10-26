#include "ffglrenderer.h"

#include <algorithm>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QOpenGLFramebufferObjectFormat>

FFGLRenderer::FFGLRenderer(FFGLItem *item)
{
    Q_UNUSED(item);
}

QOpenGLFramebufferObject *FFGLRenderer::createFramebufferObject(const QSize &size)
{
    if (!m_glInitialized) {
        initializeOpenGLFunctions();
        m_glInitialized = true;
    }
    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    return new QOpenGLFramebufferObject(size, fmt);
}

void FFGLRenderer::render()
{
    if (!m_glInitialized) {
        initializeOpenGLFunctions();
        m_glInitialized = true;
    }

    if (m_fboSize.isEmpty() || m_fboSize.width() <= 0 || m_fboSize.height() <= 0) {
        return;
    }

    glViewport(0, 0, m_fboSize.width(), m_fboSize.height());
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    ensureProgram();
    ensureBuffers();
    ensureTextures();

    if (m_imgWidth > 0 && m_imgHeight > 0 && !m_yData.isEmpty())
    {
        PreviewBridge::instance().markRendered(m_previewVersion, m_imgWidth, m_imgHeight, m_previewFingerprint);
        int w = m_imgWidth, h = m_imgHeight;
        int uvW = w / 2, uvH = h / 2;

        glBindTexture(GL_TEXTURE_2D, yTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, m_yData.constData());

        glBindTexture(GL_TEXTURE_2D, uTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, uvW, uvH, 0, GL_RED, GL_UNSIGNED_BYTE, m_uData.constData());

        glBindTexture(GL_TEXTURE_2D, vTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, uvW, uvH, 0, GL_RED, GL_UNSIGNED_BYTE, m_vData.constData());

        program->bind();
        program->setUniformValue("yTexture", 0);
        program->setUniformValue("uTexture", 1);
        program->setUniformValue("vTexture", 2);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, yTex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, uTex);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, vTex);

        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);

        maybeLogRenderedOutput();
        program->release();
    }
}

void FFGLRenderer::synchronize(QQuickFramebufferObject *item)
{
    FFGLItem *glItem = static_cast<FFGLItem *>(item);
    m_fboSize = glItem->size().toSize();

    QMutexLocker lk(&glItem->mtx);
    m_yData = glItem->yData;
    m_uData = glItem->uData;
    m_vData = glItem->vData;
    m_imgWidth = glItem->width;
    m_imgHeight = glItem->height;
    m_previewVersion = glItem->previewVersion;
    m_previewFingerprint = glItem->previewFingerprint;

    PreviewBridge::instance().markSynchronized(m_previewVersion, m_imgWidth, m_imgHeight, m_previewFingerprint);
}

void FFGLRenderer::ensureProgram()
{
    if (program)
        return;
    program = new QOpenGLShaderProgram();
    program->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaderSource/source.vert");
    program->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaderSource/source.frag");
    program->link();
    qDebug() << "GLSL log:" << program->log();
    auto ctx = QOpenGLContext::currentContext();
    qDebug() << "GL version:" << ctx->format().majorVersion() << "." << ctx->format().minorVersion()
             << ", profile:" << ctx->format().profile();
}

void FFGLRenderer::ensureBuffers()
{
    if (vao)
        return;
    // static const float vertices[] = {-1.f, 1.f, 0.f, 0.f, -1.f, -1.f, 0.f, 1.f, 1.f, -1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 0.f};
    static const float vertices[] = {
        -1.f, 1.f, 0.f, 1.f,  // 左上角，v从0改成1
        -1.f, -1.f, 0.f, 0.f, // 左下角
        1.f, -1.f, 1.f, 0.f,  // 右下角
        1.f, 1.f, 1.f, 1.f    // 右上角
    };

    static const unsigned int indices[] = {0, 1, 2, 3};
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void FFGLRenderer::ensureTextures()
{
    if (yTex)
        return;
    glGenTextures(1, &yTex);
    glGenTextures(1, &uTex);
    glGenTextures(1, &vTex);

    // 先绑定黑屏
    auto initTex = [&](GLuint tex, uint8_t v)
    {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, &v);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    uint8_t y = 0, uv = 128;
    initTex(yTex, y);
    initTex(uTex, uv);
    initTex(vTex, uv);
}

void FFGLRenderer::maybeLogRenderedOutput()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool shouldLog = m_previewVersion <= 3
        || m_previewVersion != m_lastRenderOutputVersion
        || (nowMs - m_lastRenderOutputLogMs) >= 500;
    if (!shouldLog) {
        return;
    }

    const int pixelBytes = 4;
    const int bufferSize = m_fboSize.width() * m_fboSize.height() * pixelBytes;
    if (bufferSize <= 0) {
        return;
    }

    QByteArray rgba(bufferSize, Qt::Uninitialized);
    glReadPixels(0, 0, m_fboSize.width(), m_fboSize.height(), GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    const QByteArray outputHash =
        QCryptographicHash::hash(rgba, QCryptographicHash::Md5).toHex();

    qDebug().noquote()
        << "[PreviewRenderHash]"
        << "version=" << m_previewVersion
        << "fbo=" << QString("%1x%2").arg(m_fboSize.width()).arg(m_fboSize.height())
        << "image=" << QString("%1x%2").arg(m_imgWidth).arg(m_imgHeight)
        << "inputFingerprint=0x" << QString::number(m_previewFingerprint, 16)
        << "outputMd5=" << outputHash;

    if (m_previewVersion <= 3) {
        QDir().mkpath("preview-render-dumps");
        QImage image(reinterpret_cast<const uchar *>(rgba.constData()),
                     m_fboSize.width(),
                     m_fboSize.height(),
                     QImage::Format_RGBA8888);
        image.mirrored().save(QString("preview-render-dumps/render-%1.png").arg(m_previewVersion));
    }

    m_lastRenderOutputLogMs = nowMs;
    m_lastRenderOutputVersion = m_previewVersion;
}
