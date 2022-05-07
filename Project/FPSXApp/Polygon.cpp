using namespace std;

#include "Polygon.h"
#include "GPU.h"
#include "PSXRegs.h"
#include "GPU_Timing.h"
#include "Memory.h"

Polygon POLYGON;

void Polygon::drawPolygon(Vertex v0, Vertex v1, Vertex v2, bool shaded, bool textured, bool transparent, bool rawTexture, bool dithering, bool useBilinear)
{
#ifdef FPGACOMPATIBLE
	if (v0.y <= v2.y && v2.y <= v1.y)
	{
		std::swap(v2, v1);
	}
	else if (v1.y <= v0.y && v0.y <= v2.y)
	{
		std::swap(v1, v0);
	}
	else if (v1.y <= v2.y && v2.y <= v0.y)
	{
		std::swap(v2, v0);
		std::swap(v1, v0);
	}
	else if (v2.y <= v0.y && v0.y <= v1.y)
	{
		std::swap(v2, v0);
		std::swap(v1, v2);
	}
	else if (v2.y <= v1.y && v1.y <= v0.y)
	{
		std::swap(v2, v0);
	}
#else
	if (v2.y < v1.y)
	{
		std::swap(v2, v1);
	}
	if (v1.y < v0.y)
	{
		std::swap(v1, v0);
	}
	if (v2.y < v1.y)
	{
		std::swap(v2, v1);
	}
#endif

	if (v2.y == v0.y) return;

	byte coreVertex = 0;

	if (v1.x <= v0.x)
	{
		coreVertex = 1;
		if (v2.x <= v1.x) coreVertex = 2;
	}
	else if (v2.x < v0.x)	
	{
		coreVertex = 2;
	}

	// culling
	if (v0.y == v2.y) return;

	if (std::abs(v2.x - v0.x) >= 0x400) return;
	if (std::abs(v2.x - v1.x) >= 0x400) return;
	if (std::abs(v1.x - v0.x) >= 0x400) return;
	if (std::abs(v2.y - v0.y) >= 0x200) return;

	Int64 baseCoord = ((Int64)v0.x << 32) + ((UInt64)1 << 32) - (1 << 11);
	Int64 baseStep = makePolyXFPStep(v2.x - v0.x, v2.y - v0.y);

	Int64 boundCoordUs;
	byte rightFacing = 0;
	if (v1.y == v0.y)
	{
		boundCoordUs = 0;
		if (v1.x > v0.x) rightFacing = 1;
	}
	else
	{
		boundCoordUs = makePolyXFPStep(v1.x - v0.x, v1.y - v0.y);
		if (boundCoordUs > baseStep) rightFacing = 1;
	}

	Int64 boundCoordLs;
	if (v2.y == v1.y)
	{
		boundCoordLs = 0;
	}
	else
	{
		boundCoordLs = makePolyXFPStep(v2.x - v1.x, v2.y - v1.y);
	}

	ColorDeltas cDelta;
	cDelta.drDx = 0;
	Int32 denom = ((v1.x - v0.x) * (v2.y - v1.y)) - ((v2.x - v1.x) * (v1.y - v0.y));
	if (denom == 0) return; // skip whole polygon?

	if (shaded)
	{
		cDelta.drDx = ((((v1.r - v0.r) * (v2.y - v1.y)) - ((v2.r - v1.r) * (v1.y - v0.y))) * 4096 / denom);
		cDelta.drDy = ((((v1.x - v0.x) * (v2.r - v1.r)) - ((v2.x - v1.x) * (v1.r - v0.r))) * 4096 / denom);

		cDelta.dgDx = ((((v1.g - v0.g) * (v2.y - v1.y)) - ((v2.g - v1.g) * (v1.y - v0.y))) * 4096 / denom);
		cDelta.dgDy = ((((v1.x - v0.x) * (v2.g - v1.g)) - ((v2.x - v1.x) * (v1.g - v0.g))) * 4096 / denom);

		cDelta.dbDx = ((((v1.b - v0.b) * (v2.y - v1.y)) - ((v2.b - v1.b) * (v1.y - v0.y))) * 4096 / denom);
		cDelta.dbDy = ((((v1.x - v0.x) * (v2.b - v1.b)) - ((v2.x - v1.x) * (v1.b - v0.b))) * 4096 / denom);
	}

	if  (textured)
	{
		cDelta.duDx = ((((v1.u - v0.u) * (v2.y - v1.y)) - ((v2.u - v1.u) * (v1.y - v0.y))) * 4096 / denom);
		cDelta.duDy = ((((v1.x - v0.x) * (v2.u - v1.u)) - ((v2.x - v1.x) * (v1.u - v0.u))) * 4096 / denom);
	
		cDelta.dvDx = ((((v1.v - v0.v) * (v2.y - v1.y)) - ((v2.v - v1.v) * (v1.y - v0.y))) * 4096 / denom);
		cDelta.dvDy = ((((v1.x - v0.x) * (v2.v - v1.v)) - ((v2.x - v1.x) * (v1.v - v0.v))) * 4096 / denom);

		//if (cDelta.duDx == 0x1000 && cDelta.duDy == 0 && cDelta.dvDx == 0 && cDelta.dvDy == 0x1000)
		//{
		//	//useBilinear = false;
		//}
		//else 
		if (cDelta.duDy == 0 && cDelta.dvDx == 0)
		{
			useBilinear = false;
		}
	}

	Vertex vertices[3] = { v0, v1, v2 };

	ColorGroup colorWork;
	if (textured)
	{
		colorWork.u = (((vertices[coreVertex].u) << 12) + 2048);
		colorWork.v = (((vertices[coreVertex].v) << 12) + 2048);
	}

	colorWork.r = (((vertices[coreVertex].r) << 12) + 2048);
	colorWork.g = (((vertices[coreVertex].g) << 12) + 2048);
	colorWork.b = (((vertices[coreVertex].b) << 12) + 2048);

	GPU.addColorDx(cDelta, colorWork, -vertices[coreVertex].x, shaded, textured);
	GPU.addColorDy(cDelta, colorWork, -vertices[coreVertex].y, shaded, textured);

	TriangleHalf tParts[2];
	byte vo = 0;
	byte vp = 0;
	if (coreVertex > 0) vo = 1;
	if (coreVertex == 2) vp = 1;

#ifdef FPGACOMPATIBLE
	// half 0
	tParts[0].yCoord = vertices[vo].y;
	tParts[0].yBound = vertices[1 - vo].y;
	tParts[0].xCoord[rightFacing] = ((Int64)vertices[vo].x << 32) + ((UInt64)1 << 32) - (1 << 11);
	tParts[0].xStep[rightFacing] = boundCoordUs;
	tParts[0].xCoord[1 - rightFacing] = baseCoord + (vertices[vo].y - vertices[0].y) * baseStep;
	tParts[0].xStep[1 - rightFacing] = baseStep;
	tParts[0].decMode = vo;

	// half 1
	tParts[1].yCoord = vertices[1 + vp].y;
	tParts[1].yBound = vertices[2 - vp].y;
	tParts[1].xCoord[rightFacing] = ((Int64)vertices[1 + vp].x << 32) + ((UInt64)1 << 32) - (1 << 11);
	tParts[1].xStep[rightFacing] = boundCoordLs;
	tParts[1].xCoord[1 - rightFacing] = baseCoord + (vertices[1 + vp].y - vertices[0].y) * baseStep;
	tParts[1].xStep[1 - rightFacing] = baseStep;
	tParts[1].decMode = vp;
#else
	// half 0
	tParts[vo].yCoord = vertices[vo].y;
	tParts[vo].yBound = vertices[1 - vo].y;
	tParts[vo].xCoord[rightFacing] = ((Int64)vertices[vo].x << 32) + ((UInt64)1 << 32) - (1 << 11);
	tParts[vo].xStep[rightFacing] = boundCoordUs;
	tParts[vo].xCoord[1 - rightFacing] = baseCoord + (vertices[vo].y - vertices[0].y) * baseStep;
	tParts[vo].xStep[1 - rightFacing] = baseStep;
	tParts[vo].decMode = vo;
	
	// half 1
	tParts[1 - vo].yCoord = vertices[1 + vp].y;
	tParts[1 - vo].yBound = vertices[2 - vp].y;
	tParts[1 - vo].xCoord[rightFacing] = ((Int64)vertices[1 + vp].x << 32) + ((UInt64)1 << 32) - (1 << 11);
	tParts[1 - vo].xStep[rightFacing] = boundCoordLs;
	tParts[1 - vo].xCoord[1 - rightFacing] = baseCoord + (vertices[1 + vp].y - vertices[0].y) * baseStep;
	tParts[1 - vo].xStep[1 - rightFacing] = baseStep;
	tParts[1 - vo].decMode = vp;
#endif

	for (int i = 0; i < 2; i++)
	{
		Int32 yi = tParts[i].yCoord;
		Int32 yb = tParts[i].yBound;
		UInt64 lc = tParts[i].xCoord[0];
		UInt64 ls = tParts[i].xStep[0];
		UInt64 rc = tParts[i].xCoord[1];
		UInt64 rs = tParts[i].xStep[1];

		if (tParts[i].decMode)
		{
			while (yi > yb)
			{
				yi--;
				lc -= ls;
				rc -= rs;

				Int32 y = yi & 0x7FF;
				if (y & 0x400) y = -1024 + (y & 0x3FF);
				//if (y & 0x400) y |= 0xF800; else y &= 0x3FF;

				if (y < GPU.drawingAreaTop) break;
				if (y > GPU.drawingAreaBottom) continue;

				drawSpan(y, lc >> 32, rc >> 32, cDelta, colorWork, shaded, textured, transparent, rawTexture, dithering, useBilinear);
			}
		}
		else
		{
			while (yi < yb)
			{
				Int32 y = yi & 0x7FF;
				if (y & 0x400) y = -1024 + (y & 0x3FF);
				//if (y & 0x400) y |= 0xF800; else y &= 0x3FF;

				if (y > GPU.drawingAreaBottom) break;

				if (y >= GPU.drawingAreaTop)
				{
					drawSpan(y, lc >> 32, rc >> 32, cDelta, colorWork, shaded, textured, transparent, rawTexture, dithering, useBilinear);
				}

				yi++;
				lc += ls;
				rc += rs;
			}
		}
	}

}

void Polygon::drawSpan(Int32 y, Int32 xStart, Int32 xBound, ColorDeltas cD, ColorGroup cG, bool shaded, bool textured, bool transparent, bool rawTexture, bool dithering, bool useBilinear)
{
	bool interlaceDrawing = ((PSXRegs.GPUSTAT & 0x00480400) == 0x00480000);
	if (interlaceDrawing && (GPU_Timing.activeLineLSB == (y & 1))) return;

	Int32 x_ig_adjust = xStart;
	Int32 w = xBound - xStart;
	Int32 x = xStart & 0xFFF;

	if (xStart < 0) x = 0 - (std::abs(xStart) & 0xFFF);
	//if (x & 0x400) x |= 0xF800; else x &= 0x3FF;

	if (x < GPU.drawingAreaLeft)
	{
		Int32 delta = ((Int32)GPU.drawingAreaLeft) - x;
		x_ig_adjust += delta;
		x += delta;
		w -= delta;
	}

	if ((x + w) > (GPU.drawingAreaRight + 1))
		w = ((Int32)GPU.drawingAreaRight) + 1 - x;

	if (w <= 0) return;

	if (shaded)
	{
		int a = 5;
	}

	GPU.addColorDx(cD, cG, x_ig_adjust, shaded, textured);
	GPU.addColorDy(cD, cG, y, shaded, textured);

	do
	{
		byte r = cG.r >> 12;
		byte g = cG.g >> 12;
		byte b = cG.b >> 12;
		byte u = cG.u >> 12;
		byte v = cG.v >> 12;

		GPU.shadePixel(x, y, r, g, b, u, v, textured, transparent, rawTexture, dithering, cG.u, cG.v, useBilinear);

		x++;
		GPU.addColorDx(cD, cG, 1, shaded, textured);

		if (w == 1)
		{
			int a = 5;
		}

		w--;
	} while (w > 0);
}

Int64 Polygon::makePolyXFPStep(Int32 dx, Int32 dy)
{
	Int64 ret;
	Int64 dx_ex = (UInt64)dx << 32;

	if (dx_ex < 0)
		dx_ex -= dy - 1;

	if (dx_ex > 0)
		dx_ex += dy - 1;

	ret = dx_ex / dy;

	return (ret);
}


