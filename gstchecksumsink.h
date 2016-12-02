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

#ifndef _GST_CKSUM_IMAGE_SINK_H_
#define _GST_CKSUM_IMAGE_SINK_H_

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_CKSUM_IMAGE_SINK   (gst_cksum_image_sink_get_type())
#define GST_CKSUM_IMAGE_SINK(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CKSUM_IMAGE_SINK,GstCksumImageSink))
#define GST_CKSUM_IMAGE_SINK_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CKSUM_IMAGE_SINK,GstCksumImageSinkClass))
#define GST_IS_CKSUM_IMAGE_SINK(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CKSUM_IMAGE_SINK))
#define GST_IS_CKSUM_IMAGE_SINK_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CKSUM_IMAGE_SINK))

typedef struct _GstCksumImageSink GstCksumImageSink;
typedef struct _GstCksumImageSinkClass GstCksumImageSinkClass;

struct _GstCksumImageSink
{
  GstVideoSink parent;
  GstVideoInfo vinfo;

  /* properties */
  GChecksumType hash;
  gboolean file_checksum;
  gboolean frame_checksum;
  gboolean plane_checksum;
  gboolean dump_output;

  gchar *raw_file_name;
  gint fd;

  guint8 *data;
  gsize data_size;
};

struct _GstCksumImageSinkClass
{
  GstVideoSinkClass parent_class;
};

GType gst_cksum_image_sink_get_type (void);

G_END_DECLS

#endif
