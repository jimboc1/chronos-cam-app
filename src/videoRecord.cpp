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
#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <cstring>
#include <time.h>
#include <QDebug>
#include <unistd.h>
#include <fcntl.h>

#include "videoRecord.h"

/* DEBUG: Enabling sync is a great way to reproduce IO issues, but it almost certainly will drop frames. */
static gboolean
filesink_buffer_fsync(GstPad *pad, GstBuffer *buffer, gpointer data)
{
    //fsync((int)data);
    return TRUE;
}

void * recordThread(void *arg);
gboolean bus_call (GstBus     *bus,
		GstMessage *msg,
		gpointer    data);

Int32 VideoRecord::init(void)
{


}

VideoRecord::VideoRecord()
{
	profile = OMX_H264ENC_PROFILE_HIGH;
	level = OMX_H264ENC_LVL_51;
	i_period = 90;
	force_idr_period = 90;
	encodingPreset = OMX_H264ENC_ENC_PRE_HSMQ;

	eosCallback = NULL;
	eosCallbackArg = NULL;

	running = false;

    recordRunning = false;

	bitsPerPixel = 0.7;
	maxBitrate = 40.0;
	framerate = 60;
	strcpy(filename, "");
    strcpy(fileDirectory, "/media/mmcblk1p1");
}

VideoRecord::~VideoRecord()
{
}

Int32 VideoRecord::start(UInt32 hSize, UInt32 vSize, UInt32 frames, save_mode_type save_mode)
{
	char path[1000];
	char fname[1000];
	char sequencePath[1000];

	if(running)
		return RECORD_ALREADY_RUNNING;

	imgXSize = hSize;
	imgYSize = vSize;
	numFrames = frames;

	//Build the filename
	if(strlen(fileDirectory) == 0)
		return RECORD_NO_DIRECTORY_SET;

	strcpy(path, fileDirectory);

	if(strlen(filename) == 0)
	{
		//Fill timeinfo structure with the current time
		time_t rawtime;
		struct tm * timeinfo;

		time (&rawtime);
		timeinfo = localtime (&rawtime);

		sprintf(fname, "/vid_%04d-%02d-%02d_%02d-%02d-%02d",
					timeinfo->tm_year + 1900,
					timeinfo->tm_mon + 1,
					timeinfo->tm_mday,
					timeinfo->tm_hour,
					timeinfo->tm_min,
					timeinfo->tm_sec);
		strcat(path, fname);
	}
	else
	{
		strcat(path, "/");
		strcat(path, filename);
	}

	switch(save_mode) {
	case SAVE_MODE_H264:
		strcat(path, ".mp4");
		break;
	case SAVE_MODE_RAW16:
	case SAVE_MODE_RAW16RJ:
	case SAVE_MODE_RAW12:
		strcat(path, ".raw");
		break;
	case SAVE_MODE_RAW16_PNG:
		strcpy(sequencePath, path);
		strcat(sequencePath, "-\%05d.png");
		strcat(path, "-00000.png");
		break;
	}

	//Check if a file already exists at this path

	//If a file of this name already exists
	struct stat buffer;
	if(stat (path, &buffer) == 0)
	{
		return RECORD_FILE_EXISTS;
	}

	//Check that the directory is writable
	if(access(fileDirectory, W_OK) != 0)
	{	//Not writable
		return RECORD_DIRECTORY_NOT_WRITABLE;
	}
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        return RECORD_DIRECTORY_NOT_WRITABLE;
    }

	printf("Saving file to %s\r\n", path);

	if (save_mode == SAVE_MODE_H264) {
		bitrate = min(bitsPerPixel * imgXSize * imgYSize * framerate, min(60000000, (UInt32)(maxBitrate * 1000000.0)) * framerate / 60);	//Max of 60Mbps

		qDebug() << "Starting save at bitrate " << bitrate << " framerate " << framerate;
		GstElement *perf, *encoder, *queue, *parser, *mux, *sink;
		GstBus *bus;
		GstPad *pad;
		
		eosFlag = false;
		error = false;
		
		gst_init (NULL, NULL);
		
		/* Create gstreamer elements */
		pipeline =	gst_pipeline_new ("video-record");
		
		source =	gst_element_factory_make ("omx_camera",		"vfcc-source");
		perf  =		gst_element_factory_make ("gstperf",		"perf-count");
		encoder =	gst_element_factory_make ("omx_h264enc",	"h264-encoder");
		queue =		gst_element_factory_make ("queue",			"queue");
		parser =	gst_element_factory_make ("rr_h264parser",	"h264-parser");
		mux =		gst_element_factory_make ("mp4mux",			"mp4-mux");
		sink =		gst_element_factory_make ("fdsink",			"file-sink");
		
		
		if (!pipeline || !source || !perf || !encoder || !queue || !parser || !mux || !sink) {
			g_printerr ("One element could not be created. Exiting.\n");
			close(fd);
			return -1;
		}
		
		/* Set up the pipeline */
		
		//Set up the elements as required
		g_object_set (G_OBJECT (source), "input-interface", "VIP1_PORTA", NULL);
		g_object_set (G_OBJECT (source), "capture-mode", "SC_DISCRETESYNC_ACTVID_VSYNC", NULL);
		g_object_set (G_OBJECT (source), "vif-mode", "24BIT", NULL);
		g_object_set (G_OBJECT (source), "output-buffers", (guint)10, NULL);
		g_object_set (G_OBJECT (source), "skip-frames", (guint)0, NULL);
		if(0 != numFrames)
			g_object_set (G_OBJECT (source), "num-buffers", numFrames, NULL);
		
		g_object_set (G_OBJECT (perf), "print-arm-load", (gboolean)TRUE, NULL);
		g_object_set (G_OBJECT (perf), "print-fps", (gboolean)TRUE, NULL);
		
		g_object_set (G_OBJECT (encoder), "force-idr-period", (guint)force_idr_period, NULL);
		g_object_set (G_OBJECT (encoder), "i-period", (guint)i_period, NULL);
		g_object_set (G_OBJECT (encoder), "bitrate", (guint)bitrate, NULL);
		g_object_set (G_OBJECT (encoder), "profile", (guint)profile, NULL);
		g_object_set (G_OBJECT (encoder), "level", (guint)level, NULL);
		g_object_set (G_OBJECT (encoder), "encodingPreset", (guint)encodingPreset, NULL);
		g_object_set (G_OBJECT (encoder), "framerate", (guint)framerate, NULL);
		
		g_object_set (G_OBJECT (parser), "singleNalu", (gboolean)TRUE, NULL);
		
		g_object_set (G_OBJECT (mux), "dts-method", (guint)0, NULL);
		
		g_object_set (G_OBJECT (sink), "fd", (gint)fd, NULL);
		pad = gst_element_get_static_pad(sink, "sink");
		gst_pad_add_buffer_probe(pad, G_CALLBACK(filesink_buffer_fsync), (gpointer)fd);
		gst_object_unref(pad);
		
		//Add bus message handler, which will be handled by QT since it also uses a Glib loop
		bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
		bus_watch_id = gst_bus_add_watch (bus, bus_call, this);
		gst_object_unref (bus);
		
		//Add all the elements to the pipeline
		gst_bin_add_many (GST_BIN (pipeline),
						  source, perf, encoder, queue, parser, mux, sink, NULL);
		
		//Link the source and perf element with caps
		gboolean link_ok;
		GstCaps *caps;
		
		caps = gst_caps_new_simple ("video/x-raw-yuv",
									"format", GST_TYPE_FOURCC,
									GST_MAKE_FOURCC('N', 'V', '1', '2'),
									"width", G_TYPE_INT, imgXSize,
									"height", G_TYPE_INT, imgYSize,
									"framerate", GST_TYPE_FRACTION, framerate, 1,
									//"buffer-count-requested", G_TYPE_INT, 10,
									NULL);
		
		link_ok = gst_element_link_filtered (source, perf, caps);
		gst_caps_unref (caps);
		
		if (!link_ok) {
			g_printerr ("Failed to link source to perf with caps. Exiting.\n");
			return -1;
		}
		
		//Link the rest normally
		if(!gst_element_link_many (perf, encoder, queue, parser, mux, sink, NULL))
		{
			g_printerr ("Failed to link elements. Exiting.\n");
			return -1;
		}
	}
	else if (save_mode == SAVE_MODE_RAW16_PNG) {
		GstElement *perf, *encoder, *queue, *parser, *mux, *sink;
		GstBus *bus;
		GstPad *pad;

		eosFlag = false;
		error = false;
		
		gst_init (NULL, NULL);
		
		/* Create gstreamer elements */
		pipeline =	gst_pipeline_new ("video-record");
		
		source =	gst_element_factory_make ("omx_camera",		"vfcc-source");
		perf  =		gst_element_factory_make ("gstperf",		"perf-count");
		queue =		gst_element_factory_make ("queue",			"queue");
		encoder =   gst_element_factory_make ("pngenc",         "encoder");
		sink =		gst_element_factory_make ("multifilesink",	"file-sink");
		
		printf("gst objects made\n");

		if (!pipeline || !source || !perf || !queue || !encoder || !sink) {
			printf("One element could not be created\n");
			if (!pipeline) printf("pipeline\n");
			if (!perf) printf("perf\n");
			if (!queue) printf("queue\n");
			if (!sink) printf("sink\n");

			g_printerr ("One element could not be created. Exiting.\n");
			close(fd);
			return -1;
		}
		
		/* Set up the pipeline */
		
		//Set up the elements as required
		g_object_set (G_OBJECT (source), "input-interface", "VIP1_PORTA", NULL);
		g_object_set (G_OBJECT (source), "capture-mode", "SC_DISCRETESYNC_ACTVID_VSYNC", NULL);
		g_object_set (G_OBJECT (source), "vif-mode", "24BIT", NULL);
		g_object_set (G_OBJECT (source), "output-buffers", (guint)10, NULL);
		g_object_set (G_OBJECT (source), "skip-frames", (guint)0, NULL);
		//printf("about to override colorspace\n");
		//g_object_set (G_OBJECT (source), "override-colorspace", (gboolean)TRUE, NULL);
		//printf("done override colorspace\r\n");
		if(0 != numFrames)
			g_object_set (G_OBJECT (source), "num-buffers", numFrames, NULL);
		
		printf("gst source set up\r\n");

		g_object_set (G_OBJECT (perf), "print-arm-load", (gboolean)TRUE, NULL);
		g_object_set (G_OBJECT (perf), "print-fps", (gboolean)TRUE, NULL);

		printf("gst perf set up\r\n");

		
		g_object_set (G_OBJECT (sink), "location", sequencePath, NULL);
		//pad = gst_element_get_static_pad(sink, "sink");
		//gst_pad_add_buffer_probe(pad, G_CALLBACK(filesink_buffer_fsync), (gpointer)fd);
		//gst_object_unref(pad);
		
		printf("gst sink set up\r\n");

		//Add bus message handler, which will be handled by QT since it also uses a Glib loop
		bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
		bus_watch_id = gst_bus_add_watch (bus, bus_call, this);
		gst_object_unref (bus);

		printf("gst bus handler\r\n");

		//Add all the elements to the pipeline
		gst_bin_add_many (GST_BIN (pipeline),
						  source, perf, queue, sink, NULL);

		printf("gst pipeline made\r\n");

		//Link the source and perf element with caps
		gboolean link_ok;
		GstCaps *caps;

		caps = gst_caps_new_simple ("video/x-raw-gray",
									"bpp", G_TYPE_INT, 16,
									"width", G_TYPE_INT, imgXSize,
									"height", G_TYPE_INT, imgYSize,
									"framerate", GST_TYPE_FRACTION, framerate, 1,
									//"buffer-count-requested", G_TYPE_INT, 10,
									NULL);
			
		
		link_ok = gst_element_link_filtered (source, perf, caps);
		gst_caps_unref (caps);

		printf("gst caps set\r\n");

		if (!link_ok) {
			g_printerr ("Failed to link source to perf with caps. Exiting.\n");
			return -1;
		}
		
		//Link the rest normally
		if(!gst_element_link_many (perf, queue, sink, NULL))
		{
			g_printerr ("Failed to link elements. Exiting.\n");
			return -1;
		}
		
		printf("gst linked\r\n");
	}
	else { // one of the raw modes
		GstElement *perf, *encoder, *queue, *parser, *mux, *sink;
		GstBus *bus;
		GstPad *pad;
		
		eosFlag = false;
		error = false;
		
		gst_init (NULL, NULL);
		
		/* Create gstreamer elements */
		pipeline =	gst_pipeline_new ("video-record");
		
		source =	gst_element_factory_make ("omx_camera",		"vfcc-source");
		perf  =		gst_element_factory_make ("gstperf",		"perf-count");
		queue =		gst_element_factory_make ("queue",			"queue");
		sink =		gst_element_factory_make ("fdsink",			"file-sink");
		
		printf("gst objects made\n");

		if (!pipeline || !source || !perf || !queue || !sink) {
			printf("One element could not be created\n");
			if (!pipeline) printf("pipeline\n");
			if (!perf) printf("perf\n");
			if (!queue) printf("queue\n");
			if (!sink) printf("sink\n");

			g_printerr ("One element could not be created. Exiting.\n");
			close(fd);
			return -1;
		}
		
		/* Set up the pipeline */
		
		//Set up the elements as required
		g_object_set (G_OBJECT (source), "input-interface", "VIP1_PORTA", NULL);
		g_object_set (G_OBJECT (source), "capture-mode", "SC_DISCRETESYNC_ACTVID_VSYNC", NULL);
		g_object_set (G_OBJECT (source), "vif-mode", "24BIT", NULL);
		g_object_set (G_OBJECT (source), "output-buffers", (guint)10, NULL);
		g_object_set (G_OBJECT (source), "skip-frames", (guint)0, NULL);
		//printf("about to override colorspace\n");
		//g_object_set (G_OBJECT (source), "override-colorspace", (gboolean)TRUE, NULL);
		//printf("done override colorspace\r\n");
		if(0 != numFrames)
			g_object_set (G_OBJECT (source), "num-buffers", numFrames, NULL);
		
		printf("gst source set up\r\n");

		g_object_set (G_OBJECT (perf), "print-arm-load", (gboolean)TRUE, NULL);
		g_object_set (G_OBJECT (perf), "print-fps", (gboolean)TRUE, NULL);

		printf("gst perf set up\r\n");

		g_object_set (G_OBJECT (sink), "fd", (gint)fd, NULL);
		pad = gst_element_get_static_pad(sink, "sink");
		gst_pad_add_buffer_probe(pad, G_CALLBACK(filesink_buffer_fsync), (gpointer)fd);
		gst_object_unref(pad);
		
		printf("gst sink set up\r\n");

		//Add bus message handler, which will be handled by QT since it also uses a Glib loop
		bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
		bus_watch_id = gst_bus_add_watch (bus, bus_call, this);
		gst_object_unref (bus);

		printf("gst bus handler\r\n");

		//Add all the elements to the pipeline
		gst_bin_add_many (GST_BIN (pipeline),
						  source, perf, queue, sink, NULL);

		printf("gst pipeline made\r\n");

		//Link the source and perf element with caps
		gboolean link_ok;
		GstCaps *caps;

		if (save_mode == SAVE_MODE_RAW16 || save_mode == SAVE_MODE_RAW16RJ) {
			caps = gst_caps_new_simple ("video/x-raw-gray",
										"bpp", G_TYPE_INT, 16,
										"width", G_TYPE_INT, imgXSize,
										"height", G_TYPE_INT, imgYSize,
										"framerate", GST_TYPE_FRACTION, framerate, 1,
										//"buffer-count-requested", G_TYPE_INT, 10,
										NULL);
			printf("gst set up as x-raw-gray\r\n");
		}
		else if (save_mode == SAVE_MODE_RAW12) {
			caps = gst_caps_new_simple ("video/x-raw-rgb",
										"bpp", G_TYPE_INT, 24,
										"width", G_TYPE_INT, imgXSize/2, // note that we pack it as 2-samples per pixel so half-width handles this
										"height", G_TYPE_INT, imgYSize,
										"framerate", GST_TYPE_FRACTION, framerate, 1,
										//"buffer-count-requested", G_TYPE_INT, 10,
										NULL);
			printf("gst set up as x-raw-rgb\r\n");
		}
			
		
		link_ok = gst_element_link_filtered (source, perf, caps);
		gst_caps_unref (caps);

		printf("gst caps set\r\n");

		if (!link_ok) {
			g_printerr ("Failed to link source to perf with caps. Exiting.\n");
			return -1;
		}
		
		//Link the rest normally
		if(!gst_element_link_many (perf, queue, sink, NULL))
		{
			g_printerr ("Failed to link elements. Exiting.\n");
			return -1;
		}
		
		printf("gst linked\r\n");
	}
		
		
	//Set pipeline to playing
	g_print ("Now playing\n");
	GstStateChangeReturn scr = gst_element_set_state (pipeline, GST_STATE_PLAYING);
	if(GST_STATE_CHANGE_FAILURE == scr)
	{
		g_printerr ("Failed to change pipeline state. Exiting.\n");
		return -1;
	}
	

	running = true;
    frameCount = 0;

	printf("VideoRecord start done\n");
	return SUCCESS;

}

UInt32 VideoRecord::stop()
{
	if(!running)
		return RECORD_NOT_RUNNING;

	GstState state, pending;

	/* Out of the main loop, clean up nicely */
	g_print ("Returned, stopping playback\n");
    running = false;

	gst_element_get_state (pipeline, &state, &pending, GST_CLOCK_TIME_NONE);
	g_print ("Current state: %d, pending state: %d\n", state, pending);

	g_print ("Setting pipeline to PAUSED ...\n");
	gst_element_set_state (pipeline, GST_STATE_PAUSED);
	gst_element_get_state (pipeline, &state, &pending, GST_CLOCK_TIME_NONE);
	g_print ("Current state: %d, pending state: %d\n", state, pending);

	/* iterate mainloop to process pending stuff */
	while (g_main_context_iteration (NULL, FALSE));

	g_print ("Setting pipeline to READY ...\n");
	gst_element_set_state (pipeline, GST_STATE_READY);
	gst_element_get_state (pipeline, &state, &pending, GST_CLOCK_TIME_NONE);
	g_print ("Current state: %d, pending state: %d\n", state, pending);

	g_print ("Setting pipeline to NULL ...\n");
	gst_element_set_state (pipeline, GST_STATE_NULL);
	gst_element_get_state (pipeline, &state, &pending, GST_CLOCK_TIME_NONE);
	g_print ("Current state: %d, pending state: %d\n", state, pending);

	g_print ("Deleting pipeline\n");
	gst_object_unref (GST_OBJECT (pipeline));
	g_source_remove (bus_watch_id);

    g_print ("Closing output file\n");
    fsync(fd);
    close(fd);
}

UInt32 VideoRecord::stop2()
{
	if(!running)
		return RECORD_NOT_RUNNING;

	GstEvent*  event = gst_event_new_eos();
	 gst_element_send_event(pipeline, event);
	return SUCCESS;
}

bool VideoRecord::flowReady()
{
    if (running) {
        gint level = 10;
        g_object_get (G_OBJECT (source), "buffer-level", &level, NULL);
        return (level > 1);
    }
    return TRUE;
}

void VideoRecord::debug()
{
    if (running) {
        frameCount++;
#if 0
        if (!(frameCount % 60)) {
            gint level = 77;
            gint drops = 0;
            g_object_get (G_OBJECT (source), "buffer-level", &level, "dropped-frames", &drops, NULL);
            printf("omx_vfcc: buffer-level=%d dropped-frames=%d\n", level, drops);
        }
#endif
    }
}

gboolean
bus_call (GstBus     *bus,
		GstMessage *msg,
		gpointer    data)
{
	VideoRecord* gstDia = (VideoRecord*)data;

    printf("Message received: %s\n", GST_MESSAGE_TYPE_NAME (msg));

	switch (GST_MESSAGE_TYPE (msg)) {

		case GST_MESSAGE_EOS:

			gstDia->eosFlag = true;
			if(gstDia->eosCallback)
				(*gstDia->eosCallback)(gstDia->eosCallbackArg);	//Call user callback
			break;

		case GST_MESSAGE_ERROR:
			{
				gchar  *debug;
				GError *error;

				gst_message_parse_error (msg, &error, &debug);
				g_free (debug);

				g_printerr ("Error: %s\n", error->message);
				gstDia->error = true;
				g_error_free (error);

				break;
			}
		case GST_MESSAGE_ASYNC_DONE:
			gstDia->recordRunning = true;
		break;
		default:
			break;
	}
}

UInt32 VideoRecord::getSafeHRes(UInt32 hRes)
{
	return max((hRes + 15) & 0xFFFFFFF0, ENCODER_MIN_H_RES);
}

UInt32 VideoRecord::getSafeVRes(UInt32 vRes)
{
	return max(vRes, ENCODER_MIN_V_RES);
}

bool VideoRecord::endOfStream()
{
	return eosFlag;
}



