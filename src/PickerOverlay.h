#pragma once

#include <QImage>
#include <QPoint>
#include <QRegion>
#include <QStringList>
#include <QWidget>
#include <functional>

// One clear, full-screen overlay per monitor. The desktop shows through live; the lens and
// badge are fed by small KWin captures of the area around the cursor, refreshed continuously.
class PickerOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit PickerOverlay(QWidget *parent = nullptr);

    // Live grab of a global logical rect at native resolution (thread-safe); null on failure.
    static inline std::function<QImage(const QRect &)> s_captureArea;
    // Called once if live capture fails, so main() can fall back to a frozen screenshot.
    static inline std::function<void()> s_liveFailed;
    // Fallback: freeze this whole-screen shot and stop live sampling.
    void setFrozen(const QImage &shot);
    static void stopSampler();

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;

public:
    static const QStringList &formatNames();   // ids accepted by --format, in F-cycle order
    static inline int s_format = 0;            // index into formatNames()
    static inline bool s_copy = true;          // --no-copy disables clipboard
    static inline bool s_stdoutHex = false;
    static inline int s_span = 15;             // source pixels across the lens (odd); wheel changes
    static inline bool s_lensOn = true;        // Alt toggles
    static inline bool s_badgeOn = true;       // B toggles the color badge
    static QString formatColor(const QColor &c);
    static bool setFormat(const QString &name);
    static void deliver(const QColor &c); // print to stdout and copy

    // No centered lens: it would sit on the pixels being captured.
    enum class Placement { Right, BottomRight, BottomLeft, TopLeft, TopRight, Count };

    void setPatch(const QImage &img, const QRect &globalArea); // from the sampler

private:
    QPoint toImage(QPointF widgetPos) const;
    QColor colorAt(QPoint imgPos) const;
    QRect sampleArea() const; // global logical rect to grab around the cursor
    void requestSample();
    void drawMagnifier(QPainter &p);
    void drawBadge(QPainter &p, QPointF center, const QColor &c);
    void drawKeyCursor(QPainter &p);
    void refreshAll();
    void pick();

    QImage m_patch;      // pixels around the cursor (or the whole screen when frozen)
    QRectF m_patchRect;  // where m_patch sits, widget coordinates
    bool m_frozen = false;
    QPointF m_cursor;    // widget coordinates
    bool m_keyMoved = false; // arrow keys moved m_cursor away from the real pointer
    bool m_hover = false;
    QRegion m_blur;      // KWin blur-behind region, under the badge

    // Shared across all screen overlays.
    static inline Placement s_place = Placement::Right; // Tab cycles
};
