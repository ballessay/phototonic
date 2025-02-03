/*
 *  Copyright (C) 2013-2014 Ofer Kashayov - oferkv@live.com
 *  This file is part of Phototonic Image Viewer.
 *
 *  Phototonic is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Phototonic is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Phototonic.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QLoggingCategory>
#include <QMenu>
#include <QMouseEvent>
#include <QMovie>
#include <QPainter>
#include <QScrollBar>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>

#include "CropDialog.h"
#include "CropRubberband.h"
#include "ImageWidget.h"
#include "ImageViewer.h"
#include "MessageBox.h"
#include "MetadataCache.h"
#include "Settings.h"


#define CLIPBOARD_IMAGE_NAME "clipboard.png"
#define ROUND(x) ((int) ((x) + 0.5))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

namespace { // anonymous, not visible outside of this file
Q_DECLARE_LOGGING_CATEGORY(PHOTOTONIC_EXIV2_LOG)
Q_LOGGING_CATEGORY(PHOTOTONIC_EXIV2_LOG, "phototonic.exif", QtCriticalMsg)

struct Exiv2LogHandler {
    static void handleMessage(int level, const char *message) {
        switch(level) {
            case Exiv2::LogMsg::debug:
                qCDebug(PHOTOTONIC_EXIV2_LOG) << message;
                break;
            case Exiv2::LogMsg::info:
                qCInfo(PHOTOTONIC_EXIV2_LOG) << message;
                break;
            case Exiv2::LogMsg::warn:
            case Exiv2::LogMsg::error:
            case Exiv2::LogMsg::mute:
                qCWarning(PHOTOTONIC_EXIV2_LOG) << message;
                break;
            default:
                qCWarning(PHOTOTONIC_EXIV2_LOG) << "unhandled log level" << level << message;
                break;
        }
    }

    Exiv2LogHandler() {
        Exiv2::LogMsg::setHandler(&Exiv2LogHandler::handleMessage);
    }
};

class ClickToClose : public QObject {
    public:
        ClickToClose() : QObject() {}
    protected:
        bool eventFilter(QObject *o, QEvent *e) {
            if (e->type() == QEvent::MouseButtonRelease)
                static_cast<QWidget*>(o)->hide();
            return false;
        }
};

} // anonymous namespace

ClickToClose *gs_clickToClose = nullptr;


ImageViewer::ImageViewer(QWidget *parent) : QScrollArea(parent) {
    // This is a threadsafe way to ensure that we only register it once
    static Exiv2LogHandler handler;

    if (!gs_clickToClose)
        gs_clickToClose = new ClickToClose;

    m_letterbox = QRect(QPoint(0,0), QPoint(100,100));
    myContextMenu = nullptr;
    cursorIsHidden = false;
    moveImageLocked = false;
    myMirrorLayout = MirrorNone;
    imageWidget = new ImageWidget;
    imageWidget->setLetterbox(m_letterbox);
    m_crossfade = false;
    imageWidget->setCrossfade(m_crossfade);
    animation = nullptr;

    setContentsMargins(0, 0, 0, 0);
    setAlignment(Qt::AlignCenter);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameStyle(0);
    setWidget(imageWidget);
    setWidgetResizable(false);
    setBackgroundColor();

    myFilenameLabel = new QLabel(this);
    myFilenameLabel->setVisible(Settings::showImageName);
    myFilenameLabel->setMargin(3);
    myFilenameLabel->move(10, 10);
    myFilenameLabel->setAutoFillBackground(true);
    myFilenameLabel->setFrameStyle(QFrame::Plain|QFrame::NoFrame);
    QPalette pal = myFilenameLabel->palette();
    pal.setColor(myFilenameLabel->backgroundRole(), QColor(0,0,0,128));
    pal.setColor(myFilenameLabel->foregroundRole(), QColor(255,255,255,128));
    myFilenameLabel->setPalette(pal);

    feedbackLabel = new QLabel(this);
    feedbackLabel->setVisible(false);
    feedbackLabel->setMargin(3);
    feedbackLabel->setFrameStyle(QFrame::Plain|QFrame::NoFrame);
    feedbackLabel->setAutoFillBackground(true);
    feedbackLabel->setPalette(pal);
    feedbackLabel->installEventFilter(gs_clickToClose);

    mouseMovementTimer = new QTimer(this);
    connect(mouseMovementTimer, SIGNAL(timeout()), this, SLOT(monitorCursorState()));

    Settings::hueVal = 0;
    Settings::saturationVal = 100;
    Settings::lightnessVal = 100;
    Settings::hueRedChannel = true;
    Settings::hueGreenChannel = true;
    Settings::hueBlueChannel = true;

    Settings::contrastVal = 78;
    Settings::brightVal = 100;

    Settings::dialogLastX = Settings::dialogLastY = 0;

    Settings::mouseRotateEnabled = false;

    newImage = false;
    cropRubberBand = 0;
}

static unsigned int getHeightByWidth(int imgWidth, int imgHeight, int newWidth) {
    return qRound(imgHeight * newWidth / double(imgWidth));
}

static unsigned int getWidthByHeight(int imgHeight, int imgWidth, int newHeight) {
    return qRound(imgWidth * newHeight / double(imgHeight));
}

static inline int calcZoom(int size) {
    return qRound(size * Settings::imageZoomFactor);
}

void ImageViewer::resizeImage(QPoint focus) {
    static bool busy = false;
    if (busy)
        return;

    QSize imageSize = animation ? animation->currentPixmap().size() : imageWidget->image().size();
    if (imageSize.isEmpty())
        return;

    busy = true;

    QSize originalImageSize = imageSize;
    imageSize *= Settings::imageZoomFactor;
    if (tempDisableResize) { // calcZoom to identity
        Settings::imageZoomFactor = 1.0;
    }
    switch (Settings::zoomInFlags) {
        case Disable:
            if (imageSize.width() <= width() && imageSize.height() <= height()) {
                imageSize.scale(calcZoom(imageSize.width()), calcZoom(imageSize.height()), Qt::KeepAspectRatio);
            }
            break;

        case WidthAndHeight:
            if (imageSize.width() <= width() && imageSize.height() <= height()) {
                imageSize.scale(calcZoom(width()), calcZoom(height()), Qt::KeepAspectRatio);
            }
            break;

        case Width:
            if (imageSize.width() <= width()) {
                imageSize.scale(calcZoom(width()), calcZoom(getHeightByWidth(imageSize.width(), imageSize.height(), width())), Qt::KeepAspectRatio);
            }
            break;

        case Height:
            if (imageSize.height() <= height()) {
                imageSize.scale(calcZoom(getWidthByHeight(imageSize.height(), imageSize.width(), height())), calcZoom(height()), Qt::KeepAspectRatio);
            }
            break;

        case Disprop:
            imageSize.scale(calcZoom(qMax(imageSize.width(), width())), calcZoom(qMax(imageSize.height(), height())), Qt::IgnoreAspectRatio);
            break;
    }

    switch (Settings::zoomOutFlags) {
        case Disable:
            if (imageSize.width() >= width() || imageSize.height() >= height()) {
                imageSize.scale(calcZoom(imageSize.width()), calcZoom(imageSize.height()), Qt::KeepAspectRatio);
            }
            break;

        case WidthAndHeight:
            if (imageSize.width() >= width() || imageSize.height() >= height()) {
                imageSize.scale(calcZoom(width()), calcZoom(height()), Qt::KeepAspectRatio);
            }
            break;

        case Width:
            if (imageSize.width() >= width()) {
                imageSize.scale(calcZoom(width()), calcZoom(getHeightByWidth(imageSize.width(), imageSize.height(), width())), Qt::KeepAspectRatio);
            }
            break;

        case Height:
            if (imageSize.height() >= height()) {
                imageSize.scale(calcZoom(getWidthByHeight(imageSize.height(), imageSize.width(), height())), calcZoom(height()), Qt::KeepAspectRatio);
            }
            break;

        case Disprop:
            imageSize.scale(calcZoom(qMin(imageSize.width(), width())), calcZoom(qMin(imageSize.height(), height())), Qt::IgnoreAspectRatio);
            break;
    }

    if (tempDisableResize) {
        Settings::imageZoomFactor = (float(originalImageSize.width())/imageSize.width() +
                                     float(originalImageSize.height())/imageSize.height())/2.0;
        imageSize = originalImageSize;
        imageSize.scale(imageSize.width(), imageSize.height(), Qt::KeepAspectRatio);
    }

    if (imageWidget) {
        Qt::Orientations orient;
        if (Settings::flipH) orient |= Qt::Horizontal;
        if (Settings::flipV) orient |= Qt::Vertical;
        imageWidget->setFlip(orient);
        imageWidget->setRotation(Settings::rotation);
        imageWidget->setFixedSize(size());
        if (imageSize.width() < width() || imageSize.height() < height()) {
            centerImage(imageSize);
        } else {
            const double fx = double(imageSize.width())/imageWidget->imageSize().width(),
                         fy = double(imageSize.height())/imageWidget->imageSize().height();
            int x,y;
            if (focus.x() > -1 && focus.y() > -1) {
                x = qRound(fx*(imageWidget->imagePosition().x() - focus.x()) + focus.x());
                y = qRound(fy*(imageWidget->imagePosition().y() - focus.y()) + focus.y());
            } else {
                x = qRound(imageWidget->imagePosition().x()*fx);
                y = qRound(imageWidget->imagePosition().y()*fy);
            }

            if (imageSize.width() >= width())
                x = qMax(qMin(x, 0),  width() - imageSize.width());
            if (imageSize.height() >= height())
                y = qMax(qMin(y, 0), height() - imageSize.height());
            imageWidget->setImagePosition(QPoint(x,y));
        }
        imageWidget->setImageSize(imageSize);
    } else {
        widget()->setFixedSize(imageSize);
//        widget()->adjustSize();
        if (imageSize.width() < width() + 100 || imageSize.height() < height() + 100) {
            centerImage(imageSize);
        } else {
            float positionY = verticalScrollBar()->value() > 0 ? verticalScrollBar()->value() / float(verticalScrollBar()->maximum()) : 0;
            float positionX = horizontalScrollBar()->value() > 0 ? horizontalScrollBar()->value() / float(horizontalScrollBar()->maximum()) : 0;
            horizontalScrollBar()->setValue(horizontalScrollBar()->maximum() * positionX);
            verticalScrollBar()->setValue(verticalScrollBar()->maximum() * positionY);
        }
    }
    busy = false;
}

void ImageViewer::scaleImage(QSize newSize) {
    origImage = origImage.scaled(newSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    refresh();
    setFeedback(tr("New image size: %1x%2").arg(origImage.width()).arg(origImage.height()));
    emit imageEdited(true);
}

void ImageViewer::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    const bool mapCrop = imageWidget && cropRubberBand && cropRubberBand->isVisibleTo(this);
    QRect isoCropRect;
    if (mapCrop) {
        QTransform matrix = imageWidget->transformation().inverted();
        isoCropRect = matrix.mapRect(cropRubberBand->geometry());
    }
    resizeImage();
    if (mapCrop) {
        QTransform matrix = imageWidget->transformation();
        cropRubberBand->setGeometry(matrix.mapRect(isoCropRect));
    }
}

void ImageViewer::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    resizeImage();
}

void ImageViewer::centerImage(QSize imgSize) {
    if (imageWidget) {
        imageWidget->setImagePosition(QPoint((imageWidget->width() - imgSize.width())/2, (imageWidget->height() - imgSize.height())/2));
    } else {
        ensureVisible(imgSize.width()/2, imgSize.height()/2, width()/2, height()/2);
    }
}


void ImageViewer::mirror() {
    switch (myMirrorLayout) {
        case MirrorDual: {
            mirrorImage = QImage(viewerImage.width() * 2, viewerImage.height(),
                                 QImage::Format_ARGB32);
            QPainter painter(&mirrorImage);
            painter.drawImage(0, 0, viewerImage);
            painter.drawImage(viewerImage.width(), 0, viewerImage.mirrored(true, false));
            break;
        }

        case MirrorTriple: {
            mirrorImage = QImage(viewerImage.width() * 3, viewerImage.height(),
                                 QImage::Format_ARGB32);
            QPainter painter(&mirrorImage);
            painter.drawImage(0, 0, viewerImage);
            painter.drawImage(viewerImage.width(), 0, viewerImage.mirrored(true, false));
            painter.drawImage(viewerImage.width() * 2, 0, viewerImage.mirrored(false, false));
            break;
        }

        case MirrorQuad: {
            mirrorImage = QImage(viewerImage.width() * 2, viewerImage.height() * 2,
                                 QImage::Format_ARGB32);
            QPainter painter(&mirrorImage);
            painter.drawImage(0, 0, viewerImage);
            painter.drawImage(viewerImage.width(), 0, viewerImage.mirrored(true, false));
            painter.drawImage(0, viewerImage.height(), viewerImage.mirrored(false, true));
            painter.drawImage(viewerImage.width(), viewerImage.height(),
                              viewerImage.mirrored(true, true));
            break;
        }

        case MirrorVDual: {
            mirrorImage = QImage(viewerImage.width(), viewerImage.height() * 2,
                                 QImage::Format_ARGB32);
            QPainter painter(&mirrorImage);
            painter.drawImage(0, 0, viewerImage);
            painter.drawImage(0, viewerImage.height(), viewerImage.mirrored(false, true));
            break;
        }
        default: break;
    }

    viewerImage = mirrorImage;
    static int nag_counter = 0;
    if (myMirrorLayout != MirrorNone && ++nag_counter < 9)
        setFeedback("<h1>Hello there.</h1><h4>Yes you behind the monitor.</h4>"
                    "<h2>I'm talking to you!</h2>"
                    "<p>Since you're using this you probably have an idea<br>"
                    "what this is supposed to be good for?</p>"
                    "<p>Do you mind filing a bug that explains the feature?<br>"
                    "Otherwise it might easily hit the curb for the final release.</p>"
                    "<p>Thanks<br>"
                    "You can hide this message by clicking on it.</p>", false);
    else
        setFeedback("", false);
}

void ImageViewer::setMirror(MirrorLayout layout) {
    myMirrorLayout = layout;
    refresh();
    switch (myMirrorLayout) {
        case MirrorNone: setFeedback(tr("Mirror Disabled")); break;
        case MirrorDual: setFeedback(tr("Mirror: Dual")); break;
        case MirrorTriple: setFeedback(tr("Mirror: Triple")); break;
        case MirrorQuad: setFeedback(tr("Mirror: Quad")); break;
        case MirrorVDual: setFeedback(tr("Mirror: Dual Vertical")); break;
        default: qDebug() << "invalid mirror layout" << layout;
    }
}

static inline int bound0To255(int val) {
    return ((val > 255) ? 255 : (val < 0) ? 0 : val);
}

static inline int hslValue(double n1, double n2, double hue) {
    double value;

    if (hue > 255) {
        hue -= 255;
    } else if (hue < 0) {
        hue += 255;
    }

    if (hue < 42.5) {
        value = n1 + (n2 - n1) * (hue / 42.5);
    } else if (hue < 127.5) {
        value = n2;
    } else if (hue < 170) {
        value = n1 + (n2 - n1) * ((170 - hue) / 42.5);
    } else {
        value = n1;
    }

    return ROUND(value * 255.0);
}

void rgbToHsl(int r, int g, int b, unsigned char *hue, unsigned char *sat, unsigned char *light) {
    double h, s, l;
    int min, max;
    int delta;

    if (r > g) {
        max = MAX(r, b);
        min = MIN(g, b);
    } else {
        max = MAX(g, b);
        min = MIN(r, b);
    }

    l = (max + min) / 2.0;

    if (max == min) {
        s = 0.0;
        h = 0.0;
    } else {
        delta = (max - min);

        if (l < 128) {
            s = 255 * (double) delta / (double) (max + min);
        } else {
            s = 255 * (double) delta / (double) (511 - max - min);
        }

        if (r == max) {
            h = (g - b) / (double) delta;
        } else if (g == max) {
            h = 2 + (b - r) / (double) delta;
        } else {
            h = 4 + (r - g) / (double) delta;
        }

        h = h * 42.5;
        if (h < 0) {
            h += 255;
        } else if (h > 255) {
            h -= 255;
        }
    }

    *hue = ROUND(h);
    *sat = ROUND(s);
    *light = ROUND(l);
}

void hslToRgb(double h, double s, double l,
              unsigned char *red, unsigned char *green, unsigned char *blue) {
    if (s == 0) {
        /* achromatic case */
        *red = l;
        *green = l;
        *blue = l;
    } else {
        double m1, m2;

        if (l < 128)
            m2 = (l * (255 + s)) / 65025.0;
        else
            m2 = (l + s - (l * s) / 255.0) / 255.0;

        m1 = (l / 127.5) - m2;

        /* chromatic case */
        *red = hslValue(m1, m2, h + 85);
        *green = hslValue(m1, m2, h);
        *blue = hslValue(m1, m2, h - 85);
    }
}

void ImageViewer::colorize() {
    int y, x;
    unsigned char hr, hg, hb;
    int r, g, b;
    QRgb *line;
    unsigned char h, s, l;
    static unsigned char contrastTransform[256];
    static unsigned char brightTransform[256];
    bool hasAlpha = viewerImage.hasAlphaChannel();

    switch(viewerImage.format()) {
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied:
        break;
    default:
        viewerImage = viewerImage.convertToFormat(QImage::Format_RGB32);
    }

    int i;
    float contrast = ((float) Settings::contrastVal / 100.0);
    float brightness = ((float) Settings::brightVal / 100.0);

    for (i = 0; i < 256; ++i) {
        if (i < (int) (128.0f + 128.0f * tan(contrast)) && i > (int) (128.0f - 128.0f * tan(contrast))) {
            contrastTransform[i] = bound0To255((i - 128) / tan(contrast) + 128);
        } else if (i >= (int) (128.0f + 128.0f * tan(contrast))) {
            contrastTransform[i] = 255;
        } else {
            contrastTransform[i] = 0;
        }
    }

    for (i = 0; i < 256; ++i) {
        brightTransform[i] = bound0To255(int((255.0 * pow(i / 255.0, 1.0 / brightness)) + 0.5));
    }

    for (y = 0; y < viewerImage.height(); ++y) {

        line = (QRgb *) viewerImage.scanLine(y);
        for (x = 0; x < viewerImage.width(); ++x) {
            r = qRed(line[x]);
            if (Settings::hueRedChannel) {
                if (Settings::rNegateEnabled)
                    r = 255 - r;
                r = bound0To255((r * (Settings::redVal + 100)) / 100);
                r = brightTransform[r];
                r = contrastTransform[r];
            }

            g = qGreen(line[x]);
            if (Settings::hueGreenChannel) {
                if (Settings::gNegateEnabled)
                    g = 255 - g;
                g = bound0To255((g * (Settings::greenVal + 100)) / 100);
                g = brightTransform[g];
                g = contrastTransform[g];
            }

            b = qBlue(line[x]);
            if (Settings::hueBlueChannel) {
                if (Settings::bNegateEnabled)
                    b = 255 - b;
                b = bound0To255((b * (Settings::blueVal + 100)) / 100);
                b = brightTransform[b];
                b = contrastTransform[b];
            }

            if (Settings::hueVal != 0 || Settings::saturationVal != 100 || Settings::lightnessVal != 100) {
                rgbToHsl(r, g, b, &h, &s, &l);
                h = Settings::colorizeEnabled ? Settings::hueVal : h + Settings::hueVal;
                s = bound0To255(((s * Settings::saturationVal) / 100));
                l = bound0To255(((l * Settings::lightnessVal) / 100));
                hslToRgb(h, s, l, &hr, &hg, &hb);
                if (Settings::hueRedChannel)
                    r = hr;
                if (Settings::hueGreenChannel)
                    g = hg;
                if (Settings::hueBlueChannel)
                    b = hb;
            }

            if (hasAlpha) {
                line[x] = qRgba(r, g, b, qAlpha(line[x]));
            } else {
                line[x] = qRgb(r, g, b);
            }
        }
    }
    emit imageEdited(true);
}

void ImageViewer::refresh() {
    if (!imageWidget) {
        return;
    }

    viewerImage = origImage;

    if (Settings::colorsActive || Settings::keepTransform) {
        colorize();
    }

    if (myMirrorLayout) {
        mirror();
    }

    imageWidget->setImage(viewerImage, m_exifTransformation);
    resizeImage();
}

void ImageViewer::setImage(const QImage &image) {
    if (movieWidget) {
        delete movieWidget;
        movieWidget = nullptr;
        imageWidget = new ImageWidget;
        imageWidget->setLetterbox(m_letterbox);
        imageWidget->setCrossfade(m_crossfade);
        setWidget(imageWidget);
    }

    imageWidget->setImage(image, m_exifTransformation);
}

void ImageViewer::reload() {
    emit imageEdited(false);
    static bool s_busy = false;
    static bool s_abort = false;
    if (s_busy) {
        s_abort = true;
        QMetaObject::invokeMethod(this, "reload", Qt::QueuedConnection);
        return;
    }
//    setFeedback("",false);
    if (Settings::showImageName) {
        if (fullImagePath.left(1) == ":") {
            setInfo("No Image");
        } else if (fullImagePath.isEmpty()) {
            setInfo("Clipboard");
        } else {
            setInfo(QFileInfo(fullImagePath).fileName());
        }
    }

    if (!Settings::keepTransform) {
        Settings::rotation = 0;
        Settings::flipH = Settings::flipV = false;
    }

    if (!batchMode) {
        Settings::mouseRotateEnabled = false;
        emit toolsUpdated();

        if (newImage || fullImagePath.isEmpty()) {

            newImage = true;
            fullImagePath = CLIPBOARD_IMAGE_NAME;
            origImage.load(":/images/no_image.png");
            viewerImage = origImage;
            setImage(viewerImage);
            pasteImage();
            return;
        }
    }

    QImageReader imageReader(fullImagePath);
    if (batchMode && imageReader.supportsAnimation()) {
        //: this is a warning on the console
        qWarning() << tr("skipping animation in batch mode:") << fullImagePath;
        return;
    }
    if (Settings::enableAnimations && imageReader.supportsAnimation()) {
        if (animation) {
            delete animation;
            animation = nullptr;
        }
        animation = new QMovie(fullImagePath);

        if (animation->frameCount() > 1) {
            if (!movieWidget) {
                movieWidget = new QLabel();
                movieWidget->setScaledContents(true);
                setWidget(movieWidget); // deletes imageWidget
                imageWidget = nullptr;
            }
            movieWidget->setMovie(animation);
            animation->setParent(movieWidget);
            animation->start();
            resizeImage();
            return;
        }
    }

    // It's not a movie
    if (fullImagePath == m_preloadedPath) {
        viewerImage = origImage = m_preloadedImage;
        m_preloadedImage = QImage();
        m_preloadedPath.clear();
    } else if (imageReader.size().isValid()) {
        QSize sz = imageReader.size();
        if (sz.width() * sz.height() > 8192*8192) { // allocation limit
            /// @todo this and the correct sqrt(double…) of the below still runs into the size limts?
            // perhaps because qimagereader overreads and needs some extra memory
            // sz.scale(8192,8192, Qt::KeepAspectRatio);
            double factor = double(8192*8192)/(sz.width() * sz.height());
            // the sqrt is correct, but too large - the square of the true factor will over"punish" large images
            // so we draw an average - this is by "shows the most annoying supersize image I have"
            factor = (2*factor + sqrt(factor))/3.0;
            sz *= factor;
            imageReader.setScaledSize(sz);
            setFeedback(tr( "<h1>Warning</h1>Original image size %1x%2 exceeds limits<br>"
                            "Downscaled to %3x%4<br><h3>Saving edits will save the smaller image!</h3>")
                            .arg(imageReader.size().width()).arg(imageReader.size().height())
                            .arg(sz.width()).arg(sz.height()), 10000);
        }
        bool imageOk = false;
        if (batchMode || Settings::slideShowActive) {
            imageReader.read(&origImage);
        } else {
            QThread *thread = QThread::create([&](){imageOk = imageReader.read(&origImage);});
            thread->start();
            s_busy = true;
            while (!thread->wait(30)) {
                QApplication::processEvents();
                if (s_abort) {
                    thread->terminate();
                    thread->wait();
                    break;
                }
            }
            s_busy = false;
            thread->deleteLater();
            if (s_abort) {
                s_abort = false;
                return;
            }
        }

        if (imageOk) {
            if (Settings::exifRotationEnabled) {
                m_exifTransformation = Metadata::transformation(fullImagePath);
                origImage = origImage.transformed(Metadata::transformation(fullImagePath), Qt::SmoothTransformation);
            }
            viewerImage = origImage;

            if (Settings::colorsActive || Settings::keepTransform) {
                colorize();
            }
            if (myMirrorLayout) {
                mirror();
            }
        } else {
            viewerImage = QIcon::fromTheme("image-missing",
                                        QIcon(":/images/error_image.png")).pixmap(BAD_IMAGE_SIZE, BAD_IMAGE_SIZE).toImage();
            setInfo(QFileInfo(imageReader.fileName()).fileName() + ": " + imageReader.errorString());
        }
    }

    setImage(viewerImage);
    resizeImage();
    centerImage(imageWidget->imageSize());
    if (Settings::keepTransform) {
        Qt::Orientations orient;
        if (Settings::flipH) orient |= Qt::Horizontal;
        if (Settings::flipV) orient |= Qt::Vertical;
        imageWidget->setFlip(orient);
        imageWidget->setRotation(Settings::rotation);
    }
    if (Settings::setWindowIcon) {
        window()->setWindowIcon(QPixmap::fromImage(viewerImage.scaled(WINDOW_ICON_SIZE, WINDOW_ICON_SIZE,
                                                                      Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
}

void ImageViewer::setInfo(QString infoString) {
    myFilenameLabel->setText(infoString);
    myFilenameLabel->adjustSize();
}

void ImageViewer::unsetFeedback() {
    if (m_permanentFeedback.isEmpty()) {
        feedbackLabel->clear();
        feedbackLabel->setVisible(false);
    } else {
        setFeedback(m_permanentFeedback, false);
    }
}

void ImageViewer::setFeedback(QString feedbackString, int timeLimited) {
    if (!timeLimited)
        m_permanentFeedback = feedbackString;
    if (feedbackString.isEmpty()) {
        unsetFeedback();
        return;
    }
    feedbackLabel->setText(feedbackString);
    feedbackLabel->setVisible(true/* Settings::layoutMode == Phototonic::ImageViewWidget */);

    int margin = myFilenameLabel->isVisible() ? (myFilenameLabel->height() + 15) : 10;
    feedbackLabel->move(10, margin);

    feedbackLabel->adjustSize();
    if (timeLimited) {
        static QTimer *unsetFeedbackTimer = nullptr;
        if (!unsetFeedbackTimer) {
            unsetFeedbackTimer = new QTimer(this);
            unsetFeedbackTimer->setSingleShot(true);
            connect (unsetFeedbackTimer, &QTimer::timeout, this, &ImageViewer::unsetFeedback);
        }
        unsetFeedbackTimer->setInterval(timeLimited);
        unsetFeedbackTimer->start();
    }
}

void ImageViewer::loadImage(QString imageFileName, const QImage &preview) {
    if (fullImagePath == imageFileName)
        return;

    unsetFeedback();
    newImage = false;
    fullImagePath = imageFileName;

    if (!Settings::keepZoomFactor) {
        tempDisableResize = false;
        Settings::imageZoomFactor = 1.0;
    }
    if (!preview.isNull()) {
        QSize fullSize = QImageReader(fullImagePath).size();
        // don't preview small images w/ a huge thumbnail upscale
        // it's pointless and causes ugly flicker
        if (!fullSize.isValid() || (fullSize.width() > 4*width()/5 && fullSize.height() > 4*height()/5)) {
            setImage(preview);
            const int zif = Settings::zoomInFlags;
            const bool disRes = tempDisableResize;
            tempDisableResize = false;
            Settings::zoomInFlags = WidthAndHeight;
            resizeImage();
            Settings::zoomInFlags = zif;
            tempDisableResize = disRes;
        }
    }

    QApplication::processEvents();
    reload();
}

void ImageViewer::preload(QString imageFileName) {
    if (m_preloadedPath == imageFileName)
        return;

    m_preloadedPath = imageFileName;
    // reload current one - maybe the file has changed on disk?
    /*if (m_preloadedPath == fullImagePath) {
        m_preloadedImage = origImage;
        return;
    }*/

    QImageReader imageReader(m_preloadedPath);
    if (imageReader.supportsAnimation()) {
        m_preloadedImage = QImage();
        m_preloadedPath.clear();
        return; // no preloading animations
    }
    if (imageReader.size().isValid() && imageReader.read(&m_preloadedImage)) {
        if (Settings::exifRotationEnabled) {
            m_preloadedImage = m_preloadedImage.transformed(Metadata::transformation(fullImagePath), Qt::SmoothTransformation);
        }
    } else {
        m_preloadedImage = QImage();
        m_preloadedPath.clear();
    }
}

void ImageViewer::clearImage() {
    fullImagePath.clear();
    origImage.load(":/images/no_image.png");
    viewerImage = origImage;
    setImage(viewerImage);
}

void ImageViewer::setContextMenu(QMenu *menu) {
    delete myContextMenu;
    myContextMenu = menu;
    myContextMenu->setParent(this);
}

void ImageViewer::setCrossfade(bool yesno) {
    m_crossfade = yesno;
    if (imageWidget)
        imageWidget->setCrossfade(m_crossfade);
}

void ImageViewer::monitorCursorState() {
    static QPoint lastPos;

    if (QCursor::pos() != lastPos) {
        lastPos = QCursor::pos();
        if (cursorIsHidden) {
            QApplication::restoreOverrideCursor();
            cursorIsHidden = false;
        }
    } else {
        if (!cursorIsHidden) {
            QApplication::setOverrideCursor(Qt::BlankCursor);
            cursorIsHidden = true;
        }
    }
}

void ImageViewer::setCursorHiding(bool hide) {
    if (hide) {
        mouseMovementTimer->start(500);
    } else {
        mouseMovementTimer->stop();
        if (cursorIsHidden) {
            QApplication::restoreOverrideCursor();
            cursorIsHidden = false;
        }
    }
}

void ImageViewer::mouseDoubleClickEvent(QMouseEvent *event) {
    QWidget::mouseDoubleClickEvent(event);
    while (QApplication::overrideCursor()) {
        QApplication::restoreOverrideCursor();
    }
}

void ImageViewer::mousePressEvent(QMouseEvent *event) {
    if (!imageWidget) {
        return;
    }
    if (event->button() == Qt::LeftButton) {
        if (event->modifiers() == Qt::ControlModifier) {
            cropOrigin = event->pos();
            if (!cropRubberBand) {
                cropRubberBand = new CropRubberBand(this);
                connect(cropRubberBand, &CropRubberBand::selectionChanged,
                        this, &ImageViewer::updateRubberBandFeedback);
                connect(cropRubberBand, &CropRubberBand::cropConfirmed,
                        this, &ImageViewer::applyCropAndRotation);
            }
            cropRubberBand->show();
            cropRubberBand->setGeometry(QRect(cropOrigin, event->pos()).normalized());
        } else if (!(Settings::mouseRotateEnabled || event->modifiers() == Qt::ShiftModifier)) {
            if (cropRubberBand && cropRubberBand->isVisible()) {
                cropRubberBand->hide();
                setFeedback("", false);
            }
        }
        QPointF fulcrum(QPointF(imageWidget->width() / 2.0, imageWidget->height() / 2.0));
        QLineF vector(fulcrum, event->position());
        initialRotation = imageWidget->rotation() + vector.angle();
        setMouseMoveData(true, event->position().x(), event->position().y());
        QApplication::setOverrideCursor(Qt::ClosedHandCursor);
        event->accept();
    }
    QWidget::mousePressEvent(event);
}

void ImageViewer::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        setMouseMoveData(false, 0, 0);
        while (QApplication::overrideCursor()) {
            QApplication::restoreOverrideCursor();
        }
    }

    QWidget::mouseReleaseEvent(event);
}

void ImageViewer::updateRubberBandFeedback(QRect geom) {
    if (!imageWidget) {
        return;
    }
    bool ok;
    QTransform matrix = imageWidget->transformation().inverted(&ok);
    if (!ok)
        qDebug() << "something's fucked up about the transformation matrix!";
    m_isoCropRect = geom = matrix.mapRect(geom);
    setFeedback(tr("Selection: ") + QString("%1x%2").arg(geom.width()).arg(geom.height())
                                  + QString(geom.x() < 0 ? "%1" : "+%1").arg(geom.x())
                                  + QString(geom.y() < 0 ? "%1" : "+%1").arg(geom.y()), false);
    static QTimer *doubleclickhint = nullptr;
    if (!doubleclickhint) {
        doubleclickhint = new QTimer(this);
        doubleclickhint->setInterval(2000);
        doubleclickhint->setSingleShot(true);
        connect(doubleclickhint, &QTimer::timeout, [=]() {
                                        if (cropRubberBand && cropRubberBand->isVisible())
                                            setFeedback(tr("Doubleclick to crop, right click to abort"), 10000);
                                        });
    }
    doubleclickhint->start();
}

void ImageViewer::applyCropAndRotation() {
    if (!imageWidget || ! cropRubberBand)
        return;

    cropRubberBand->hide();

    QTransform matrix = imageWidget->transformation();
    bool ok;
    // the inverted mapping of the crop area matches the coordinates of the original image
    m_isoCropRect = matrix.inverted(&ok).mapRect(cropRubberBand->geometry());
    if (!ok) {
        qDebug() << "something's fucked up about the transformation matrix! Not cropping";
        return;
    }

    if (!matrix.isRotating()) {
        // … we can just copy the area, later apply flips and be done
        origImage = origImage.copy(m_isoCropRect);
    } else {
        // The rotated case is more involved. The inverted matrix still maps image coordinates
        // but that's not what the user sees or expects.
        //
        // This is inherently lossy because of the pixel transpositon, so special-case it

        const QSize visualSize = imageWidget->imageSize();
        float scale = qMax(float(visualSize.width()) / viewerImage.width(), float(visualSize.height()) / viewerImage.height());
        if (scale <= 0.0) {
            qDebug() << "something is seriously wrong with the scale, not cropping" << scale;
            return;
        }

        // The new image size must be the size of the visible crop area, compensated for the current scale factor
        QImage target(cropRubberBand->geometry().size()/scale, origImage.format());
        // but still be at the same position
        QRect sourceRect = target.rect();
        sourceRect.moveCenter(m_isoCropRect.center());

        target.fill(Qt::black /* Qt::green */);
        QPainter painter(&target);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        // rotate in relation to the paint device
        QPoint center(target.width() / 2, target.height() / 2);
        painter.translate(center);
        // onedirectional flipping inverts the rotation
        if (Settings::flipH xor Settings::flipV)
            painter.rotate(360.0 - imageWidget->rotation());
        else
            painter.rotate(imageWidget->rotation());
        painter.translate(-center);

        // offset by crop rect
        painter.translate(-sourceRect.topLeft());

        // crop
        painter.drawImage(0,0, origImage);
        painter.end();
        origImage = target;
    }

    // apply flip-flop
    origImage.mirror(Settings::flipH, Settings::flipV);

    // reset transformations for the new image
    if (!batchMode) {
        Settings::flipH = Settings::flipV = false;
        Settings::rotation = 0;
        imageWidget->setRotation(Settings::rotation);
        if (!Settings::keepZoomFactor) {
            tempDisableResize = false;
            Settings::imageZoomFactor = 1.0;
        }
        m_isoCropRect = QRect(); // invalidate
    }
    refresh();
    setFeedback("", false);
    setFeedback(tr("New image size: %1x%2").arg(origImage.width()).arg(origImage.height()));
    emit imageEdited(true);
}

void ImageViewer::configureLetterbox() {
    static CropDialog *dlg = nullptr;
    if (!dlg) {
        dlg = new CropDialog(this);
        connect(dlg, &CropDialog::valuesChanged, [=](int left, int top, int right, int bottom) {
            m_letterbox = QRect(QPoint(left, top), QPoint(100-right, 100-bottom));
            imageWidget->setLetterbox(m_letterbox);
        });
    }
    dlg->exec();
}

QSize ImageViewer::currentImageSize() const {
    return origImage.size();
}

void ImageViewer::setMouseMoveData(bool lockMove, int lMouseX, int lMouseY) {
    if (!imageWidget) {
        return;
    }
    moveImageLocked = lockMove;
    mouseX = lMouseX;
    mouseY = lMouseY;
    layoutX = imageWidget->imagePosition().x();
    layoutY = imageWidget->imagePosition().y();
}

void ImageViewer::mouseMoveEvent(QMouseEvent *event) {
    if (!imageWidget) {
        return;
    }

    if (event->modifiers() == Qt::ControlModifier) {
        if (!cropRubberBand || !cropRubberBand->isVisible()) {
            return;
        }
        QRect newRect;
        newRect = QRect(cropOrigin, event->pos());
/*** @todo this doesn't work at all and also the resize typically happens unconstrained using the qsizegrip
        figure whether to keep this at all
        // Force square
        if (event->modifiers() & Qt::ShiftModifier) {
            const int deltaX = cropOrigin.x() - event->pos().x();
            const int deltaY = cropOrigin.y() - event->pos().y();
            newRect.setSize(QSize(-deltaX, deltaY < 0 ? qAbs(deltaX) : -qAbs(deltaX)));
        }
        **/
        cropRubberBand->setGeometry(newRect.normalized());

    } else if (Settings::mouseRotateEnabled || event->modifiers() == Qt::ShiftModifier) {
        QPointF fulcrum(QPointF(imageWidget->width() / 2.0, imageWidget->height() / 2.0));
        QLineF vector(fulcrum, event->position());
        Settings::rotation = initialRotation - vector.angle();
        if (qAbs(Settings::rotation) > 360.0)
            Settings::rotation -= int(360*Settings::rotation)/360;
        if (Settings::rotation < 0)
            Settings::rotation += 360.0;
        imageWidget->setRotation(Settings::rotation);
        setFeedback(tr("Rotation %1°").arg(Settings::rotation));
        // qDebug() << "image center" << fulcrum << "line" << vector << "angle" << vector.angle() << "geom" << imageWidget->geometry();

    } else if (moveImageLocked) {
        int newX = layoutX + (event->pos().x() - mouseX);
        int newY = layoutY + (event->pos().y() - mouseY);
        bool needToMove = false;

        if (imageWidget->imageSize().width() > size().width()) {
            if (newX > 0) {
                newX = 0;
            } else if (newX < (size().width() - imageWidget->imageSize().width())) {
                newX = (size().width() - imageWidget->imageSize().width());
            }
            needToMove = true;
        } else {
            newX = layoutX;
        }

        if (imageWidget->imageSize().height() > size().height()) {
            if (newY > 0) {
                newY = 0;
            } else if (newY < (size().height() - imageWidget->imageSize().height())) {
                newY = (size().height() - imageWidget->imageSize().height());
            }
            needToMove = true;
        } else {
            newY = layoutY;
        }

        if (needToMove) {
            imageWidget->setImagePosition(QPoint(newX, newY));
        }
    }
}

void ImageViewer::slideImage(QPoint delta) {
    if (!imageWidget) {
        return;
    }

    QPoint newPos = imageWidget->imagePosition() + delta;
    layoutX = newPos.x();
    layoutY = newPos.y();
    bool needToMove = false;

    if (imageWidget->imageSize().width() > size().width()) {
        if (newPos.x() > 0) {
            newPos.setX(0);
        } else if (newPos.x() < (size().width() - imageWidget->imageSize().width())) {
            newPos.setX(size().width() - imageWidget->imageSize().width());
        }
        needToMove = true;
    }

    if (imageWidget->imageSize().height() > size().height()) {
        if (newPos.y() > 0) {
            newPos.setY(0);
        } else if (newPos.y() < (size().height() - imageWidget->imageSize().height())) {
            newPos.setY(size().height() - imageWidget->imageSize().height());
        }
        needToMove = true;
    }

    if (needToMove) {
        imageWidget->setImagePosition(newPos);
    }
}

void ImageViewer::saveImage() {
#if __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#if EXIV2_TEST_VERSION(0,28,0)
    Exiv2::Image::UniquePtr image;
#else
    Exiv2::Image::AutoPtr image;
#endif
#if __clang__
#pragma GCC diagnostic pop
#endif

    bool exifError = false;
    static bool showExifError = true;

    if (newImage) {
        saveImageAs();
        return;
    }

    setFeedback(tr("Saving..."));

    try {
        image = Exiv2::ImageFactory::open(fullImagePath.toStdString());
        image->readMetadata();
    }
    catch (const Exiv2::Error &error) {
        qWarning() << "EXIV2:" << error.what();
        exifError = true;
    }

    QImageReader imageReader(fullImagePath);
    QString savePath = fullImagePath;
    if (!Settings::saveDirectory.isEmpty()) {
        QDir saveDir(Settings::saveDirectory);
        savePath = saveDir.filePath(QFileInfo(fullImagePath).fileName());
    }
    int rotation = qRound(Settings::rotation);
    if (!batchMode && (Settings::flipH || Settings::flipV || !(rotation % 90))) {
        QTransform matrix;
        matrix.scale(Settings::flipH ? -1 : 1, Settings::flipV ? -1 : 1);
        if (!(rotation % 90))
            matrix.rotate((Settings::flipH xor Settings::flipV) ? 360-rotation : rotation);
        viewerImage = viewerImage.transformed(matrix);
    }
    if (!viewerImage.save(savePath, imageReader.format().toUpper(), Settings::defaultSaveQuality)) {
        MessageBox msgBox(this);
        msgBox.critical(tr("Error"), tr("Failed to save image."));
        return;
    }

    if (!exifError) {
        try {
            if (Settings::saveDirectory.isEmpty()) {
                image->writeMetadata();
            } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#if EXIV2_TEST_VERSION(0,28,0)
                Exiv2::Image::UniquePtr imageOut = Exiv2::ImageFactory::open(savePath.toStdString());
#else
                Exiv2::Image::AutoPtr imageOut = Exiv2::ImageFactory::open(savePath.toStdString());
#endif
#pragma clang diagnostic pop

                imageOut->setMetadata(*image);
                Exiv2::ExifThumb thumb(imageOut->exifData());
                thumb.erase();
                // TODO: thumb.setJpegThumbnail(thumbnailPath);
                imageOut->writeMetadata();
            }
        }
        catch (Exiv2::Error &error) {
            if (showExifError) {
                MessageBox msgBox(this);
                QCheckBox cb(tr("Don't show this message again"));
                msgBox.setCheckBox(&cb);
                msgBox.critical(tr("Error"), tr("Failed to save Exif metadata."));
                showExifError = !(cb.isChecked());
            } else {
                //: this is a warning on the console
                qWarning() << tr("Failed to safe Exif metadata:") << error.what();
            }
        }
    }

    reload();
    setFeedback(tr("Image saved."));
}

void ImageViewer::saveImageAs() {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#if EXIV2_TEST_VERSION(0,28,0)
    Exiv2::Image::UniquePtr exifImage;
    Exiv2::Image::UniquePtr newExifImage;
#else
    Exiv2::Image::AutoPtr exifImage;
    Exiv2::Image::AutoPtr newExifImage;
#endif
#pragma clang diagnostic pop

    bool exifError = false;

    setCursorHiding(false);

    QString fileName = QFileDialog::getSaveFileName(this,
                                                    tr("Save image as"),
                                                    fullImagePath,
                                                    tr("Images") +
                                                    " (*.jpg *.jpeg *.png *.bmp *.tif *.tiff *.ppm *.pgm *.pbm *.xbm *.xpm *.cur *.ico *.icns *.wbmp *.webp)");

    if (!fileName.isEmpty()) {
        try {
            exifImage = Exiv2::ImageFactory::open(fullImagePath.toStdString());
            exifImage->readMetadata();
        }
        catch (const Exiv2::Error &error) {
            qWarning() << "EXIV2" << error.what();
            exifError = true;
        }

        int rotation = qRound(Settings::rotation);
        if (!batchMode && (Settings::flipH || Settings::flipV || !(rotation % 90))) {
            QTransform matrix;
            matrix.scale(Settings::flipH ? -1 : 1, Settings::flipV ? -1 : 1);
            if (!(rotation % 90))
                matrix.rotate((Settings::flipH xor Settings::flipV) ? 360-rotation : rotation);
            viewerImage = viewerImage.transformed(matrix);
        }
        if (!viewerImage.save(fileName, 0, Settings::defaultSaveQuality)) {
            MessageBox msgBox(this);
            msgBox.critical(tr("Error"), tr("Failed to save image."));
        } else {
            if (!exifError) {
                try {
                    newExifImage = Exiv2::ImageFactory::open(fileName.toStdString());
                    newExifImage->setMetadata(*exifImage);
                    newExifImage->writeMetadata();
                }
                catch (Exiv2::Error &error) {
                    exifError = true;
                }
            }

            setFeedback(tr("Image saved."));
        }
    }
    if (window()->isFullScreen()) {
        setCursorHiding(true);
    }
}

void ImageViewer::contextMenuEvent(QContextMenuEvent *) {
//    if (Settings::layoutMode != Phototonic::ImageViewWidget)
//        return;

    while (QApplication::overrideCursor()) {
        QApplication::restoreOverrideCursor();
    }
    m_contextSpot = mapFromGlobal(QCursor::pos());
    myContextMenu->exec(QCursor::pos());
}

void ImageViewer::focusInEvent(QFocusEvent *event) {
    QScrollArea::focusInEvent(event);
    emit gotFocus();
}

bool ImageViewer::isNewImage() {
    return newImage;
}

void ImageViewer::copyImage() {
    QApplication::clipboard()->setImage(viewerImage);
}

void ImageViewer::pasteImage() {
    if (!imageWidget) {
        return;
    }

    if (!QApplication::clipboard()->image().isNull()) {
        origImage = QApplication::clipboard()->image();
        refresh();
    }
    window()->setWindowTitle(tr("Clipboard") + " - Phototonic");
    if (Settings::setWindowIcon) {
        window()->setWindowIcon(QApplication::windowIcon());
    }
}

void ImageViewer::setBackgroundColor() {
    QPalette pal = palette();
    pal.setColor(backgroundRole(), QColor(Settings::viewerBackgroundColor.red(), Settings::viewerBackgroundColor.green(), Settings::viewerBackgroundColor.blue()));
    setPalette(pal);
}

QPoint ImageViewer::contextSpot() {
    return m_contextSpot;
}

