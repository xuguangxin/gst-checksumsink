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

#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gstchecksumsink.h"

static gboolean gst_cksum_image_sink_start (GstBaseSink * sink);
static gboolean gst_cksum_image_sink_stop (GstBaseSink * sink);
static gboolean gst_cksum_image_sink_set_caps (GstBaseSink * base_sink,
    GstCaps * caps);
static gboolean gst_cksum_image_sink_propose_allocation (GstBaseSink *
    base_sink, GstQuery * query);
static GstFlowReturn gst_cksum_image_sink_show_frame (GstVideoSink * sink,
    GstBuffer * buffer);
static void gst_cksum_image_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cksum_image_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

enum
{
  PROP_0,
  PROP_HASH,
  PROP_FILE_CHECKSUM,
  PROP_FRAME_CHECKSUM,
  PROP_PLANE_CHECKSUM,
  PROP_RAW_OUTPUT
};

static GstStaticPadTemplate gst_cksum_image_sink_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{NV12, I420, YV12}")));

/* class initialization */

#define GST_TYPE_CKSUM_IMAGE_SINK_HASH (gst_cksum_image_sink_hash_get_type ())
static GType
gst_cksum_image_sink_hash_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {G_CHECKSUM_MD5, "MD5", "md5"},
      {G_CHECKSUM_SHA1, "SHA-1", "sha1"},
      {G_CHECKSUM_SHA256, "SHA-256", "sha256"},
      {G_CHECKSUM_SHA512, "SHA-512", "sha512"},
      {0, NULL, NULL},
    };

    gtype = g_enum_register_static ("GstCksumImageSinkHash", values);
  }
  return gtype;
}

GST_DEBUG_CATEGORY_STATIC (gst_cksum_image_sink_debug);
#define GST_CAT_DEFAULT gst_cksum_image_sink_debug

#define gst_cksum_image_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstCksumImageSink, gst_cksum_image_sink,
    GST_TYPE_VIDEO_SINK, GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT,
        "checksumsink2", 0, "checksum image sink"));

static void
gst_cksum_image_sink_class_init (GstCksumImageSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);
  GstVideoSinkClass *video_sink_class = GST_VIDEO_SINK_CLASS (klass);

  gobject_class->set_property = gst_cksum_image_sink_set_property;
  gobject_class->get_property = gst_cksum_image_sink_get_property;
  base_sink_class->start = GST_DEBUG_FUNCPTR (gst_cksum_image_sink_start);
  base_sink_class->stop = GST_DEBUG_FUNCPTR (gst_cksum_image_sink_stop);
  base_sink_class->set_caps = gst_cksum_image_sink_set_caps;
  base_sink_class->propose_allocation = gst_cksum_image_sink_propose_allocation;

  video_sink_class->show_frame = gst_cksum_image_sink_show_frame;

  g_object_class_install_property (gobject_class, PROP_HASH,
      g_param_spec_enum ("hash", "Hash", "Checksum type",
          GST_TYPE_CKSUM_IMAGE_SINK_HASH, G_CHECKSUM_MD5,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FILE_CHECKSUM,
      g_param_spec_boolean ("file-checksum", "File checksum",
          "calculate checksum for the whole raw data file (MD5 only)",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FRAME_CHECKSUM,
      g_param_spec_boolean ("frame-checksum", "Frame checksum",
          "calculate checksum per frame", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PLANE_CHECKSUM,
      g_param_spec_boolean ("plane-checksum", "Plane checksum",
          "calculate checksum per plane", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RAW_OUTPUT,
      g_param_spec_boolean ("dump-output", "Dump output",
          "save decoded raw frames into file (YV12 and I420 only)",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_cksum_image_sink_sink_template));

  gst_element_class_set_static_metadata (element_class, "Checksum Image sink",
      "Debug/Sink", "Calculates a checksum for video frames",
      "David Schleef <ds@schleef.org>, "
      "Sreerenj Balachandran <sreerenj.balachandran@intel.com>");
}

static void
gst_cksum_image_sink_init (GstCksumImageSink * checksumsink)
{
  gst_base_sink_set_sync (GST_BASE_SINK (checksumsink), FALSE);
  checksumsink->hash = G_CHECKSUM_MD5;
  checksumsink->frame_checksum = TRUE;
}

static void
gst_cksum_image_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCksumImageSink *checksumsink = GST_CKSUM_IMAGE_SINK (object);

  switch (prop_id) {
    case PROP_HASH:
      checksumsink->hash = g_value_get_enum (value);
      break;
    case PROP_FILE_CHECKSUM:
      checksumsink->file_checksum = g_value_get_boolean (value);
      break;
    case PROP_FRAME_CHECKSUM:
      checksumsink->frame_checksum = g_value_get_boolean (value);
      break;
    case PROP_PLANE_CHECKSUM:
      checksumsink->plane_checksum = g_value_get_boolean (value);
      break;
    case PROP_RAW_OUTPUT:
      checksumsink->dump_output = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cksum_image_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCksumImageSink *checksumsink = GST_CKSUM_IMAGE_SINK (object);

  switch (prop_id) {
    case PROP_HASH:
      g_value_set_enum (value, checksumsink->hash);
      break;
    case PROP_FILE_CHECKSUM:
      g_value_set_boolean (value, checksumsink->file_checksum);
      break;
    case PROP_FRAME_CHECKSUM:
      g_value_set_boolean (value, checksumsink->frame_checksum);
      break;
    case PROP_PLANE_CHECKSUM:
      g_value_set_boolean (value, checksumsink->plane_checksum);
      break;
    case PROP_RAW_OUTPUT:
      g_value_set_boolean (value, checksumsink->dump_output);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
open_raw_file (GstCksumImageSink * checksumsink)
{
  GError *err;

  if (!checksumsink->file_checksum)
    return TRUE;
  if (checksumsink->raw_file_name)
    return TRUE;

  checksumsink->fd =
      g_file_open_tmp ("tmp_XXXXXX.yuv", &checksumsink->raw_file_name, &err);
  if (checksumsink->fd == -1) {
    GST_ELEMENT_ERROR (checksumsink, RESOURCE, OPEN_WRITE,
        ("failed to create output file"),
        ("reason: %s", err ? err->message : ""));
    g_error_free (err);
    return FALSE;
  }

  GST_INFO_OBJECT (checksumsink, "raw file name: %s",
      checksumsink->raw_file_name);
  return TRUE;
}

static gboolean
gst_cksum_image_sink_start (GstBaseSink * sink)
{
  GstCksumImageSink *checksumsink = GST_CKSUM_IMAGE_SINK (sink);

  if (!open_raw_file (checksumsink))
    return FALSE;

  if (checksumsink->dump_output) {
    checksumsink->raw_output = fopen ("dump_output.yuv", "wb");
    if (checksumsink->raw_output == NULL) {
      GST_ERROR_OBJECT (checksumsink, "Failed to create dump_output.yuv file");
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
checksum_raw_file (GstCksumImageSink * checksumsink)
{
  GChecksum *csum;
  FILE *fd;
  guchar data[8 * BUFSIZ + 1];
  size_t len;
  gboolean ret;

  if (!checksumsink->file_checksum)
    return TRUE;
  if (!checksumsink->raw_file_name) {
    GST_WARNING_OBJECT (checksumsink, "unspecified raw file");
    return FALSE;
  }

  ret = FALSE;
  fd = fopen (checksumsink->raw_file_name, "r");
  if (!fd) {
    GST_WARNING_OBJECT (checksumsink, "failed to open %s: %s",
        checksumsink->raw_file_name, strerror (errno));
    goto remove_file;
  }

  csum = g_checksum_new (G_CHECKSUM_MD5);

  while ((len = fread (data, 1, 8 * BUFSIZ, fd)) > 0)
    g_checksum_update (csum, data, len);

  g_print ("%s\n", g_checksum_get_string (csum));

  g_checksum_free (csum);
  fclose (fd);

  ret = TRUE;

remove_file:
  if (g_unlink (checksumsink->raw_file_name) != 0) {
    GST_WARNING_OBJECT (checksumsink, "failed to remove %s: %s",
        checksumsink->raw_file_name, strerror (errno));
  }

  return ret;
}

static gboolean
gst_cksum_image_sink_stop (GstBaseSink * sink)
{
  GstCksumImageSink *checksumsink = GST_CKSUM_IMAGE_SINK (sink);

  if (checksumsink->fd != -1)
    close (checksumsink->fd);

  checksum_raw_file (checksumsink);

  g_clear_pointer (&checksumsink->raw_file_name, g_free);

  if (checksumsink->raw_output) {
    fclose (checksumsink->raw_output);
  }

  return TRUE;
}

static gboolean
gst_cksum_image_sink_set_caps (GstBaseSink * base_sink, GstCaps * caps)
{
  GstCksumImageSink *checksumsink = GST_CKSUM_IMAGE_SINK (base_sink);
  GstVideoInfo vinfo;

  if (!caps)
    return FALSE;
  if (!gst_video_info_from_caps (&vinfo, caps))
    return FALSE;

  checksumsink->vinfo = vinfo;

  return TRUE;
}

static gboolean
gst_cksum_image_sink_propose_allocation (GstBaseSink * base_sink,
    GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query,
      GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  return TRUE;
}

static void
get_plane_width_and_height (guint plane, guint width, guint height, guint * w,
    guint * h)
{
  if (plane != 0) {
    /* u_v width */
    if (width % 2 != 0)
      *w = (width + 1) / 2;
    else
      *w = width / 2;

    /* u_v heihgt */
    if (height % 2 != 0)
      *h = (height + 1) / 2;
    else
      *h = height / 2;

  } else {
    *w = width;
    *h = height;
  }
}

static GstFlowReturn
gst_cksum_image_sink_show_frame (GstVideoSink * sink, GstBuffer * buffer)
{
  GstCksumImageSink *checksumsink = GST_CKSUM_IMAGE_SINK (sink);
  gchar *checksum;
  GstVideoFrame frame;
  GstVideoFormat format;
  GstVideoInfo *vinfo;
  guint8 *data = NULL, *dp, *sp, *pp;
  guint j, n_planes, plane;
  guint w, h, size = 0, file_size = 0;
  guint Ysize = 0, Usize = 0, Vsize = 0;
  guint width, height, y_width, y_height, uv_width, uv_height;
  GstVideoCropMeta *const crop_meta = gst_buffer_get_video_crop_meta (buffer);

  vinfo = &checksumsink->vinfo;
  if (!gst_video_frame_map (&frame, vinfo, buffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (checksumsink, "failed to map frame");
    return GST_FLOW_ERROR;
  }

  if (!crop_meta) {
    width = GST_VIDEO_FRAME_WIDTH (&frame);
    height = GST_VIDEO_FRAME_HEIGHT (&frame);
  } else {
    width = crop_meta->width;
    height = crop_meta->height;
  }

  /* get width and height for luma */
  get_plane_width_and_height (0, width, height, &y_width, &y_height);
  /* get width and height for chroma */
  get_plane_width_and_height (1, width, height, &uv_width, &uv_height);

  Ysize = y_width * y_height;
  Usize = uv_width * uv_height;
  Vsize = Usize;

  size = Ysize + Usize + Vsize;

  n_planes = GST_VIDEO_FRAME_N_PLANES (&frame);
  format = GST_VIDEO_FRAME_FORMAT (&frame);

  data = (guint8 *) g_malloc (size);

  dp = data;
  for (plane = 0; plane < n_planes; plane++) {

    sp = GST_VIDEO_FRAME_PLANE_DATA (&frame, plane);

    if (plane == 0) {
      w = y_width;
      h = y_height;
    } else {
      if (format == GST_VIDEO_FORMAT_NV12) {
        w = 2 * uv_width;
        h = uv_height;
      } else {
        w = uv_width;
        h = uv_height;
      }
    }

    for (j = 0; j < h; j++) {
      memcpy (data, sp, w);
      data += w;
      sp += GST_VIDEO_FRAME_PLANE_STRIDE (&frame, plane);
    }

    if (checksumsink->plane_checksum) {
      guint plane_size;

      switch (plane) {
        case 0:
          pp = dp;
          plane_size = Ysize;
          break;
        case 1:
          pp = dp + Ysize;
          if (format == GST_VIDEO_FORMAT_NV12)
            plane_size = Usize + Vsize;
          else if (format == GST_VIDEO_FORMAT_I420)
            plane_size = Usize;
          else
            plane_size = Vsize;
          break;
        case 2:
          if (format == GST_VIDEO_FORMAT_I420) {
            pp = dp + Ysize + Usize;
            plane_size = Vsize;
          } else {
            pp = dp + Ysize + Vsize;
            plane_size = Usize;
          }
          break;
      }

      checksum =
          g_compute_checksum_for_data (checksumsink->hash, pp, plane_size);
      g_print ("%s  ", checksum);
      g_free (checksum);
    }
  }
  data = dp;

  if (checksumsink->dump_output && checksumsink->raw_output) {
    fwrite (data, 1, size, checksumsink->raw_output);
  }

  if (checksumsink->plane_checksum)
    g_print ("\n");

  if (checksumsink->frame_checksum) {
    checksum = g_compute_checksum_for_data (checksumsink->hash, data, size);
    g_print ("FrameChecksum %s\n", checksum);
    g_free (checksum);
  }

  if (checksumsink->file_checksum) {
    if (write (checksumsink->fd, data, size) < size) {
      GST_ERROR_OBJECT (checksumsink, "Failed to write to the file");
      return GST_FLOW_ERROR;
    }
    file_size += size;
  }

  g_free (data);
  gst_video_frame_unmap (&frame);

  return GST_FLOW_OK;
}
