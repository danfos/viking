/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * viking
 * Copyright (C) Guilhem Bonnefille 2009 <guilhem.bonnefille@gmail.com>
 * 
 * viking is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * viking is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_MATH_H
#include <math.h>
#endif

#include "globals.h"
#include "terraservermapsource.h"

static gboolean _coord_to_mapcoord ( VikMapSource *self, const VikCoord *src, gdouble xzoom, gdouble yzoom, MapCoord *dest );
static void _mapcoord_to_center_coord ( VikMapSource *self, MapCoord *src, VikCoord *dest );
static int _download ( VikMapSource *self, MapCoord *src, const gchar *dest_fn );

/* FIXME Huge gruik */
static DownloadOptions terraserver_options = { NULL, 0, a_check_map_file };

typedef struct _TerraserverMapSourcePrivate TerraserverMapSourcePrivate;
struct _TerraserverMapSourcePrivate
{
  int type;
};

#define TERRASERVER_MAP_SOURCE_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), TERRASERVER_TYPE_MAP_SOURCE, TerraserverMapSourcePrivate))

G_DEFINE_TYPE_EXTENDED (TerraserverMapSource, terraserver_map_source, VIK_TYPE_MAP_SOURCE_DEFAULT, (GTypeFlags)0,);

static void
terraserver_map_source_init (TerraserverMapSource *self)
{
	/* initialize the object here */
	vik_map_source_default_set_tilesize_x (VIK_MAP_SOURCE_DEFAULT (self), 200);
	vik_map_source_default_set_tilesize_y (VIK_MAP_SOURCE_DEFAULT (self), 200);
	vik_map_source_default_set_drawmode   (VIK_MAP_SOURCE_DEFAULT (self), VIK_VIEWPORT_DRAWMODE_UTM);
}

static void
terraserver_map_source_finalize (GObject *object)
{
	/* TODO: Add deinitalization code here */

	G_OBJECT_CLASS (terraserver_map_source_parent_class)->finalize (object);
}

static void
terraserver_map_source_class_init (TerraserverMapSourceClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);
	VikMapSourceClass* parent_class = VIK_MAP_SOURCE_CLASS (klass);

	/* Overiding methods */
	parent_class->coord_to_mapcoord =        _coord_to_mapcoord;
	parent_class->mapcoord_to_center_coord = _mapcoord_to_center_coord;
	parent_class->download =                 _download;
		
	g_type_class_add_private (klass, sizeof (TerraserverMapSourcePrivate));
	
	object_class->finalize = terraserver_map_source_finalize;
}

#define TERRASERVER_SITE "terraserver-usa.com"
#define MARGIN_OF_ERROR 0.001

static int mpp_to_scale ( gdouble mpp, guint8 type )
{
  mpp *= 4;
  gint t = (gint) mpp;
  if ( ABS(mpp - t) > MARGIN_OF_ERROR )
    return FALSE;

  switch ( t ) {
    case 1: return (type == 4) ? 8 : 0;
    case 2: return (type == 4) ? 9 : 0;
    case 4: return (type != 2) ? 10 : 0;
    case 8: return 11;
    case 16: return 12;
    case 32: return 13;
    case 64: return 14;
    case 128: return 15;
    case 256: return 16;
    case 512: return 17;
    case 1024: return 18;
    case 2048: return 19;
    default: return 0;
  }
}

static gdouble scale_to_mpp ( gint scale )
{
  return pow(2,scale - 10);
}

static gboolean
_coord_to_mapcoord ( VikMapSource *self, const VikCoord *src, gdouble xmpp, gdouble ympp, MapCoord *dest )
{
	g_return_val_if_fail(TERRASERVER_IS_MAP_SOURCE(self), FALSE);
	
	TerraserverMapSourcePrivate *priv = TERRASERVER_MAP_SOURCE_PRIVATE(self);
	int type = priv->type;
	g_assert ( src->mode == VIK_COORD_UTM );

	if ( xmpp != ympp )
		return FALSE;

	dest->scale = mpp_to_scale ( xmpp, type );
	if ( ! dest->scale )
		return FALSE;

	dest->x = (gint)(((gint)(src->east_west))/(200*xmpp));
	dest->y = (gint)(((gint)(src->north_south))/(200*xmpp));
	dest->z = src->utm_zone;
	return TRUE;
}

static void
_mapcoord_to_center_coord ( VikMapSource *self, MapCoord *src, VikCoord *dest )
{
	// FIXME: slowdown here!
	gdouble mpp = scale_to_mpp ( src->scale );
	dest->mode = VIK_COORD_UTM;
	dest->utm_zone = src->z;
	dest->east_west = ((src->x * 200) + 100) * mpp;
	dest->north_south = ((src->y * 200) + 100) * mpp;
}

static int
_download ( VikMapSource *self, MapCoord *src, const gchar *dest_fn )
{
	g_return_val_if_fail(TERRASERVER_IS_MAP_SOURCE(self), FALSE);
	
	TerraserverMapSourcePrivate *priv = TERRASERVER_MAP_SOURCE_PRIVATE(self);  int res = -1;
	int type = priv->type;
	gchar *uri = g_strdup_printf ( "/tile.ashx?T=%d&S=%d&X=%d&Y=%d&Z=%d", type,
                                  src->scale, src->x, src->y, src->z );
	res = a_http_download_get_url ( TERRASERVER_SITE, uri, dest_fn, &terraserver_options );
	g_free ( uri );
	return res;
}

TerraserverMapSource *
terraserver_map_source_new_with_id (guint8 id, int type)
{
	TerraserverMapSource *ret = g_object_new(TERRASERVER_TYPE_MAP_SOURCE, NULL);
	
	/* TODO use property */
	vik_map_source_default_set_uniq_id(VIK_MAP_SOURCE_DEFAULT(ret), id);

	TerraserverMapSourcePrivate *priv = TERRASERVER_MAP_SOURCE_PRIVATE(ret);
	priv->type = type;
	return ret;
}