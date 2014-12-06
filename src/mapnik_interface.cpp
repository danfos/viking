/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2015, Rob Norris <rw_norris@hotmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <mapnik/version.hpp>
#include <mapnik/map.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/agg_renderer.hpp>
#include <mapnik/load_map.hpp>
#include <mapnik/graphics.hpp>
#include <mapnik/image_util.hpp>

#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <stdlib.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "mapnik_interface.h"
#include "globals.h"

#if MAPNIK_VERSION < 200000
#include <mapnik/envelope.hpp>
#define image_32 Image32
#define image_data_32 ImageData32
#define box2d Envelope
#define zoom_to_box zoomToBox
#else
#include <mapnik/box2d.hpp>
#endif

#define MAPNIK_INTERFACE_TYPE            (mapnik_interface_get_type ())
#define MAPNIK_INTERFACE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MAPNIK_INTERFACE_TYPE, MapnikInterface))
#define MAPNIK_INTERFACE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MAPNIK_INTERFACE_TYPE, MapnikInterfaceClass))
#define IS_MAPNIK_INTERFACE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MAPNIK_INTERFACE_TYPE))
#define IS_MAPNIK_INTERFACE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), MAPNIK_INTERFACE_TYPE))

typedef struct _MapnikInterfaceClass MapnikInterfaceClass;
typedef struct _MapnikInterface MapnikInterface;

GType mapnik_interface_get_type ();
struct _MapnikInterfaceClass
{
	GObjectClass object_class;
};

static void mapnik_interface_class_init ( MapnikInterfaceClass *mic )
{
}

static void mapnik_interface_init ( MapnikInterface *mi )
{
}

struct _MapnikInterface {
	GObject obj;
	mapnik::Map *myMap;
};

G_DEFINE_TYPE (MapnikInterface, mapnik_interface, G_TYPE_OBJECT)

// Can't change prj after init - but ATM only support drawing in Spherical Mercator
static mapnik::projection prj( mapnik::MAPNIK_GMERC_PROJ );

MapnikInterface* mapnik_interface_new ()
{
	MapnikInterface* mi = MAPNIK_INTERFACE ( g_object_new ( MAPNIK_INTERFACE_TYPE, NULL ) );
	mi->myMap = new mapnik::Map;
	return mi;
}

void mapnik_interface_free (MapnikInterface* mi)
{
	if ( mi )
		delete mi->myMap;
	g_object_unref ( G_OBJECT(mi) );
}

/**
 * mapnik_interface_initialize:
 */
void mapnik_interface_initialize (const char *plugins_dir, const char* font_dir, int font_dir_recurse)
{
	g_debug ("using mapnik version %s", MAPNIK_VERSION_STRING );
	try {
		if ( plugins_dir )
#if MAPNIK_VERSION >= 200200
			mapnik::datasource_cache::instance().register_datasources(plugins_dir);
			// FUTURE: Make this an 'about' property
			if ( vik_verbose ) {
				//for (auto& name in mapnik::datasource_cache::instance().plugin_names()) std::cout << name << '\n';// C++11
				std::vector<std::string> plugins = mapnik::datasource_cache::instance().plugin_names();
				for (int nn = 0; nn < plugins.size(); nn++ )
					g_printf ("mapnik enabled plugin: %s\n", plugins[nn].c_str());
			}
#else
			mapnik::datasource_cache::instance()->register_datasources(plugins_dir);
#endif
		if ( font_dir )
			if ( ! mapnik::freetype_engine::register_fonts(font_dir, font_dir_recurse ? true : false) )
				g_warning ("%s: No fonts found", __FUNCTION__);
		g_debug ("mapnik font faces found: %d", mapnik::freetype_engine::face_names().size());
	} catch (std::exception const& ex) {
		g_warning ("An error occurred while initialising mapnik: %s", ex.what());
	} catch (...) {
		g_warning ("An unknown error occurred while initialising mapnik");
	}
}

/**
 * mapnik_interface_load_map_file:
 *
 * Returns 0 on success.
 */
int mapnik_interface_load_map_file ( MapnikInterface* mi,
                                     const gchar *filename,
                                     guint width,
                                     guint height )
{
	if ( !mi ) return 1;
	try {
		mi->myMap->remove_all(); // Support reloading
		mapnik::load_map(*mi->myMap, filename);

		mi->myMap->resize(width,height);
		mi->myMap->set_srs ( mapnik::MAPNIK_GMERC_PROJ ); // ONLY WEB MERCATOR output supported ATM

		// IIRC This size is the number of pixels outside the tile to be considered so stuff is shown (i.e. particularly labels)
		// Only set buffer size if the buffer size isn't explicitly set in the mapnik stylesheet.
		// Alternatively render a bigger 'virtual' tile and then only use the appropriate subset
		if (mi->myMap->buffer_size() == 0) {
			mi->myMap->set_buffer_size((width+height/4)); // e.g. 128 for a 256x256 image.
		}

		g_debug ("%s layers: %d", __FUNCTION__, mi->myMap->layer_count() );
	} catch (std::exception const& ex) {
		g_debug ("An error occurred while loading the mapnik config '%s': %s", filename, ex.what());
		return 2;
	} catch (...) {
		g_debug ("An unknown error occurred while loading the mapnik config '%s': %s", filename );
		return 3;
	}
	return 0;
}

// These two functions copied from gpsdrive 2.11
/**
 * convert the color channel
 */
inline unsigned char
convert_color_channel (unsigned char Source, unsigned char Alpha)
{
	return Alpha ? ((Source << 8) - Source) / Alpha : 0;
}

/**
 * converting argb32 to gdkpixbuf
 */
static void
convert_argb32_to_gdkpixbuf_data (unsigned char const *Source, unsigned int width, unsigned int height, unsigned char *Dest)
{
	unsigned char const *SourcePixel = Source;
	unsigned char *DestPixel = Dest;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			DestPixel[0] = convert_color_channel(SourcePixel[0], SourcePixel[3]);
			DestPixel[1] = convert_color_channel(SourcePixel[1], SourcePixel[3]);
			DestPixel[2] = convert_color_channel(SourcePixel[2], SourcePixel[3]);
			DestPixel += 3;
			SourcePixel += 4;
		}
	}
}

/**
 * mapnik_interface_render:
 *
 * Returns a #GdkPixbuf of the specified area. #GdkPixbuf may be NULL
 */
GdkPixbuf* mapnik_interface_render ( MapnikInterface* mi, double lat_tl, double lon_tl, double lat_br, double lon_br )
{
	if ( !mi ) return NULL;

	// Note prj & bbox want stuff in lon,lat order!
	double p0x = lon_tl;
	double p0y = lat_tl;
	double p1x = lon_br;
	double p1y = lat_br;

	// Convert into projection coordinates for bbox
	prj.forward(p0x, p0y);
	prj.forward(p1x, p1y);

	GdkPixbuf *pixbuf = NULL;
	try {
		unsigned width  = mi->myMap->width();
		unsigned height = mi->myMap->height();
		mapnik::image_32 image(width,height);
		mapnik::box2d<double> bbox(p0x, p0y, p1x, p1y);
		mi->myMap->zoom_to_box(bbox);
		// FUTURE: option to use cairo / grid renderers?
		mapnik::agg_renderer<mapnik::image_32> render(*mi->myMap,image);
		render.apply();

		if ( image.painted() ) {
			unsigned char *ImageRawDataPtr = (unsigned char *) g_malloc(width * 3 * height);
			if (!ImageRawDataPtr)
				return NULL;
			convert_argb32_to_gdkpixbuf_data(image.raw_data(), width, height, ImageRawDataPtr);
			pixbuf = gdk_pixbuf_new_from_data(ImageRawDataPtr, GDK_COLORSPACE_RGB, FALSE, 8, width, height, width * 3, NULL, NULL);
		}
		else
			g_warning ("%s not rendered", __FUNCTION__ );
	}
	catch (const std::exception & ex) {
		g_warning ("An error occurred while rendering: %s", ex.what());
	} catch (...) {
		g_warning ("An unknown error occurred while rendering");
	}

	return pixbuf;
}
