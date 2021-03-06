﻿#include "ZBlend.h"
#include "ZReproject.h"
#include "AudioVideoProcessor.h"
#include "Timer.h"

#include "opencv2/core.hpp"
#include "opencv2/core/cuda.hpp"
#include "opencv2/highgui.hpp"

int main()
{
    cv::Size dstSize = cv::Size(2048, 1024);
    cv::Size srcSize = cv::Size(1280, 960);

    std::vector<PhotoParam> params;
    loadPhotoParamFromXML("F:\\QQRecord\\452103256\\FileRecv\\test1\\changtai_cam_param.xml", params);

    std::vector<std::string> srcVideoNames;
    srcVideoNames.push_back("F:\\QQRecord\\452103256\\FileRecv\\test1\\YDXJ0078.mp4");
    srcVideoNames.push_back("F:\\QQRecord\\452103256\\FileRecv\\test1\\YDXJ0081.mp4");
    srcVideoNames.push_back("F:\\QQRecord\\452103256\\FileRecv\\test1\\YDXJ0087.mp4");
    srcVideoNames.push_back("F:\\QQRecord\\452103256\\FileRecv\\test1\\YDXJ0108.mp4");
    srcVideoNames.push_back("F:\\QQRecord\\452103256\\FileRecv\\test1\\YDXJ0118.mp4");
    srcVideoNames.push_back("F:\\QQRecord\\452103256\\FileRecv\\test1\\YDXJ0518.mp4");
    int numVideos = srcVideoNames.size();

    int offset[] = {563, 0, 268, 651, 91, 412};
    int numSkip = 200/*1*//*2100*/;

    //ReprojectParam pi;
    //pi.LoadConfig("F:\\QQRecord\\452103256\\FileRecv\\test2\\changtai.xml");
    //pi.SetPanoSize(frameSize);

    //std::vector<std::string> srcVideoNames;
    //srcVideoNames.push_back("F:\\QQRecord\\452103256\\FileRecv\\test2\\YDXJ0072.mp4");
    //srcVideoNames.push_back("F:\\QQRecord\\452103256\\FileRecv\\test2\\YDXJ0075.mp4");
    //srcVideoNames.push_back("F:\\QQRecord\\452103256\\FileRecv\\test2\\YDXJ0080.mp4");
    //srcVideoNames.push_back("F:\\QQRecord\\452103256\\FileRecv\\test2\\YDXJ0101.mp4");
    //srcVideoNames.push_back("F:\\QQRecord\\452103256\\FileRecv\\test2\\YDXJ0112.mp4");
    //srcVideoNames.push_back("F:\\QQRecord\\452103256\\FileRecv\\test2\\YDXJ0512.mp4");
    //int numVideos = srcVideoNames.size();

    //int offset[] = {554, 0, 436, 1064, 164, 785};
    //int numSkip = 3000;

    std::vector<avp::AudioVideoReader> readers(numVideos);
    for (int i = 0; i < numVideos; i++)
    {
        readers[i].open(srcVideoNames[i], false, true, avp::PixelTypeBGR24);
        int count = offset[i] + numSkip;
        readers[i].seek(count * 1000000 / readers[i].getVideoFps() + 0.5, avp::VIDEO);
    }

    std::vector<cv::Mat> dstMasks;
    std::vector<cv::Mat> dstSrcMaps;
    getReprojectMapsAndMasks(params, srcSize, dstSize, dstSrcMaps, dstMasks);

    TilingMultibandBlend blender;
    bool success = blender.prepare(dstMasks, 16, 2);
    //TilingFeatherBlend blender;
    //bool success = blender.prepare(dstMasks);

    int frameCount = 0;
    std::vector<avp::AudioVideoFrame> rawImages(numVideos);
    std::vector<cv::Mat> images(numVideos), reprojImages(numVideos);
    cv::Mat blendImage;

    for (int i = 0; i < numVideos; i++)
    {
        readers[i].read(rawImages[i]);
        images[i] = cv::Mat(rawImages[i].height, rawImages[i].width, CV_8UC3, rawImages[i].data, rawImages[i].step);
    }
    reprojectParallel(images, reprojImages, dstSrcMaps);
    for (int i = 0; i < numVideos; i++)
    {
        char buf[128];
        sprintf(buf, "image%d.bmp", i);
        cv::imwrite(buf, images[i]);
        sprintf(buf, "reprojimage%d.bmp", i);
        cv::imwrite(buf, reprojImages[i]);
        sprintf(buf, "mask%d.bmp", i);
        cv::imwrite(buf, dstMasks[i]);
    }
    return 0;

    ztool::Timer timerTotal, timerDecode, timerReproject, timerBlend, timerEncode;

    avp::AudioVideoWriter writer;
    writer.open("testnewcpu.mp4", "", false, false, "", 0, 0, 0, 0,
        true, "", avp::PixelTypeBGR24, dstSize.width, dstSize.height, readers[0].getVideoFps(), 4000000);
    while (true)
    {
        printf("currCount = %d\n", frameCount++);
        if (frameCount >= 480)
            break;

        timerTotal.start();

        bool success = true;
        timerDecode.start();
        for (int i = 0; i < numVideos; i++)
        {
            if (!readers[i].read(rawImages[i]))
            {
                success = false;
                break;
            }
        }
        timerDecode.end();
        if (!success)
            break;

        timerReproject.start();
        for (int i = 0; i < numVideos; i++)
            images[i] = cv::Mat(rawImages[i].height, rawImages[i].width, CV_8UC3, rawImages[i].data, rawImages[i].step);
        reprojectParallel(images, reprojImages, dstSrcMaps);
        timerReproject.end();

        timerBlend.start();
        blender.blend(reprojImages, dstMasks, blendImage);
        //blender.blend(reprojImages, blendImage);
        timerBlend.end();

        timerEncode.start();
        avp::AudioVideoFrame image = avp::videoFrame(blendImage.data, blendImage.step, avp::PixelTypeBGR24, blendImage.cols, blendImage.rows, -1LL);
        writer.write(image);
        timerEncode.end();

        timerTotal.end();
        printf("time elapsed = %f, dec = %f, proj = %f, blend = %f, enc = %f\n", 
            timerTotal.elapse(), timerDecode.elapse(), timerReproject.elapse(), 
            timerBlend.elapse(), timerEncode.elapse());
    }
    return 0;
}