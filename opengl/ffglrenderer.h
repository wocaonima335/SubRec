#ifndef FFGLRENDERER_H
#define FFGLRENDERER_H

#include "ffglitem.h"

#include <QOpenGLFunctions>
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLShaderProgram>

class FFGLRenderer : public QQuickFramebufferObject::Renderer, protected QOpenGLFunctions_4_5_Core
{
public:
    FFGLRenderer(FFGLItem *item);
    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override;
    void render() override;
    void synchronize(QQuickFramebufferObject *item) override;

private:
    void ensureProgram();
    void ensureBuffers();
    void ensureTextures();
    void maybeLogRenderedOutput();

private:
    QSize m_fboSize;
    QOpenGLShaderProgram *program = nullptr;
    GLuint vao = 0, vbo = 0, ebo = 0;
    GLuint yTex = 0, uTex = 0, vTex = 0;

    QByteArray m_yData, m_uData, m_vData;
    int m_imgWidth = 0, m_imgHeight = 0;
    quint64 m_previewVersion = 0;
    quint64 m_previewFingerprint = 0;
    bool m_glInitialized = false;
    qint64 m_lastRenderOutputLogMs = 0;
    quint64 m_lastRenderOutputVersion = 0;
};

#endif // FFGLRENDERER_H
