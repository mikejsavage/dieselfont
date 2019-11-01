// MIT License
//
// Copyright( c ) 2016 Michael Steinberg
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files( the "Software" ), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// uses msdfgen (multichannel signed distance field) by Viktor Chlumsky
// (Github Repo: https://github.com/Chlumsky/msdfgen)
// to create a texture atlas with accompanying description files.

#include <algorithm>
#include <cassert>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <boost/program_options.hpp>
#include "msdfgen.h"
#include "msdfgen-ext.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include "freetype/freetype.h"
#include "binpacking.h"

#include "types.h"
#include "serialization.h"

using namespace msdfgen;

struct texture_dimensions {
	size_t width, height;
};

enum class tex_rect_alignment {
	lower_left,
	upper_left,
	upper_right,
	lower_right,
};

struct settings {
	texture_dimensions tex_dims;

	size_t max_char_height;
	bool auto_height;

	size_t spacing;
	size_t smoothpixels;
	double range;

	std::string font_file_name;
	std::string output_file_name;
};

struct char_info {
	char_info( int cp, box< double > box, Shape s, double adv )
		: codepoint( cp ), bbox( box ), shape( s), advance( adv )
	{}

	int codepoint;
	box< double > bbox;
	box<size_t> placement;
	Shape shape;
	Vector2 translation;
	double advance;
	Bitmap< FloatRGB > bitmap;
};

box< double > bounds( const Shape& shape )
{
	double l = 500000;
	double r = -5000000;
	double t = -5000000;
	double b = 5000000;
	shape.bounds( l, b, r, t );

	return { l, b, r - l, t - b };
}

struct Glyph {
	MinMax2 bounds;
	MinMax2 uv_bounds;
	float advance;
};

struct Font {
	float glyph_padding;
	float pixel_range;
	float ascent;

	Glyph glyphs[ 256 ];
};

static void Serialize( SerializationBuffer * buf, Font & font ) {
	*buf & font.glyph_padding & font.pixel_range & font.ascent;
	for( Glyph & glyph : font.glyphs ) {
		*buf & glyph.bounds & glyph.uv_bounds & glyph.advance;
	}
}

static void write_specification( std::vector< char_info >& charinfos, const settings& cfg, double scaling ) {
	std::fstream desc(cfg.output_file_name+".msdf", std::ios::out | std::ios::binary | std::ios::trunc );

	auto max_y = max_element( charinfos.begin(), charinfos.end(), [](auto& a, auto& b) {return a.bbox.top() < b.bbox.top();});
	auto min_y = min_element( charinfos.begin(), charinfos.end(), [](auto& a, auto& b) {return a.bbox.y < b.bbox.y;});
	float scale = 1.0f / ( max_y->bbox.top() - min_y->bbox.y );

	char buf[ sizeof( Font ) ];
	Font font = { };
	font.glyph_padding = cfg.smoothpixels * scale;
	font.pixel_range = scaling * cfg.range;
	font.ascent = scale * max_y->bbox.top();

	for( const char_info & info : charinfos ) {
		Glyph & glyph = font.glyphs[ info.codepoint ];

		glyph.bounds.mins.x = scale * info.bbox.x;
		glyph.bounds.mins.y = -scale * info.bbox.top();
		glyph.bounds.maxs.x = scale * info.bbox.right();
		glyph.bounds.maxs.y = -scale * info.bbox.y;

		glyph.uv_bounds.mins.x = ( info.placement.x + 0.5f ) / cfg.tex_dims.width;
		glyph.uv_bounds.mins.y = 1.0f - ( info.placement.top() + 0.5f ) / cfg.tex_dims.height;
		glyph.uv_bounds.maxs.x = ( info.placement.right() + 0.5f ) / cfg.tex_dims.width;
		glyph.uv_bounds.maxs.y = 1.0f - ( info.placement.y + 0.5f ) / cfg.tex_dims.height;

		glyph.advance = scale * info.advance;
	}

	bool ok = Serialize( font, buf, sizeof( buf ) );
	assert( ok );
	desc.write( buf, sizeof( buf ) );
}

void write_image( const std::vector< char_info >& charinfos, const settings& cfg ) {
	const size_t width  = cfg.tex_dims.width;
	const size_t height = cfg.tex_dims.height;

	Bitmap< FloatRGB > bitmap = Bitmap< FloatRGB >( width, height );
	for( auto& ch : charinfos ) {
		bitmap.place( ch.placement.x, ch.placement.y, ch.bitmap );
	}
	savePng( bitmap, (cfg.output_file_name + ".png").c_str() );
}

std::vector< char_info > read_shapes( FontHandle* font, const settings& cfg ) {
	std::vector< char_info > result;

	for( uint32_t i = 0; i <= 255; ++i ) {
		Shape shape;
		double advance;
		if( i == ' ' || i == '\t' ) {
			double spaceAdvance, tabAdvance;
			if( !getFontWhitespaceWidth( spaceAdvance, tabAdvance, font ) )
				continue;

			result.emplace_back( i, box< double >(), Shape(), i == ' ' ? spaceAdvance : tabAdvance );
		}

		if( FT_Get_Char_Index( font->face, i ) != 0 && loadGlyph( shape, font, i, &advance ) ) {
			box< double > thebox = bounds( shape );
			shape.normalize();
			if( thebox.width > 0 ) {
				result.emplace_back( i, thebox, shape, advance );
			}
		}
	}

	return result;
}

std::vector< char_info > build_charset( FontHandle* font, const settings& cfg, double& scaling ) {
	auto charinfos = read_shapes( font, cfg );
	double maxheight = 0;

	for( auto& ch : charinfos ) {
		maxheight = std::max( ch.bbox.height, maxheight );
	}

	scaling = double(cfg.max_char_height) / maxheight;

	for( auto& ch : charinfos ) {
		ch.bbox.scale( scaling );
		ch.advance *= scaling;

		float ceil_width  = ceil( ch.bbox.width );
		float ceil_height = ceil( ch.bbox.height );

		int width  = static_cast<int>( ceil_width  + 2*cfg.smoothpixels );
		int height = static_cast<int>( ceil_height + 2*cfg.smoothpixels );

		Vector2 offset( -ch.bbox.x + cfg.smoothpixels, -ch.bbox.y + cfg.smoothpixels );
		ch.translation = offset;
		ch.placement.width  = width;
		ch.placement.height = height;

		ch.bitmap = Bitmap< FloatRGB >( width, height );
		edgeColoringSimple( ch.shape, 2.5 );
		generateMSDF( ch.bitmap, ch.shape, cfg.range, scaling, offset / scaling );
	}

	return charinfos;
}

bool build_atlas( std::vector< char_info >& charinfos, settings& cfg ) {
	std::vector< box< size_t >* > placerefs;
	for( auto& ch : charinfos ) {
		placerefs.emplace_back( &ch.placement );
	}

	return bin_pack_max_rect( placerefs, cfg.tex_dims.width, cfg.tex_dims.height, cfg.spacing );
}

void run( FontHandle* font, settings& cfg ) {
	std::cout << "using char height " << cfg.max_char_height << ".\n";

	double scaling;
	std::cout << "building chars...\n";
	auto charinfos = build_charset( font, cfg, scaling );

	std::cout << "packing atlas...";
	if( !build_atlas( charinfos, cfg ) ) {
		std::cout << "error: packing atlas failed.\n";
		return;
	}

	write_specification( charinfos, cfg, scaling );
	write_image( charinfos, cfg );
}

namespace po = boost::program_options;

std::istream& operator >> ( std::istream& stream, texture_dimensions& dims ) {
	stream >> dims.width;
	if( stream.get() != 'x' ) return stream;
	stream >> dims.height;
	return stream;
}

std::ostream& operator<<( std::ostream& stream, const texture_dimensions& range ) {
	stream << range.width << 'x' << range.height;
	return stream;
}

bool parse_options( int argc, char* argv[], settings& cfg ) {
	po::options_description desc( "Allowed options" );
	desc.add_options()
		("help", "produce help message")
		("texture-size,T",  po::value< texture_dimensions >(&cfg.tex_dims)->default_value({2048, 2048}), "texture dimensions {width}x{height}" )
		("char-height,L",   po::value< size_t >(&cfg.max_char_height)->default_value(32),                "maximum character height in texels")
		("smooth-pixels,S", po::value< size_t >(&cfg.smoothpixels)->default_value(2),                    "smoothing-pixels")
		("range,R",         po::value< double >(&cfg.range)->default_value(1.0),                         "smoothing-range")
		("spacing,S",       po::value< size_t >(&cfg.spacing)->default_value(2),                         "inter-character spacing in texels")
		("font,F",          po::value<std::string>(&cfg.font_file_name)->required(), "font file name")
		("output-name,O",   po::value<std::string>(&cfg.output_file_name)->required(), "base filename of output files")
		("auto-height",     po::value<bool>(&cfg.auto_height)->default_value(false), "automatically determine best char height (might consume time)")
		;

	po::variables_map vm;
	po::store( po::parse_command_line( argc, argv, desc ), vm );
	po::notify( vm );

	if(vm.count("help")) {
		desc.print(std::cout);
		return false;
	}

	return true;
}

int main( int argc, char* argv[]) {
	settings cfg;
	try {
		if( !parse_options( argc, argv, cfg ) ) {
			return 0;
		}
	} catch( po::error& err ) {
		std::cout << err.what() << "\n";
		return 0;
	}

	FreetypeHandle *ft = initializeFreetype();
	if( ft ) {
		FontHandle *font = loadFont( ft, cfg.font_file_name.c_str() );
		if( font ) {
			run( font, cfg );

			destroyFont( font );
		} else {
			std::cout << "Could not open font \"" << cfg.font_file_name << "\".\n";
		}
		deinitializeFreetype( ft );
	}

	return 0;
}

