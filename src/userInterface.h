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
#ifndef USERINTERFACE_H
#define USERINTERFACE_H

#include <pthread.h>
#include <poll.h>
#include "errorCodes.h"
#include "types.h"
/*
typedef enum
{
	UI_ERROR_NONE = 0,
	UI_FILE_ERROR,
	UI_THREAD_ERROR
} UIErrortype;
*/
void* encAThread(void *arg);
void* encBThread(void *arg);

class UserInterface {
public:
	Int32 init(void);
	UserInterface();
	~UserInterface();

	Int32 getEncoderValue(Int32 * encValLowResPtr = 0);
	bool getEncoderSwitch();
	bool getShutterButton();
	void setRecLEDBack(bool on);
	void setRecLEDFront(bool on);
private:
	Int32 encValue;
	Int32 encValueLowRes;
	bool encAVal, encBVal, encALast, encBLast;
	Int32 encAgpioFD, encBgpioFD, encSwgpioFD, shSwgpioFD, recLedFrontFD, recLedBackFD;
	struct pollfd encAgpioPoll, encBgpioPoll;
	pthread_t encAThreadID, encBThreadID;
	friend void* encAThread(void *arg);
	friend void* encBThread(void *arg);
	volatile bool terminateEncThreads;
	void encoderCB(void);

};

#endif // USERINTERFACE_H
