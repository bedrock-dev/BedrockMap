#ifndef BEDROCKMAP_RENDERTIMINGS_H
#define BEDROCKMAP_RENDERTIMINGS_H

#include <QElapsedTimer>
#include <QHash>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QStringList>
#include <QThread>
#include <chrono>
#include <memory>
#include <vector>

#if defined(BEDROCKMAP_ENABLE_RENDER_TIMINGS)

class RenderTimings {
   public:
    void push(const QString& name) {
        auto& state = threadState();
        Node* node = nullptr;
        if (state.stack.empty()) {
            state.root = Node{name};
            node = &state.root;
            const QMutexLocker lock(&mutex_);
            state.generation = generation_;
        } else {
            auto& children = state.stack.back().node->children;
            for (auto& child : children) {
                if (child.name == name) {
                    node = &child;
                    break;
                }
            }
            if (!node) {
                children.push_back(Node{name});
                node = &children.back();
            }
        }
        QElapsedTimer timer;
        timer.start();
        state.stack.push_back({node, timer});
    }

    void pop() { pop(threadState()); }

   private:
    struct ThreadState;
    void pop(ThreadState& state) {
        Q_ASSERT(!state.stack.empty());
        if (state.stack.empty()) return;
        const auto active = state.stack.back();
        active.node->nanoseconds += active.timer.nsecsElapsed();
        ++active.node->calls;
        state.stack.pop_back();
        if (!state.stack.empty()) return;
        // Only completed roots cross threads. Formatting never touches a live stack.
        const QMutexLocker lock(&mutex_);
        if (state.generation != generation_) return;
        if (!captures_.contains(state.root.name)) capture_order_.append(state.root.name);
        auto& capture = captures_[state.root.name];
        ++capture.count;
        capture.total += state.root.nanoseconds;
        if (state.root.nanoseconds > capture.slowest.nanoseconds) capture.slowest = state.root;
        capture.latest = std::move(state.root);
    }

   public:
    void recordFrameInterval(double ms) {
        const QMutexLocker lock(&mutex_);
        interval_ms_ = ms;
        if (ms > interval_peak_ms_) interval_peak_ms_ = ms;
        interval_total_ms_ += ms;
        ++interval_count_;
    }
    double framesPerSecond() const {
        const QMutexLocker lock(&mutex_);
        return interval_total_ms_ > 0 ? 1000.0 * interval_count_ / interval_total_ms_ : 0.0;
    }
    QString graphicsInfo() const {
        const QMutexLocker lock(&mutex_);
        return graphics_info_;
    }
    void recordGpuTime(double ms) {
        if (!(ms > 0.0)) return;
        const QMutexLocker lock(&mutex_);
        gpu_ms_ = ms;
        if (ms > gpu_peak_ms_) gpu_peak_ms_ = ms;
    }
    void recordFrameUploadPeak(double frameMs, int terrainTiles, int blocksPerTexel) {
        if (terrainTiles <= 0) return;
        const QMutexLocker lock(&mutex_);
        if (frameMs > upload_peak_.frameMs)
            upload_peak_ = UploadPeak{frameMs, terrainTiles, blocksPerTexel};
    }
    void setGraphicsInfo(const QString& info) {
        const QMutexLocker lock(&mutex_);
        graphics_info_ = info;
    }
    void setDragActive(bool active) {
        const QMutexLocker lock(&mutex_);
        drag_active_ = active;
        if (!active) pointer_received_ns_ = 0;
    }
    void pointerEventReceived() {
        const QMutexLocker lock(&mutex_);
        pointer_received_ns_ = nowNs();
    }
    void dragCameraUpdated() {
        const QMutexLocker lock(&mutex_);
        if (!drag_active_ || !pointer_received_ns_) return;
        const qint64 now = nowNs();
        dispatch_latency_.record(now - pointer_received_ns_);
        if (!pending_input_.first) pending_input_.first = pointer_received_ns_;
        pending_input_.latest = pointer_received_ns_;
        ++pending_input_.events;
        ++drag_events_;
        pointer_received_ns_ = 0;
    }
    void recordGuiTimerSlip(double ms) {
        const QMutexLocker lock(&mutex_);
        if (drag_active_) gui_slip_.record(static_cast<qint64>(ms * 1.0e6));
    }
    void dragWaitingForLod() {
        const QMutexLocker lock(&mutex_);
        if (!pending_input_.first) return;
        pending_input_.lod_waited = true;
        ++lod_wait_frames_;
    }
    void dragCameraApplied() {
        const QMutexLocker lock(&mutex_);
        if (!pending_input_.first) return;
        sync_latency_.record(nowNs() - pending_input_.latest);
        // Capture only input actually applied to this frame. GUI events arriving
        // after scene synchronization belong to a later frame's pending group.
        frame_input_ = pending_input_;
        pending_input_ = {};
    }
    void dragFrameSubmitted() {
        const QMutexLocker lock(&mutex_);
        if (!frame_input_.first) return;
        const qint64 now = nowNs();
        submit_latency_.record(now - frame_input_.latest);
        oldest_latency_.record(now - frame_input_.first);
        merged_events_ = frame_input_.events;
        last_lod_waited_ = frame_input_.lod_waited;
        ++drag_frames_;
        frame_input_ = {};
    }
    void reset() {
        const QMutexLocker lock(&mutex_);
        ++generation_;
        captures_.clear();
        capture_order_.clear();
        interval_ms_ = interval_total_ms_ = 0.0;
        interval_peak_ms_ = 0.0;
        interval_count_ = 0;
        gpu_ms_ = gpu_peak_ms_ = 0.0;
        upload_peak_ = {};
        pointer_received_ns_ = 0;
        pending_input_ = frame_input_ = {};
        dispatch_latency_ = sync_latency_ = submit_latency_ = oldest_latency_ = gui_slip_ = {};
        drag_events_ = drag_frames_ = lod_wait_frames_ = merged_events_ = 0;
        last_lod_waited_ = false;
    }
    QString toString() const {
        QMap<QString, Capture> captures;
        double interval = 0.0;
        double intervalPeak = 0.0;
        double intervalAverage = 0.0;
        double gpu = 0.0;
        double gpuPeak = 0.0;
        UploadPeak uploadPeak;
        QStringList captureOrder;
        QStringList dragCurrentLines;
        {
            const QMutexLocker lock(&mutex_);
            captures = captures_;
            captureOrder = capture_order_;
            interval = interval_ms_;
            intervalPeak = interval_peak_ms_;
            intervalAverage = interval_count_ ? interval_total_ms_ / interval_count_ : 0.0;
            gpu = gpu_ms_;
            gpuPeak = gpu_peak_ms_;
            uploadPeak = upload_peak_;
            if (drag_events_) {
                dragCurrentLines
                    << QStringLiteral("Drag ms latest: event->camera %1 | GUI slip %2")
                           .arg(dispatch_latency_.latest, 0, 'f', 1)
                           .arg(gui_slip_.latest, 0, 'f', 1);
                dragCurrentLines << QStringLiteral("  Input->sync %1 | ->submit %2")
                                        .arg(sync_latency_.latest, 0, 'f', 1)
                                        .arg(submit_latency_.latest, 0, 'f', 1);
                dragCurrentLines << QStringLiteral("  Oldest->submit %1 | merged %2 | frames %3")
                                        .arg(oldest_latency_.latest, 0, 'f', 1)
                                        .arg(merged_events_)
                                        .arg(drag_frames_);
                dragCurrentLines << QStringLiteral("  LOD wait frames %1 | last submitted %2 | pending %3 ms")
                                        .arg(lod_wait_frames_)
                                        .arg(last_lod_waited_ ? QStringLiteral("waited") : QStringLiteral("ready"))
                                        .arg(pending_input_.first ? (nowNs() - pending_input_.first) / 1.0e6 : 0.0, 0, 'f', 1);
            }
        }
        QStringList lines;
        lines << QStringLiteral("FRAME BLOCKING");
        lines << QStringLiteral("Frame interval (incl. idle): %1 ms | avg %2 ms")
                     .arg(interval, 0, 'f', 2)
                     .arg(intervalAverage, 0, 'f', 2);
        const auto renderCapture = captures.constFind(QStringLiteral("Render frame CPU"));
        const bool hasRenderCapture = renderCapture != captures.cend();
        const auto rhiCapture = captures.constFind(QStringLiteral("RHI texture commit"));
        const bool rhiNestedInFrame =
            hasRenderCapture &&
            (nodeContainsName(renderCapture->latest, QStringLiteral("RHI texture commit")) ||
             nodeContainsName(renderCapture->slowest, QStringLiteral("RHI texture commit")));
        if (hasRenderCapture) {
            lines << QStringLiteral("Render frame CPU: latest %1 ms | avg %2 ms | n=%3")
                         .arg(renderCapture->latest.nanoseconds / 1.0e6, 0, 'f', 2)
                         .arg(renderCapture->total / renderCapture->count / 1.0e6, 0, 'f', 2)
                         .arg(renderCapture->count);
            lines << QStringLiteral("Latest execution tree");
            appendTree(lines, renderCapture->latest, 0);
        }
        if (!rhiNestedInFrame && rhiCapture != captures.cend()) {
            lines << QStringLiteral("RHI texture commit: latest %1 ms | avg %2 ms | n=%3")
                         .arg(rhiCapture->latest.nanoseconds / 1.0e6, 0, 'f', 2)
                         .arg(rhiCapture->total / rhiCapture->count / 1.0e6, 0, 'f', 2)
                         .arg(rhiCapture->count);
        }
        lines << (gpu > 0.0 ? QStringLiteral("GPU (delayed): %1 ms").arg(gpu, 0, 'f', 2)
                            : QStringLiteral("GPU: pending / unavailable"));

        lines << QString() << QStringLiteral("FRAME BLOCKING PEAKS (NOT LIVE)");
        lines << QStringLiteral("Frame interval peak (incl. idle): %1 ms").arg(intervalPeak, 0, 'f', 2);
        if (hasRenderCapture && renderCapture->slowest.nanoseconds >= displayThresholdNs) {
            lines << QStringLiteral("Render frame CPU peak: %1 ms")
                         .arg(renderCapture->slowest.nanoseconds / 1.0e6, 0, 'f', 2);
            lines << QStringLiteral("Slowest execution tree");
            appendTree(lines, renderCapture->slowest, 0);
        }
        if (!rhiNestedInFrame && rhiCapture != captures.cend() && rhiCapture->slowest.nanoseconds >= displayThresholdNs) {
            lines << QStringLiteral("RHI texture commit peak: %1 ms")
                         .arg(rhiCapture->slowest.nanoseconds / 1.0e6, 0, 'f', 2);
            appendTree(lines, rhiCapture->slowest, 0);
        }
        if (gpuPeak > 0.0) lines << QStringLiteral("GPU peak: %1 ms").arg(gpuPeak, 0, 'f', 2);
        if (uploadPeak.frameMs > 0.0) {
            lines << QStringLiteral("Peak upload frame: %1 terrain tiles | LOD %2 | %3 ms")
                         .arg(uploadPeak.terrainTiles)
                         .arg(uploadPeak.blocksPerTexel)
                         .arg(uploadPeak.frameMs, 0, 'f', 2);
        }

        QStringList nonFrameLines;
        nonFrameLines << dragCurrentLines;
        for (const auto& name : captureOrder) {
            if (name == QStringLiteral("Render frame CPU")) continue;
            const auto capture = captures.constFind(name);
            if (capture == captures.cend()) continue;
            if (hasRenderCapture && (nodeContainsName(renderCapture->latest, name) || nodeContainsName(renderCapture->slowest, name)))
                continue;
            if (name == QStringLiteral("RHI texture commit")) continue;
            nonFrameLines << QStringLiteral("%1: latest %2 | avg %3 ms | n=%4")
                                 .arg(name)
                                 .arg(capture->latest.nanoseconds / 1.0e6, 0, 'f', 2)
                                 .arg(capture->total / capture->count / 1.0e6, 0, 'f', 2)
                                 .arg(capture->count);
        }
        if (!nonFrameLines.isEmpty())
            lines << QStringLiteral("----------------------") << QStringLiteral("NON-FRAME (LATEST + AVG)") << nonFrameLines;
        return lines.join(QLatin1Char('\n'));
    }

   private:
    friend class RenderTimingScope;
    static qint64 nowNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    struct Latency {
        double latest{0.0};
        double peak{0.0};
        void record(qint64 ns) {
            latest = ns / 1.0e6;
            if (latest > peak) peak = latest;
        }
    };
    struct InputGroup {
        qint64 first{0};
        qint64 latest{0};
        quint64 events{0};
        bool lod_waited{false};
    };
    static constexpr qint64 displayThresholdNs = 100000;
    struct Node {
        QString name;
        qint64 nanoseconds{0};
        quint64 calls{0};
        std::vector<Node> children;
    };
    struct Active {
        Node* node;
        QElapsedTimer timer;
    };
    struct ThreadState {
        Node root;
        std::vector<Active> stack;
        quint64 generation{0};
    };
    struct Capture {
        Node latest;
        Node slowest;
        qint64 total{0};
        quint64 count{0};
    };
    struct UploadPeak {
        double frameMs{0.0};
        int terrainTiles{0};
        int blocksPerTexel{1};
    };
    ThreadState& threadState() { return *currentState(); }
    std::shared_ptr<ThreadState> currentState() {
        const QMutexLocker lock(&states_mutex_);
        auto& state = states_[QThread::currentThreadId()];
        if (!state) state = std::make_shared<ThreadState>();
        return state;
    }
    static bool nodeContainsName(const Node& node, const QString& name) {
        if (node.name == name) return true;
        for (const auto& child : node.children)
            if (nodeContainsName(child, name)) return true;
        return false;
    }
    static void appendTree(QStringList& lines, const Node& node, int depth) {
        qint64 childTime = 0;
        for (const auto& child : node.children) childTime += child.nanoseconds;
        const QString label = QString(depth * 2, QLatin1Char(' ')) + (depth ? QStringLiteral("|- ") : QString()) + node.name;
        lines << QStringLiteral("%1 %2 %3 %4")
                     .arg(label.leftJustified(32))
                     .arg(node.nanoseconds / 1.0e6, 7, 'f', 2)
                     .arg((node.nanoseconds - childTime) / 1.0e6, 7, 'f', 2)
                     .arg(node.calls, 5);
        // Filtering affects display only; self time still excludes every child.
        for (const auto& child : node.children) {
            if (child.nanoseconds >= displayThresholdNs) appendTree(lines, child, depth + 1);
        }
    }
    // A map can close while the scene graph and worker threads are still alive.
    // Own their timing states here: destroying a per-instance QThreadStorage
    // leaves values on those threads, whose slot IDs Qt may reuse for another
    // type before thread exit, invoking the wrong destructor on the old value.
    QMutex states_mutex_;
    QHash<Qt::HANDLE, std::shared_ptr<ThreadState>> states_;
    mutable QMutex mutex_;
    QMap<QString, Capture> captures_;
    QStringList capture_order_;
    quint64 generation_{0};
    double interval_ms_{0.0};
    double interval_peak_ms_{0.0};
    double interval_total_ms_{0.0};
    quint64 interval_count_{0};
    double gpu_ms_{0.0};
    double gpu_peak_ms_{0.0};
    UploadPeak upload_peak_;
    QString graphics_info_;
    bool drag_active_{false};
    qint64 pointer_received_ns_{0};
    InputGroup pending_input_, frame_input_;
    Latency dispatch_latency_, sync_latency_, submit_latency_, oldest_latency_, gui_slip_;
    quint64 drag_events_{0}, drag_frames_{0}, lod_wait_frames_{0}, merged_events_{0};
    bool last_lod_waited_{false};
};

class RenderTimingScope {
   public:
    RenderTimingScope(RenderTimings* timings, const QString& name) : timings_(timings) {
        if (timings_) {
            state_ = timings_->currentState();
            timings_->push(name);
        }
    }
    ~RenderTimingScope() { stop(); }
    RenderTimingScope(const RenderTimingScope&) = delete;
    RenderTimingScope& operator=(const RenderTimingScope&) = delete;
    void stop() {
        if (timings_) timings_->pop(*state_);
        timings_ = nullptr;
    }
    void next(const QString& name) {
        if (!timings_) return;
        timings_->pop(*state_);
        timings_->push(name);
    }

   private:
    RenderTimings* timings_;
    std::shared_ptr<RenderTimings::ThreadState> state_;
};

#else

class RenderTimings {
   public:
    void push(const QString&) {}
    void pop() {}
    void recordFrameInterval(double) {}
    [[nodiscard]] double framesPerSecond() const { return 0.0; }
    [[nodiscard]] QString graphicsInfo() const { return {}; }
    void recordGpuTime(double) {}
    void recordFrameUploadPeak(double, int, int) {}
    void setGraphicsInfo(const QString&) {}
    void setDragActive(bool) {}
    void pointerEventReceived() {}
    void dragCameraUpdated() {}
    void recordGuiTimerSlip(double) {}
    void dragWaitingForLod() {}
    void dragCameraApplied() {}
    void dragFrameSubmitted() {}
    void reset() {}
    [[nodiscard]] QString toString() const { return {}; }
};

class RenderTimingScope {
   public:
    RenderTimingScope(RenderTimings*, const QString&) {}
    void stop() {}
    void next(const QString&) {}
};

#endif  // BEDROCKMAP_ENABLE_RENDER_TIMINGS

#endif
