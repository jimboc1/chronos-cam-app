/****************************************************************************
 *  Copyright (C) 2013-2017 Kron Technologies Inc <http://www.krontech.ca>. *
 *                                                                          *
 *  This program is free software: you can redistribute it and/or modify    *
 *  it under the terms of the GNU General Public License as published by    *
 *  the Free Software Foundation, either version 3 of the License, or       *
 *  (at your option) any later version.                                     *
 *                                                                          *
 *  This program is distributed in the hope that it will be useful,         *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU General Public License for more details.                            *
 *                                                                          *
 *  You should have received a copy of the GNU General Public License       *
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 ****************************************************************************/
#include <time.h>

#include "videoRecord.h"
#include "util.h"
#include "camera.h"

#include "savesettingswindow.h"
#include "playbackwindow.h"
#include "ui_playbackwindow.h"

#include <QTimer>
#include <QMessageBox>


playbackWindow::playbackWindow(QWidget *parent, Camera * cameraInst, bool autosave) :
	QWidget(parent),
	ui(new Ui::playbackWindow)
{
	ui->setupUi(this);
	this->setWindowFlags(Qt::Dialog /*| Qt::WindowStaysOnTopHint*/ | Qt::FramelessWindowHint);
	this->move(600,0);
	camera = cameraInst;
	autoSaveFlag = autosave;

	sw = new StatusWindow;

	connect(ui->cmdClose, SIGNAL(clicked()), this, SLOT(close()));

	ui->verticalSlider->setMinimum(0);
	ui->verticalSlider->setMaximum(camera->recordingData.totalFrames - 1);
	ui->verticalSlider->setValue(camera->playFrame);
	markInFrame = 1;
	markOutFrame = camera->recordingData.totalFrames;

	camera->setPlayMode(true);

	lastPlayframe = camera->playFrame;

	playbackRate = 1;
	updatePlayRateLabel(playbackRate);

	timer = new QTimer(this);
	connect(timer, SIGNAL(timeout()), this, SLOT(updatePlayFrame()));
	timer->start(30);

	updateStatusText();

	if(autoSaveFlag) {
		on_cmdSave_clicked();
	}
}

playbackWindow::~playbackWindow()
{
	camera->setPlayMode(false);
	timer->stop();
	emit finishedSaving();
	delete sw;
	delete ui;
}

void playbackWindow::on_verticalSlider_sliderMoved(int position)
{
	camera->playFrame = position;
}

void playbackWindow::on_verticalSlider_valueChanged(int value)
{
	camera->playFrame = value;
}

void playbackWindow::on_cmdPlayForward_pressed()
{
	camera->setPlaybackRate(playbackRate, true);
}

void playbackWindow::on_cmdPlayForward_released()
{
	camera->setPlaybackRate(0, true);
}

void playbackWindow::on_cmdPlayReverse_pressed()
{
	camera->setPlaybackRate(playbackRate, false);
}

void playbackWindow::on_cmdPlayReverse_released()
{
	camera->setPlaybackRate(0, false);
}

void playbackWindow::on_cmdSave_clicked()
{
	UInt32 ret;
	QMessageBox msg;
	char parentPath[1000];

	//Build the parent path of the save directory, to determine if it's a mount point
	strcpy(parentPath, camera->recorder->fileDirectory);
	strcat(parentPath, "/..");

	if(!camera->recorder->getRunning())
	{
		//If no directory set, complain to the user
		if(strlen(camera->recorder->fileDirectory) == 0)
		{
			msg.setText("No save location set! Set save location in Settings");
			msg.exec();
			return;
		}

		//Check that the path exists
		struct stat sb;
		struct stat sbP;
		if (stat(camera->recorder->fileDirectory, &sb) == 0 && S_ISDIR(sb.st_mode) &&
				stat(parentPath, &sbP) == 0 && sb.st_dev != sbP.st_dev)		//If location is directory and is a mount point (device ID of parent is different from device ID of path)
		{
			ret = camera->startSave(markInFrame - 1, markOutFrame - markInFrame + 1);
			if(RECORD_FILE_EXISTS == ret)
			{
				msg.setText("File already exists. Rename then try saving again.");
				msg.exec();
				return;
			}
			else if(RECORD_DIRECTORY_NOT_WRITABLE == ret)
			{
				msg.setText("Save directory is not writable.");
				msg.exec();
				return;
			}
			ui->cmdSave->setText("Abort\nSave");
			setControlEnable(false);
			sw->setText("Saving...");
			sw->show();
			saveDoneTimer = new QTimer(this);
			connect(saveDoneTimer, SIGNAL(timeout()), this, SLOT(checkForSaveDone()));
			saveDoneTimer->start(100);
		}
		else
		{

			msg.setText(QString("Save location ") + QString(camera->recorder->fileDirectory) + " not found, set save location in Settings");
			msg.exec();
			return;
		}
	}
	else
	{
		camera->recorder->stop2();
	}

}

void playbackWindow::on_cmdStopSave_clicked()
{

}

void playbackWindow::on_cmdSaveSettings_clicked()
{
	saveSettingsWindow *w = new saveSettingsWindow(NULL, camera);
	w->setAttribute(Qt::WA_DeleteOnClose);
	w->show();
}

void playbackWindow::on_cmdMarkIn_clicked()
{
	markInFrame = camera->playFrame + 1;
	if(markOutFrame < markInFrame)
		markOutFrame = markInFrame;
	updateStatusText();
}

void playbackWindow::on_cmdMarkOut_clicked()
{
	markOutFrame = camera->playFrame + 1;
	if(markInFrame > markOutFrame)
		markInFrame = markOutFrame;
	updateStatusText();
}

void playbackWindow::updateStatusText()
{
	char text[100];
	sprintf(text, "Frame %d/%d\r\nMark in %d\r\nMark out %d", camera->playFrame + 1, camera->recordingData.totalFrames, markInFrame, markOutFrame);
	ui->lblInfo->setText(text);
}

//Periodically check if the play frame is updated
void playbackWindow::updatePlayFrame()
{
	UInt32 playFrame = camera->playFrame;
	if(playFrame != lastPlayframe)
	{
		ui->verticalSlider->setValue(playFrame);
		updateStatusText();
		lastPlayframe = camera->playFrame;
	}
}

//Once save is done, re-enable the window
void playbackWindow::checkForSaveDone()
{
	if(camera->recorder->endOfStream())
	{
		saveDoneTimer->stop();
		delete saveDoneTimer;

		sw->close();
		ui->cmdSave->setText("Save");
		setControlEnable(true);

		if(autoSaveFlag) {
			close();
		}
	}
}

void playbackWindow::on_cmdRateUp_clicked()
{
	if(playbackRate < 5)
		playbackRate++;
	if(0 == playbackRate)	//Don't let playback rate be 0 (no playback)
		playbackRate = 1;

	updatePlayRateLabel(playbackRate);
}

void playbackWindow::on_cmdRateDn_clicked()
{
	if(playbackRate > -12)
		playbackRate--;
	if(0 == playbackRate)	//Don't let playback rate be 0 (no playback)
		playbackRate = -1;

	updatePlayRateLabel(playbackRate);
}

void playbackWindow::updatePlayRateLabel(Int32 playbackRate)
{
	char playRateStr[100];
	double playRate;

	playRate = playbackRate > 0 ? 60.0 * (double)(1 << (playbackRate - 1)) : 60.0 / (double)(-playbackRate+1);
	sprintf(playRateStr, "%.1ffps", playRate);

	ui->lblFrameRate->setText(playRateStr);
}

void playbackWindow::setControlEnable(bool en)
{
	ui->cmdClose->setEnabled(en);
	ui->cmdMarkIn->setEnabled(en);
	ui->cmdMarkOut->setEnabled(en);
	ui->cmdPlayForward->setEnabled(en);
	ui->cmdPlayReverse->setEnabled(en);
	ui->cmdRateDn->setEnabled(en);
	ui->cmdRateUp->setEnabled(en);
	ui->cmdSaveSettings->setEnabled(en);
	ui->verticalSlider->setEnabled(en);

}
