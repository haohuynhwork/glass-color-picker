#include "PickerOverlay.h"

#include <QApplication>
#include <QColorDialog>
#include <QDBusArgument>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDir>
#include <QHash>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <LayerShellQt/Window>
#include <memory>
#include <vector>
#include <QStandardPaths>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

// Fast path: ask KWin for a raw per-monitor capture over D-Bus (~40ms, native pixels).
// Read a KWin ScreenShot2 capture: call `method` with `args` + options + the pipe's write end.
// Thread-safe (own D-Bus call, own pipe), so the live sampler can run it off the GUI thread.
static QImage captureKWin(const QString &method, QVariantList args)
{
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0)
        return {};
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/ScreenShot2"),
                                                      QStringLiteral("org.kde.KWin.ScreenShot2"), method);
    args << QVariantMap{{"native-resolution", true}} << QVariant::fromValue(QDBusUnixFileDescriptor(fds[1]));
    msg.setArguments(args);
    const QDBusReply<QVariantMap> reply = QDBusConnection::sessionBus().call(msg);
    // msg and args hold dup()s of the write end; drop them or read() never sees EOF.
    msg = {};
    args.clear();
    close(fds[1]);
    if (!reply.isValid()) {
        close(fds[0]);
        std::fprintf(stderr, "KWin %s failed: %s\n", qPrintable(method), qPrintable(reply.error().message()));
        return {};
    }
    const QVariantMap meta = reply.value();
    const int w = meta.value("width").toInt(), h = meta.value("height").toInt();
    const int stride = meta.value("stride").toInt();
    const auto format = QImage::Format(meta.value("format").toInt());

    QByteArray buf;
    buf.reserve(qsizetype(stride) * h);
    char chunk[1 << 16];
    ssize_t n;
    while ((n = read(fds[0], chunk, sizeof chunk)) > 0)
        buf.append(chunk, n);
    close(fds[0]);
    if (w <= 0 || h <= 0 || buf.size() < qsizetype(stride) * h)
        return {};
    // QImage doesn't own buf, so copy while converting.
    return QImage(reinterpret_cast<const uchar *>(buf.constData()), w, h, stride, format)
        .convertToFormat(QImage::Format_RGB32);
}

// Fallback: spectacle grabs the whole desktop at a uniform scale; slice it per screen.
static QHash<QString, QImage> captureSpectacle()
{
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                             .filePath(QStringLiteral("glass-color-picker-%1.bmp").arg(QCoreApplication::applicationPid()));
    QProcess p;
    p.start(QStringLiteral("spectacle"), {"-b", "-n", "-f", "-o", path});
    if (!p.waitForFinished(10000) || p.exitCode() != 0) {
        std::fprintf(stderr, "spectacle failed: %s\n", qPrintable(p.readAllStandardError()));
        return {};
    }
    const QImage desk = QImage(path).convertToFormat(QImage::Format_RGB32);
    QFile::remove(path);
    if (desk.isNull())
        return {};

    QRect virt;
    for (QScreen *s : QGuiApplication::screens())
        virt = virt.united(s->geometry());
    const qreal fx = qreal(desk.width()) / virt.width(), fy = qreal(desk.height()) / virt.height();
    QHash<QString, QImage> shots;
    for (QScreen *s : QGuiApplication::screens()) {
        const QRect g = s->geometry().translated(-virt.topLeft());
        shots[s->name()] = desk.copy(qRound(g.x() * fx), qRound(g.y() * fy),
                                     qRound(g.width() * fx), qRound(g.height() * fy));
    }
    return shots;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("color-picker"));

    QCommandLineParser cli;
    cli.addHelpOption();
    const QCommandLineOption fmt({"f", "format"}, QStringLiteral("Color format: %1.").arg(PickerOverlay::formatNames().join(", ")), "format",
                                 qEnvironmentVariable("COLOR_PICKER_FORMAT", "hex"));
    cli.addOption(fmt);
    const QCommandLineOption hexOut("stdout-hex", "Always print #RRGGBB to stdout (for scripts/the widget).");
    cli.addOption(hexOut);
    const QCommandLineOption noCopy("no-copy", "Print the color but don't copy it to the clipboard.");
    cli.addOption(noCopy);
    const QCommandLineOption lens("magnifier", "Start with the magnifier on or off (Alt toggles).", "on|off", "on");
    cli.addOption(lens);
    const QCommandLineOption badge("badge", "Start with the color badge on or off (B toggles).", "on|off", "on");
    cli.addOption(badge);
    const QCommandLineOption zoom("zoom", "Source pixels across the magnifier, odd 7-31 (wheel changes it).", "pixels", "15");
    cli.addOption(zoom);
    const QCommandLineOption state("print-state", "On exit, print magnifier=on|off, zoom=N and badge=on|off to stdout (lets the widget remember them).");
    cli.addOption(state);
    const QCommandLineOption mode("mode", "Picker: custom (magnifier overlay), kwin (KDE's built-in picker) or dialog (Qt color dialog).", "mode", "custom");
    cli.addOption(mode);
    const QCommandLineOption sampling("sampling", "Custom picker: freeze (one screenshot at start) or live (sample the screen as it changes).", "freeze|live", "freeze");
    cli.addOption(sampling);
    cli.process(app);
    if (!PickerOverlay::setFormat(cli.value(fmt))) {
        std::fprintf(stderr, "unknown format: %s\n", qPrintable(cli.value(fmt)));
        return 2;
    }
    PickerOverlay::s_stdoutHex = cli.isSet(hexOut);
    PickerOverlay::s_copy = !cli.isSet(noCopy);
    PickerOverlay::s_lensOn = cli.value(lens) != QLatin1String("off");
    PickerOverlay::s_badgeOn = cli.value(badge) != QLatin1String("off");
    PickerOverlay::s_span = qBound(7, cli.value(zoom).toInt() | 1, 31);

    const auto printState = [&] {
        if (cli.isSet(state)) {
            std::printf("magnifier=%s\nzoom=%d\nbadge=%s\n", PickerOverlay::s_lensOn ? "on" : "off", PickerOverlay::s_span,
                        PickerOverlay::s_badgeOn ? "on" : "off");
            std::fflush(stdout);
        }
    };
    if (cli.value(mode) == QLatin1String("kwin")) {
        // Same as the stock Plasma picker: KWin shows a crosshair and returns the clicked color.
        const QDBusMessage reply = QDBusConnection::sessionBus().call(
            QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/ColorPicker"),
                                           QStringLiteral("org.kde.kwin.ColorPicker"), QStringLiteral("pick")),
            QDBus::Block, 24 * 3600 * 1000);
        printState();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            if (reply.type() == QDBusMessage::ErrorMessage)
                std::fprintf(stderr, "KWin pick: %s\n", qPrintable(reply.errorMessage()));
            return 1; // cancelled
        }
        uint argb = 0;
        const QDBusArgument arg = reply.arguments().first().value<QDBusArgument>();
        arg.beginStructure();
        arg >> argb;
        arg.endStructure();
        PickerOverlay::deliver(QColor::fromRgb(argb));
        return 0;
    }
    if (cli.value(mode) == QLatin1String("dialog")) {
        // Qt's dialog; its "Pick Screen Color" goes through the desktop portal on Wayland.
        const QColor c = QColorDialog::getColor(Qt::white, nullptr, QStringLiteral("Pick a Color"));
        printState();
        if (!c.isValid())
            return 1;
        PickerOverlay::deliver(c);
        return 0;
    }

    std::vector<std::unique_ptr<PickerOverlay>> overlays;
    for (QScreen *s : QGuiApplication::screens()) {
        auto o = std::make_unique<PickerOverlay>();
        o->winId(); // create the native window so the screen request sticks
        QWindow *w = o->windowHandle();
        w->setScreen(s);
        // Layer-shell overlay (like spectacle): no KWin open/close animation, covers panels.
        if (auto *ls = LayerShellQt::Window::get(w)) {
            ls->setLayer(LayerShellQt::Window::LayerOverlay);
            ls->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
                                                         | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
            ls->setExclusiveZone(-1);
            ls->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
            // KWin derives the window type from the scope; unknown names become "normal" and get
            // the Scale open animation, "dock" does not.
            ls->setScope(QStringLiteral("dock"));
            ls->setScreenConfiguration(LayerShellQt::Window::ScreenFromQWindow);
        }
        o->setGeometry(s->geometry());
        o->show();
        overlays.push_back(std::move(o));
    }
    // Live: the sampler grabs just the area around the cursor. If KWin won't do that, freeze a
    // whole-desktop screenshot instead (the overlay is still clear, so nothing of ours is in it).
    PickerOverlay::s_captureArea = [](const QRect &r) {
        return captureKWin(QStringLiteral("CaptureArea"), {r.x(), r.y(), uint(r.width()), uint(r.height())});
    };
    const auto freeze = [&] {
        QHash<QString, QImage> shots;
        for (QScreen *s : QGuiApplication::screens()) {
            const QImage img = captureKWin(QStringLiteral("CaptureScreen"), {s->name()});
            if (img.isNull()) {
                shots = captureSpectacle();
                break;
            }
            shots[s->name()] = img;
        }
        if (shots.isEmpty()) {
            std::fprintf(stderr, "could not capture the screen\n");
            app.exit(1);
            return;
        }
        for (auto &o : overlays)
            o->setFrozen(shots.value(o->windowHandle()->screen()->name()));
    };
    PickerOverlay::s_liveFailed = freeze;
    if (cli.value(sampling) == QLatin1String("freeze"))
        freeze(); // overlays are still clear, so nothing of ours is in the shot
    const int rc = app.exec();
    PickerOverlay::stopSampler();
    printState();
    return rc;
}
