/* GStreamer
 * Copyright (C) 2010 David Schleef <ds@schleef.org>
 * Copyright (C) 2015 Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstchecksumsink.h"
#include <stdlib.h>
#include <string.h>

static void gst_checksum_sink_dispose (GObject * object);
static void gst_checksum_sink_finalize (GObject * object);

static gboolean gst_checksum_sink_start (GstBaseSink * sink);
static gboolean gst_checksum_sink_stop (GstBaseSink * sink);
static gboolean gst_checksum_sink_set_caps (GstBaseSink * base_sink,
    GstCaps * caps);
static gboolean gst_checksum_sink_propose_allocation (GstBaseSink * base_sink,
    GstQuery * query);
static GstFlowReturn gst_checksum_sink_render (GstBaseSink * sink,
    GstBuffer * buffer);

static GstStaticPadTemplate gst_checksum_sink_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_checksum_sink_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* class initialization */

#define gst_checksum_sink_parent_class parent_class
G_DEFINE_TYPE (GstChecksumSink, gst_checksum_sink, GST_TYPE_BASE_SINK);

static void
gst_checksum_sink_class_init (GstChecksumSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->dispose = gst_checksum_sink_dispose;
  gobject_class->finalize = gst_checksum_sink_finalize;
  base_sink_class->start = GST_DEBUG_FUNCPTR (gst_checksum_sink_start);
  base_sink_class->stop = GST_DEBUG_FUNCPTR (gst_checksum_sink_stop);
  base_sink_class->set_caps = gst_checksum_sink_set_caps;
  base_sink_class->propose_allocation = gst_checksum_sink_propose_allocation;
  base_sink_class->render = GST_DEBUG_FUNCPTR (gst_checksum_sink_render);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_checksum_sink_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_checksum_sink_sink_template));

  gst_element_class_set_static_metadata (element_class, "Checksum sink",
      "Debug/Sink", "Calculates a checksum for buffers",
      "David Schleef <ds@schleef.org>, Sreerenj Balachandran <sreerenj.balachandran@intel.com>");
}

static void
gst_checksum_sink_init (GstChecksumSink * checksumsink)
{
  gst_base_sink_set_sync (GST_BASE_SINK (checksumsink), FALSE);
}

void
gst_checksum_sink_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_checksum_sink_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_checksum_sink_start (GstBaseSink * sink)
{
  return TRUE;
}

static gboolean
gst_checksum_sink_stop (GstBaseSink * sink)
{
  return TRUE;
}

static gboolean
gst_checksum_sink_set_caps (GstBaseSink * base_sink, GstCaps * caps)
{
  GstChecksumSink *checksumsink = GST_CHECKSUM_SINK (base_sink);

  if (caps)
    gst_video_info_from_caps (&checksumsink->vinfo, caps);

  return TRUE;
}

static gboolean
gst_checksum_sink_propose_allocation (GstBaseSink * base_sink, GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query,
      GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  return TRUE;
}

static GstFlowReturn
gst_checksum_sink_render (GstBaseSink * sink, GstBuffer * buffer)
{
  GstChecksumSink *checksumsink = GST_CHECKSUM_SINK (sink);
  gchar *checksum;
  GstVideoFrame frame;
  GstVideoInfo *sinfo;
  guint8 *data = NULL, *dp, *sp;
  guint j, n_planes, plane;
  guint w, h, size = 0;
  guint width, height;

  GstVideoCropMeta *const crop_meta = gst_buffer_get_video_crop_meta (buffer);

  if (!crop_meta) {
    width = GST_VIDEO_INFO_WIDTH (&checksumsink->vinfo);
    height = GST_VIDEO_INFO_HEIGHT (&checksumsink->vinfo);
  } else {
    width = crop_meta->width;
    height = crop_meta->height;
  }

  /* only i420 and yv12 are supported */
  if (GST_VIDEO_INFO_FORMAT (&checksumsink->vinfo) != GST_VIDEO_FORMAT_I420 &&
      GST_VIDEO_INFO_FORMAT (&checksumsink->vinfo) != GST_VIDEO_FORMAT_YV12) {
    GST_ERROR_OBJECT (checksumsink,
        "Unsupported raw video format, Only supporting I420 and YV12!!");
    return GST_FLOW_ERROR;
  }

  size = (width * height) + (2 * ((width / 2) * (height / 2)));

  if (!gst_video_frame_map (&frame, &checksumsink->vinfo, buffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (checksumsink, "Failed to map frame");
    g_assert_not_reached ();
  }

  sinfo = &frame.info;
  n_planes = sinfo->finfo->n_planes;

  data = (guint8 *) g_malloc (size);

  dp = data;
  for (plane = 0; plane < n_planes; plane++) {

    if (plane != 0) {
      w = width / 2;
      h = height / 2;
    } else {
      w = width;
      h = height;
    }
    sp = frame.data[plane];

    for (j = 0; j < h; j++) {
      memcpy (data, sp, w);
      data += w;
      sp += sinfo->stride[plane];
    }
  }
  data = dp;

  checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA1, data, size);
  g_print ("checksum %s\n", checksum);

  gst_video_frame_unmap (&frame);
  g_free (checksum);
  g_free (data);

  return GST_FLOW_OK;
}
