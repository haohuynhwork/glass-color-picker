#include "PickerOverlay.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QEnterEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QScreen>
#include <QWindow>
#include <QTimer>
#include <QtMath>
#include <QMutex>
#include <QPointer>
#include <QThread>
#include <QWaitCondition>
#include <KWindowEffects>
#include <cstdio>

namespace {
constexpr int kLens = 160;      // lens diameter in logical px
constexpr int kOffset = 14;     // gap between cursor and lens (frozen; live adds room for the grab)
constexpr int kShadow = 12;     // lens drop shadow reach, kept out of the live grab
constexpr int kRightDrop = 72;  // Right placement: lens center sits this far below the cursor
constexpr int kGrabRadius = 18; // live grab half-size (logical px): room for cursor motion between grabs
constexpr int kBadgeGrabRadius = 8; // lens off: only the cursor pixel is needed, so the badge can sit close

// One worker grabs the latest requested area over and over while the cursor is on a screen,
// so the lens follows the cursor and also shows animations underneath it.
struct Sampler {
    QMutex mutex;
    QWaitCondition wake;
    QRect want;      // global logical; null = idle
    bool quit = false;
    bool failed = false;
    QThread *thread = nullptr;

    void request(const QRect &r)
    {
        QMutexLocker l(&mutex);
        want = r;
        wake.wakeOne();
        if (!thread) {
            thread = QThread::create([this] { run(); });
            thread->start();
        }
    }
    void run()
    {
        for (;;) {
            QRect r;
            {
                QMutexLocker l(&mutex);
                while (want.isNull() && !quit)
                    wake.wait(&mutex);
                if (quit)
                    return;
                r = want;
            }
            const QImage img = PickerOverlay::s_captureArea(r);
            QMetaObject::invokeMethod(qApp, [this, img, r] {
                if (img.isNull()) {
                    if (!failed) {
                        failed = true;
                        stop();
                        if (PickerOverlay::s_liveFailed)
                            PickerOverlay::s_liveFailed();
                    }
                    return;
                }
                for (QWidget *w : QApplication::topLevelWidgets())
                    if (auto *o = qobject_cast<PickerOverlay *>(w))
                        o->setPatch(img, r);
            });
            QThread::msleep(4); // grabs take ~1ms; don't hammer KWin
        }
    }
    void stop()
    {
        {
            QMutexLocker l(&mutex);
            quit = true;
            wake.wakeOne();
        }
        if (thread && thread != QThread::currentThread())
            thread->wait(500);
    }
};
Sampler s_sampler;
}

void PickerOverlay::stopSampler()
{
    s_sampler.stop();
}

PickerOverlay::PickerOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);
    m_cursor = mapFromGlobal(QCursor::pos());
}

void PickerOverlay::setFrozen(const QImage &shot)
{
    m_frozen = true;
    m_patch = shot;
    m_patchRect = rect();
    update();
}

// Enough pixels for the widest zoom, centered on the cursor; small enough
// that the lens (kept outside it, see drawMagnifier) and the badge never fall inside it.
QRect PickerOverlay::sampleArea() const
{
    const qreal dpr = windowHandle() ? windowHandle()->screen()->devicePixelRatio() : devicePixelRatioF();
    const int side = s_lensOn ? qMax(2 * kGrabRadius, qCeil((s_span + 2) / dpr)) | 1
                              : (2 * kBadgeGrabRadius) | 1;
    const QPoint c = mapToGlobal(m_cursor.toPoint());
    return QRect(c.x() - side / 2, c.y() - side / 2, side, side);
}

void PickerOverlay::requestSample()
{
    if (!m_frozen && m_hover && s_captureArea)
        s_sampler.request(sampleArea());
}

void PickerOverlay::setPatch(const QImage &img, const QRect &area)
{
    if (m_frozen || !m_hover)
        return;
    m_patch = img;
    m_patchRect = QRectF(mapFromGlobal(area.topLeft()), QSizeF(area.size()));
    update();
}

// Same ids and output as the Plasma widget's format list.
const QStringList &PickerOverlay::formatNames()
{
    static const QStringList names = {"hex",     "hex-nohash", "hex-lower", "hex-lower-nohash", "rgb-plain",
                                      "rgb",     "rgba",       "qt-rgba",   "hsl",              "hsv",
                                      "cmyk",    "latex"};
    return names;
}

QString PickerOverlay::formatColor(const QColor &c)
{
    const QString id = formatNames().value(s_format);
    const QString hex = c.name(QColor::HexRgb).toUpper();
    const auto f2 = [](qreal v) { return QString::number(v, 'f', 2); };
    const auto pct = [](qreal v) { return qRound(v * 100); };

    if (id == "hex-nohash") return hex.mid(1);
    if (id == "hex-lower") return hex.toLower();
    if (id == "hex-lower-nohash") return hex.mid(1).toLower();
    if (id == "rgb-plain") return QStringLiteral("%1, %2, %3").arg(c.red()).arg(c.green()).arg(c.blue());
    if (id == "rgb") return QStringLiteral("rgb(%1, %2, %3)").arg(c.red()).arg(c.green()).arg(c.blue());
    if (id == "rgba") return QStringLiteral("rgba(%1, %2, %3, 1)").arg(c.red()).arg(c.green()).arg(c.blue());
    if (id == "qt-rgba")
        return QStringLiteral("Qt.rgba(%1, %2, %3, 1)").arg(f2(c.redF()), f2(c.greenF()), f2(c.blueF()));
    if (id == "hsl") {
        const QColor h = c.toHsl();
        return QStringLiteral("hsl(%1, %2%, %3%)").arg(qMax(0, h.hslHue())).arg(pct(h.hslSaturationF())).arg(pct(h.lightnessF()));
    }
    if (id == "hsv") {
        const QColor h = c.toHsv();
        return QStringLiteral("hsv(%1, %2%, %3%)").arg(qMax(0, h.hsvHue())).arg(pct(h.hsvSaturationF())).arg(pct(h.valueF()));
    }
    if (id == "cmyk") {
        const QColor k = c.toCmyk();
        return QStringLiteral("cmyk(%1%, %2%, %3%, %4%)")
            .arg(pct(k.cyanF())).arg(pct(k.magentaF())).arg(pct(k.yellowF())).arg(pct(k.blackF()));
    }
    if (id == "latex")
        return QStringLiteral("\\definecolor{ColorName}{rgb}{%1,%2,%3}").arg(f2(c.redF()), f2(c.greenF()), f2(c.blueF()));
    return hex;
}

bool PickerOverlay::setFormat(const QString &name)
{
    const int i = formatNames().indexOf(name.toLower());
    if (i < 0)
        return false;
    s_format = i;
    return true;
}

QPoint PickerOverlay::toImage(QPointF pos) const
{
    // Map the cursor's range [0, w-1] onto the image's [0, W-1] so both edges are reachable
    // (plain pos * W/w stops one image pixel short of the right/bottom edge on scaled screens).
    const qreal sx = m_patchRect.width() > 1 ? qreal(m_patch.width() - 1) / (m_patchRect.width() - 1) : 1;
    const qreal sy = m_patchRect.height() > 1 ? qreal(m_patch.height() - 1) / (m_patchRect.height() - 1) : 1;
    const QPointF local = pos - m_patchRect.topLeft();
    QPoint ip(qRound(local.x() * sx), qRound(local.y() * sy));
    ip.setX(qBound(0, ip.x(), m_patch.width() - 1));
    ip.setY(qBound(0, ip.y(), m_patch.height() - 1));
    return ip;
}

QColor PickerOverlay::colorAt(QPoint ip) const
{
    return m_patch.pixelColor(ip);
}

void PickerOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    if (m_frozen)
        p.drawImage(rect(), m_patch);
    else
        p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    const QRegion oldBlur = m_blur;
    m_blur = {};
    if (m_hover && !m_patch.isNull()) {
    if (s_lensOn) {
        drawMagnifier(p); // the crosshair cursor marks the pick point
    } else {
        // Lens off: small badge beside the crosshair, flipped near screen edges.
        p.setRenderHint(QPainter::Antialiasing);
        // Pill's near edge sits kGap from the crosshair (size matches drawBadge).
        constexpr qreal kPillH = 30;
        const qreal kGap = m_frozen ? 10 : kBadgeGrabRadius + 4; // live: clear the sampled area
        const QColor c = colorAt(toImage(m_cursor));
        QFont f = font();
        f.setPointSizeF(10);
        f.setBold(true);
        const qreal halfW = (QFontMetricsF(f).horizontalAdvance(formatColor(c)) + 44) / 2;
        QPointF ctr = m_cursor + QPointF(kGap + halfW, kGap + kPillH / 2);
        if (ctr.x() + halfW > width()) ctr.setX(m_cursor.x() - kGap - halfW);
        if (ctr.y() + kPillH / 2 > height()) ctr.setY(m_cursor.y() - kGap - kPillH / 2);
        drawBadge(p, ctr, c);
    }
    }
    if (m_hover && m_keyMoved)
        drawKeyCursor(p);
    if (m_blur != oldBlur && windowHandle())
        KWindowEffects::enableBlurBehind(windowHandle(), !m_blur.isEmpty(), m_blur);
}

void PickerOverlay::refreshAll()
{
    for (QWidget *w : QApplication::topLevelWidgets())
        if (auto *o = qobject_cast<PickerOverlay *>(w)) {
            o->update();
        }
}

void PickerOverlay::enterEvent(QEnterEvent *e)
{
    m_hover = true;
    m_cursor = e->position();
    activateWindow();
    setFocus();
    requestSample();
    update();
}

void PickerOverlay::leaveEvent(QEvent *)
{
    m_hover = false;
    if (!m_frozen)
        m_patch = {};
    update();
}

void PickerOverlay::drawMagnifier(QPainter &p)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const QPoint ip = toImage(m_cursor);
    const QColor c = colorAt(ip);

    // Place the lens per s_place (Tab cycles), flipping when it would leave the screen.
    // Live: the lens and its shadow must stay outside the area being grabbed around the cursor.
    const qreal gap = m_frozen ? kOffset : qMax<qreal>(kOffset, sampleArea().width() / 2.0 + kShadow);
    const qreal right = m_cursor.x() + gap, left = m_cursor.x() - gap - kLens;
    const qreal below = m_cursor.y() + gap, above = m_cursor.y() - gap - kLens - 44;
    QPointF tl;
    switch (s_place) {
    case Placement::Right: tl = {right, m_cursor.y() - kLens / 2.0 + kRightDrop}; break;
    case Placement::BottomRight: tl = {right, below}; break;
    case Placement::BottomLeft: tl = {left, below}; break;
    case Placement::TopLeft: tl = {left, above}; break;
    case Placement::TopRight: tl = {right, above}; break;
    default: tl = {right, above}; break;
    }
    if (tl.x() + kLens > width()) tl.setX(left);
    if (tl.x() < 0) tl.setX(right);
    if (s_place == Placement::Right)
        tl.setY(qBound(0.0, tl.y(), height() - kLens - 44.0));
    else {
        if (tl.y() + kLens + 44 > height()) tl.setY(above);
        if (tl.y() < 0) tl.setY(below);
    }
    const QRectF lens(tl, QSizeF(kLens, kLens));
    const QPointF ctr = lens.center();
    const qreal r = kLens / 2.0;

    // Soft drop shadow.
    p.setPen(Qt::NoPen);
    for (int i = 10; i > 0; --i) {
        p.setBrush(QColor(0, 0, 0, 6));
        p.drawEllipse(ctr + QPointF(0, 4), r + i, r + i);
    }

    // Zoomed pixels, clipped to the circle.
    QPainterPath circle;
    circle.addEllipse(lens);
    p.save();
    p.setClipPath(circle);
    const int half = s_span / 2;
    QRect src(ip.x() - half, ip.y() - half, s_span, s_span);
    // A live patch can trail a fast-moving cursor by a grab; keep the view inside it rather than
    // showing black beyond its edge (it re-centers on the next grab, ~5ms later).
    if (!m_frozen && m_patch.width() >= s_span && m_patch.height() >= s_span) {
        src.moveLeft(qBound(0, src.left(), m_patch.width() - s_span));
        src.moveTop(qBound(0, src.top(), m_patch.height() - s_span));
    }
    QImage crop(s_span, s_span, QImage::Format_RGB32);
    crop.fill(Qt::black);
    {
        QPainter cp(&crop);
        cp.drawImage(QPoint(0, 0), m_patch, src);
        // drawImage with negative src origin is clipped, so handle edges manually.
        if (src.left() < 0 || src.top() < 0) {
            crop.fill(Qt::black);
            cp.drawImage(QPoint(qMax(0, -src.left()), qMax(0, -src.top())), m_patch,
                         src.intersected(m_patch.rect()));
        }
    }
    const qreal cell = qreal(kLens) / s_span;
    p.drawImage(lens, crop.scaled(s_span * int(qCeil(cell)), s_span * int(qCeil(cell)),
                                  Qt::IgnoreAspectRatio, Qt::FastTransformation));

    // Faint pixel grid.
    p.setPen(QPen(QColor(0, 0, 0, 35), 1));
    for (int i = 1; i < s_span; ++i) {
        p.drawLine(QPointF(lens.left() + i * cell, lens.top()), QPointF(lens.left() + i * cell, lens.bottom()));
        p.drawLine(QPointF(lens.left(), lens.top() + i * cell), QPointF(lens.right(), lens.top() + i * cell));
    }

    // Center pixel marker in a contrasting color.
    const QRectF centerCell(lens.left() + half * cell, lens.top() + half * cell, cell, cell);
    const bool light = c.lightnessF() > 0.5;
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(light ? QColor(0, 0, 0, 200) : QColor(255, 255, 255, 220), 1.5));
    p.drawRect(centerCell.adjusted(-0.5, -0.5, 0.5, 0.5));

    // Glass rim: darker toward the edge, clear in the middle (lens refraction feel).
    QRadialGradient rim(ctr, r);
    rim.setColorAt(0.0, QColor(255, 255, 255, 0));
    rim.setColorAt(0.78, QColor(255, 255, 255, 0));
    rim.setColorAt(0.92, QColor(255, 255, 255, 40));
    rim.setColorAt(1.0, QColor(0, 0, 0, 90));
    p.setPen(Qt::NoPen);
    p.setBrush(rim);
    p.drawEllipse(lens);

    // Specular highlight, top-left.
    QRectF hl(lens.left() + kLens * 0.12, lens.top() + kLens * 0.06, kLens * 0.62, kLens * 0.38);
    QLinearGradient hg(hl.topLeft(), hl.bottomLeft());
    hg.setColorAt(0.0, QColor(255, 255, 255, 130));
    hg.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setBrush(hg);
    p.drawEllipse(hl);
    p.restore();

    // Thin bright border.
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 170), 1.5));
    p.drawEllipse(lens.adjusted(0.75, 0.75, -0.75, -0.75));

    // Frosted label pill under the lens.
    drawBadge(p, QPointF(ctr.x(), lens.bottom() + 26), c);
}

void PickerOverlay::drawBadge(QPainter &p, QPointF center, const QColor &c)
{
    if (!s_badgeOn)
        return;
    const QString text = formatColor(c);
    QFont f = font();
    f.setPointSizeF(10);
    f.setBold(true);
    p.setFont(f);
    const int tw = p.fontMetrics().horizontalAdvance(text);
    QRectF pill(0, 0, tw + 44, 30);
    pill.moveCenter(center);

    // Frosted glass: KWin blurs what's behind the pill, we tint it.
    QPainterPath pillPath;
    pillPath.addRoundedRect(pill, 15, 15);
    m_blur += pill.toAlignedRect();
    p.fillPath(pillPath, QColor(255, 255, 255, 70));
    p.fillPath(pillPath, QColor(20, 20, 25, 110));
    p.setPen(QPen(QColor(255, 255, 255, 120), 1));
    p.drawPath(pillPath);

    // Swatch + text.
    p.setPen(QPen(QColor(255, 255, 255, 200), 1));
    p.setBrush(c);
    p.drawEllipse(QPointF(pill.left() + 16, pill.center().y()), 7, 7);
    p.setPen(Qt::white);
    p.drawText(pill.adjusted(30, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
}

void PickerOverlay::mouseMoveEvent(QMouseEvent *e)
{
    m_cursor = e->position();
    m_keyMoved = false;
    requestSample();
    update();
}

void PickerOverlay::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_cursor = e->position();
        pick();
    } else if (e->button() == Qt::RightButton) {
        QCoreApplication::exit(1);
    }
}

void PickerOverlay::wheelEvent(QWheelEvent *e)
{
    // Wheel up = zoom in = fewer source pixels.
    s_span += e->angleDelta().y() > 0 ? -2 : 2;
    s_span = qBound(7, s_span, 31);
    requestSample();
    refreshAll();
}

void PickerOverlay::keyPressEvent(QKeyEvent *e)
{
    QPoint step;
    switch (e->key()) {
    case Qt::Key_Escape: QCoreApplication::exit(1); return;
    case Qt::Key_Alt: s_lensOn = !s_lensOn; requestSample(); refreshAll(); return;
    case Qt::Key_B: s_badgeOn = !s_badgeOn; refreshAll(); return;
    case Qt::Key_F:
        s_format = (s_format + 1) % formatNames().size();
        refreshAll();
        return;
    case Qt::Key_Tab:
        s_place = Placement((int(s_place) + 1) % int(Placement::Count));
        refreshAll();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter: pick(); return;
    case Qt::Key_Left: step = {-1, 0}; break;
    case Qt::Key_Right: step = {1, 0}; break;
    case Qt::Key_Up: step = {0, -1}; break;
    case Qt::Key_Down: step = {0, 1}; break;
    default: QWidget::keyPressEvent(e); return;
    }
    // Shift moves 10 px at a time.
    if (e->modifiers() & Qt::ShiftModifier)
        step *= 10;
    m_cursor += step;
    m_cursor.setX(qBound<qreal>(0, m_cursor.x(), width() - 1));
    m_cursor.setY(qBound<qreal>(0, m_cursor.y(), height() - 1));
    // Warp the real pointer where allowed (X11). Wayland ignores this, so the overlay draws its
    // own crosshair at the pick point until the mouse moves again.
    m_keyMoved = true;
    QCursor::setPos(mapToGlobal(m_cursor.toPoint()));
    requestSample();
    update();
}

void PickerOverlay::drawKeyCursor(QPainter &p)
{
    p.setRenderHint(QPainter::Antialiasing, false);
    const QPointF c(qFloor(m_cursor.x()) + 0.5, qFloor(m_cursor.y()) + 0.5);
    for (const auto &[pen, w] : {std::pair{QColor(0, 0, 0, 200), 3.0}, std::pair{QColor(Qt::white), 1.0}}) {
        p.setPen(QPen(pen, w));
        p.drawLine(c + QPointF(-10, 0), c + QPointF(-3, 0));
        p.drawLine(c + QPointF(3, 0), c + QPointF(10, 0));
        p.drawLine(c + QPointF(0, -10), c + QPointF(0, -3));
        p.drawLine(c + QPointF(0, 3), c + QPointF(0, 10));
    }
}

// Print the color (stdout is what the widget reads) and copy it.
void PickerOverlay::deliver(const QColor &picked)
{
    const QString text = formatColor(picked);
    std::printf("%s\n", qPrintable(s_stdoutHex ? picked.name(QColor::HexRgb).toUpper() : text));
    std::fflush(stdout);
    // Wayland clipboard content dies with the app, so hand it to wl-copy which forks and stays.
    if (s_copy && !QProcess::startDetached(QStringLiteral("wl-copy"), {"--", text}))
        QGuiApplication::clipboard()->setText(text);
}

void PickerOverlay::pick()
{
    // Grab once more so the pick is exactly what's on screen now, not a frame behind.
    if (!m_frozen && s_captureArea) {
        const QRect area = sampleArea();
        const QImage img = s_captureArea(area);
        if (!img.isNull()) {
            m_patch = img;
            m_patchRect = QRectF(mapFromGlobal(area.topLeft()), QSizeF(area.size()));
        }
    }
    if (m_patch.isNull())
        return;
    const QColor picked = colorAt(toImage(m_cursor));
    stopSampler();
    for (QWidget *w : QApplication::topLevelWidgets())
        w->hide();
    deliver(picked);
    QTimer::singleShot(100, qApp, [] { QCoreApplication::quit(); });
}
