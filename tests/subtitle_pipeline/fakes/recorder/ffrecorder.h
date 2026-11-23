#pragma once
#include <QObject>
// The worker receives nullptr: only recorder/UI error delivery is replaced.
// Real audio worker, AAC encoder, sync/frame/packet queues are linked unchanged.
class FFRecorder : public QObject {};
