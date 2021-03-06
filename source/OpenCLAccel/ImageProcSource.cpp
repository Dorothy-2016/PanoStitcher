const char* sourceImageProc = R"(

#define UTIL_BLOCK_WIDTH 16
#define UTIL_BLOCK_HEIGHT 16

__kernel void alphaBlend8UC4(__global unsigned char* data, int step,
    __global const unsigned char* blendData, int blendStep, int rows, int cols)
{
    const int x = get_global_id(0);
    const int y = get_global_id(1);
    if (x < cols && y < rows)
    {
        int ofs = x * 4;
        __global unsigned char* ptr = data + step * y + ofs;
        __global const unsigned char* ptrBlend = blendData + blendStep * y + ofs;
        if (ptrBlend[3])
        {
            int val = ptrBlend[3];
            int comp = 255 - ptrBlend[3];
            ptr[0] = (comp * ptr[0] + val * ptrBlend[0] + 254) / 255;
            ptr[1] = (comp * ptr[1] + val * ptrBlend[1] + 254) / 255;
            ptr[2] = (comp * ptr[2] + val * ptrBlend[2] + 254) / 255;
        }
    }
}

// Coefficients for RGB to YUV420p conversion
__constant int ITUR_BT_601_CRY = 269484;
__constant int ITUR_BT_601_CGY = 528482;
__constant int ITUR_BT_601_CBY = 102760;
__constant int ITUR_BT_601_CRU = -155188;
__constant int ITUR_BT_601_CGU = -305135;
__constant int ITUR_BT_601_CBU = 460324;
__constant int ITUR_BT_601_CGV = -385875;
__constant int ITUR_BT_601_CBV = -74448;

__constant int ITUR_BT_601_CY = 1220542;
__constant int ITUR_BT_601_CUB = 2116026;
__constant int ITUR_BT_601_CUG = -409993;
__constant int ITUR_BT_601_CVG = -852492;
__constant int ITUR_BT_601_CVR = 1673527;

#define ITUR_BT_601_SHIFT 20

#define shifted16 (16 << ITUR_BT_601_SHIFT)
#define halfShift (1 << (ITUR_BT_601_SHIFT - 1))
#define shifted128 (128 << ITUR_BT_601_SHIFT)

__inline int clamp0255(int val)
{
    return val < 0 ? 0 : (val > 255 ? 255 : val);
}

__inline void cvtBGRToYUV2x2Block(__global const unsigned char* bgrTopData, __global const unsigned char* bgrBotData,
    __global unsigned char* yTopData, __global unsigned char* yBotData, __global unsigned char* uData, __global unsigned char* vData)
{
    int b00 = bgrTopData[0];      int g00 = bgrTopData[1];      int r00 = bgrTopData[2];
    int b01 = bgrTopData[4];      int g01 = bgrTopData[5];      int r01 = bgrTopData[6];
    int b10 = bgrBotData[0];      int g10 = bgrBotData[1];      int r10 = bgrBotData[2];
    int b11 = bgrBotData[4];      int g11 = bgrBotData[5];      int r11 = bgrBotData[6];
    
    int y00 = ITUR_BT_601_CRY * r00 + ITUR_BT_601_CGY * g00 + ITUR_BT_601_CBY * b00 + halfShift + shifted16;
    int y01 = ITUR_BT_601_CRY * r01 + ITUR_BT_601_CGY * g01 + ITUR_BT_601_CBY * b01 + halfShift + shifted16;
    int y10 = ITUR_BT_601_CRY * r10 + ITUR_BT_601_CGY * g10 + ITUR_BT_601_CBY * b10 + halfShift + shifted16;
    int y11 = ITUR_BT_601_CRY * r11 + ITUR_BT_601_CGY * g11 + ITUR_BT_601_CBY * b11 + halfShift + shifted16;

    yTopData[0] = clamp0255(y00 >> ITUR_BT_601_SHIFT);
    yTopData[1] = clamp0255(y01 >> ITUR_BT_601_SHIFT);
    yBotData[0] = clamp0255(y10 >> ITUR_BT_601_SHIFT);
    yBotData[1] = clamp0255(y11 >> ITUR_BT_601_SHIFT);
    
    int u00 = ITUR_BT_601_CRU * r00 + ITUR_BT_601_CGU * g00 + ITUR_BT_601_CBU * b00 + halfShift + shifted128;
    int v00 = ITUR_BT_601_CBU * r00 + ITUR_BT_601_CGV * g00 + ITUR_BT_601_CBV * b00 + halfShift + shifted128;
    int u10 = ITUR_BT_601_CRU * r10 + ITUR_BT_601_CGU * g10 + ITUR_BT_601_CBU * b10 + halfShift + shifted128;
    int v10 = ITUR_BT_601_CBU * r10 + ITUR_BT_601_CGV * g10 + ITUR_BT_601_CBV * b10 + halfShift + shifted128;

    *uData = clamp0255((u00 + u10) >> (ITUR_BT_601_SHIFT + 1));
    *vData = clamp0255((v00 + v10) >> (ITUR_BT_601_SHIFT + 1));
}

__kernel void cvtBGR32ToYUV420P(__global const unsigned char* bgrData, int bgrStep,
    __global unsigned char* yData, int yStep, __global unsigned char* uData, int uStep, 
	__global unsigned char* vData, int vStep, int yRows, int yCols)
{
    const int x = get_global_id(0);
    const int y = get_global_id(1);
    const int xx = x * 2, yy = y * 2;
    if (xx < yCols && yy < yRows)
    {
        cvtBGRToYUV2x2Block(bgrData + yy * bgrStep + xx * 4, bgrData + (yy + 1) * bgrStep + xx * 4,
            yData + yy * yStep + xx, yData + (yy + 1) * yStep + xx,
            uData + y * uStep + x, vData + y * vStep + x);
    }
}

__kernel void cvtBGR32ToNV12(__global const unsigned char* bgrData, int bgrStep,
    __global unsigned char* yData, int yStep, __global unsigned char* uvData, int uvStep, int yRows, int yCols)
{
    const int x = get_global_id(0);
    const int y = get_global_id(1);
    const int xx = x * 2, yy = y * 2;
    if (xx < yCols && yy < yRows)
    {
        cvtBGRToYUV2x2Block(bgrData + yy * bgrStep + xx * 4, bgrData + (yy + 1) * bgrStep + xx * 4,
            yData + yy * yStep + xx, yData + (yy + 1) * yStep + xx,
            uvData + y * uvStep + xx, uvData + y * uvStep + xx + 1);
    }
}

)";