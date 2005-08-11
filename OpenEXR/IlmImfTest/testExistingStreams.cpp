///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2004, Industrial Light & Magic, a division of Lucas
// Digital Ltd. LLC
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// *       Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// *       Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
// *       Neither the name of Industrial Light & Magic nor the names of
// its contributors may be used to endorse or promote products derived
// from this software without specific prior written permission. 
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////


#include <tmpDir.h>

#include <ImfRgbaFile.h>
#include <ImfTiledRgbaFile.h>
#include <ImfStdIO.h>
#include <ImfArray.h>

#include <stdio.h>
#include <assert.h>
#include <Iex.h>
#include <errno.h>

using namespace Imf;
using namespace Imath;
using namespace std;

namespace {

void
fillPixels1 (Array2D<Rgba> &pixels, int w, int h)
{
    for (int y = 0; y < h; ++y)
    {
	for (int x = 0; x < w; ++x)
	{
	    Rgba &p = pixels[y][x];

	    p.r = (x & 1);
	    p.g = ((x + y) & 1);
	    p.b = (y & 1);
	    p.a = (p.r + p.b + p.g) / 3.0;
	}
    }
}


void
fillPixels2 (Array2D<Rgba> &pixels, int w, int h)
{
    for (int y = 0; y < h; ++y)
    {
	for (int x = 0; x < w; ++x)
	{
	    Rgba &p = pixels[y][x];

	    p.r = (x & 2);
	    p.g = ((x + y) & 2);
	    p.b = (y & 2);
	    p.a = (p.r + p.b + p.g) / 3.0;
	}
    }
}


//-----------------------------------------------------------
// class MMIFStream -- a memory-mapped implementation of
// class IStream based on class std::ifstream
//-----------------------------------------------------------

class MMIFStream: public IStream
{
  public:

    //-------------------------------------------------------
    // A constructor that opens the file with the given name.
    // It reads the whole file into an internal buffer and
    // then immediately closes the file.
    //-------------------------------------------------------

    MMIFStream (const char fileName[]);

    virtual ~MMIFStream ();

    virtual bool        isMemoryMapped () const {return true;}

    virtual bool	read (char c[/*n*/], int n);
    virtual char*       readMemoryMapped (int n);
    virtual Int64	tellg () {return _pos;}
    virtual void	seekg (Int64 pos) {_pos = pos;}
    virtual void	clear () {}

  private:

    char*               _buffer;
    Int64               _length;
    Int64               _pos;
};



MMIFStream::MMIFStream (const char fileName[]):
    IStream (fileName),
    _buffer (0),
    _length (0),
    _pos (0)
{
#ifndef HAVE_IOS_BASE
    std::ifstream ifs (fileName, ios::binary | ios::in);
#else
    std::ifstream ifs (fileName, ios_base::binary);
#endif

    // get length of file:
    ifs.seekg (0, ios::end);
    _length = ifs.tellg();
    ifs.seekg (0, ios::beg);
    
    // allocate memory:
    _buffer = new char [_length];
    
    // read data as a block:
    ifs.read (_buffer, _length);
    
    ifs.close();
}


MMIFStream::~MMIFStream ()
{
    delete [] _buffer;
}


bool
MMIFStream::read (char c[/*n*/], int n)
{
    if ((_pos < 0 || _pos >= _length) && n != 0)
	throw Iex::InputExc ("Unexpected end of file.");
        
    Int64 n2 = n;
    bool retVal = true;
    if (_length - _pos <= n2)
    {
        n2 = _length - _pos;
        retVal = false;
    }

    memcpy (c, &(_buffer[_pos]), n2);
    _pos += n2;

    return retVal;
}


char*
MMIFStream::readMemoryMapped (int n)
{
    if (_pos < 0 || _pos >= _length)
	throw Iex::InputExc ("Unexpected end of file.");
       
    if (_pos + n > _length)
        throw Iex::InputExc ("Reading past end of file.");    

    char* retVal = &(_buffer[_pos]);
    _pos += n;
    return retVal;
}


void
writeReadScanLines (const char fileName[],
		    int width,
		    int height,
		    const Array2D<Rgba> &p1)
{
    //
    // Save a scanline-based RGBA image, but instead of
    // letting the RgbaOutputFile object open the file,
    // make the RgbaOutputFile object use an existing
    // StdOFStream.  Read the image back, using an
    // existing  SgdIFStream, and compare the pixels
    // with the original data.
    //

    cout << "scan-line based file: ";

    Header header (width, height);

    {
        cout << "writing";
	remove (fileName);
#ifndef HAVE_IOS_BASE
	std::ofstream os (fileName, ios::binary);
#else
	std::ofstream os (fileName, ios_base::binary);
#endif
	StdOFStream ofs (os, fileName);
	RgbaOutputFile out (ofs, header, WRITE_RGBA);
	out.setFrameBuffer (&p1[0][0], 1, width);
	out.writePixels (height);
    }

    {
        cout << ", reading";
#ifndef HAVE_IOS_BASE
	std::ifstream is (fileName, ios::binary|ios::in);
#else
	std::ifstream is (fileName, ios_base::binary);
#endif
	StdIFStream ifs (is, fileName);
	RgbaInputFile in (ifs);

	const Box2i &dw = in.dataWindow();
	int w = dw.max.x - dw.min.x + 1;
	int h = dw.max.y - dw.min.y + 1;
	int dx = dw.min.x;
	int dy = dw.min.y;

	Array2D<Rgba> p2 (h, w);
	in.setFrameBuffer (&p2[-dy][-dx], 1, w);
	in.readPixels (dw.min.y, dw.max.y);

        cout << ", comparing";
	for (int y = 0; y < h; ++y)
	{
	    for (int x = 0; x < w; ++x)
	    {
		assert (p2[y][x].r == p1[y][x].r);
		assert (p2[y][x].g == p1[y][x].g);
		assert (p2[y][x].b == p1[y][x].b);
		assert (p2[y][x].a == p1[y][x].a);
	    }
	}
    }
    
    {
        cout << ", reading (memory-mapped)";
	MMIFStream ifs (fileName);
	RgbaInputFile in (ifs);

	const Box2i &dw = in.dataWindow();
	int w = dw.max.x - dw.min.x + 1;
	int h = dw.max.y - dw.min.y + 1;
	int dx = dw.min.x;
	int dy = dw.min.y;

	Array2D<Rgba> p2 (h, w);
	in.setFrameBuffer (&p2[-dy][-dx], 1, w);
	in.readPixels (dw.min.y, dw.max.y);

        cout << ", comparing";
	for (int y = 0; y < h; ++y)
	{
	    for (int x = 0; x < w; ++x)
	    {
		assert (p2[y][x].r == p1[y][x].r);
		assert (p2[y][x].g == p1[y][x].g);
		assert (p2[y][x].b == p1[y][x].b);
		assert (p2[y][x].a == p1[y][x].a);
	    }
	}
    }
    
    cout << endl;

    remove (fileName);
}


void
writeReadTiles (const char fileName[],
		int width,
		int height,
		const Array2D<Rgba> &p1)
{
    //
    // Save a tiled RGBA image, but instead of letting
    // the TiledRgbaOutputFile object open the file, make
    // it use an existing StdOFStream.  Read the image back,
    // using an existing  SgdIFStream, and compare the pixels
    // with the original data.
    //

    cout << "tiled file: ";

    Header header (width, height);

    {
        cout << "writing";
	//remove (fileName);
#ifndef HAVE_IOS_BASE
	std::ofstream os (fileName, ios::binary);
#else
	std::ofstream os (fileName, ios_base::binary);
#endif
	StdOFStream ofs (os, fileName);
	TiledRgbaOutputFile out (ofs, header, WRITE_RGBA, 20, 20, ONE_LEVEL);
	out.setFrameBuffer (&p1[0][0], 1, width);
        out.writeTiles (0, out.numXTiles() - 1, 0, out.numYTiles() - 1);
    }

    {
        cout << ", reading";
#ifndef HAVE_IOS_BASE
	std::ifstream is (fileName, ios::binary|ios::in);
#else
	std::ifstream is (fileName, ios_base::binary);
#endif
	StdIFStream ifs (is, fileName);
	TiledRgbaInputFile in (ifs);

	const Box2i &dw = in.dataWindow();
	int w = dw.max.x - dw.min.x + 1;
	int h = dw.max.y - dw.min.y + 1;
	int dx = dw.min.x;
	int dy = dw.min.y;

	Array2D<Rgba> p2 (h, w);
	in.setFrameBuffer (&p2[-dy][-dx], 1, w);
        in.readTiles (0, in.numXTiles() - 1, 0, in.numYTiles() - 1);

        cout << ", comparing";
	for (int y = 0; y < h; ++y)
	{
	    for (int x = 0; x < w; ++x)
	    {
		assert (p2[y][x].r == p1[y][x].r);
		assert (p2[y][x].g == p1[y][x].g);
		assert (p2[y][x].b == p1[y][x].b);
		assert (p2[y][x].a == p1[y][x].a);
	    }
	}
    }
    
    {
        cout << ", reading (memory-mapped)";
	MMIFStream ifs (fileName);
	TiledRgbaInputFile in (ifs);

	const Box2i &dw = in.dataWindow();
	int w = dw.max.x - dw.min.x + 1;
	int h = dw.max.y - dw.min.y + 1;
	int dx = dw.min.x;
	int dy = dw.min.y;

	Array2D<Rgba> p2 (h, w);
	in.setFrameBuffer (&p2[-dy][-dx], 1, w);
        in.readTiles (0, in.numXTiles() - 1, 0, in.numYTiles() - 1);

        cout << ", comparing";
	for (int y = 0; y < h; ++y)
	{
	    for (int x = 0; x < w; ++x)
	    {
		assert (p2[y][x].r == p1[y][x].r);
		assert (p2[y][x].g == p1[y][x].g);
		assert (p2[y][x].b == p1[y][x].b);
		assert (p2[y][x].a == p1[y][x].a);
	    }
	}
    }
    
    cout << endl;

    remove (fileName);
}


} // namespace


void
testExistingStreams ()
{
    try
    {
	cout << "Testing reading and writing using existing streams" << endl;

	const int W = 119;
	const int H = 237;

	Array2D<Rgba> p1 (H, W);

	fillPixels1 (p1, W, H);
	writeReadScanLines (IMF_TMP_DIR "imf_test_streams.exr", W, H, p1);

	fillPixels2 (p1, W, H);
	writeReadTiles (IMF_TMP_DIR "imf_test_streams2.exr", W, H, p1);

	cout << "ok\n" << endl;
    }
    catch (const std::exception &e)
    {
	cerr << "ERROR -- caught exception: " << e.what() << endl;
	assert (false);
    }
}

