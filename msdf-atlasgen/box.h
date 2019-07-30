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

#ifndef ATLASGEN_BOX_H_INCLUDED__
#define ATLASGEN_BOX_H_INCLUDED__

#include <vector>

template< typename T >
struct box {
    T top() const { return y + height; }
    T right() const { return x + width; }

    void scale( T val ) {
        x *= val;
        y *= val;
        width  *= val;
        height *= val;
    }

    T x, y, width, height;
};

bool overlap( const box<size_t>& a, const box<size_t>& b, size_t spacing ) {
    return !(a.right() + spacing <= b.x || b.right() + spacing <= a.x || a.top() + spacing <= b.y || b.top() + spacing <= a.y);
}

void make_splits( box<size_t> a, box<size_t> b, std::vector< box< size_t > >& result, size_t spacing ) {
    result.clear();

    if( a.x + spacing < b.x ) {
        result.push_back( box< size_t >{ a.x, a.y, b.x - a.x - spacing, a.height } );
    }

    if( a.right() > b.right() + spacing ) {
        result.push_back( box< size_t >{ b.right() + spacing, a.y, a.right() - b.right() - spacing, a.height } );
    }

    if( a.top() > b.top() + spacing ) {
        result.push_back( box< size_t >{ a.x, b.top() + spacing, a.width, a.top() - b.top() - spacing } );
    }

    if( a.y + spacing < b.y ) {
        result.push_back( box< size_t >{ a.x, a.y, a.width, b.y - a.y - spacing } );
    }
}

bool can_fit( const box<size_t>& a, const box<size_t>& b ) {
    return a.width >= b.width && a.height >= b.height;
}

bool contains( const box<size_t>& a, const box<size_t>& b ) {
    return b.x >= a.x && b.y >= a.y && b.right() <= a.right() && b.top() <= a.top();
}

bool operator==( const box<size_t>& a, const box<size_t>& b ) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

#endif
