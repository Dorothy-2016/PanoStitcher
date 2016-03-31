#include "ZBlend.h"
#include "ZBlendAlgo.h"
#include "opencv2/imgproc.hpp"
#include "opencv2/highgui.hpp"

#define ENABLE_MAIN 1

static void getExtendedMasks(const std::vector<cv::Mat>& masks, int radius, std::vector<cv::Mat>& extendedMasks)
{
    int numImages = masks.size();
    std::vector<cv::Mat> uniqueMasks(numImages);
    getNonIntersectingMasks(masks, uniqueMasks);

    std::vector<cv::Mat> compMasks(numImages);
    for (int i = 0; i < numImages; i++)
        cv::bitwise_not(masks[i], compMasks[i]);

    std::vector<cv::Mat> blurMasks(numImages);
    cv::Mat intersect;
    int validCount, r;
    for (r = radius; r > 0; r = r - 2)
    {
        cv::Size blurSize(r * 2 + 1, r * 2 + 1);
        double sigma = r / 3.0;
        validCount = 0;
        for (int i = 0; i < numImages; i++)
        {
            cv::GaussianBlur(uniqueMasks[i], blurMasks[i], blurSize, sigma, sigma);
            cv::bitwise_and(blurMasks[i], compMasks[i], intersect);
            if (cv::countNonZero(intersect) == 0)
                validCount++;
        }
        if (validCount == numImages)
            break;
    }

    extendedMasks.resize(numImages);
    for (int i = 0; i < numImages; i++)
        extendedMasks[i] = (blurMasks[i] != 0);
}

static void getLineParam(const cv::Point& a, const cv::Point& b, cv::Point2d& p, cv::Point2d& dir)
{
    if (a.x == b.x)
    {
        p.x = a.x;
        p.y = (a.y + b.y) * 0.5;
        dir = cv::Point2d(0, 1);
        return;
    }

    double k = double(b.y - a.y) / double(b.x - a.x);
    double h = a.y - k * a.x;

    dir.y = k / sqrt(1 + k * k);
    dir.x = sqrt(1.0 - dir.y * dir.y);
    p.x = (a.x + b.x) * 0.5;
    p.y = (a.y + b.y) * 0.5;
}

inline double distToLine(const cv::Point& a, const cv::Point2d& p, const cv::Point2d& dir)
{
    if (dir.x == 0)
    {
        return abs(a.x - p.x);
    }

    double k = dir.y / dir.x;
    return abs(k * a.x - a.y + p.y - k * p.x) / sqrt(1.0 + k * k);
}

inline void cvtPDirToKH(const cv::Point2d& p, const cv::Point2d& dir, double& k, double& h)
{
    k = dir.y / dir.x;
    h = p.y - k * p.x;
}

static void randomPermute(int size, int count, std::vector<int>& arr, std::vector<int>& buf)
{
    arr.clear();
    CV_Assert(size > 0 && count > 0 && count <= size);
    cv::RNG rng(cv::getTickCount());
    std::vector<int>& total = buf;
    total.resize(size);
    for (int i = 0; i < size; i++)
        total[i] = i;
    for (int i = 0; i < count; i++)
    {
        int index = rng.uniform(i, size);
        std::swap(total[i], total[index]);
    }
    arr.resize(count);
    for (int i = 0; i < count; i++)
        arr[i] = total[i];
}

static void select(const std::vector<cv::Point>& total, const std::vector<int>& index, std::vector<cv::Point>& subset)
{
    subset.clear();
    int size = index.size();
    subset.resize(size);
    for (int i = 0; i < size; i++)
        subset[i] = total[index[i]];
}

static void select(const std::vector<cv::Point>& total, const std::vector<unsigned char>& mask, std::vector<cv::Point>& subset)
{
    subset.clear();
    CV_Assert(total.size() == mask.size());
    int size = total.size();
    subset.reserve(size);
    for (int i = 0; i < size; i++)
    {
        if (mask[i])
            subset.push_back(total[i]);
    }
}

static int getInliers(const std::vector<cv::Point>& points, const cv::Point2d& p, const cv::Point2d& dir, 
    double error, std::vector<unsigned char>& mask)
{
    mask.clear();
    if (points.empty())
        return 0;

    int size = points.size();
    mask.resize(size, 0);
    int count = 0;
    double minError = DBL_MAX, maxError = 0;
    for (int i = 0; i < size; i++)
    {
        double currError = distToLine(points[i], p, dir);
        minError = MIN(currError, minError);
        maxError = MAX(currError, maxError);
        if (currError < error)
        {
            mask[i] = 255;
            count++;
        }
    }
    //printf("in getInliers, minError = %f, maxError = %f\n", minError, maxError);
    return count;
}

static int updateNumIters(double p, double ep, int modelPoints, int maxIters)
{
    CV_Assert(modelPoints > 0);

    p = MAX(p, 0.);
    p = MIN(p, 1.);
    ep = MAX(ep, 0.);
    ep = MIN(ep, 1.);

    // avoid inf's & nan's
    double num = MAX(1. - p, DBL_MIN);
    double denom = 1. - pow(1. - ep, modelPoints);
    if (denom < DBL_MIN)
        return 0;

    num = log(num);
    denom = log(denom);

    return denom >= 0 || -num >= maxIters*(-denom) ?
    maxIters : cvRound(num / denom);
}

static int getLineRANSAC(const std::vector<cv::Point>& points, cv::Point2d& p, cv::Point2d& dir)
{
    CV_Assert(points.size() > 3);
    int size = points.size();
    //printf("RANSAC:\n");
    //printf("size = %d\n", size);

    int numModelPoints = 2;
    int numMaxIters = 1000;
    int numIters = numMaxIters;
    double confidence = 0.99;
    double projError = 2;
    std::vector<unsigned char> currMask, bestMask;
    std::vector<int> currIndex, indexBuf;
    std::vector<cv::Point> currPoints;
    int numMaxInliers = 0;
    for (int i = 0; i < numIters; i++)
    {
        //printf("iter = %d\n", i);
        randomPermute(size, numModelPoints, currIndex, indexBuf);
        select(points, currIndex, currPoints);
        cv::Point2d currP, currDir;
        getLineParam(currPoints[0], currPoints[1], currP, currDir);
        int currNumInliers = getInliers(points, currP, currDir, projError, currMask);
        //printf("currNumInliers = %d\n", currNumInliers);
        if (currNumInliers > MAX(numMaxInliers, numModelPoints - 1))
        {
            numIters = updateNumIters(confidence, double(size - currNumInliers) / size, numModelPoints, numMaxIters);
            //int currNumIters = updateNumIters(confidence, double(size - currNumInliers) / size, numModelPoints, numMaxIters);
            //numIters = MIN(numMaxIters, i > 2 * currNumIters ? currNumIters : currNumIters + i); 
            //printf("numIters changed to %d\n", numIters);
            numMaxInliers = currNumInliers;
            bestMask = currMask;
        }
    }
    select(points, bestMask, currPoints);
    cv::Mat line;
    cv::fitLine(points, line, CV_DIST_L2, 0, 0, 0);
    dir.x = line.at<float>(0);
    dir.y = line.at<float>(1);
    p.x = line.at<float>(2);
    p.y = line.at<float>(3);
    return numMaxInliers;
}

static void calcHist2D(const std::vector<cv::Point>& points, cv::Mat& hist)
{
    hist.create(256, 256, CV_32SC1);
    hist.setTo(0);

    int size = points.size();
    for (int i = 0; i < size; i++)
    {
        cv::Point p = points[i];
        if (p.x >= 0 && p.x < 256 && p.y >= 0 && p.y < 256)
        {
            hist.at<int>(p)++;
        }
    }
}

static void normalizeAndConvert(const cv::Mat& hist, cv::Mat& image)
{
    image.create(256, 256, CV_8UC1);
    double minVal, maxVal;
    cv::minMaxLoc(hist, &minVal, &maxVal);
    double scale = 255.0 / (maxVal - minVal);
    for (int i = 0; i < 256; i++)
    {
        const int* ptrHist = hist.ptr<int>(i);
        unsigned char* ptrImage = image.ptr<unsigned char>(i);
        for (int j = 0; j < 256; j++)
        {
            ptrImage[j] = (ptrHist[j] - minVal) * scale + 0.5;
        }
    }
}

static void getLUT(std::vector<unsigned char>& lut, double k, double b)
{
    lut.resize(256);
    for (int i = 0; i < 256; i++)
        lut[i] = cv::saturate_cast<unsigned char>(i * k + b);
}

void transform(const cv::Mat& src, cv::Mat& dst, const std::vector<unsigned char>& lut, const cv::Mat& mask)
{
    CV_Assert(src.type() == CV_8UC3 && lut.size() == 256 &&
        (!mask.data || (mask.data && src.size() == mask.size())));

    dst.create(src.size(), src.type());

    const unsigned char* ptrLUT = &lut[0];

    int rows = src.rows, cols = src.cols;

    if (mask.data)
    {
        for (int i = 0; i < rows; i++)
        {
            const unsigned char* ptrMask = mask.ptr<unsigned char>(i);
            const unsigned char* ptrSrc = src.ptr<unsigned char>(i);
            unsigned char* ptrDst = dst.ptr<unsigned char>(i);
            for (int j = 0; j < cols; j++)
            {
                if (*(ptrMask++))
                {
                    *(ptrDst++) = ptrLUT[*(ptrSrc++)];
                    *(ptrDst++) = ptrLUT[*(ptrSrc++)];
                    *(ptrDst++) = ptrLUT[*(ptrSrc++)];
                }
                else
                {
                    *(ptrDst++) = 0;
                    *(ptrDst++) = 0;
                    *(ptrDst++) = 0;
                }
            }
        }
    }
    else
    {
        for (int i = 0; i < rows; i++)
        {
            const unsigned char* ptrSrc = src.ptr<unsigned char>(i);
            unsigned char* ptrDst = dst.ptr<unsigned char>(i);
            for (int j = 0; j < cols; j++)
            {
                *(ptrDst++) = ptrLUT[*(ptrSrc++)];
                *(ptrDst++) = ptrLUT[*(ptrSrc++)];
                *(ptrDst++) = ptrLUT[*(ptrSrc++)];
            }
        }
    }
}

bool MultibandBlendGainAdjust::prepare(const std::vector<cv::Mat>& masks, int radius)
{
    prepareSuccess = false;
    calcGainSuccess = false;
    if (masks.empty())
        return false;
    if (radius < 1)
        return false;

    int currNumMasks = masks.size();
    int currRows = masks[0].rows, currCols = masks[0].cols;
    for (int i = 0; i < currNumMasks; i++)
    {
        if (!masks[i].data || masks[i].type() != CV_8UC1 ||
            masks[i].rows != currRows || masks[i].cols != currCols)
            return false;
    }
    numImages = currNumMasks;
    rows = currRows;
    cols = currCols;

    blender.prepare(masks, 20, 2);
    getExtendedMasks(masks, radius, extendedMasks);

    luts.resize(numImages);
    for (int i = 0; i < numImages; i++)
    {
        luts[i].resize(256);
        for (int j = 0; j < 256; j++)
            luts[i][j] = j;
    }

    prepareSuccess = true;
    calcGainSuccess = true;
    return true;
}

bool MultibandBlendGainAdjust::calcGain(const std::vector<cv::Mat>& images, std::vector<std::vector<unsigned char> >& LUTs)
{
    if (!prepareSuccess)
        return false;

    if (numImages != images.size())
        return false;

    for (int i = 0; i < numImages; i++)
    {
        if (!images[i].data || images[i].type() != CV_8UC3 ||
            images[i].rows != rows || images[i].cols != cols)
            return false;
    }

    blender.blend(images, blendImage);

    std::vector<double> kvals(numImages), hvals(numImages);

    for (int k = 0; k < numImages; k++)
    {
        const cv::Mat& image = images[k];
        const cv::Mat& mask = extendedMasks[k];

        int count = cv::countNonZero(mask);
        std::vector<cv::Point> valPairs(count);
        int rows = mask.rows, cols = mask.cols;
        cv::Mat blendGray, imageGray;
        cv::cvtColor(blendImage, blendGray, CV_BGR2GRAY);
        cv::cvtColor(image, imageGray, CV_BGR2GRAY);
        int index = 0;
        for (int i = 0; i < rows; i++)
        {
            const unsigned char* ptrBlend = blendGray.ptr<unsigned char>(i);
            const unsigned char* ptrImage = imageGray.ptr<unsigned char>(i);
            const unsigned char* ptrMask = mask.ptr<unsigned char>(i);
            for (int j = 0; j < cols; j++)
            {
                if (ptrMask[j])
                    valPairs[index++] = cv::Point(ptrImage[j], ptrBlend[j]);
            }
        }
        cv::Point2d p, dir;
        getLineRANSAC(valPairs, p, dir);

        //cv::Mat hist2D, histShow;
        //calcHist2D(valPairs, hist2D);
        //normalizeAndConvert(hist2D, histShow);
        //cv::imshow("hist", histShow);
        //cv::imwrite("hist.bmp", histShow);

        //double r = 500;
        //cv::Point2d p0(p.x + dir.x * r, p.y + dir.y * r), p1(p.x - dir.x * r, p.y - dir.y * r);
        //cv::Mat lineShow = cv::Mat::zeros(256, 256, CV_8UC1);
        //cv::line(lineShow, p0, p1, cv::Scalar(255));
        //cv::imshow("line", lineShow);
        //cv::waitKey(0);

        cvtPDirToKH(p, dir, kvals[k], hvals[k]);
    }

    for (int i = 0; i < numImages; i++)
        getLUT(luts[i], kvals[i], hvals[i]);

    LUTs = luts;

    calcGainSuccess = true;

    return true;
}

void calcAccumHist(const cv::Mat& image, const cv::Mat& mask, std::vector<double>& hist)
{
    CV_Assert(image.data && image.type() == CV_8UC1 && 
        mask.data && mask.type() == CV_8UC1 && image.size() == mask.size());

    hist.resize(256, 0);
    std::vector<int> tempHist(256, 0);
    int rows = image.rows, cols = image.cols;
    for (int i = 0; i < rows; i++)
    {
        const unsigned char* ptr = image.ptr<unsigned char>(i);
        const unsigned char* ptrMask = mask.ptr<unsigned char>(i);
        for (int j = 0; j < cols; j++)
        {
            if (ptrMask[j])
                tempHist[ptr[j]] += 1;
        }
            
    }
    for (int i = 1; i < 256; i++)
        tempHist[i] += tempHist[i - 1];
    double scale = 1.0 / tempHist[255];
    for (int i = 0; i < 256; i++)
        hist[i] = tempHist[i] * scale;
}

void histSpecification(std::vector<double>& src, std::vector<double>& dst, std::vector<unsigned char>& lut)
{
    CV_Assert(src.size() == 256 && dst.size() == 256);

    lut.resize(256);
    for (int i = 0; i < 256; i++)
    {
        double val = src[i];
        double minDiff = fabs(val - dst[0]);
        int index = 0;
        for (int j = 1; j < 256; j++)
        {
            double currDiff = fabs(val - dst[j]);
            if (currDiff < minDiff)
            {
                index = j;
                minDiff = currDiff;
            }
        }
        lut[i] = index;
    }
}

void calcHistSpecLUT(const cv::Mat& src, const cv::Mat& srcMask,
    const cv::Mat& dst, const cv::Mat& dstMask, std::vector<unsigned char>& lutSrcToDst)
{
    CV_Assert(src.data && src.type() == CV_8UC1 && srcMask.data && srcMask.type() == CV_8UC1 &&
        dst.data && dst.type() == CV_8UC1 && dstMask.data && dstMask.type() == CV_8UC1 &&
        src.size() == srcMask.size() && dst.size() == dstMask.size() && src.size() == dst.size());

    cv::Mat intersect = srcMask & dstMask;
    std::vector<double> srcAccumHist, dstAccumHist;
    calcAccumHist(src, intersect, srcAccumHist);
    calcAccumHist(dst, intersect, dstAccumHist);
    histSpecification(srcAccumHist, dstAccumHist, lutSrcToDst);
}

#if ENABLE_MAIN

int main()
{
    std::vector<std::string> imagePaths;
    //imagePaths.push_back("F:\\panoimage\\detuoffice\\image0.bmp");
    //imagePaths.push_back("F:\\panoimage\\detuoffice\\image1.bmp");
    //imagePaths.push_back("F:\\panoimage\\detuoffice\\image2.bmp");
    //imagePaths.push_back("F:\\panoimage\\detuoffice\\image3.bmp");
    imagePaths.push_back("F:\\panoimage\\zhanxiang\\0.bmp");
    imagePaths.push_back("F:\\panoimage\\zhanxiang\\1.bmp");
    imagePaths.push_back("F:\\panoimage\\zhanxiang\\2.bmp");
    imagePaths.push_back("F:\\panoimage\\zhanxiang\\3.bmp");
    imagePaths.push_back("F:\\panoimage\\zhanxiang\\4.bmp");
    imagePaths.push_back("F:\\panoimage\\zhanxiang\\5.bmp");
    std::vector<std::string> maskPaths;
    //maskPaths.push_back("F:\\panoimage\\detuoffice\\mask0.bmp");
    //maskPaths.push_back("F:\\panoimage\\detuoffice\\mask1.bmp");
    //maskPaths.push_back("F:\\panoimage\\detuoffice\\mask2.bmp");
    //maskPaths.push_back("F:\\panoimage\\detuoffice\\mask3.bmp");
    maskPaths.push_back("F:\\panoimage\\zhanxiang\\0mask.bmp");
    maskPaths.push_back("F:\\panoimage\\zhanxiang\\1mask.bmp");
    maskPaths.push_back("F:\\panoimage\\zhanxiang\\2mask.bmp");
    maskPaths.push_back("F:\\panoimage\\zhanxiang\\3mask.bmp");
    maskPaths.push_back("F:\\panoimage\\zhanxiang\\4mask.bmp");
    maskPaths.push_back("F:\\panoimage\\zhanxiang\\5mask.bmp");

    int numImages = imagePaths.size();
    std::vector<cv::Mat> images(numImages), masks(numImages), grayImages(numImages);

    for (int k = 0; k < numImages; k++)
    {
        images[k] = cv::imread(imagePaths[k]);
        masks[k] = cv::imread(maskPaths[k], -1);
        cv::cvtColor(images[k], grayImages[k], CV_BGR2GRAY);
    }

    std::vector<unsigned char> lut;
    calcHistSpecLUT(grayImages[4], masks[4], grayImages[0], masks[0], lut);

    cv::Mat result;
    transform(images[4], result, lut);
    cv::imshow("old", images[4]);
    cv::imshow("new", result);
    cv::waitKey(0);
    return 0;
}

#endif