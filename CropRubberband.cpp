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

#include <QBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QRubberBand>
#include <QSizeGrip>

#include "CropRubberband.h"

CropRubberBand::CropRubberBand(QWidget *parent) : QWidget(parent) {

    setWindowFlags(Qt::SubWindow);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout *topLayout = new QHBoxLayout();
    topLayout->setContentsMargins(0, 0, 0, 0);
    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->setContentsMargins(0, 0, 0, 0);

    QSizeGrip *grip = new QSizeGrip(this);
    QPalette pal = grip->palette();
    pal.setColor(grip->backgroundRole(), Qt::transparent);
    pal.setColor(grip->foregroundRole(), Qt::transparent);
    grip->setPalette(pal);
    topLayout->addWidget(grip, 0, Qt::AlignTop | Qt::AlignLeft);
    grip = new QSizeGrip(this); grip->setPalette(pal);
    topLayout->addWidget(grip, 1, Qt::AlignTop | Qt::AlignRight);
    grip = new QSizeGrip(this); grip->setPalette(pal);
    bottomLayout->addWidget(grip, 0, Qt::AlignBottom | Qt::AlignLeft);
    grip = new QSizeGrip(this); grip->setPalette(pal);
    bottomLayout->addWidget(grip, 1, Qt::AlignBottom | Qt::AlignRight);

    mainLayout->addLayout(topLayout);
    mainLayout->addLayout(bottomLayout);

    setFocusPolicy(Qt::ClickFocus);

    rubberband = new QRubberBand(QRubberBand::Rectangle, this);
    rubberband->show();
}

void CropRubberBand::showEvent(QShowEvent *) {
    setFocus();
}

void CropRubberBand::keyPressEvent(QKeyEvent *event) {
    QPoint cursorPosGlobal = QCursor::pos();
    QPoint cursorPos = mapFromGlobal(cursorPosGlobal);
    QRect geom = geometry();
//    qDebug() << "cursor" << cursorPos << "geom" << geom;
    switch (event->key()) {
    case Qt::LeftArrow:
    case Qt::Key_H:
        if (cursorPos.x() > -10 && cursorPos.x() < 10) {
            geom.setLeft(geom.left() - 1);
            QCursor::setPos(cursorPosGlobal.x() - 1, cursorPosGlobal.y());
        } else if (cursorPos.x() > width() - 10 && cursorPos.x() < width() + 10) {
            geom.setRight(geom.right() - 1);
            QCursor::setPos(cursorPosGlobal.x() - 1, cursorPosGlobal.y());
        } else if (QRect(geom).translated(-geom.x(), -geom.y()).contains(cursorPos)) {
            geom.moveLeft(geom.x() - 1);
        }
        setGeometry(geom);
        emit selectionChanged(geom);
        event->accept();
        break;
    case Qt::DownArrow:
    case Qt::Key_J:
        if (cursorPos.y() > -10 && cursorPos.y() < 10) {
            geom.setTop(geom.top() + 1);
            QCursor::setPos(cursorPosGlobal.x(), cursorPosGlobal.y() + 1);
        } else if (cursorPos.y() > height() - 10 && cursorPos.y() < height() + 10) {
            geom.setBottom(geom.bottom() + 1);
            QCursor::setPos(cursorPosGlobal.x(), cursorPosGlobal.y() + 1);
        } else if (QRect(geom).translated(-geom.x(), -geom.y()).contains(cursorPos)) {
            geom.moveTop(geom.top() + 1);
        }
        setGeometry(geom);
        emit selectionChanged(geom);
        event->accept();
        break;
    case Qt::UpArrow:
    case Qt::Key_K:
        if (cursorPos.y() > -10 && cursorPos.y() < 10) {
            geom.setTop(geom.top() - 1);
            QCursor::setPos(cursorPosGlobal.x(), cursorPosGlobal.y() - 1);
        } else if (cursorPos.y() > height() - 10 && cursorPos.y() < height() + 10) {
            geom.setBottom(geom.bottom() - 1);
            QCursor::setPos(cursorPosGlobal.x(), cursorPosGlobal.y() - 1);
        } else if (QRect(geom).translated(-geom.x(), -geom.y()).contains(cursorPos)) {
            geom.moveTop(geom.top() - 1);
        }
        setGeometry(geom);
        emit selectionChanged(geom);
        event->accept();
        break;
    case Qt::RightArrow:
    case Qt::Key_L:
        if (cursorPos.x() > -10 && cursorPos.x() < 10) {
            geom.setLeft(geom.left() + 1);
            QCursor::setPos(cursorPosGlobal.x() + 1, cursorPosGlobal.y());
        } else if (cursorPos.x() > width() - 10 && cursorPos.x() < width() + 10) {
            geom.setRight(geom.right() + 1);
            QCursor::setPos(cursorPosGlobal.x() + 1, cursorPosGlobal.y());
        } else if (QRect(geom).translated(-geom.x(), -geom.y()).contains(cursorPos)) {
            geom.moveLeft(geom.x() + 1);
        }
        setGeometry(geom);
        emit selectionChanged(geom);
        event->accept();
        break;
    }
}

void CropRubberBand::resizeEvent(QResizeEvent *) {
    rubberband->resize(size());
    emit selectionChanged(geometry());
}

void CropRubberBand::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit cropConfirmed();
        hide();
    }
}

void CropRubberBand::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        prevPos = event->globalPosition().toPoint();
    else if (event->button() == Qt::RightButton)
        hide();
}

void CropRubberBand::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        move(pos() + (event->globalPosition().toPoint() - prevPos));
        prevPos = event->globalPosition().toPoint();
        emit selectionChanged(geometry());
    }
}
