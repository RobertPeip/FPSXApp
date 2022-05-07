#pragma once
#include "types.h"
#include "Gpu.h"

struct TriangleHalf
{
    UInt64 xCoord[2];
    UInt64 xStep[2];

    Int32 yCoord;
    Int32 yBound;

    bool decMode;
};

class Polygon
{
public:
	void drawPolygon(Vertex v0, Vertex v1, Vertex v2, bool shaded, bool textured, bool transparent, bool rawTexture, bool dithering, bool useBilinear);
    void drawSpan(Int32 y, Int32 xStart, Int32 xBound, ColorDeltas cD, ColorGroup cG, bool shaded, bool textured, bool transparent, bool rawTexture, bool dithering, bool useBilinear);
	Int64 makePolyXFPStep(Int32 dx, Int32 dy);
};
extern Polygon POLYGON;