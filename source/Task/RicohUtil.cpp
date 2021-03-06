#include "PanoramaTaskUtil.h"
#include "Blend/ZBlend.h"
#include "Blend/ZBlendAlgo.h"
#include "Tool/Timer.h"
#include "Tool/Print.h"
#include "opencv2/core.hpp"
#include "opencv2/core/cuda.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
#include "cuda_runtime_api.h"
#include <thread>
#include <exception>

static const int MAX_NUM_LEVELS = 16; // 16
static const int MIN_SIDE_LENGTH = 2; // 2

static const int UNIT_SHIFT = 10;
static const int UNIT = 1 << UNIT_SHIFT;

void prepare(const cv::Mat& mask1, const cv::Mat& mask2,
    cv::Mat& from1, cv::Mat& from2, cv::Mat& intersect, cv::Mat& weight1, cv::Mat& weight2)
{
    cv::Mat dist1, dist2;
    cv::distanceTransform(mask1, dist1, CV_DIST_L1, 3);
    cv::distanceTransform(mask2, dist2, CV_DIST_L1, 3);

    cv::Mat newMask1 = dist1 > dist2;
    cv::Mat newMask2 = ~newMask1;
    newMask1 &= mask1;
    newMask2 &= mask2;

    int radius = 20;
    cv::Size kernSize(radius * 2 + 1, radius * 2 + 1);
    double sigma = radius / 3.0;
    cv::Mat blurMask1, blurMask2;
    cv::GaussianBlur(newMask1, blurMask1, kernSize, sigma, sigma);
    cv::GaussianBlur(newMask2, blurMask2, kernSize, sigma, sigma);
    //cv::imshow("orig blur mask 1", blurMask1);
    //cv::imshow("orig blur mask 2", blurMask2);
    blurMask1 &= mask1;
    blurMask2 &= mask2;
    //cv::imshow("mask 1", mask1);
    //cv::imshow("mask 2", mask2);
    //cv::imshow("blur mask 1", blurMask1);
    //cv::imshow("blur mask 2", blurMask2);
    //cv::waitKey(0);

    int rows = mask1.rows, cols = mask1.cols;

    from1.create(rows, cols, CV_8UC1);
    from1.setTo(0);
    from2.create(rows, cols, CV_8UC1);
    from2.setTo(0);
    intersect.create(rows, cols, CV_8UC1);
    intersect.setTo(0);
    weight1.create(rows, cols, CV_32SC1);
    weight1.setTo(0);
    weight2.create(rows, cols, CV_32SC1);
    weight2.setTo(0);

    for (int i = 0; i < rows; i++)
    {
        const unsigned char* ptrMask1 = blurMask1.ptr<unsigned char>(i);
        const unsigned char* ptrMask2 = blurMask2.ptr<unsigned char>(i);
        unsigned char* ptrFrom1 = from1.ptr<unsigned char>(i);
        unsigned char* ptrFrom2 = from2.ptr<unsigned char>(i);
        unsigned char* ptrIntersect = intersect.ptr<unsigned char>(i);
        int* ptrWeight1 = weight1.ptr<int>(i);
        int* ptrWeight2 = weight2.ptr<int>(i);
        for (int j = 0; j < cols; j++)
        {
            double w1 = ptrMask1[j], w2 = ptrMask2[j];
            double wsum = w1 + w2;
            if (wsum < std::numeric_limits<double>::epsilon())
                continue;

            w1 /= wsum;
            w2 /= wsum;

            int iw1 = w1 * UNIT + 0.5;
            int iw2 = UNIT - iw1;

            if (iw1 == UNIT)
            {
                ptrFrom1[j] = 255;
            }
            else if (iw2 == UNIT)
            {
                ptrFrom2[j] = 255;
            }
            else
            {
                ptrIntersect[j] = 255;
            }

            ptrWeight1[j] = iw1;
            ptrWeight2[j] = iw2;
        }
    }
}

void prepareSmart(const cv::Mat& mask1, const cv::Mat& mask2, int initRadius,
    cv::Mat& from1, cv::Mat& from2, cv::Mat& intersect, cv::Mat& weight1, cv::Mat& weight2)
{
    cv::Mat dist1, dist2;
    cv::distanceTransform(mask1, dist1, CV_DIST_L1, 3);
    cv::distanceTransform(mask2, dist2, CV_DIST_L1, 3);

    cv::Mat region1 = dist1 > dist2;
    cv::Mat region2 = ~region1;
    cv::Mat newMask1 = region1 & mask1;
    cv::Mat newMask2 = region2 & mask2;
    region1 |= mask1;
    region2 |= mask2;
    region1 = ~region1;
    region2 = ~region2;

    int step = 2;
    cv::Mat blurMask1, blurMask2;
    cv::Mat binaryBlurMask1, binaryBlurMask2;
    cv::Mat outSide1, outSide2;
    for (int radius = initRadius; radius >= 1; radius -= step)
    {
        cv::Size kernSize(radius * 2 + 1, radius * 2 + 1);
        double sigma = radius / 3.0;
        cv::GaussianBlur(newMask1, blurMask1, kernSize, sigma, sigma);
        cv::GaussianBlur(newMask2, blurMask2, kernSize, sigma, sigma);
        binaryBlurMask1 = blurMask1 > 0;
        binaryBlurMask2 = blurMask2 > 0;
        int numInside1 = cv::countNonZero(binaryBlurMask1 & region1);
        int numInside2 = cv::countNonZero(binaryBlurMask2 & region2);
        if (numInside1 == 0 && numInside2 == 0)
        {
            ztool::lprintf("final radius = %d\n", radius);
            break;
        }
    }
    
    //cv::imshow("orig blur mask 1", blurMask1);
    //cv::imshow("orig blur mask 2", blurMask2);
    blurMask1 &= mask1;
    blurMask2 &= mask2;
    //cv::imshow("mask 1", mask1);
    //cv::imshow("mask 2", mask2);
    //cv::imshow("blur mask 1", blurMask1);
    //cv::imshow("blur mask 2", blurMask2);
    //cv::waitKey(0);

    int rows = mask1.rows, cols = mask1.cols;

    from1.create(rows, cols, CV_8UC1);
    from1.setTo(0);
    from2.create(rows, cols, CV_8UC1);
    from2.setTo(0);
    intersect.create(rows, cols, CV_8UC1);
    intersect.setTo(0);
    weight1.create(rows, cols, CV_32SC1);
    weight1.setTo(0);
    weight2.create(rows, cols, CV_32SC1);
    weight2.setTo(0);

    for (int i = 0; i < rows; i++)
    {
        const unsigned char* ptrMask1 = blurMask1.ptr<unsigned char>(i);
        const unsigned char* ptrMask2 = blurMask2.ptr<unsigned char>(i);
        unsigned char* ptrFrom1 = from1.ptr<unsigned char>(i);
        unsigned char* ptrFrom2 = from2.ptr<unsigned char>(i);
        unsigned char* ptrIntersect = intersect.ptr<unsigned char>(i);
        int* ptrWeight1 = weight1.ptr<int>(i);
        int* ptrWeight2 = weight2.ptr<int>(i);
        for (int j = 0; j < cols; j++)
        {
            double w1 = ptrMask1[j], w2 = ptrMask2[j];
            double wsum = w1 + w2;
            if (wsum < std::numeric_limits<double>::epsilon())
                continue;

            w1 /= wsum;
            w2 /= wsum;

            int iw1 = w1 * UNIT + 0.5;
            int iw2 = UNIT - iw1;

            if (iw1 == UNIT)
            {
                ptrFrom1[j] = 255;
            }
            else if (iw2 == UNIT)
            {
                ptrFrom2[j] = 255;
            }
            else
            {
                ptrIntersect[j] = 255;
            }

            ptrWeight1[j] = iw1;
            ptrWeight2[j] = iw2;
        }
    }
}

void blend(const cv::Mat& image1, const cv::Mat& image2, const cv::Mat& from1, const cv::Mat& from2, 
    const cv::Mat& intersect, const cv::Mat& weight1, const cv::Mat& weight2, cv::Mat& blendImage)
{
    int rows = image1.rows, cols = image1.cols;
    blendImage.create(rows, cols, CV_8UC3);
    blendImage.setTo(0);
    image1.copyTo(blendImage, from1);
    image2.copyTo(blendImage, from2);
    for (int i = 0; i < rows; i++)
    {
        const unsigned char* ptrImage1 = image1.ptr<unsigned char>(i);
        const unsigned char* ptrImage2 = image2.ptr<unsigned char>(i);
        const unsigned char* ptrInt = intersect.ptr<unsigned char>(i);
        const int* ptrW1 = weight1.ptr<int>(i);
        const int* ptrW2 = weight2.ptr<int>(i);
        unsigned char* ptrB = blendImage.ptr<unsigned char>(i);
        for (int j = 0; j < cols; j++)
        {
            if (ptrInt[j])
            {
                int w1 = ptrW1[j], w2 = ptrW2[j];
                ptrB[j * 3] = (ptrImage1[j * 3] * w1 + ptrImage2[j * 3] * w2) >> UNIT_SHIFT;
                ptrB[j * 3 + 1] = (ptrImage1[j * 3 + 1] * w1 + ptrImage2[j * 3 + 1] * w2) >> UNIT_SHIFT;
                ptrB[j * 3 + 2] = (ptrImage1[j * 3 + 2] * w1 + ptrImage2[j * 3 + 2] * w2) >> UNIT_SHIFT;
            }
        }
    }
}

inline void bilinearResampling(int width, int height, int step, const unsigned char* data,
    double x, double y, unsigned char rgb[3])
{
    int x0 = x, y0 = y, x1 = x0 + 1, y1 = y0 + 1;
    if (x0 < 0) x0 = 0;
    if (x1 > width - 1) x1 = width - 1;
    if (y0 < 0) y0 = 0;
    if (y1 > height - 1) y1 = height - 1;
    double wx0 = x - x0, wx1 = 1 - wx0;
    double wy0 = y - y0, wy1 = 1 - wy0;
    double w00 = wx1 * wy1, w01 = wx0 * wy1;
    double w10 = wx1 * wy0, w11 = wx0 * wy0;

    double b = 0, g = 0, r = 0;
    const unsigned char* ptr;
    ptr = data + step * y0 + x0 * 3;
    b += *(ptr++) * w00;
    g += *(ptr++) * w00;
    r += *(ptr++) * w00;
    b += *(ptr++) * w01;
    g += *(ptr++) * w01;
    r += *(ptr++) * w01;
    ptr = data + step * y1 + x0 * 3;
    b += *(ptr++) * w10;
    g += *(ptr++) * w10;
    r += *(ptr++) * w10;
    b += *(ptr++) * w11;
    g += *(ptr++) * w11;
    r += *(ptr++) * w11;

    rgb[0] = b;
    rgb[1] = g;
    rgb[2] = r;
}

void reprojectAndBlend(const cv::Mat& src1, const cv::Mat& src2, 
    const cv::Mat& dstSrcMap1, const cv::Mat& dstSrcMap2,
    const cv::Mat& from1, const cv::Mat& from2, const cv::Mat& intersect, 
    const cv::Mat& weight1, const cv::Mat& weight2, cv::Mat& dst)
{
    /*CV_Assert(src1.data && src2.data);
    CV_Assert(src1.type() == CV_8UC3 && src2.type() == CV_8UC3);    
    CV_Assert(dstSrcMap1.data && dstSrcMap2.data && from1.data && from2.data &&
        intersect.data && weight1.data && weight2.data);
    CV_Assert(dstSrcMap1.type() == CV_64FC2 && dstSrcMap2.type() == CV_64FC2 &&
        from1.type() == CV_8UC1 && from2.type() == CV_8UC1 && intersect.type() == CV_8UC1 &&
        weight1.type() == CV_32SC1 && weight2.type() == CV_32SC1);
    cv::Size dstSize = intersect.size();
    CV_Assert(dstSrcMap1.size() == dstSize && dstSrcMap2.size() == dstSize &&
        from1.size() == dstSize && from2.size() == dstSize &&
        weight1.size() == dstSize && weight2.size() == dstSize);*/

    int rows = intersect.rows, cols = intersect.cols;
    dst.create(rows, cols, CV_8UC3);
    dst.setTo(0);

    int src1Width = src1.cols, src1Height = src1.rows, src1Step = src1.step;
    int src2Width = src2.cols, src2Height = src2.rows, src2Step = src2.step;
    const unsigned char* src1Data = src1.data;
    const unsigned char* src2Data = src2.data;
    for (int i = 0; i < rows; i++)
    {
        const cv::Point2d* ptrMap1 = dstSrcMap1.ptr<cv::Point2d>(i);
        const cv::Point2d* ptrMap2 = dstSrcMap2.ptr<cv::Point2d>(i);
        const unsigned char* ptrFrom1 = from1.ptr<unsigned char>(i);
        const unsigned char* ptrFrom2 = from2.ptr<unsigned char>(i);
        const unsigned char* ptrIntersect = intersect.ptr<unsigned char>(i);
        const int* ptrWeight1 = weight1.ptr<int>(i);
        const int* ptrWeight2 = weight2.ptr<int>(i);
        unsigned char* ptrDst = dst.ptr<unsigned char>(i);
        for (int j = 0; j < cols; j++)
        {
            if (ptrFrom1[j])
            {
                cv::Point2d pt = ptrMap1[j];
                bilinearResampling(src1Width, src1Height, src1Step, src1Data, pt.x, pt.y, ptrDst + j * 3);
            }
            else if (ptrFrom2[j])
            {
                cv::Point2d pt = ptrMap2[j];
                bilinearResampling(src2Width, src2Height, src2Step, src2Data, pt.x, pt.y, ptrDst + j * 3);
            }
            else if (ptrIntersect[j])
            {
                cv::Point2d pt;
                unsigned char bgr1[3], bgr2[3];
                pt = ptrMap1[j];
                bilinearResampling(src1Width, src1Height, src1Step, src1Data, pt.x, pt.y, bgr1);
                pt = ptrMap2[j];
                bilinearResampling(src2Width, src2Height, src2Step, src2Data, pt.x, pt.y, bgr2);
                int w1 = ptrWeight1[j], w2 = ptrWeight2[j];
                ptrDst[j * 3] = (bgr1[0] * w1 + bgr2[0] * w2) >> UNIT_SHIFT;
                ptrDst[j * 3 + 1] = (bgr1[1] * w1 + bgr2[1] * w2) >> UNIT_SHIFT;
                ptrDst[j * 3 + 2] = (bgr1[2] * w1 + bgr2[2] * w2) >> UNIT_SHIFT;
            }
        }
    }
}

class ReprojectAndBlendLoop : public cv::ParallelLoopBody
{
public:
    ReprojectAndBlendLoop(const cv::Mat& src1_, const cv::Mat& src2_,
        const cv::Mat& dstSrcMap1_, const cv::Mat& dstSrcMap2_,
        const cv::Mat& from1_, const cv::Mat& from2_, const cv::Mat& intersect_,
        const cv::Mat& weight1_, const cv::Mat& weight2_, cv::Mat& dst_)
        : src1(src1_), src2(src2_), dstSrcMap1(dstSrcMap1_), dstSrcMap2(dstSrcMap2_),
          from1(from1_), from2(from2_), intersect(intersect_),
          weight1(weight1_), weight2(weight2_), dst(dst_)
    {
        src1Width = src1.cols, src1Height = src1.rows, src1Step = src1.step;
        src2Width = src2.cols, src2Height = src2.rows, src2Step = src2.step;
        src1Data = src1.data;
        src2Data = src2.data;
        dstWidth = dst.cols, dstHeight = dst.rows;
    }

    virtual ~ReprojectAndBlendLoop() {}

    virtual void operator()(const cv::Range& r) const
    {
        int start = r.start, end = std::min(r.end, dstHeight);
        for (int i = start; i < end; i++)
        {
            const cv::Point2d* ptrMap1 = dstSrcMap1.ptr<cv::Point2d>(i);
            const cv::Point2d* ptrMap2 = dstSrcMap2.ptr<cv::Point2d>(i);
            const unsigned char* ptrFrom1 = from1.ptr<unsigned char>(i);
            const unsigned char* ptrFrom2 = from2.ptr<unsigned char>(i);
            const unsigned char* ptrIntersect = intersect.ptr<unsigned char>(i);
            const int* ptrWeight1 = weight1.ptr<int>(i);
            const int* ptrWeight2 = weight2.ptr<int>(i);
            unsigned char* ptrDst = dst.ptr<unsigned char>(i);
            for (int j = 0; j < dstWidth; j++)
            {
                if (ptrFrom1[j])
                {
                    cv::Point2d pt = ptrMap1[j];
                    bilinearResampling(src1Width, src1Height, src1Step, src1Data, pt.x, pt.y, ptrDst + j * 3);
                }
                else if (ptrFrom2[j])
                {
                    cv::Point2d pt = ptrMap2[j];
                    bilinearResampling(src2Width, src2Height, src2Step, src2Data, pt.x, pt.y, ptrDst + j * 3);
                }
                else if (ptrIntersect[j])
                {
                    cv::Point2d pt;
                    unsigned char bgr1[3], bgr2[3];
                    pt = ptrMap1[j];
                    bilinearResampling(src1Width, src1Height, src1Step, src1Data, pt.x, pt.y, bgr1);
                    pt = ptrMap2[j];
                    bilinearResampling(src2Width, src2Height, src2Step, src2Data, pt.x, pt.y, bgr2);
                    int w1 = ptrWeight1[j], w2 = ptrWeight2[j];
                    ptrDst[j * 3] = (bgr1[0] * w1 + bgr2[0] * w2) >> UNIT_SHIFT;
                    ptrDst[j * 3 + 1] = (bgr1[1] * w1 + bgr2[1] * w2) >> UNIT_SHIFT;
                    ptrDst[j * 3 + 2] = (bgr1[2] * w1 + bgr2[2] * w2) >> UNIT_SHIFT;
                }
            }
        }
    }

    const cv::Mat& src1; const cv::Mat& src2;
    const cv::Mat& dstSrcMap1; const cv::Mat& dstSrcMap2;
    const cv::Mat& from1; const cv::Mat& from2; const cv::Mat& intersect;
    const cv::Mat& weight1; const cv::Mat& weight2; cv::Mat& dst;
    int src1Width, src1Height, src1Step;
    int src2Width, src2Height, src2Step;
    const unsigned char* src1Data;
    const unsigned char* src2Data;
    int dstWidth, dstHeight;
};

void reprojectAndBlendParallel(const cv::Mat& src1, const cv::Mat& src2,
    const cv::Mat& dstSrcMap1, const cv::Mat& dstSrcMap2,
    const cv::Mat& from1, const cv::Mat& from2, const cv::Mat& intersect,
    const cv::Mat& weight1, const cv::Mat& weight2, cv::Mat& dst)
{
    int rows = intersect.rows, cols = intersect.cols;
    dst.create(rows, cols, CV_8UC3);
    dst.setTo(0);

    ReprojectAndBlendLoop loop(src1, src2, dstSrcMap1, dstSrcMap2, from1, from2, intersect, weight1, weight2, dst);
    cv::parallel_for_(cv::Range(0, rows), loop);
}

#include "RicohUtil.h"
#include "Warp/ZReproject.h"
#include "CudaAccel/CudaInterface.h"

bool RicohPanoramaRender::prepare(const std::string& path_, 
    const cv::Size& srcSize_, const cv::Size& dstSize_)
{
    success = 0;

    if (!((dstSize_.width & 1) == 0 && (dstSize_.height & 1) == 0 &&
        dstSize_.height * 2 == dstSize_.width))
        return false;

    srcSize = srcSize_;
    dstSize = dstSize_;

    std::vector<PhotoParam> params;
    loadPhotoParamFromXML(path_, params);
    if (params.size() != 2)
        return false;

    cv::Mat mask1, mask2;
    getReprojectMapAndMask(params[0], srcSize, dstSize, dstSrcMap1, mask1);
    getReprojectMapAndMask(params[1], srcSize, dstSize, dstSrcMap2, mask2);

    ::prepare(mask1, mask2, from1, from2, intersect, weight1, weight2);

    success = 1;
    return true;
}

void RicohPanoramaRender::render(const cv::Mat& src, cv::Mat& dst)
{
    if (!success)
        return;

    CV_Assert(src.data && src.type() == CV_8UC3 && src.size() == srcSize);

    reprojectAndBlendParallel(src, src, dstSrcMap1, dstSrcMap2,
        from1, from2, intersect, weight1, weight2, dst);
}

bool DetuPanoramaRender::prepare(const std::string& path_, const cv::Size& srcSize_, const cv::Size& dstSize_)
{
    success = 0;

    if (!((dstSize_.width & 1) == 0 && (dstSize_.height & 1) == 0 &&
        dstSize_.height * 2 == dstSize_.width))
        return false;

    srcSize = srcSize_;
    dstSize = dstSize_;

    std::vector<PhotoParam> params;
    loadPhotoParamFromXML(path_, params);

    cv::Mat mask;
    getReprojectMapAndMask(params[0], srcSize, dstSize, dstSrcMap, mask);

    success = 1;
    return true;
}

void DetuPanoramaRender::render(const cv::Mat& src, cv::Mat& dst)
{
    if (!success)
        return;

    CV_Assert(src.size() == srcSize);
    
    dst.create(dstSrcMap.size(), CV_8UC3);
    
    reprojectParallel(src, dst, dstSrcMap);
}

bool DualGoProPanoramaRender::prepare(const std::string& path_, int blendType_, const cv::Size& srcSize_, const cv::Size& dstSize_)
{
    success = 0;

    if (!((dstSize_.width & 1) == 0 && (dstSize_.height & 1) == 0 &&
        dstSize_.height * 2 == dstSize_.width))
        return false;

    srcSize = srcSize_;
    dstSize = dstSize_;

    std::vector<PhotoParam> params;
    loadPhotoParamFromPTS(path_, params);
    if (params.size() != 2)
        return false;

    PhotoParam param1 = params[0], param2 = params[1];
    getReprojectMapAndMask(param1, srcSize, dstSize, dstSrcMap1, mask1);
    getReprojectMapAndMask(param2, srcSize, dstSize, dstSrcMap2, mask2);

    ::prepareSmart(mask1, mask2, 50, from1, from2, intersect, weight1, weight2);
    //::prepare(mask1, mask2, from1, from2, intersect, weight1, weight2);

    success = 1;
    return true;    
}

bool DualGoProPanoramaRender::render(const cv::Mat& src1, const cv::Mat& src2, cv::Mat& dst)
{
    if (!success)
        return false;

    if (!(src1.data && src2.data && src1.type() == CV_8UC3 && src2.type() == CV_8UC3 &&
        src1.size() == srcSize && src2.size() == srcSize))
        return false;

    reprojectAndBlendParallel(src1, src2, dstSrcMap1, dstSrcMap2,
        from1, from2, intersect, weight1, weight2, dst);
    return true;
}

bool DualGoProPanoramaRender::render(const std::vector<cv::Mat>& src, cv::Mat& dst)
{
    if (src.size() != 2)
        return false;

    return render(src[0], src[1], dst);
}

bool CPUMultiCameraPanoramaRender::prepare(const std::string& path_, int blendType_, const cv::Size& srcSize_, const cv::Size& dstSize_)
{
    success = 0;

    if (!((dstSize_.width & 1) == 0 && (dstSize_.height & 1) == 0 &&
        dstSize_.height * 2 == dstSize_.width))
        return false;

    srcSize = srcSize_;
    dstSize = dstSize_;

    std::vector<PhotoParam> params;
    if (!loadPhotoParams(path_, params) || params.empty())
        return false;

    numImages = params.size();
    getReprojectMapsAndMasks(params, srcSize, dstSize, dstSrcMaps, masks);
    if (!blender.prepare(masks, MAX_NUM_LEVELS, MIN_SIDE_LENGTH))
        return false;

    success = 1;
    return true;
}

bool CPUMultiCameraPanoramaRender::render(const std::vector<cv::Mat>& src, cv::Mat& dst)
{
    if (!success)
        return false;

    if (src.size() != numImages)
        return false;

    for (int i = 0; i < numImages; i++)
    {
        if (src[i].size() != srcSize)
            return false;
    }

    reprojImages.resize(numImages);
    for (int i = 0; i < numImages; i++)
        reprojectParallel(src[i], reprojImages[i], dstSrcMaps[i]);

    blender.blend(reprojImages, dst);
    return true;
}

bool CudaMultiCameraPanoramaRender::prepare(const std::string& path_, int blendType_, const cv::Size& srcSize_, const cv::Size& dstSize_)
{
    success = 0;

    if (!((dstSize_.width & 1) == 0 && (dstSize_.height & 1) == 0 &&
        dstSize_.height * 2 == dstSize_.width))
        return false;

    srcSize = srcSize_;
    dstSize = dstSize_;

    std::vector<PhotoParam> params;
    if (!loadPhotoParams(path_, params) || params.empty())
        return false;

    numImages = params.size();
    std::vector<cv::Mat> masks, dstSrcMaps;
    getReprojectMapsAndMasks(params, srcSize, dstSize, dstSrcMaps, masks);
    if (!blender.prepare(masks, MAX_NUM_LEVELS, MIN_SIDE_LENGTH))
        return false;

    cudaGenerateReprojectMaps(params, srcSize, dstSize, dstSrcXMapsGPU, dstSrcYMapsGPU);
    srcMems.resize(numImages);
    srcImages.resize(numImages);
    for (int i = 0; i < numImages; i++)
    {
        srcMems[i].create(srcSize, CV_8UC4);
        srcImages[i] = srcMems[i].createMatHeader();
    }

    streams.resize(numImages);

    success = 1;
    return true;
}

bool CudaMultiCameraPanoramaRender::render(const std::vector<cv::Mat>& src, cv::Mat& dst)
{
    if (!success)
        return false;

    if (src.size() != numImages)
        return false;

    for (int i = 0; i < numImages; i++)
    {
        if (src[i].size() != srcSize)
            return false;
        if (src[i].type() != CV_8UC3 && src[i].type() != CV_8UC4)
            return false;
    }

    //double freq = cv::getTickFrequency();
    //long long int beg = cv::getTickCount();

    int fromTo[] = { 0, 0, 1, 1, 2, 2 };
    for (int i = 0; i < numImages; i++)
    {
        if (src[i].type() == CV_8UC4)
            src[i].copyTo(srcImages[i]);
        else
            cv::mixChannels(&src[i], 1, &srcImages[i], 1, fromTo, 3);
    }

    srcImagesGPU.resize(numImages);
    reprojImagesGPU.resize(numImages);
    for (int i = 0; i < numImages; i++)
        srcImagesGPU[i].upload(srcImages[i], streams[i]);    
    for (int i = 0; i < numImages; i++)
        cudaReprojectTo16S(srcImagesGPU[i], reprojImagesGPU[i], dstSrcXMapsGPU[i], dstSrcYMapsGPU[i], streams[i]);
    for (int i = 0; i < numImages; i++)
        streams[i].waitForCompletion();

    blender.blend(reprojImagesGPU, blendImageGPU);
    blendImageGPU.download(dst);
    return true;
}

bool CudaMultiCameraPanoramaRender2::prepare(const std::string& path_, int type_, const cv::Size& srcSize_, const cv::Size& dstSize_)
{
    success = 0;

    if (type_ != PanoramaRender::BlendTypeLinear && type_ != PanoramaRender::BlendTypeMultiband)
        return false;

    if (!((dstSize_.width & 1) == 0 && (dstSize_.height & 1) == 0 &&
        dstSize_.height * 2 == dstSize_.width))
        return false;

    blendType = type_;
    srcSize = srcSize_;
    dstSize = dstSize_;

    std::vector<PhotoParam> params;
    if (!loadPhotoParams(path_, params) || params.empty())
        return false;

    numImages = params.size();
    std::vector<cv::Mat> masks, dstSrcMaps;
    getReprojectMapsAndMasks(params, srcSize, dstSize, dstSrcMaps, masks);
    if (blendType == PanoramaRender::BlendTypeLinear)
    {
        if (!lBlender.prepare(masks, 50))
            return false;
    }
    else if (blendType == PanoramaRender::BlendTypeMultiband)
    {
        if (!mbBlender.prepare(masks, MAX_NUM_LEVELS, MIN_SIDE_LENGTH))
            return false;
    }
    else
        return false;

    cudaGenerateReprojectMaps(params, srcSize, dstSize, dstSrcXMapsGPU, dstSrcYMapsGPU);
    streams.resize(numImages);

    success = 1;
    return true;
}

bool CudaMultiCameraPanoramaRender2::render(const std::vector<cv::Mat>& src, cv::cuda::GpuMat& dst)
{
    if (!success)
        return false;

    if (src.size() != numImages)
        return false;

    for (int i = 0; i < numImages; i++)
    {
        if (src[i].size() != srcSize)
            return false;
        if (src[i].type() != CV_8UC4)
            return false;
    }

    //ztool::Timer blendTime, uploadTime, reprjTime;

    if (blendType == PanoramaRender::BlendTypeLinear)
    {
        srcImagesGPU.resize(numImages);
        reprojImagesGPU.resize(numImages);
        for (int i = 0; i < numImages; i++)
            srcImagesGPU[i].upload(src[i], streams[i]);
        for (int i = 0; i < numImages; i++)
            cudaReproject(srcImagesGPU[i], reprojImagesGPU[i], dstSrcXMapsGPU[i], dstSrcYMapsGPU[i], streams[i]);
        for (int i = 0; i < numImages; i++)
            streams[i].waitForCompletion();
        lBlender.blend(reprojImagesGPU, dst);
    }
    else if (blendType == PanoramaRender::BlendTypeMultiband)
    {
        srcImagesGPU.resize(numImages);
        reprojImagesGPU.resize(numImages);
        //uploadTime.start();
        for (int i = 0; i < numImages; i++)
            srcImagesGPU[i].upload(src[i], streams[i]);
        //uploadTime.end();
        //reprjTime.start();
        for (int i = 0; i < numImages; i++)
            cudaReprojectTo16S(srcImagesGPU[i], reprojImagesGPU[i], dstSrcXMapsGPU[i], dstSrcYMapsGPU[i], streams[i]);
        for (int i = 0; i < numImages; i++)
            streams[i].waitForCompletion();
        //reprjTime.end();
        //blendTime.start();
        mbBlender.blend(reprojImagesGPU, dst);
        //blendTime.end();
        //printf("upload %f, reprj %f, mb blend %f\n", uploadTime.elapse(), reprjTime.elapse(), blendTime.elapse());
    }
    
    return true;
}

int CudaMultiCameraPanoramaRender2::getNumImages() const
{
    return numImages;
}

bool CudaMultiCameraPanoramaRender3::prepare(const std::string& path_, int type_, const cv::Size& srcSize_, const cv::Size& dstSize_)
{
    success = 0;

    if (!((dstSize_.width & 1) == 0 && (dstSize_.height & 1) == 0 &&
        dstSize_.height * 2 == dstSize_.width))
        return false;

    srcSize = srcSize_;
    dstSize = dstSize_;

    std::vector<PhotoParam> params;
    if (!loadPhotoParams(path_, params) || params.empty())
        return false;

    numImages = params.size();
    std::vector<cv::Mat> masks, dstSrcMaps;
    getReprojectMapsAndMasks(params, srcSize, dstSize, dstSrcMaps, masks);

    if (!adjuster.prepare(masks, 50))
        return false;

    if (!blender.prepare(masks, 50))
        return false;

    cudaGenerateReprojectMaps(params, srcSize, dstSize, dstSrcXMapsGPU, dstSrcYMapsGPU);
    streams.resize(numImages);

    success = 1;
    return true;
}

bool CudaMultiCameraPanoramaRender3::render(const std::vector<cv::Mat>& src, cv::Mat& dst)
{
    if (!success)
        return false;

    if (src.size() != numImages)
        return false;

    for (int i = 0; i < numImages; i++)
    {
        if (src[i].size() != srcSize)
            return false;
        if (src[i].type() != CV_8UC4)
            return false;
    }

    srcImagesGPU.resize(numImages);
    reprojImagesGPU.resize(numImages);
    for (int i = 0; i < numImages; i++)
        srcImagesGPU[i].upload(src[i], streams[i]);
    for (int i = 0; i < numImages; i++)
        cudaReproject(srcImagesGPU[i], reprojImagesGPU[i], dstSrcXMapsGPU[i], dstSrcYMapsGPU[i], streams[i]);
    for (int i = 0; i < numImages; i++)
        streams[i].waitForCompletion();
    if (luts.empty())
    {
        cv::Mat imageC4;
        std::vector<cv::Mat> imagesC3(numImages);
        int fromTo[] = { 0, 0, 1, 1, 2, 2 };
        for (int i = 0; i < numImages; i++)
        {
            reprojImagesGPU[i].download(imageC4);
            imagesC3[i].create(reprojImagesGPU[i].size(), CV_8UC3);
            cv::mixChannels(&imageC4, 1, &imagesC3[i], 1, fromTo, 3);
        }
        adjuster.calcGain(imagesC3, luts);
    }
    for (int i = 0; i < numImages; i++)
        cudaTransform(reprojImagesGPU[i], reprojImagesGPU[i], luts[i]);
    blender.blend(reprojImagesGPU, blendImageGPU);
    blendImageGPU.download(dst);

    return true;
}

bool CudaPanoramaRender::prepare(const std::string& path_, const std::string& customMaskPath_, int highQualityBlend_, int completeQueue_, 
    const cv::Size& srcSize_, const cv::Size& dstSize_)
{
    clear();

    success = 0;

    if (!((dstSize_.width & 1) == 0 && (dstSize_.height & 1) == 0 &&
        dstSize_.height * 2 == dstSize_.width))
    {
        ztool::lprintf("Error in %s, dstSize not qualified\n", __FUNCTION__);
        return false;
    }

    std::vector<PhotoParam> params;
    bool ok = loadPhotoParams(path_, params);
    if (!ok || params.empty())
    {
        ztool::lprintf("Error in %s, load photo params failed\n", __FUNCTION__);
        return false;
    }

    highQualityBlend = highQualityBlend_;
    completeQueue = completeQueue_;
    srcSize = srcSize_;
    dstSize = dstSize_;

    std::vector<cv::Mat> masks, dstSrcMaps;
    try
    {
        numImages = params.size();        
        getReprojectMapsAndMasks(params, srcSize, dstSize, dstSrcMaps, masks);
        if (highQualityBlend)
        {
            if (!mbBlender.prepare(masks, MAX_NUM_LEVELS, MIN_SIDE_LENGTH))
                return false;
        }
        else
        {
            std::vector<cv::Mat> weights;
            //getWeightsLinearBlendBoundedRadius32F(masks, dstSize.width * 0.05, 10, weights);
            getWeightsLinearBlend32F(masks, dstSize.width * 0.05, weights);
            weightsGPU.resize(numImages);
            for (int i = 0; i < numImages; i++)
                weightsGPU[i].upload(weights[i]);
            accumGPU.create(dstSize, CV_32FC4);
        }

        cudaGenerateReprojectMaps(params, srcSize, dstSize, dstSrcXMapsGPU, dstSrcYMapsGPU);
        streams.resize(numImages);

        pool.init(dstSize.height, dstSize.width, CV_8UC4, cv::cuda::HostMem::SHARED);

        if (completeQueue)
            cpQueue.setMaxSize(4);
    }
    catch (std::exception& e)
    {
        ztool::lprintf("Error in %s, exception caught: %s\n", __FUNCTION__, e.what());
        return false;
    }

    useCustomMasks = 0;
    if (customMaskPath_.size())
    {
        if (highQualityBlend)
        {
            std::vector<std::vector<IntervaledContour> > contours;
            ok = loadIntervaledContours(customMaskPath_, contours);
            if (!ok)
            {
                ztool::lprintf("Error in %s, load custom masks failed\n", __FUNCTION__);
                return false;
            }
            if (contours.size() != numImages)
            {
                ztool::lprintf("Error in %s, loaded contours.size() != numVideos\n", __FUNCTION__);
                return false;
            }
            if (!cvtContoursToCudaMasks(contours, masks, customMasks))
            {
                ztool::lprintf("Error in %s, convert contours to customMasks failed\n", __FUNCTION__);
                return false;
            }
            mbBlender.getUniqueMasks(dstUniqueMasksGPU);
            useCustomMasks = 1;
        }
        else
            ztool::lprintf("Warning in %s, non high quality blend, i.e. linear blend, does not support custom masks\n", __FUNCTION__);
    }

    success = 1;
    return true;
}

bool CudaPanoramaRender::render(const std::vector<cv::Mat>& src, const std::vector<long long int> timeStamps)
{
    if (!success)
    {
        ztool::lprintf("Error in %s, have not prepared or prepare failed before\n", __FUNCTION__);
        return false;
    }

    if (src.size() != numImages || timeStamps.size() != numImages)
    {
        ztool::lprintf("Error in %s, size not equal\n", __FUNCTION__);
        return false;
    }

    for (int i = 0; i < numImages; i++)
    {
        if (src[i].size() != srcSize)
        {
            ztool::lprintf("Error in %s, src[%d] size (%d, %d), not equal to (%d, %d)\n",
                __FUNCTION__, i, src[i].size().width, src[i].size().height, 
                srcSize.width, srcSize.height);
            return false;
        }
            
        if (src[i].type() != CV_8UC4)
        {
            ztool::lprintf("Error in %s, type %d not equal to %d\n", __FUNCTION__, src[i].type(), CV_8UC4);
            return false;
        }
            
    }

    try
    {
        cv::cuda::HostMem blendImageMem;
        if (!pool.get(blendImageMem))
            return false;

        cv::cuda::GpuMat blendImageGPU = blendImageMem.createGpuMatHeader();
        if (!highQualityBlend)
        {
            accumGPU.setTo(0);
            srcImagesGPU.resize(numImages);
            reprojImagesGPU.resize(numImages);
            for (int i = 0; i < numImages; i++)
                srcImagesGPU[i].upload(src[i], streams[i]);
            for (int i = 0; i < numImages; i++)
                cudaReprojectWeightedAccumulateTo32F(srcImagesGPU[i], accumGPU, dstSrcXMapsGPU[i], dstSrcYMapsGPU[i], weightsGPU[i], streams[i]);
            for (int i = 0; i < numImages; i++)
                streams[i].waitForCompletion();
            accumGPU.convertTo(blendImageGPU, CV_8U);
        }
        else
        {
            srcImagesGPU.resize(numImages);
            reprojImagesGPU.resize(numImages);
            // Add the following two lines to prevent exception if dstSize is around (1200, 600)
            //for (int i = 0; i < numImages; i++)
            //    reprojImagesGPU[i].create(dstSize, CV_16SC4);
            // Further test shows that the above two lines cannot prevent cuda runtime
            // from throwing exception, so they are commented.
            // It seems that the only way to avoid exception is to call 
            // cudaReproject instead of cudaReprojectTo16S, but then CudaTilingMultibandBlend::blend
            // will perform data conversion from type CV_8UC4 to CV_16SC4, more time consumed.
            for (int i = 0; i < numImages; i++)
                srcImagesGPU[i].upload(src[i], streams[i]);
            for (int i = 0; i < numImages; i++)
                cudaReprojectTo16S(srcImagesGPU[i], reprojImagesGPU[i], dstSrcXMapsGPU[i], dstSrcYMapsGPU[i], streams[i]);
            for (int i = 0; i < numImages; i++)
                streams[i].waitForCompletion();

            mbBlender.blend(reprojImagesGPU, blendImageGPU);
        }
        if (completeQueue)
            cpQueue.push(std::make_pair(blendImageMem, timeStamps[0]));
        else
            rtQueue.push(std::make_pair(blendImageMem, timeStamps[0]));
    }
    catch (std::exception& e)
    {
        ztool::lprintf("Error in %s, exception caught: %s\n", __FUNCTION__, e.what());
        return false;
    }

    return true;
}

bool CudaPanoramaRender::getResult(cv::Mat& dst, long long int& timeStamp)
{
    std::pair<cv::cuda::HostMem, long long int> item;
    bool ret = completeQueue ? cpQueue.pull(item) : rtQueue.pull(item);
    if (ret)
    {
        cv::Mat temp = item.first.createMatHeader();
        temp.copyTo(dst);
        timeStamp = item.second;
    }        
    return ret;
}

void CudaPanoramaRender::stop()
{
    rtQueue.stop();
    cpQueue.stop();
}

void CudaPanoramaRender::resume()
{
    rtQueue.resume();
    cpQueue.resume();
}

void CudaPanoramaRender::waitForCompletion()
{
    if (completeQueue)
    {
        while (cpQueue.size())
            std::this_thread::sleep_for(std::chrono::microseconds(25));
    }
    else
    {
        while (rtQueue.size())
            std::this_thread::sleep_for(std::chrono::microseconds(25));
    }
}

void CudaPanoramaRender::clear()
{
    dstUniqueMasksGPU.clear();
    currMasksGPU.clear();
    useCustomMasks = 0;
    customMasks.clear();
    dstSrcXMapsGPU.clear();
    dstSrcYMapsGPU.clear();
    srcImagesGPU.clear();
    reprojImagesGPU.clear();
    weightsGPU.clear();
    rtQueue.stop();
    cpQueue.stop();
    pool.clear();
    rtQueue.clear();
    cpQueue.clear();
    streams.clear();
    highQualityBlend = 0;
    completeQueue = 0;
    numImages = 0;
    success = 0;
}

int CudaPanoramaRender::getNumImages() const
{
    return success ? numImages : 0;
}

bool CudaPanoramaRender2::prepare(const std::string& path_, int highQualityBlend_, int blendParam_, 
    const cv::Size& srcSize_, const cv::Size& dstSize_)
{
    clear();

    success = 0;

    if (!((dstSize_.width & 1) == 0 && (dstSize_.height & 1) == 0 &&
        dstSize_.height * 2 == dstSize_.width))
    {
        ztool::lprintf("Error in %s, dstSize not qualified\n", __FUNCTION__);
        return false;
    }

    if (blendParam_ <= 0)
    {
        ztool::lprintf("Error in %s, blendParam = %d, should be positive\n", __FUNCTION__, blendParam_);
        return false;
    }

    bool ok = loadPhotoParams(path_, params);
    if (!ok || params.empty())
    {
        ztool::lprintf("Error in %s, load photo params failed\n", __FUNCTION__);
        return false;
    }

    highQualityBlend = highQualityBlend_;
    srcSize = srcSize_;
    dstSize = dstSize_;

    std::vector<cv::Mat> masks, dstSrcMaps, dstSrcXMaps, dstSrcYMaps;
    try
    {        
        if (highQualityBlend)
        {
            numImages = params.size();
            getReprojectMaps32FAndMasks(params, srcSize, dstSize, dstSrcXMaps, dstSrcYMaps, masks);
            if (!mbBlender.prepare(masks, blendParam_, MIN_SIDE_LENGTH))
            {
                ztool::lprintf("Error in %s, failed to prepare for blending\n", __FUNCTION__);
                return false;
            }

            size_t gpuTotalMemSize, gpuFreeMemSize;
            cudaMemGetInfo(&gpuFreeMemSize, &gpuTotalMemSize);

            long long int origImagesMemSize, reprojImagesMemSize, mapsMemSize, blenderMemSize, totalMemSize;
            origImagesMemSize = (long long int)(numImages) * srcSize.width * srcSize.height * 4;
            reprojImagesMemSize = (long long int)(numImages) * dstSize.width * dstSize.height * 2 * 4;
            mapsMemSize = (long long int)(numImages)* dstSize.width * dstSize.height * 4 * 2;
            blenderMemSize = CudaTilingMultibandBlendFast::estimateMemorySize(dstSize.width, dstSize.height, numImages);
            totalMemSize = origImagesMemSize + reprojImagesMemSize + mapsMemSize + blenderMemSize;

            long long int memSizeDiff = gpuFreeMemSize - totalMemSize;
            if (memSizeDiff < 500000000)
            {
                ztool::lprintf("Info in %s, estimated gpu mem size for stream based high quality blend is %lld bytes, "
                    "free gpu mem size is %lld bytes, disable streams, use %lld gpu mem size only\n", 
                    __FUNCTION__, totalMemSize, gpuFreeMemSize, 
                    origImagesMemSize / numImages + reprojImagesMemSize / numImages + blenderMemSize);

                useStreams = 0;
            }
            else
            {
                ztool::lprintf("Info in %s, estimated gpu mem size for stream based high quality blend is %lld bytes, "
                    "free gpu mem size is %lld bytes, enable streams\n", __FUNCTION__, totalMemSize, gpuFreeMemSize);

                dstSrcXMapsGPU.resize(numImages);
                dstSrcYMapsGPU.resize(numImages);
                for (int i = 0; i < numImages; i++)
                {
                    dstSrcXMapsGPU[i].upload(dstSrcXMaps[i]);
                    dstSrcYMapsGPU[i].upload(dstSrcYMaps[i]);
                }

                streams.resize(numImages);
                useStreams = 1;
            }
        }
        else
        {
            numImages = params.size();
            getReprojectMaps32FAndMasks(params, srcSize, dstSize, dstSrcXMaps, dstSrcYMaps, masks);

            std::vector<cv::Mat> weights;
            //getWeightsLinearBlendBoundedRadius32F(masks, dstSize.width * 0.05, 10, weights);
            getWeightsLinearBlend32F(masks, blendParam_, weights);
            weightsGPU.resize(numImages);
            for (int i = 0; i < numImages; i++)
                weightsGPU[i].upload(weights[i]);
            accumGPU.create(dstSize, CV_32FC4);

            dstSrcXMapsGPU.resize(numImages);
            dstSrcYMapsGPU.resize(numImages);
            for (int i = 0; i < numImages; i++)
            {
                dstSrcXMapsGPU[i].upload(dstSrcXMaps[i]);
                dstSrcYMapsGPU[i].upload(dstSrcYMaps[i]);
            }

            streams.resize(numImages);
            useStreams = 1;
        }
    }
    catch (std::exception& e)
    {
        ztool::lprintf("Error in %s, exception caught: %s\n", __FUNCTION__, e.what());
        return false;
    }

    success = 1;
    return true;
}

bool CudaPanoramaRender2::render(const std::vector<cv::Mat>& src, cv::cuda::GpuMat& dst,
    const std::vector<std::vector<std::vector<unsigned char> > >& luts)
{
    if (!success)
    {
        ztool::lprintf("Error in %s, have not prepared or prepare failed before\n", __FUNCTION__);
        return false;
    }

    if (src.size() != numImages)
    {
        ztool::lprintf("Error in %s, size not equal\n", __FUNCTION__);
        return false;
    }

    for (int i = 0; i < numImages; i++)
    {
        if (src[i].size() != srcSize)
        {
            ztool::lprintf("Error in %s, src[%d] size (%d, %d), not equal to (%d, %d)\n",
                __FUNCTION__, i, src[i].size().width, src[i].size().height,
                srcSize.width, srcSize.height);
            return false;
        }

        if (src[i].type() != CV_8UC4)
        {
            ztool::lprintf("Error in %s, type %d not equal to %d\n", __FUNCTION__, src[i].type(), CV_8UC4);
            return false;
        }
    }

    bool correct = true;
    if (luts.size() != numImages)
        correct = false;
    if (luts.size() == numImages)
    {
        for (int i = 0; i < numImages; i++)
        {
            if (luts[i].size() != 3)
            {
                correct = false;
                break;
            }

            for (int j = 0; j < 3; j++)
            {
                if (luts[i][j].size() != 256)
                {
                    correct = false;
                    break;
                }
            }
            if (!correct)
                break;
        }
    }
    if (!correct && luts.size())
        ztool::lprintf("Warning in %s, the non-empty look up tables not satisfied, skip correction\n", __FUNCTION__);

    try
    {
        if (!highQualityBlend)
        {
            accumGPU.setTo(0);
            srcImagesGPU.resize(numImages);
            reprojImagesGPU.resize(numImages);
            for (int i = 0; i < numImages; i++)
                srcImagesGPU[i].upload(src[i], streams[i]);
            if (correct)
            {
                for (int i = 0; i < numImages; i++)
                    cudaTransform(srcImagesGPU[i], srcImagesGPU[i], luts[i], streams[i]);
            }
            for (int i = 0; i < numImages; i++)
                cudaReprojectWeightedAccumulateTo32F(srcImagesGPU[i], accumGPU, dstSrcXMapsGPU[i], dstSrcYMapsGPU[i], weightsGPU[i], streams[i]);
            for (int i = 0; i < numImages; i++)
                streams[i].waitForCompletion();
            accumGPU.convertTo(dst, CV_8U);
        }
        else
        {
            if (useStreams)
            {
                srcImagesGPU.resize(numImages);
                reprojImagesGPU.resize(numImages);
                // Add the following two lines to prevent exception if dstSize is around (1200, 600)
                //for (int i = 0; i < numImages; i++)
                //    reprojImagesGPU[i].create(dstSize, CV_16SC4);
                // Further test shows that the above two lines cannot prevent cuda runtime
                // from throwing exception, so they are commented.
                // It seems that the only way to avoid exception is to call 
                // cudaReproject instead of cudaReprojectTo16S, but then CudaTilingMultibandBlend::blend
                // will perform data conversion from type CV_8UC4 to CV_16SC4, more time consumed.
                for (int i = 0; i < numImages; i++)
                    srcImagesGPU[i].upload(src[i], streams[i]);
                if (correct)
                {
                    for (int i = 0; i < numImages; i++)
                        cudaTransform(srcImagesGPU[i], srcImagesGPU[i], luts[i], streams[i]);
                }
                for (int i = 0; i < numImages; i++)
                    cudaReprojectTo16S(srcImagesGPU[i], reprojImagesGPU[i], dstSrcXMapsGPU[i], dstSrcYMapsGPU[i], streams[i]);
                for (int i = 0; i < numImages; i++)
                    streams[i].waitForCompletion();

                mbBlender.blend(reprojImagesGPU, dst);
            }
            else
            {
                srcImagesGPU.resize(1);
                reprojImagesGPU.resize(1);
                mbBlender.begin();
                for (int i = 0; i < numImages; i++)
                {
                    srcImagesGPU[0].upload(src[i]);
                    if (correct)
                        cudaTransform(srcImagesGPU[0], srcImagesGPU[0], luts[i]);
                    cudaReprojectTo16S(srcImagesGPU[0], reprojImagesGPU[0], dstSize, params[i]);
                    mbBlender.tile(reprojImagesGPU[0], i);
                }
                mbBlender.end(dst);
            }
        }
    }
    catch (std::exception& e)
    {
        ztool::lprintf("Error in %s, exception caught: %s\n", __FUNCTION__, e.what());
        return false;
    }

    return true;
}

void CudaPanoramaRender2::clear()
{
    params.clear();
    dstUniqueMasksGPU.clear();
    currMasksGPU.clear();
    dstSrcXMapsGPU.clear();
    dstSrcYMapsGPU.clear();
    srcImagesGPU.clear();
    reprojImagesGPU.clear();
    weightsGPU.clear();
    streams.clear();
    useStreams = 0;
    highQualityBlend = 0;
    numImages = 0;
    success = 0;
}

int CudaPanoramaRender2::getNumImages() const
{
    return success ? numImages : 0;
}

bool CudaRicohPanoramaRender::prepare(const std::string& path, int highQualityBlend, int blendParam, 
    const cv::Size& srcSize, const cv::Size& dstSize)
{
    bool ok = CudaPanoramaRender2::prepare(path, highQualityBlend, blendParam, srcSize, dstSize);
    if (!ok)
        return false;
    if (numImages != 2)
    {
        ztool::lprintf("Error in %s, num images = %d, requires 2\n", __FUNCTION__, numImages);
        CudaPanoramaRender2::clear();
        return false;
    }
    return true;
}

bool CudaRicohPanoramaRender::render(const std::vector<cv::Mat>& src, cv::cuda::GpuMat& dst,
    const std::vector<std::vector<std::vector<unsigned char> > >& luts)
{
    if (!success)
    {
        ztool::lprintf("Error in %s, have not prepared or prepare failed before\n", __FUNCTION__);
        return false;
    }

    if (src.size() != 1)
    {
        ztool::lprintf("Error in %s, src size = %d, requires 1\n", __FUNCTION__, src.size());
        return false;
    }

    std::vector<cv::Mat> newSrc(2);
    newSrc[0] = newSrc[1] = src[0];
    return CudaPanoramaRender2::render(newSrc, dst, luts);
}

void CudaRicohPanoramaRender::clear()
{
    CudaPanoramaRender2::clear();
}

int CudaRicohPanoramaRender::getNumImages() const
{
    return success ? 1 : 0;
}

static int cpuMultibandBlendMT = 0;

void setCPUMultibandBlendMultiThread(bool multiThread)
{
    cpuMultibandBlendMT = multiThread;
}

bool CPUPanoramaRender::prepare(const std::string& path_, int highQualityBlend_, int blendParam_, 
    const cv::Size& srcSize_, const cv::Size& dstSize_)
{
    clear();

    success = 0;

    if (!((dstSize_.width & 1) == 0 && (dstSize_.height & 1) == 0 &&
        dstSize_.height * 2 == dstSize_.width))
    {
        ztool::lprintf("Error in %s, dstSize not qualified\n", __FUNCTION__);
        return false;
    }

    if (blendParam_ <= 0)
    {
        ztool::lprintf("Error in %s, blendParam = %d, should be positive\n", __FUNCTION__, blendParam_);
        return false;
    }

    std::vector<PhotoParam> params;
    bool ok = loadPhotoParams(path_, params);
    if (!ok || params.empty())
    {
        ztool::lprintf("Error in %s, load photo params failed\n", __FUNCTION__);
        return false;
    }

    highQualityBlend = highQualityBlend_;
    srcSize = srcSize_;
    dstSize = dstSize_;

    try
    {
        numImages = params.size();
        std::vector<cv::Mat> masks;
        getReprojectMapsAndMasks(params, srcSize, dstSize, maps, masks);
        if (highQualityBlend)
        {
            if (cpuMultibandBlendMT)
                mbBlender.reset(new TilingMultibandBlendFastParallel);
            else
                mbBlender.reset(new TilingMultibandBlendFast);
            if (!mbBlender->prepare(masks, blendParam_, MIN_SIDE_LENGTH))
            {
                ztool::lprintf("Error in %s, multiband blend prepare failed\n", __FUNCTION__);
                return false;
            }
        }
        else
        {
            //getWeightsLinearBlendBoundedRadius32F(masks, dstSize.width * 0.05, 10, weights);
            getWeightsLinearBlend32F(masks, blendParam_, weights);
            accum.create(dstSize, CV_32FC3);
        }
    }
    catch (std::exception& e)
    {
        ztool::lprintf("Error in %s, exception caught: %s\n", __FUNCTION__, e.what());
        return false;
    }

    success = 1;
    return true;
}

bool CPUPanoramaRender::render(const std::vector<cv::Mat>& src, cv::Mat& dst,
    const std::vector<std::vector<std::vector<unsigned char> > >& luts)
{
    if (!success)
    {
        ztool::lprintf("Error in %s, have not prepared or prepare failed before\n", __FUNCTION__);
        return false;
    }

    if (src.size() != numImages)
    {
        ztool::lprintf("Error in %s, size not equal\n", __FUNCTION__);
        return false;
    }

    for (int i = 0; i < numImages; i++)
    {
        if (src[i].size() != srcSize)
        {
            ztool::lprintf("Error in %s, src[%d] size (%d, %d), not equal to (%d, %d)\n",
                __FUNCTION__, i, src[i].size().width, src[i].size().height,
                srcSize.width, srcSize.height);
            return false;
        }

        if (src[i].type() != CV_8UC3)
        {
            ztool::lprintf("Error in %s, type %d not equal to %d\n", __FUNCTION__, src[i].type(), CV_8UC3);
            return false;
        }
    }

    bool correct = true;
    if (luts.size() != numImages)
        correct = false;
    if (luts.size() == numImages)
    {
        for (int i = 0; i < numImages; i++)
        {
            if (luts[i].size() != 3)
            {
                correct = false;
                break;
            }

            for (int j = 0; j < 3; j++)
            {
                if (luts[i][j].size() != 256)
                {
                    correct = false;
                    break;
                }
            }
            if (!correct)
                break;
        }
    }
    if (!correct && luts.size())
        ztool::lprintf("Warning in %s, the non-empty look up tables not satisfied, skip correction\n", __FUNCTION__);

    try
    {
        if (!highQualityBlend)
        {
            accum.setTo(0);
            if (correct)
            {
                for (int i = 0; i < numImages; i++)
                {
                    transform(src[i], correctImage, luts[i]);
                    reprojectWeightedAccumulateParallelTo32F(correctImage, accum, maps[i], weights[i]);
                }
            }
            else
            {
                for (int i = 0; i < numImages; i++)
                    reprojectWeightedAccumulateParallelTo32F(src[i], accum, maps[i], weights[i]);
            }
            accum.convertTo(dst, CV_8U);
        }
        else
        {
            reprojImages.resize(numImages);
            if (correct)
            {
                for (int i = 0; i < numImages; i++)
                {
                    transform(src[i], correctImage, luts[i]);
                    reprojectParallelTo16S(correctImage, reprojImages[i], maps[i]);
                }
            }
            else
            {
                for (int i = 0; i < numImages; i++)
                    reprojectParallelTo16S(src[i], reprojImages[i], maps[i]);
            }
            mbBlender->blend(reprojImages, dst);
        }
    }
    catch (std::exception& e)
    {
        ztool::lprintf("Error in %s, exception caught: %s\n", __FUNCTION__, e.what());
        return false;
    }

    return true;
}

void CPUPanoramaRender::clear()
{
    maps.clear();
    reprojImages.clear();
    mbBlender.reset();
    weights.clear();
    correctImage.release();
    accum.release();
    success = 0;
    numImages = 0;
    highQualityBlend = 0;
}

int CPUPanoramaRender::getNumImages() const
{
    return success ? numImages : 0;
}

bool CPURicohPanoramaRender::prepare(const std::string& path, int highQualityBlend, int blendParam,
    const cv::Size& srcSize, const cv::Size& dstSize)
{
    bool ok = CPUPanoramaRender::prepare(path, highQualityBlend, blendParam, srcSize, dstSize);
    if (!ok)
        return false;
    if (numImages != 2)
    {
        ztool::lprintf("Error in %s, num images = %d, requires 2\n", __FUNCTION__, numImages);
        CPUPanoramaRender::clear();
        return false;
    }
    return true;
}

bool CPURicohPanoramaRender::render(const std::vector<cv::Mat>& src, cv::Mat& dst,
    const std::vector<std::vector<std::vector<unsigned char> > >& luts)
{
    if (!success)
    {
        ztool::lprintf("Error in %s, have not prepared or prepare failed before\n", __FUNCTION__);
        return false;
    }

    if (src.size() != 1)
    {
        ztool::lprintf("Error in %s, src size = %d, requires 1\n", __FUNCTION__, src.size());
        return false;
    }

    std::vector<cv::Mat> newSrc(2);
    newSrc[0] = newSrc[1] = src[0];
    return CPUPanoramaRender::render(newSrc, dst, luts);
}

void CPURicohPanoramaRender::clear()
{
    CPUPanoramaRender::clear();
}

int CPURicohPanoramaRender::getNumImages() const
{
    return success ? 1 : 0;
}

#include "CompileControl.h"

#if COMPILE_INTEL_OPENCL
#include "IntelOpenCLInterface.h"
#include "RunTimeObjects.h"
#include "MatOp.h"

bool IOclPanoramaRender::prepare(const std::string& path_, int highQualityBlend_, int completeQueue_,
    const cv::Size& srcSize_, const cv::Size& dstSize_)
{
    clear();

    success = 0;

    if (!((dstSize_.width & 1) == 0 && (dstSize_.height & 1) == 0 &&
        dstSize_.height * 2 == dstSize_.width))
    {
        ztool::lprintf("Error in %s, dstSize not qualified\n", __FUNCTION__);
        return false;
    }

    std::vector<PhotoParam> params;
    bool ok = loadPhotoParams(path_, params);
    if (!ok || params.empty())
    {
        ztool::lprintf("Error in %s, load photo params failed\n", __FUNCTION__);
        return false;
    }

    highQualityBlend = highQualityBlend_;
    completeQueue = completeQueue_;
    srcSize = srcSize_;
    dstSize = dstSize_;

    ok = ioclInit();
    if (!ok)
    {
        ztool::lprintf("Error in %s, opencl init failed\n", __FUNCTION__);
        return false;
    }

    try
    {
        numImages = params.size();
        std::vector<cv::Mat> masks, xmaps32F, ymaps32F;
        getReprojectMaps32FAndMasks(params, srcSize, dstSize, xmaps32F, ymaps32F, masks);
        xmaps.resize(numImages);
        ymaps.resize(numImages);
        cv::Mat map32F;
        for (int i = 0; i < numImages; i++)
        {
            xmaps[i].create(dstSize, CV_32FC1);
            ymaps[i].create(dstSize, CV_32FC1);
            cv::Mat headx = xmaps[i].toOpenCVMat();
            cv::Mat heady = ymaps[i].toOpenCVMat();
            xmaps32F[i].copyTo(headx);
            ymaps32F[i].copyTo(heady);
        }
        //if (highQualityBlend)
        //{
        //}
        //else
        {
            std::vector<cv::Mat> ws;
            getWeightsLinearBlendBoundedRadius32F(masks, 75, ws);
            weights.resize(numImages);
            for (int i = 0; i < numImages; i++)
            {
                weights[i].create(dstSize, CV_32FC1);
                cv::Mat header = weights[i].toOpenCVMat();
                ws[i].copyTo(header);
            }
        }

        pool.init(dstSize.height, dstSize.width, CV_32FC4);

        if (completeQueue)
            cpQueue.setMaxSize(4);
    }
    catch (std::exception& e)
    {
        ztool::lprintf("Error in %s, exception caught: %s\n", __FUNCTION__, e.what());
        return false;
    }

    success = 1;
    return true;
}

bool IOclPanoramaRender::render(const std::vector<cv::Mat>& src, const std::vector<long long int>& timeStamps)
{
    ztool::Timer t, tt;;
    if (!success)
    {
        ztool::lprintf("Error in %s, have not prepared or prepare failed before\n", __FUNCTION__);
        return false;
    }

    if (src.size() != numImages)
    {
        ztool::lprintf("Error in %s, size not equal\n", __FUNCTION__);
        return false;
    }

    for (int i = 0; i < numImages; i++)
    {
        if (src[i].size() != srcSize)
        {
            ztool::lprintf("Error in %s, src[%d] size (%d, %d), not equal to (%d, %d)\n",
                __FUNCTION__, i, src[i].size().width, src[i].size().height, 
                srcSize.width, srcSize.height);
            return false;
        }

        if (src[i].type() != CV_8UC4)
        {
            ztool::lprintf("Error in %s, type %d not equal to %d\n", __FUNCTION__, src[i].type(), CV_8UC4);
            return false;
        }

    }

    try
    {
        iocl::UMat blendImage;
        if (!pool.get(blendImage))
            return false;

        //if (!highQualityBlend)
        {
            tt.start();
            setZero(blendImage);
            for (int i = 0; i < numImages; i++)
            {
                iocl::UMat temp(src[i].size(), CV_8UC4, src[i].data, src[i].step);
                ioclReprojectWeightedAccumulateTo32F(temp, blendImage, xmaps[i], ymaps[i], weights[i]);
            }
            tt.end();
        }
        //else
        //{
        //}
        if (completeQueue)
            cpQueue.push(std::make_pair(blendImage, timeStamps[0]));
        else
            rtQueue.push(std::make_pair(blendImage, timeStamps[0]));

        t.end();
        //ztool::lprintf("t = %f, tt = %f\n", t.elapse(), tt.elapse());
    }
    catch (std::exception& e)
    {
        ztool::lprintf("Error in %s, exception caught: %s\n", __FUNCTION__, e.what());
        return false;
    }
    return true;
}

bool IOclPanoramaRender::getResult(cv::Mat& dst, long long int& timeStamp)
{
    std::pair<iocl::UMat, long long int> item;
    bool ret = completeQueue ? cpQueue.pull(item) : rtQueue.pull(item);
    if (ret)
    {
        cv::Mat header = item.first.toOpenCVMat();
        header.convertTo(dst, CV_8U);
        timeStamp = item.second;
    }
    return ret;
}

void IOclPanoramaRender::stop()
{
    rtQueue.stop();
    cpQueue.stop();
}

void IOclPanoramaRender::resume()
{
    rtQueue.resume();
    cpQueue.resume();
}

void IOclPanoramaRender::waitForCompletion()
{
    if (completeQueue)
    {
        while (cpQueue.size())
            std::this_thread::sleep_for(std::chrono::microseconds(25));
    }
    else
    {
        while (rtQueue.size())
            std::this_thread::sleep_for(std::chrono::microseconds(25));
    }
}

void IOclPanoramaRender::clear()
{
    xmaps.clear();
    ymaps.clear();
    weights.clear();
    rtQueue.stop();
    cpQueue.stop();
    pool.clear();
    rtQueue.clear();
    cpQueue.clear();
}

int IOclPanoramaRender::getNumImages() const
{
    return success ? numImages : 0;
}

#endif

#if COMPILE_INTEGRATED_OPENCL
#include "IntelOpenCL/RunTimeObjects.h"
bool IOclPanoramaRender::prepare(const std::string& path_, int highQualityBlend_, int blendParam_,
    const cv::Size& srcSize_, const cv::Size& dstSize_)
{
    clear();

    if (!iocl::init())
    {
        ztool::lprintf("Error in %s, intel opencl initialize failed\n", __FUNCTION__);
        return false;
    }

    success = 0;

    if (!((dstSize_.width & 1) == 0 && (dstSize_.height & 1) == 0 &&
        dstSize_.height * 2 == dstSize_.width))
    {
        ztool::lprintf("Error in %s, dstSize not qualified\n", __FUNCTION__);
        return false;
    }

    std::vector<PhotoParam> params;
    bool ok = loadPhotoParams(path_, params);
    if (!ok || params.empty())
    {
        ztool::lprintf("Error in %s, load photo params failed\n", __FUNCTION__);
        return false;
    }

    highQualityBlend = highQualityBlend_;
    srcSize = srcSize_;
    dstSize = dstSize_;

    try
    {
        numImages = params.size();
        std::vector<cv::Mat> masks;
        xmaps.resize(numImages);
        ymaps.resize(numImages);
        for (int i = 0; i < numImages; i++)
        {
            xmaps[i].create(dstSize, CV_32FC1);
            ymaps[i].create(dstSize, CV_32FC1);
        }
        std::vector<cv::Mat> xheaders(numImages), yheaders(numImages);
        for (int i = 0; i < numImages; i++)
        {
            xheaders[i] = xmaps[i].toOpenCVMat();
            yheaders[i] = ymaps[i].toOpenCVMat();
        }
        getReprojectMaps32FAndMasks(params, srcSize, dstSize, xheaders, yheaders, masks);
        if (highQualityBlend)
        {
            if (!mbBlender.prepare(masks, blendParam_, MIN_SIDE_LENGTH))
            {
                ztool::lprintf("Error in %s, multiband blend prepare failed\n", __FUNCTION__);
                return false;
            }
        }
        else
        {
            weights.resize(numImages);
            for (int i = 0; i < numImages; i++)
                weights[i].create(dstSize, CV_32FC1);
            std::vector<cv::Mat> headers(numImages);
            for (int i = 0; i < numImages; i++)
                headers[i] = weights[i].toOpenCVMat();
            //getWeightsLinearBlendBoundedRadius32F(masks, 75, 10, headers);
            getWeightsLinearBlend32F(masks, blendParam_, headers);
            accum.create(dstSize, CV_32FC4);
        }
    }
    catch (std::exception& e)
    {
        ztool::lprintf("Error in %s, exception caught: %s\n", __FUNCTION__, e.what());
        return false;
    }

    success = 1;
    return true;
}

#include "IntelOpenCL/MatOp.h"
bool IOclPanoramaRender::render(const std::vector<iocl::UMat>& src, iocl::UMat& dst)
{
    if (!success)
    {
        ztool::lprintf("Error in %s, have not prepared or prepare failed before\n", __FUNCTION__);
        return false;
    }

    if (src.size() != numImages)
    {
        ztool::lprintf("Error in %s, size not equal\n", __FUNCTION__);
        return false;
    }

    for (int i = 0; i < numImages; i++)
    {
        if (src[i].size() != srcSize)
        {
            ztool::lprintf("Error in %s, src[%d] size (%d, %d), not equal to (%d, %d)\n",
                __FUNCTION__, i, src[i].size().width, src[i].size().height,
                srcSize.width, srcSize.height);
            return false;
        }

        if (src[i].type != CV_8UC4)
        {
            ztool::lprintf("Error in %s, type %d not equal to %d\n", __FUNCTION__, src[i].type, CV_8UC4);
            return false;
        }

    }

    try
    {
        if (!highQualityBlend)
        {
            setZero(accum);
            for (int i = 0; i < numImages; i++)
                ioclReprojectWeightedAccumulateTo32F(src[i], accum, xmaps[i], ymaps[i], weights[i]);
            //accum.convertTo(dst, CV_8U);
            convert32FC4To8UC4(accum, dst);
        }
        else
        {
            reprojImages.resize(numImages);
            for (int i = 0; i < numImages; i++)
                ioclReprojectTo16S(src[i], reprojImages[i], xmaps[i], ymaps[i]);
            mbBlender.blend(reprojImages, dst);
        }
    }
    catch (std::exception& e)
    {
        ztool::lprintf("Error in %s, exception caught: %s\n", __FUNCTION__, e.what());
        return false;
    }

    return true;
}

void IOclPanoramaRender::clear()
{
    xmaps.clear();
    ymaps.clear();
    reprojImages.clear();
    weights.clear();
    accum.release();
    success = 0;
    numImages = 0;
    highQualityBlend = 0;
}

int IOclPanoramaRender::getNumImages() const
{
    return success ? numImages : 0;
}

#endif
/////////////////////////////////////////////////////////////////////////////////////////////////

#if COMPILE_DISCRETE_OPENCL
#include "OpenCLAccel/CompileControl.h"
#include "OpenCLAccel/ProgramSourceStrings.h"

bool DOclPanoramaRender::prepare(const std::string& path_, int highQualityBlend_, int blendParam_,
    const cv::Size& srcSize_, const cv::Size& dstSize_)
{
    clear();

    if (!docl::init())
    {
        ztool::lprintf("Error in %s, intel opencl initialize failed\n", __FUNCTION__);
        return false;
    }

    success = 0;

    if (!((dstSize_.width & 1) == 0 && (dstSize_.height & 1) == 0 &&
        dstSize_.height * 2 == dstSize_.width))
    {
        ztool::lprintf("Error in %s, dstSize not qualified\n", __FUNCTION__);
        return false;
    }

    std::vector<PhotoParam> params;
    bool ok = loadPhotoParams(path_, params);
    if (!ok || params.empty())
    {
        ztool::lprintf("Error in %s, load photo params failed\n", __FUNCTION__);
        return false;
    }

    highQualityBlend = highQualityBlend_;
    srcSize = srcSize_;
    dstSize = dstSize_;

    try
    {
        numImages = params.size();
        std::vector<cv::Mat> masks;
        std::vector<cv::Mat> xmapsCpu(numImages), ymapsCpu(numImages);
        getReprojectMaps32FAndMasks(params, srcSize, dstSize, xmapsCpu, ymapsCpu, masks);
        xmaps.resize(numImages);
        ymaps.resize(numImages);
        for (int i = 0; i < numImages; i++)
        {
            xmaps[i].upload(xmapsCpu[i]);
            ymaps[i].upload(ymapsCpu[i]);
        }
        if (highQualityBlend)
        {
            if (!mbBlender.prepare(masks, blendParam_, MIN_SIDE_LENGTH))
            {
                ztool::lprintf("Error in %s, multiband blend prepare failed\n", __FUNCTION__);
                return false;
            }

            reprojKernels.resize(numImages);
            queues.resize(numImages);
            for (int i = 0; i < numImages; i++)
            {
                reprojKernels[i].reset(new OpenCLProgramOneKernel(*docl::ocl, 
                    PROG_FILE_NAME(L"ReprojectLinearTemplate.txt"), PROG_STRING(sourceReprojectLinearTemplate), 
                    "reprojectLinearKernel", "-D DST_TYPE=short"));
                queues[i].reset(new OpenCLQueue(*docl::ocl));
            }
        }
        else
        {
            std::vector<cv::Mat> weightsCpu(numImages);
            //getWeightsLinearBlendBoundedRadius32F(masks, 75, 10, weightsCpu);
            getWeightsLinearBlend32F(masks, blendParam_, weightsCpu);
            weights.resize(numImages);
            for (int i = 0; i < numImages; i++)
                weights[i].upload(weightsCpu[i]);
            accum.create(dstSize, CV_32FC4);

            reprojKernels.resize(numImages);
            queues.resize(numImages);
            for (int i = 0; i < numImages; i++)
            {
                reprojKernels[i].reset(new OpenCLProgramOneKernel(*docl::ocl, 
                    PROG_FILE_NAME(L"ReprojectWeightedAccumulate.txt"), PROG_STRING(sourceReprojectWeightedAccumulate), 
                    "reprojectWeightedAccumulateTo32FKernel"));
                queues[i].reset(new OpenCLQueue(*docl::ocl));
            }
        }
    }
    catch (std::exception& e)
    {
        ztool::lprintf("Error in %s, exception caught: %s\n", __FUNCTION__, e.what());
        return false;
    }

    success = 1;
    return true;
}

#include "..\..\source\DiscreteOpenCL\MatOp.h"
bool DOclPanoramaRender::render(const std::vector<docl::HostMem>& src, docl::GpuMat& dst)
{
    if (!success)
    {
        ztool::lprintf("Error in %s, have not prepared or prepare failed before\n", __FUNCTION__);
        return false;
    }

    if (src.size() != numImages)
    {
        ztool::lprintf("Error in %s, size not equal\n", __FUNCTION__);
        return false;
    }

    for (int i = 0; i < numImages; i++)
    {
        if (src[i].size() != srcSize)
        {
            ztool::lprintf("Error in %s, src[%d] size (%d, %d), not equal to (%d, %d)\n",
                __FUNCTION__, i, src[i].size().width, src[i].size().height,
                srcSize.width, srcSize.height);
            return false;
        }

        if (src[i].type != CV_8UC4)
        {
            ztool::lprintf("Error in %s, type %d not equal to %d\n", __FUNCTION__, src[i].type, CV_8UC4);
            return false;
        }

    }

    try
    {
        if (!highQualityBlend)
        {
            images.resize(numImages);
            //for (int i = 0; i < numImages; i++)
            //    images[i].upload(src[i]);
            //setZero(accum);
            //for (int i = 0; i < numImages; i++)
            //    doclReprojectWeightedAccumulateTo32F(images[i], accum, xmaps[i], ymaps[i], weights[i]);
            images.resize(numImages);
            setZero(accum);
            for (int i = 0; i < numImages; i++)
            {
                images[i].upload(src[i], *queues[i]);
                doclReprojectWeightedAccumulateTo32F(images[i], accum, xmaps[i], ymaps[i], weights[i], *reprojKernels[i], *queues[i]);
                //doclReprojectWeightedAccumulateTo32F(src[i], accum, xmaps[i], ymaps[i], weights[i], *reprojKernels[i], *queues[i]);
            }
            for (int i = 0; i < numImages; i++)
                queues[i]->waitForCompletion();
            convert32FC4To8UC4(accum, dst);
        }
        else
        {
            //ztool::Timer t;
            //images.resize(numImages);
            //for (int i = 0; i < numImages; i++)
            //    images[i].upload(src[i]);
            //reprojImages.resize(numImages);
            //for (int i = 0; i < numImages; i++)
            //    doclReprojectTo16S(images[i], reprojImages[i], xmaps[i], ymaps[i]);
            images.resize(numImages);
            reprojImages.resize(numImages);
            for (int i = 0; i < numImages; i++)
            {
                images[i].upload(src[i], *queues[i]);
                doclReprojectTo16S(images[i], reprojImages[i], xmaps[i], ymaps[i], *reprojKernels[i], *queues[i]);
                //doclReprojectTo16S(src[i], reprojImages[i], xmaps[i], ymaps[i], *reprojKernels[i], *queues[i]);
            }
            for (int i = 0; i < numImages; i++)
                queues[i]->waitForCompletion();
            //t.end();
            //printf("t = %f\n", t.elapse());
            mbBlender.blend(reprojImages, dst);
        }
    }
    catch (std::exception& e)
    {
        ztool::lprintf("Error in %s, exception caught: %s\n", __FUNCTION__, e.what());
        return false;
    }

    return true;
}

void DOclPanoramaRender::clear()
{
    xmaps.clear();
    ymaps.clear();
    images.clear();
    reprojImages.clear();
    weights.clear();
    accum.release();
    success = 0;
    numImages = 0;
    highQualityBlend = 0;

    reprojKernels.clear();
    queues.clear();
}

int DOclPanoramaRender::getNumImages() const
{
    return success ? numImages : 0;
}

#endif

/////////////////////////////////////////////////////////////////////////////////////////////////

bool ImageVisualCorrect::prepare(const std::string& path, const cv::Size& srcSize, const cv::Size& dstSize)
{
    maps.clear();
    success = 0;

    bool ok = false;

    std::vector<PhotoParam> params;
    ok = loadPhotoParams(path, params);
    if (!ok)
    {
        ztool::lprintf("Error in %s, load photo params failed\n", __FUNCTION__);
        return false;
    }

    std::vector<cv::Mat> masks;
    getReprojectMapsAndMasks(params, srcSize, dstSize, maps, masks);

    ok = corrector.prepare(masks);
    if (!ok)
    {
        ztool::lprintf("Error in %s, exposure color correct prepare failed\n", __FUNCTION__);
        maps.clear();
        return false;
    }

    numImages = maps.size();
    srcWidth = srcSize.width;
    srcHeight = srcSize.height;
    equiRectWidth = dstSize.width;
    equiRectHeight = dstSize.height;

    success = 1;
    return true;
}

bool ImageVisualCorrect::correct(const std::vector<cv::Mat>& images, std::vector<double>& exposures)
{
    exposures.clear();

    if (!success)
    {
        ztool::lprintf("Error in %s, have not been prepared or prepared not success\n", __FUNCTION__);
        return false;
    }

    if (images.size() != numImages)
    {
        ztool::lprintf("Error in %s, input images num not match, input %d, required %d\n",
            __FUNCTION__, images.size(), numImages);
        return false;
    }

    for (int i = 0; i < numImages; i++)
    {
        if (images[i].cols != srcWidth || images[i].rows != srcHeight)
        {
            ztool::lprintf("Error in %s, input image size invalid\n", __FUNCTION__);
            return false;
        }
    }

    std::vector<cv::Mat> reprojImages;
    reprojectParallel(images, reprojImages, maps);
    bool ok = corrector.correctExposure(reprojImages, exposures);
    return ok;
}

void ImageVisualCorrect::clear()
{
    corrector.clear();
    maps.clear();
    numImages = 0;
    srcWidth = 0;
    srcHeight = 0;
    equiRectWidth = 0;
    equiRectHeight = 0;
    success = 0;
}

bool RicohImageVisualCorrect::prepare(const std::string& path, const cv::Size& srcSize, const cv::Size& dstSize)
{
    bool ok = ImageVisualCorrect::prepare(path, srcSize, dstSize);
    if (!ok)
        return false;

    if (numImages != 2)
    {
        ztool::lprintf("Error in %s, num images = %d, requires 2\n", __FUNCTION__, numImages);
        clear();
        return false;
    }

    return true;
}

bool RicohImageVisualCorrect::correct(const std::vector<cv::Mat>& images, std::vector<double>& exposures)
{
    if (!success)
    {
        ztool::lprintf("Error in %s, have not prepared or prepare failed before\n", __FUNCTION__);
        return false;
    }

    if (images.size() != 1)
    {
        ztool::lprintf("Error in %s, src size = %d, requires 1\n", __FUNCTION__, images.size());
        return false;
    }

    std::vector<cv::Mat> newSrc(2);
    newSrc[0] = newSrc[1] = images[0];
    return ImageVisualCorrect::correct(newSrc, exposures);
}

void RicohImageVisualCorrect::clear()
{
    ImageVisualCorrect::clear();
}

bool ImageVisualCorrect2::prepare(const std::string& path)
{
    success = 0;

    bool ok = loadPhotoParams(path, params);
    if (!ok)
    {
        ztool::lprintf("Error in %s, load photo params failed\n", __FUNCTION__);
        return false;
    }

    numImages = params.size();
    success = 1;
    return true;
}

bool ImageVisualCorrect2::correct(const std::vector<cv::Mat>& images, std::vector<double>& exposures) const
{
    exposures.clear();

    if (!success)
    {
        ztool::lprintf("Error in %s, have not been prepared or prepared not success\n", __FUNCTION__);
        return false;
    }

    if (images.size() != numImages)
    {
        ztool::lprintf("Error in %s, input images num not match, input %d, required %d\n",
            __FUNCTION__, images.size(), numImages);
        return false;
    }

    bool ok = true;
    try
    {
        std::vector<double> rs, bs;
        exposureColorOptimize(images, params, std::vector<int>(), HISTOGRAM, EXPOSURE,
            exposures, rs, bs);
    }
    catch (const std::exception& e)
    {
        ztool::lprintf("Error in %s, exception caught, %s\n", __FUNCTION__, e.what());
        ok = false;
    }
    return ok;
}

bool ImageVisualCorrect2::correct(const std::vector<cv::Mat>& images, std::vector<double>& exposures,
    std::vector<double>& redRatios, std::vector<double>& blueRatios) const
{
    exposures.clear();
    redRatios.clear();
    blueRatios.clear();

    if (!success)
    {
        ztool::lprintf("Error in %s, have not been prepared or prepared not success\n", __FUNCTION__);
        return false;
    }

    if (images.size() != numImages)
    {
        ztool::lprintf("Error in %s, input images num not match, input %d, required %d\n",
            __FUNCTION__, images.size(), numImages);
        return false;
    }

    bool ok = true;
    try
    {
        exposureColorOptimize(images, params, std::vector<int>(), HISTOGRAM, EXPOSURE | WHITE_BALANCE,
            exposures, redRatios, blueRatios);
    }
    catch (const std::exception& e)
    {
        ztool::lprintf("Error in %s, exception caught, %s\n", __FUNCTION__, e.what());
        ok = false;
    }
    return ok;
}

void ImageVisualCorrect2::clear()
{
    params.clear();
    success = 0;
}

bool ImageVisualCorrect2::getLUTs(const std::vector<double>& exposures, std::vector<std::vector<unsigned char> >& luts)
{
    luts.clear();

    int size = exposures.size();
    if (!size)
    {
        ztool::lprintf("Error in %s, exposures.size() == 0\n", __FUNCTION__);
        return false;
    }

    for (int i = 0; i < size; i++)
    {
        if (exposures[i] <= 0)
        {
            ztool::lprintf("Error in %s, exposure[%d] = %f, not positive\n",
                __FUNCTION__, i, exposures[i]);
            return false;
        }
    }

    luts.resize(size);
    for (int i = 0; i < size; i++)
        getLUT(luts[i], exposures[i]);
    
    return true;
}

bool ImageVisualCorrect2::getLUTs(const std::vector<double>& exposures, 
    const std::vector<double>& redRatios, const std::vector<double>& blueRatios, 
    std::vector<std::vector<std::vector<unsigned char> > >& luts)
{
    luts.clear();

    int size = exposures.size();
    if (!size)
    {
        ztool::lprintf("Error in %s, exposures.size() == 0\n", __FUNCTION__);
        return false;
    }

    for (int i = 0; i < size; i++)
    {
        if (exposures[i] <= 0)
        {
            ztool::lprintf("Error in %s, exposure[%d] = %f, not positive\n",
                __FUNCTION__, i, exposures[i]);
            return false;
        }

        if (redRatios[i] <= 0)
        {
            ztool::lprintf("Error in %s, redRatios[%d] = %f, not positive\n",
                __FUNCTION__, i, redRatios[i]);
            return false;
        }

        if (blueRatios[i] <= 0)
        {
            ztool::lprintf("Error in %s, blueRatios[%d] = %f, not positive\n",
                __FUNCTION__, i, blueRatios[i]);
            return false;
        }
    }

    getExposureColorOptimizeLUTs(exposures, redRatios, blueRatios, luts);

    return true;
}

bool RicohImageVisualCorrect2::prepare(const std::string& path)
{
    bool ok = ImageVisualCorrect2::prepare(path);
    if (!ok)
        return false;

    if (numImages != 2)
    {
        ztool::lprintf("Error in %s, num images = %d, requires 2\n", __FUNCTION__, numImages);
        clear();
        return false;
    }

    return true;
}

bool RicohImageVisualCorrect2::correct(const std::vector<cv::Mat>& images, std::vector<double>& exposures) const
{
    if (!success)
    {
        ztool::lprintf("Error in %s, have not prepared or prepare failed before\n", __FUNCTION__);
        return false;
    }

    if (images.size() != 1)
    {
        ztool::lprintf("Error in %s, src size = %d, requires 1\n", __FUNCTION__, images.size());
        return false;
    }

    std::vector<cv::Mat> newSrc(2);
    newSrc[0] = newSrc[1] = images[0];
    return ImageVisualCorrect2::correct(newSrc, exposures);
}

bool RicohImageVisualCorrect2::correct(const std::vector<cv::Mat>& images, std::vector<double>& exposures,
    std::vector<double>& redRatios, std::vector<double>& blueRatios) const
{
    if (!success)
    {
        ztool::lprintf("Error in %s, have not prepared or prepare failed before\n", __FUNCTION__);
        return false;
    }

    if (images.size() != 1)
    {
        ztool::lprintf("Error in %s, src size = %d, requires 1\n", __FUNCTION__, images.size());
        return false;
    }

    std::vector<cv::Mat> newSrc(2);
    newSrc[0] = newSrc[1] = images[0];
    return ImageVisualCorrect2::correct(newSrc, exposures, redRatios, blueRatios);
}

void RicohImageVisualCorrect2::clear()
{
    ImageVisualCorrect2::clear();
}