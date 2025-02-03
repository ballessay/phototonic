/*
 *  Copyright (C) 2013-2018 Ofer Kashayov <oferkv@live.com>
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

#ifndef COLORS_DIALOG_H
#define COLORS_DIALOG_H

class ImageViewer;
class QCheckBox;
class QSlider;

#include <QDialog>

class ColorsDialog : public QDialog {
    Q_OBJECT

public:
    ColorsDialog(QWidget *parent, ImageViewer *imageViewer);

public slots:
    void reset();

private slots:
    void applyColors();
    void ok();

private:
    ImageViewer *imageViewer;
    QSlider *hueSlider;
    QCheckBox *colorizeCheckBox;
    QSlider *saturationSlider;
    QSlider *lightnessSlider;
    QCheckBox *redCheckBox;
    QCheckBox *greenCheckBox;
    QCheckBox *blueCheckBox;
    QSlider *brightSlider;
    QSlider *contrastSlider;
    QSlider *redSlider;
    QSlider *greenSlider;
    QSlider *blueSlider;
    QCheckBox *rNegateCheckBox;
    QCheckBox *gNegateCheckBox;
    QCheckBox *bNegateCheckBox;
    bool dontApply;
};

#endif // COLORS_DIALOG_H