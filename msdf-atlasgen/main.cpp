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
#include <boost/variant.hpp>
#include <boost/program_options.hpp>
#include "msdfgen.h"
#include "msdfgen-ext.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include "freetype/freetype.h"
#include "binpacking.h"

using namespace msdfgen;
using namespace binpack;

enum class font_mode {
    msdf,
    sdf,
    pseudo_sdf
};

using msdf_bitmap    = Bitmap< FloatRGB >;
using sdf_bitmap     = Bitmap< float >;
using bitmap_variant = boost::variant< msdf_bitmap, sdf_bitmap >;

struct texture_dimensions
{
    size_t width;
    size_t height;
};

struct codepoint_range
{
    size_t begin;
    size_t end;
};

struct settings {
    std::vector< codepoint_range > codepoint_ranges;
    texture_dimensions tex_dims;

    size_t max_char_height;
    bool auto_height;

    size_t spacing;
    size_t smoothpixels;
    double range;

    font_mode mode;

    std::string font_file_name;
    std::string output_file_name;
};

struct char_info {
    char_info( int cp, boxd box, Shape s, double adv = 0)
        : codepoint( cp ), bbox( box ), shape( s), advance( adv )
    {}

    int codepoint;
    boxd  bbox;
    box<size_t> placement;
    Shape shape;
    Vector2 translation;
    double advance;
    bitmap_variant bitmap;
};


boxd bounds( const Shape& shape )
{
    double l = 500000;
    double r = -5000000;
    double t = -5000000;
    double b = 5000000;
    shape.bounds( l, b, r, t );

    return { l, b, r - l, t - b };
}

void write_description( std::vector< char_info >& charinfos, settings& cfg, double scaling )
{
    std::fstream desc(cfg.output_file_name+"_desc.c", std::ios::out | std::ios::trunc );
    size_t last_written = 0;

    desc << "// Generated by msdf-atlasgen, do not modify.\n";
    desc << "static const struct bitmap_glyph {\n";
    desc << "    unsigned int atlas_x, atlas_y;\n";
    desc << "    unsigned int atlas_w, atlas_h;\n";
    desc << "    unsigned int atlas_shift;\n";
    desc << "    float miny, maxy;\n";
    desc << "    float advance;\n";
    desc << "    float x_off;\n";
    desc << "} bitmap_chars[] = {\n";

    // sort chars for codepoint
    std::sort( charinfos.begin(), charinfos.end(), []( auto& a, auto& b ) {return a.codepoint < b.codepoint; } );


    for( const auto& info : charinfos ) {
        while( last_written < info.codepoint ) {
            desc << "{ 0, 0, 0, 0, 0, 0, 0, 0, 0 },\n";
            ++last_written;
        }

        desc << "{ "
            << info.placement.x() << ", "   // atlas_x
            << info.placement.y() << ", "   // atlas_y
            << info.placement.width()  << ", "  // atlas_width
            << info.placement.height() << ", "  // atlas_height
            << cfg.max_char_height - (info.placement.height() - 2 * cfg.smoothpixels) << ", "   // -.-
            << std::setiosflags( std::ios::fixed )
            << std::setprecision(4) << info.bbox.y()   << "f, " // width
            << std::setprecision(4) << info.bbox.top() << "f, "     // height
            << std::setprecision(4) << info.advance    << "f, "     // advance
            << std::setprecision(4) << info.bbox.x()   << "f },\n"; // x-offset

        last_written = info.codepoint+1;
    }

    desc << "};\n";
    desc << "static const int bitmap_chars_count = " << last_written+1 << ";\n";
}

void write_image( const std::vector< char_info >& charinfos, const settings& cfg )
{
    const size_t width  = cfg.tex_dims.width;
    const size_t height = cfg.tex_dims.height;

    bitmap_variant bitmap;
    switch( cfg.mode ) {
    case font_mode::sdf:
    case font_mode::pseudo_sdf:
        bitmap = sdf_bitmap( width, height );
        for( auto& ch : charinfos ) {
            boost::get<sdf_bitmap>(bitmap).place( ch.placement.x(), ch.placement.y(), boost::get<sdf_bitmap>(ch.bitmap) );
        }
        savePng( boost::get<sdf_bitmap>( bitmap ), (cfg.output_file_name + "_img.png").c_str() );
        break;
    case font_mode::msdf:
        bitmap = msdf_bitmap( width, height );
        for( auto& ch : charinfos ) {
            boost::get<msdf_bitmap>( bitmap ).place( ch.placement.x(), ch.placement.y(), boost::get<msdf_bitmap>( ch.bitmap ) );
        }
        savePng( boost::get<msdf_bitmap>( bitmap ), (cfg.output_file_name + "_img.png").c_str() );
        break;
    }



    std::fstream desc( cfg.output_file_name + "_img.c", std::ios::out | std::ios::trunc );

    desc << "// Generated by msdf-atlasgen, do not modify.\n"
         << "\n"
         << "static const struct {\n"
         << "    unsigned int width, height;\n"
         << "    unsigned int char_border;\n"
         << "    unsigned int spacing;\n"
         << "    unsigned char pixels["<< width << "*" << height;
    if(cfg.mode == font_mode::msdf) {
        desc << "*3";
    }
    desc << "];\n";
    desc << "} bitmap_font = {\n";

    desc << "    " << width << ", " << height << ", " << cfg.smoothpixels
         << ", "   << cfg.spacing << ", {\n";

    switch(cfg.mode) {
    case font_mode::msdf:
        {
            auto& msdf = boost::get<msdf_bitmap>(bitmap);
            for( size_t y = 0; y < height; ++y ) {
                for( size_t x = 0; x < width; ++x ) {
                    desc << clamp( int( msdf( x, y ).r * 0x100 ), 0xff ) << ",";
                    desc << clamp( int( msdf( x, y ).g * 0x100 ), 0xff ) << ",";
                    desc << clamp( int( msdf( x, y ).b * 0x100 ), 0xff ) << ",";
                }
                desc << "\n";
            }
        }
        break;
    case font_mode::sdf:
    case font_mode::pseudo_sdf:
        {
            auto& sdf = boost::get<sdf_bitmap>( bitmap );
            for( size_t y = 0; y < height; ++y ) {
                for( size_t x = 0; x < width; ++x ) {
                    desc << clamp( int( sdf( x, y ) * 0x100 ), 0xff ) << ",";
                }
                desc << "\n";
            }

        }
        break;
    }

    desc << "}};\n";
}

std::vector< char_info > read_shapes( FontHandle* font, const settings& cfg )
{
    std::vector< char_info > result;

    for( auto& range : cfg.codepoint_ranges ) {
        for( size_t i = range.begin; i<range.end; ++i ) {
            Shape shape;
            double advance;
            if( FT_Get_Char_Index( font->face, i ) != 0 && loadGlyph( shape, font, i, &advance ) ) {
                boxd thebox = bounds( shape );
                shape.normalize();
                if( thebox.width() > 0 ) {
                    result.emplace_back( i, thebox, shape, advance );
                }
            }
        }
    }

    return result;
}

std::vector< char_info > build_charset( FontHandle* font, const settings& cfg, double& scaling, bool build_images = true )
{
    auto charinfos = read_shapes( font, cfg );
    double maxheight = 0;

    for( auto& ch : charinfos ) {
        maxheight = std::max( ch.bbox.height(), maxheight );
    }

    scaling = double(cfg.max_char_height) / maxheight;

    for( auto& ch : charinfos ) {
        ch.bbox.scale( scaling );
        ch.advance *= scaling;

        float ceil_width  = ceil( ch.bbox.width() );
        float ceil_height = ceil( ch.bbox.height() );

        int width  = static_cast<int>( ceil_width  + 2*cfg.smoothpixels );
        int height = static_cast<int>( ceil_height + 2*cfg.smoothpixels );

        Vector2 offset( -ch.bbox.x() + cfg.smoothpixels, -ch.bbox.y() + cfg.smoothpixels );
        ch.translation = offset;
        ch.placement.width_  = width;
        ch.placement.height_ = height;

        if( build_images ) {
            switch( cfg.mode ) {
            case font_mode::msdf:
                ch.bitmap = Bitmap< FloatRGB >( width, height );
                edgeColoringSimple( ch.shape, 2.5 );
                generateMSDF( boost::get<msdf_bitmap>( ch.bitmap ), ch.shape, cfg.range, scaling, offset / scaling );
                break;
            case font_mode::sdf:
                ch.bitmap = Bitmap< float >( width, height );
                generateSDF( boost::get<sdf_bitmap>( ch.bitmap ), ch.shape, cfg.range, scaling, offset / scaling );
                break;
            case font_mode::pseudo_sdf:
                ch.bitmap = Bitmap< float >( width, height );
                generatePseudoSDF( boost::get<sdf_bitmap>( ch.bitmap ), ch.shape, cfg.range, scaling, offset / scaling );
                break;
            }
        }

    }

    return charinfos;
}

bool build_atlas( std::vector< char_info >& charinfos, settings& cfg )
{
    std::vector< box< size_t >* > placerefs;
    for( auto& ch : charinfos ) {
        placerefs.emplace_back( &ch.placement );
    }

    return bin_pack_max_rect( placerefs, cfg.tex_dims.width, cfg.tex_dims.height, cfg.spacing );
}

void run( FontHandle* font, settings& cfg )
{
    if( cfg.auto_height ) {
        size_t highest = cfg.tex_dims.height + 1;
        std::pair< size_t, size_t > range( 0, cfg.max_char_height );

        while( range.first != range.second ) {
            double scaling;
            std::cout << "trying " << range.second << '\n';
            cfg.max_char_height = range.second;
            auto charinfos = build_charset( font, cfg, scaling, false );

            std::cout << "packing atlas...";
            if( build_atlas( charinfos, cfg ) ) {
                range.first = range.second;
                range.second = min( range.first * 2, highest-1 );
            } else {
                highest = min( highest, range.second );
                range.second = range.first + (range.second-range.first)/2;
            }
        }
        cfg.max_char_height = range.first;
    }

    std::cout << "using char height " << cfg.max_char_height << ".\n";

    double scaling;
    std::cout << "building chars...\n";
    auto charinfos = build_charset( font, cfg, scaling );

    std::cout << "packing atlas...";
    if( !build_atlas( charinfos, cfg ) ) {
        std::cout << "error: packing atlas failed.\n";
        return;
    }

    write_description( charinfos, cfg, scaling );
    write_image( charinfos, cfg );
}

namespace po = boost::program_options;

std::istream& operator>>( std::istream& stream, font_mode& mode )
{
    std::string token;
    stream >> token;

    if( token == "msdf" ) {
        mode = font_mode::msdf;
    } else if( token == "sdf") {
        mode = font_mode::sdf;
    } else if( token == "psdf" ) {
        mode = font_mode::pseudo_sdf;
    }

    return stream;
}

std::ostream& operator<<( std::ostream& stream, const font_mode& mode )
{
    switch( mode ) {
    case font_mode::msdf:
        stream << "msdf";
        break;
    case font_mode::sdf:
        stream << "sdf";
        break;
    case font_mode::pseudo_sdf:
        stream << "psdf";
        break;
    }

    return stream;
}

std::istream& operator>>( std::istream& stream, codepoint_range& range )
{
    stream >> range.begin;
    if( stream.get() != '-' ) return stream;
    stream >> range.end;
    return stream;
}

std::ostream& operator<<( std::ostream& stream, const codepoint_range& range )
{
    stream << range.begin << '-' << range.end;
    return stream;
}

std::istream& operator >> ( std::istream& stream, texture_dimensions& dims )
{
    stream >> dims.width;
    if( stream.get() != 'x' ) return stream;
    stream >> dims.height;
    return stream;
}

std::ostream& operator<<( std::ostream& stream, const texture_dimensions& range )
{
    stream << range.width << 'x' << range.height;
    return stream;
}

std::ostream& operator<<( std::ostream& stream, const std::vector< codepoint_range >& ranges )
{
    stream << "{";
    for( auto& range : ranges ) {
        stream << range.begin << '-' << range.end;
    }
    stream << "}";

    return stream;
}

bool parse_options( int argc, char* argv[], settings& cfg )
{
    po::options_description desc( "Allowed options" );
    desc.add_options()
        ("help", "produce help message")
        ("code-range,C",    po::value< std::vector<codepoint_range> >(&cfg.codepoint_ranges)->default_value( {codepoint_range{ 0, 65536}} ), "unicode character point range exclusive")
        ("texture-size,T",  po::value< texture_dimensions >(&cfg.tex_dims)->default_value({2048, 2048}), "texture dimensions {width}x{height}" )
        ("mode,M",          po::value< font_mode >(&cfg.mode)->default_value( font_mode::msdf ),         "font mode { msdf, sdf, psdf }")
        ("char-height,L",   po::value< size_t >(&cfg.max_char_height)->default_value(32),                "maximum character height in texels")
        ("smooth-pixels,S", po::value< size_t >(&cfg.smoothpixels)->default_value(2),                    "smoothing-pixels")
        ("range,R",         po::value< double >(&cfg.range)->default_value(1.0),                         "smoothing-range")
        ("spacing,S",       po::value< size_t >(&cfg.spacing)->default_value(2),                         "inter-character spacing in texels")
        ("font,F",          po::value<std::string>(&cfg.font_file_name)->default_value("UbuntuMono-R.ttf"), "font file name")
        ("output-name,O",   po::value<std::string>(&cfg.output_file_name)->default_value("bitmap_font"), "base filename of output files")
        ("auto-height",     po::value<bool>(&cfg.auto_height)->default_value(false), "automatically determine best char height (might consume time)");

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

