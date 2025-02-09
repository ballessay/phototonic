/*
 *  Copyright (C) 2013-2014 Ofer Kashayov <oferkv@live.com>
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

#ifndef COPY_MOVE_DIALOG_H
#define COPY_MOVE_DIALOG_H

class ThumbsViewer;
class QLabel;
#include <QProgressDialog>

class CopyMoveDialog : public QProgressDialog {
Q_OBJECT

public:
    CopyMoveDialog(QWidget *parent);

    static int copyFile(const QString &srcPath, QString &dstPath) {
        return copyOrMoveFile(srcPath, dstPath, true);
    }
    static int moveFile(const QString &srcPath, QString &dstPath) {
        return copyOrMoveFile(srcPath, dstPath, false);
    }
    static int copyOrMoveFile(const QString &srcPath, QString &dstPath, bool copy);

    void execute(ThumbsViewer *thumbView, QString &destDir, bool pasteInCurrDir);
    int latestRow;
private:
    QLabel *m_label;
};

#endif // COPY_MOVE_DIALOG_H
