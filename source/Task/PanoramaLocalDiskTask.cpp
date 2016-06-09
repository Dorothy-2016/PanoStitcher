#include "PanoramaTask.h"
#include "PanoramaTaskUtil.h"
#include "ConcurrentQueue.h"
#include "ZBlend.h"
#include "ZReproject.h"
#include "RicohUtil.h"
#include "PinnedMemoryPool.h"
#include "SharedAudioVideoFramePool.h"
#include "Timer.h"
#include "Image.h"

struct CPUPanoramaLocalDiskTask::Impl
{
    Impl();
    ~Impl();
    bool init(const std::vector<std::string>& srcVideoFiles, const std::vector<int> offsets, int audioIndex,
        const std::string& cameraParamFile, const std::string& customMaskFile, const std::string& dstVideoFile, int dstWidth, int dstHeight,
        int dstVideoBitRate, const std::string& dstVideoEncoder, const std::string& dstVideoPreset, 
        int dstVideoMaxFrameCount);
    bool start();
    void waitForCompletion();
    int getProgress() const;
    void cancel();

    void getLastSyncErrorMessage(std::string& message) const;
    bool hasAsyncErrorMessage() const;
    void getLastAsyncErrorMessage(std::string& message);

    void run();
    void clear();

    int numVideos;
    int audioIndex;
    cv::Size srcSize, dstSize;
    std::vector<avp::AudioVideoReader> readers;
    std::vector<cv::Mat> dstSrcMaps, dstMasks, dstUniqueMasks, currMasks;
    int useCustomMasks;
    std::vector<CustomIntervaledMasks> customMasks;
    TilingMultibandBlendFast blender;
    std::vector<cv::Mat> reprojImages;
    cv::Mat blendImage;
    LogoFilter logoFilter;
    avp::AudioVideoWriter2 writer;
    bool endFlag;
    std::atomic<int> finishPercent;
    int validFrameCount;
    std::unique_ptr<std::thread> thread;

    std::string syncErrorMessage;
    std::mutex mtxAsyncErrorMessage;
    std::string asyncErrorMessage;
    int hasAsyncError;
    void setAsyncErrorMessage(const std::string& message);
    void clearAsyncErrorMessage();

    bool initSuccess;
    bool finish;
};

CPUPanoramaLocalDiskTask::Impl::Impl()
{
    clear();
}

CPUPanoramaLocalDiskTask::Impl::~Impl()
{
    clear();
}

bool CPUPanoramaLocalDiskTask::Impl::init(const std::vector<std::string>& srcVideoFiles, const std::vector<int> offsets,
    int tryAudioIndex, const std::string& cameraParamFile, const std::string& customMaskFile, const std::string& dstVideoFile, int dstWidth, int dstHeight,
    int dstVideoBitRate, const std::string& dstVideoEncoder, const std::string& dstVideoPreset,
    int dstVideoMaxFrameCount)
{
    clear();

    if (srcVideoFiles.empty() || (srcVideoFiles.size() != offsets.size()))
    {
        ptlprintf("Error in %s, size of srcVideoFiles and size of offsets empty or unmatch.\n", __FUNCTION__);
        syncErrorMessage = "����У��ʧ�ܡ�";
        return false;
    }

    numVideos = srcVideoFiles.size();

    std::vector<PhotoParam> params;
    if (!loadPhotoParams(cameraParamFile, params))
    {
        ptlprintf("Error in %s, failed to load params\n", __FUNCTION__);
        syncErrorMessage = "��ʼ��ƴ��ʧ�ܡ�";
        return false;
    }
    if (params.size() != numVideos)
    {
        ptlprintf("Error in %s, params.size() != numVideos\n", __FUNCTION__);
        syncErrorMessage = "��ʼ��ƴ��ʧ�ܡ�";
        return false;
    }

    dstSize.width = dstWidth;
    dstSize.height = dstHeight;

    bool ok = false;
    ok = prepareSrcVideos(srcVideoFiles, true, offsets, tryAudioIndex, readers, audioIndex, srcSize, validFrameCount);
    if (!ok)
    {
        ptlprintf("Error in %s, could not open video file(s)\n", __FUNCTION__);
        syncErrorMessage = "����Ƶʧ�ܡ�";
        return false;
    }

    if (dstVideoMaxFrameCount > 0 && validFrameCount > dstVideoMaxFrameCount)
        validFrameCount = dstVideoMaxFrameCount;

    getReprojectMapsAndMasks(params, srcSize, dstSize, dstSrcMaps, dstMasks);

    ok = blender.prepare(dstMasks, 16, 2);
    //ok = blender.prepare(dstMasks, 50);
    if (!ok)
    {
        ptlprintf("Error in %s, blender prepare failed\n", __FUNCTION__);
        syncErrorMessage = "��ʼ��ƴ��ʧ�ܡ�";
        return false;
    }

    useCustomMasks = 0;
    if (customMaskFile.size())
    {
        std::vector<std::vector<IntervaledContour> > contours;
        ok = loadIntervaledContours(customMaskFile, contours);
        if (!ok)
        {
            ptlprintf("Error in %s, load custom masks failed\n", __FUNCTION__);
            syncErrorMessage = "��ʼ��ƴ��ʧ�ܡ�";
            return false;
        }
        if (contours.size() != numVideos)
        {
            ptlprintf("Error in %s, loaded contours.size() != numVideos\n", __FUNCTION__);
            syncErrorMessage = "��ʼ��ƴ��ʧ�ܡ�";
            return false;
        }
        if (!cvtContoursToMasks(contours, dstMasks, customMasks))
        {
            ptlprintf("Error in %s, convert contours to customMasks failed\n", __FUNCTION__);
            syncErrorMessage = "��ʼ��ƴ��ʧ�ܡ�";
            return false;
        }
        blender.getUniqueMasks(dstUniqueMasks);
        useCustomMasks = 1;
    }

    ok = logoFilter.init(dstSize.width, dstSize.height, CV_8UC3);
    if (!ok)
    {
        ptlprintf("Error in %s, init logo filter failed\n", __FUNCTION__);
        syncErrorMessage = "��ʼ��ƴ��ʧ�ܡ�";
        return false;
    }

    std::vector<avp::Option> options;
    options.push_back(std::make_pair("preset", dstVideoPreset));
    std::string format = dstVideoEncoder == "h264_qsv" ? "h264_qsv" : "h264";
    if (audioIndex >= 0 && audioIndex < numVideos)
    {
        ok = writer.open(dstVideoFile, "", true, 
            true, "aac", readers[audioIndex].getAudioSampleType(), readers[audioIndex].getAudioChannelLayout(), 
            readers[audioIndex].getAudioSampleRate(), 128000,
            true, format, avp::PixelTypeBGR24, dstSize.width, dstSize.height, readers[0].getVideoFps(), dstVideoBitRate, options);
    }
    else
    {
        ok = writer.open(dstVideoFile, "", false, false, "", avp::SampleTypeUnknown, 0, 0, 0,
            true, format, avp::PixelTypeBGR24, dstSize.width, dstSize.height, readers[0].getVideoFps(), dstVideoBitRate, options);
    }
    if (!ok)
    {
        ptlprintf("Error in %s, video writer open failed\n", __FUNCTION__);
        syncErrorMessage = "�޷�����ƴ����Ƶ��";
        return false;
    }

    finishPercent.store(0);

    initSuccess = true;
    finish = false;
    return true;
}

void CPUPanoramaLocalDiskTask::Impl::run()
{
    if (!initSuccess)
        return;

    if (finish)
        return;

    ptlprintf("Info in %s, write video begin\n", __FUNCTION__);

    int count = 0;
    int step = 1;
    if (validFrameCount > 0)
        step = validFrameCount / 100.0 + 0.5;
    if (step < 1)
        step = 1;
    ptlprintf("Info in %s, validFrameCount = %d, step = %d\n", __FUNCTION__, validFrameCount, step);

    try
    {
        std::vector<avp::AudioVideoFrame> frames(numVideos);
        std::vector<cv::Mat> images(numVideos);
        bool ok = true;
        blendImage.create(dstSize, CV_8UC3);
        while (true)
        {
            ok = true;
            if (audioIndex >= 0 && audioIndex < numVideos)
            {
                if (!readers[audioIndex].read(frames[audioIndex]))
                    break;

                if (frames[audioIndex].mediaType == avp::AUDIO)
                {
                    ok = writer.write(frames[audioIndex]);
                    if (!ok)
                    {
                        ptlprintf("Error in %s, write audio frame fail\n", __FUNCTION__);
                        setAsyncErrorMessage("д����Ƶʧ�ܣ�������ֹ��");
                        break;
                    }
                    continue;
                }
                else
                {
                    images[audioIndex] = cv::Mat(frames[audioIndex].height, frames[audioIndex].width, CV_8UC3,
                        frames[audioIndex].data, frames[audioIndex].step);
                }
            }
            for (int i = 0; i < numVideos; i++)
            {
                if (i == audioIndex)
                    continue;

                if (!readers[i].read(frames[i]))
                {
                    ok = false;
                    break;
                }

                images[i] = cv::Mat(frames[i].height, frames[i].width, CV_8UC3, frames[i].data, frames[i].step);
            }
            if (!ok || endFlag)
                break;

            reprojectParallelTo16S(images, reprojImages, dstSrcMaps);

            if (useCustomMasks)
            {
                bool custom = false;
                currMasks.resize(numVideos);
                for (int i = 0; i < numVideos; i++)
                {
                    if (customMasks[i].getMask(frames[i].timeStamp, currMasks[i]))
                        custom = true;
                    else
                        currMasks[i] = dstUniqueMasks[i];
                }

                if (custom)
                {
                    printf("custom masks\n");
                    blender.blend(reprojImages, currMasks, blendImage);
                }
                else
                    blender.blend(reprojImages, blendImage);
            }
            else
                blender.blend(reprojImages, blendImage);

            if (addLogo)
                ok = logoFilter.addLogo(blendImage);
            if (!ok)
            {
                ptlprintf("Error in %s, add logo fail\n", __FUNCTION__);
                setAsyncErrorMessage("д����Ƶʧ�ܣ�������ֹ��");
                break;
            }
            avp::AudioVideoFrame frame = avp::videoFrame(blendImage.data, blendImage.step, avp::PixelTypeBGR24,
                blendImage.cols, blendImage.rows, frames[0].timeStamp);
            ok = writer.write(frame);
            if (!ok)
            {
                ptlprintf("Error in %s, write video frame fail\n", __FUNCTION__);
                setAsyncErrorMessage("д����Ƶʧ�ܣ�������ֹ��");
                break;
            }

            count++;
            if (count % step == 0)
                finishPercent.store(double(count) / (validFrameCount > 0 ? validFrameCount : 100) * 100);

            if (count >= validFrameCount)
                break;
        }

        for (int i = 0; i < numVideos; i++)
            readers[i].close();
        writer.close();
    }
    catch (std::exception& e)
    {
        ptlprintf("Error in %s, exception caught: %s\n", __FUNCTION__, e.what());
        setAsyncErrorMessage("��Ƶƴ�ӷ�������������ֹ��");
    }

    finishPercent.store(100);

    ptlprintf("Info in %s, write video finish\n", __FUNCTION__);

    finish = true;
}

bool CPUPanoramaLocalDiskTask::Impl::start()
{
    if (!initSuccess)
        return false;

    if (finish)
        return false;

    thread.reset(new std::thread(&CPUPanoramaLocalDiskTask::Impl::run, this));
    return true;
}

void CPUPanoramaLocalDiskTask::Impl::waitForCompletion()
{
    if (thread && thread->joinable())
        thread->join();
    thread.reset(0);
}

int CPUPanoramaLocalDiskTask::Impl::getProgress() const
{
    return finishPercent.load();
}

void CPUPanoramaLocalDiskTask::Impl::cancel()
{
    endFlag = true;
}

void CPUPanoramaLocalDiskTask::Impl::getLastSyncErrorMessage(std::string& message) const
{
    message = syncErrorMessage;
}

bool CPUPanoramaLocalDiskTask::Impl::hasAsyncErrorMessage() const
{
    return hasAsyncError;
}

void CPUPanoramaLocalDiskTask::Impl::getLastAsyncErrorMessage(std::string& message)
{
    if (hasAsyncError)
    {
        std::lock_guard<std::mutex> lg(mtxAsyncErrorMessage);
        message = asyncErrorMessage;
        hasAsyncError = 0;
    }
    else
        message.clear();
}

void CPUPanoramaLocalDiskTask::Impl::clear()
{
    numVideos = 0;
    srcSize = cv::Size();
    dstSize = cv::Size();
    readers.clear();
    dstMasks.clear();
    dstUniqueMasks.clear();
    currMasks.clear();
    useCustomMasks = 0;
    customMasks.clear();
    dstSrcMaps.clear();
    reprojImages.clear();
    writer.close();
    endFlag = false;

    finishPercent.store(0);

    validFrameCount = 0;

    if (thread && thread->joinable())
        thread->join();
    thread.reset(0);

    syncErrorMessage.clear();
    clearAsyncErrorMessage();

    initSuccess = false;
    finish = true;
}

void CPUPanoramaLocalDiskTask::Impl::setAsyncErrorMessage(const std::string& message)
{
    std::lock_guard<std::mutex> lg(mtxAsyncErrorMessage);
    hasAsyncError = 1;
    asyncErrorMessage = message;
}

void CPUPanoramaLocalDiskTask::Impl::clearAsyncErrorMessage()
{
    std::lock_guard<std::mutex> lg(mtxAsyncErrorMessage);
    hasAsyncError = 0;
    asyncErrorMessage.clear();
}

CPUPanoramaLocalDiskTask::CPUPanoramaLocalDiskTask()
{
    ptrImpl.reset(new Impl);
}

CPUPanoramaLocalDiskTask::~CPUPanoramaLocalDiskTask()
{

}

bool CPUPanoramaLocalDiskTask::init(const std::vector<std::string>& srcVideoFiles, const std::vector<int> offsets, int audioIndex,
    const std::string& cameraParamFile, const std::string& customMaskFile, const std::string& dstVideoFile, int dstWidth, int dstHeight,
    int dstVideoBitRate, const std::string& dstVideoEncoder, const std::string& dstVideoPreset, 
    int dstVideoMaxFrameCount)
{
    return ptrImpl->init(srcVideoFiles, offsets, audioIndex, cameraParamFile, customMaskFile, dstVideoFile, dstWidth, dstHeight,
        dstVideoBitRate, dstVideoEncoder, dstVideoPreset, dstVideoMaxFrameCount);
}

bool CPUPanoramaLocalDiskTask::start()
{
    return ptrImpl->start();
}

void CPUPanoramaLocalDiskTask::waitForCompletion()
{
    ptrImpl->waitForCompletion();
}

int CPUPanoramaLocalDiskTask::getProgress() const
{
    return ptrImpl->getProgress();
}

void CPUPanoramaLocalDiskTask::cancel()
{
    ptrImpl->cancel();
}

void CPUPanoramaLocalDiskTask::getLastSyncErrorMessage(std::string& message) const
{
    ptrImpl->getLastSyncErrorMessage(message);
}

bool CPUPanoramaLocalDiskTask::hasAsyncErrorMessage() const
{
    return ptrImpl->hasAsyncErrorMessage();
}

void CPUPanoramaLocalDiskTask::getLastAsyncErrorMessage(std::string& message)
{
    return ptrImpl->getLastAsyncErrorMessage(message);
}

struct StampedPinnedMemoryVector
{
    std::vector<cv::cuda::HostMem> frames;
    std::vector<long long int> timeStamps;
};

typedef BoundedCompleteQueue<avp::SharedAudioVideoFrame> FrameBuffer;
typedef BoundedCompleteQueue<StampedPinnedMemoryVector> FrameVectorBuffer;

struct CudaPanoramaLocalDiskTask::Impl
{
    Impl();
    ~Impl();
    bool init(const std::vector<std::string>& srcVideoFiles, const std::vector<int> offsets, int audioIndex,
        const std::string& cameraParamFile, const std::string& customMaskFile, const std::string& dstVideoFile, int dstWidth, int dstHeight,
        int dstVideoBitRate, const std::string& dstVideoEncoder, const std::string& dstVideoPreset, 
        int dstVideoMaxFrameCount);
    bool start();
    void waitForCompletion();
    int getProgress() const;
    void cancel();

    void getLastSyncErrorMessage(std::string& message) const;
    bool hasAsyncErrorMessage() const;
    void getLastAsyncErrorMessage(std::string& message);

    void clear();

    int numVideos;
    int audioIndex;
    cv::Size srcSize, dstSize;
    std::vector<avp::AudioVideoReader> readers;
    CudaPanoramaRender render;
    PinnedMemoryPool srcFramesMemoryPool;
    SharedAudioVideoFramePool audioFramesMemoryPool, dstFramesMemoryPool;
    FrameVectorBuffer decodeFramesBuffer;
    FrameBuffer procFrameBuffer;
    cv::Mat blendImageCpu;
    LogoFilter logoFilter;
    avp::AudioVideoWriter2 writer;

    int decodeCount;
    int procCount;
    int encodeCount;
    std::atomic<int> finishPercent;
    int validFrameCount;

    void decode();
    void proc();
    void postProc();
    void encode();
    std::unique_ptr<std::thread> decodeThread;
    std::unique_ptr<std::thread> procThread;
    std::unique_ptr<std::thread> postProcThread;
    std::unique_ptr<std::thread> encodeThread;

    std::string syncErrorMessage;
    std::mutex mtxAsyncErrorMessage;
    std::string asyncErrorMessage;
    int hasAsyncError;
    void setAsyncErrorMessage(const std::string& message);
    void clearAsyncErrorMessage();

    bool initSuccess;
    bool finish;
    bool isCanceled;
};

CudaPanoramaLocalDiskTask::Impl::Impl()
{
    clear();
}

CudaPanoramaLocalDiskTask::Impl::~Impl()
{
    clear();
}

bool CudaPanoramaLocalDiskTask::Impl::init(const std::vector<std::string>& srcVideoFiles, const std::vector<int> offsets,
    int tryAudioIndex, const std::string& cameraParamFile, const std::string& customMaskFile, const std::string& dstVideoFile, int dstWidth, int dstHeight,
    int dstVideoBitRate, const std::string& dstVideoEncoder, const std::string& dstVideoPreset, 
    int dstVideoMaxFrameCount)
{
    clear();

    if (srcVideoFiles.empty() || (srcVideoFiles.size() != offsets.size()))
    {
        ptlprintf("Error in %s, size of srcVideoFiles and size of offsets empty or unmatch.\n", __FUNCTION__);
        syncErrorMessage = "����У��ʧ�ܡ�";
        return false;
    }

    numVideos = srcVideoFiles.size();

    dstSize.width = dstWidth;
    dstSize.height = dstHeight;

    bool ok = false;
    ok = prepareSrcVideos(srcVideoFiles, false, offsets, tryAudioIndex, readers, audioIndex, srcSize, validFrameCount);
    if (!ok)
    {
        ptlprintf("Error in %s, could not open video file(s)\n", __FUNCTION__);
        syncErrorMessage = "����Ƶʧ�ܡ�";
        return false;
    }

    if (dstVideoMaxFrameCount > 0 && validFrameCount > dstVideoMaxFrameCount)
        validFrameCount = dstVideoMaxFrameCount;

    ok = srcFramesMemoryPool.init(readers[0].getVideoHeight(), readers[0].getVideoWidth(), CV_8UC4);
    if (!ok)
    {
        ptlprintf("Error in %s, could not init memory pool\n", __FUNCTION__);
        syncErrorMessage = "��ʼ��ƴ��ʧ�ܡ�";
        return false;
    }

    if (audioIndex >= 0 && audioIndex < numVideos)
    {
        ok = audioFramesMemoryPool.initAsAudioFramePool(readers[audioIndex].getAudioSampleType(),
            readers[audioIndex].getAudioNumChannels(), readers[audioIndex].getAudioChannelLayout(),
            readers[audioIndex].getAudioNumSamples());
        if (!ok)
        {
            ptlprintf("Error in %s, could not init memory pool\n", __FUNCTION__);
            syncErrorMessage = "��ʼ��ƴ��ʧ�ܡ�";
            return false;
        }
    }

    ok = render.prepare(cameraParamFile, customMaskFile, true, true, srcSize, dstSize);
    if (!ok)
    {
        ptlprintf("Error in %s, render prepare failed\n", __FUNCTION__);
        syncErrorMessage = "��ʼ��ƴ��ʧ�ܡ�";
        return false;
    }

    if (render.getNumImages() != numVideos)
    {
        ptlprintf("Error in %s, num images in render not equal to num videos\n", __FUNCTION__);
        syncErrorMessage = "��ʼ��ƴ��ʧ�ܡ�";
        return false;
    }

    ok = dstFramesMemoryPool.initAsVideoFramePool(avp::PixelTypeBGR32, dstSize.width, dstSize.height);
    if (!ok)
    {
        ptlprintf("Error in %s, could not init memory pool\n", __FUNCTION__);
        syncErrorMessage = "��ʼ��ƴ��ʧ�ܡ�";
        return false;
    }

    ok = logoFilter.init(dstSize.width, dstSize.height, CV_8UC4);
    if (!ok)
    {
        ptlprintf("Error in %s, init logo filter failed\n", __FUNCTION__);
        syncErrorMessage = "��ʼ��ƴ��ʧ�ܡ�";
        return false;
    }

    std::vector<avp::Option> options;
    options.push_back(std::make_pair("preset", dstVideoPreset));
    std::string format = dstVideoEncoder == "h264_qsv" ? "h264_qsv" : "h264";
    if (audioIndex >= 0 && audioIndex < numVideos)
    {
        ok = writer.open(dstVideoFile, "", true, true, "aac", readers[audioIndex].getAudioSampleType(),
            readers[audioIndex].getAudioChannelLayout(), readers[audioIndex].getAudioSampleRate(), 128000,
            true, format, avp::PixelTypeBGR32, dstSize.width, dstSize.height, readers[0].getVideoFps(), dstVideoBitRate, options);
    }
    else
    {
        ok = writer.open(dstVideoFile, "", false, false, "", avp::SampleTypeUnknown, 0, 0, 0,
            true, format, avp::PixelTypeBGR32, dstSize.width, dstSize.height, readers[0].getVideoFps(), dstVideoBitRate, options);
    }
    if (!ok)
    {
        ptlprintf("Error in %s, video writer open failed\n", __FUNCTION__);
        syncErrorMessage = "�޷�����ƴ����Ƶ��";
        return false;
    }

    decodeFramesBuffer.setMaxSize(4);
    procFrameBuffer.setMaxSize(8);

    finishPercent.store(0);

    initSuccess = true;
    finish = false;
    return true;
}

bool CudaPanoramaLocalDiskTask::Impl::start()
{
    if (!initSuccess)
        return false;

    if (finish)
        return false;

    decodeThread.reset(new std::thread(&CudaPanoramaLocalDiskTask::Impl::decode, this));
    procThread.reset(new std::thread(&CudaPanoramaLocalDiskTask::Impl::proc, this));
    postProcThread.reset(new std::thread(&CudaPanoramaLocalDiskTask::Impl::postProc, this));
    encodeThread.reset(new std::thread(&CudaPanoramaLocalDiskTask::Impl::encode, this));

    return true;
}

void CudaPanoramaLocalDiskTask::Impl::waitForCompletion()
{
    if (decodeThread && decodeThread->joinable())
        decodeThread->join();
    decodeThread.reset();
    if (procThread && procThread->joinable())
        procThread->join();
    procThread.reset(0);
    if (postProcThread && postProcThread->joinable())
        postProcThread->join();
    postProcThread.reset(0);
    if (encodeThread && encodeThread->joinable())
        encodeThread->join();
    encodeThread.reset(0);

    if (!finish)
        ptlprintf("Info in %s, write video finish\n", __FUNCTION__);

    finish = true;
}

int CudaPanoramaLocalDiskTask::Impl::getProgress() const
{
    return finishPercent.load();
}

void CudaPanoramaLocalDiskTask::Impl::getLastSyncErrorMessage(std::string& message) const
{
    message = syncErrorMessage;
}

bool CudaPanoramaLocalDiskTask::Impl::hasAsyncErrorMessage() const
{
    return hasAsyncError;
}

void CudaPanoramaLocalDiskTask::Impl::getLastAsyncErrorMessage(std::string& message)
{
    if (hasAsyncError)
    {
        std::lock_guard<std::mutex> lg(mtxAsyncErrorMessage);
        message = asyncErrorMessage;
        hasAsyncError = 0;
    }
    else
        message.clear();
}

void CudaPanoramaLocalDiskTask::Impl::clear()
{
    numVideos = 0;
    srcSize = cv::Size();
    dstSize = cv::Size();
    readers.clear();
    writer.close();

    srcFramesMemoryPool.clear();
    audioFramesMemoryPool.clear();
    dstFramesMemoryPool.clear();

    decodeFramesBuffer.clear();
    procFrameBuffer.clear();

    decodeCount = 0;
    procCount = 0;
    encodeCount = 0;
    finishPercent.store(0);

    validFrameCount = 0;

    if (decodeThread && decodeThread->joinable())
        decodeThread->join();
    decodeThread.reset(0);
    if (procThread && procThread->joinable())
        procThread->join();
    procThread.reset(0);
    if (postProcThread && postProcThread->joinable())
        postProcThread->join();
    postProcThread.reset(0);
    if (encodeThread && encodeThread->joinable())
        encodeThread->join();
    encodeThread.reset(0);

    syncErrorMessage.clear();
    clearAsyncErrorMessage();

    initSuccess = false;
    finish = true;
    isCanceled = false;
}

void CudaPanoramaLocalDiskTask::Impl::cancel()
{
    isCanceled = true;
}

void CudaPanoramaLocalDiskTask::Impl::decode()
{
    size_t id = std::this_thread::get_id().hash();
    ptlprintf("Thread %s [%8x] started\n", __FUNCTION__, id);

    decodeCount = 0;
    std::vector<avp::AudioVideoFrame> shallowFrames(numVideos);
    avp::SharedAudioVideoFrame audioFrame;

    while (true)
    {
        if (audioIndex >= 0 && audioIndex < numVideos)
        {
            if (!readers[audioIndex].read(shallowFrames[audioIndex]))
                break;

            if (shallowFrames[audioIndex].mediaType == avp::AUDIO)
            {
                audioFramesMemoryPool.get(audioFrame);
                avp::copy(shallowFrames[audioIndex], audioFrame);
                procFrameBuffer.push(audioFrame);
                continue;
            }
        }

        bool successRead = true;
        for (int i = 0; i < numVideos; i++)
        {
            if (i == audioIndex)
                continue;

            if (!readers[i].read(shallowFrames[i]))
            {
                successRead = false;
                break;
            }
        }
        if (!successRead || isCanceled)
            break;

        StampedPinnedMemoryVector deepFrames;
        deepFrames.timeStamps.resize(numVideos);
        deepFrames.frames.resize(numVideos);
        for (int i = 0; i < numVideos; i++)
        {
            srcFramesMemoryPool.get(deepFrames.frames[i]);
            cv::Mat src(shallowFrames[i].height, shallowFrames[i].width, CV_8UC4, shallowFrames[i].data, shallowFrames[i].step);
            cv::Mat dst = deepFrames.frames[i].createMatHeader();
            src.copyTo(dst);
            deepFrames.timeStamps[i] = shallowFrames[i].timeStamp;
        }

        decodeFramesBuffer.push(deepFrames);
        decodeCount++;
        //ptlprintf("decode count = %d\n", decodeCount);

        if (decodeCount >= validFrameCount)
            break;
    }

    if (!isCanceled)
    {
        while (decodeFramesBuffer.size())
            std::this_thread::sleep_for(std::chrono::microseconds(25));
    }    
    decodeFramesBuffer.stop();

    for (int i = 0; i < numVideos; i++)
        readers[i].close();

    ptlprintf("In %s, total decode %d\n", __FUNCTION__, decodeCount);
    ptlprintf("Thread %s [%8x] end\n", __FUNCTION__, id);
}

void CudaPanoramaLocalDiskTask::Impl::proc()
{
    size_t id = std::this_thread::get_id().hash();
    ptlprintf("Thread %s [%8x] started\n", __FUNCTION__, id);

    procCount = 0;
    StampedPinnedMemoryVector srcFrames;
    std::vector<cv::Mat> images(numVideos);
    while (true)
    {
        if (!decodeFramesBuffer.pull(srcFrames))
            break;

        if (isCanceled)
            break;
        
        for (int i = 0; i < numVideos; i++)
            images[i] = srcFrames.frames[i].createMatHeader();        
        bool ok = render.render(images, srcFrames.timeStamps);
        if (!ok)
        {
            ptlprintf("Error in %s, render failed\n", __FUNCTION__);
            setAsyncErrorMessage("��Ƶƴ�ӷ�������������ֹ��");
            isCanceled = true;
            break;
        }
        procCount++;
        //ptlprintf("proc count = %d\n", procCount);
    }
    
    if (!isCanceled)
        render.waitForCompletion();
    render.stop();

    ptlprintf("In %s, total proc %d\n", __FUNCTION__, procCount);
    ptlprintf("Thread %s [%8x] end\n", __FUNCTION__, id);
}

void CudaPanoramaLocalDiskTask::Impl::postProc()
{
    size_t id = std::this_thread::get_id().hash();
    ptlprintf("Thread %s [%8x] started\n", __FUNCTION__, id);

    avp::SharedAudioVideoFrame dstFrame;
    while (true)
    {
        dstFramesMemoryPool.get(dstFrame);
        cv::Mat result(dstSize, CV_8UC4, dstFrame.data, dstFrame.step);
        if (!render.getResult(result, dstFrame.timeStamp))
            break;

        if (isCanceled)
            break;

        cv::Mat image(dstFrame.height, dstFrame.width, CV_8UC4, dstFrame.data, dstFrame.step);
        if (addLogo)
        {
            bool ok = logoFilter.addLogo(image);
            if (!ok)
            {
                ptlprintf("Error in %s, render failed\n", __FUNCTION__);
                setAsyncErrorMessage("��Ƶƴ�ӷ�������������ֹ��");
                isCanceled = true;
                break;
            }
        }            

        procFrameBuffer.push(dstFrame);
    }

    if (!isCanceled)
    {
        while (procFrameBuffer.size())
            std::this_thread::sleep_for(std::chrono::microseconds(25));
    }    
    procFrameBuffer.stop();

    ptlprintf("In %s, total proc %d\n", __FUNCTION__, procCount);
    ptlprintf("Thread %s [%8x] end\n", __FUNCTION__, id);
}

void CudaPanoramaLocalDiskTask::Impl::encode()
{
    size_t id = std::this_thread::get_id().hash();
    ptlprintf("Thread %s [%8x] started\n", __FUNCTION__, id);

    encodeCount = 0;
    int step = 1;
    if (validFrameCount > 0)
        step = validFrameCount / 100.0 + 0.5;
    if (step < 1)
        step = 1;
    ptlprintf("In %s, validFrameCount = %d, step = %d\n", __FUNCTION__, validFrameCount, step);
    ztool::Timer timerEncode;
    encodeCount = 0;
    avp::SharedAudioVideoFrame deepFrame;
    while (true)
    {
        if (!procFrameBuffer.pull(deepFrame))
            break;

        if (isCanceled)
            break;

        //timerEncode.start();
        bool ok = writer.write(avp::AudioVideoFrame(deepFrame));
        //timerEncode.end();
        if (!ok)
        {
            ptlprintf("Error in %s, render failed\n", __FUNCTION__);
            setAsyncErrorMessage("��Ƶƴ�ӷ�������������ֹ��");
            isCanceled = true;
            break;
        }

        // Only when the frame is of type video can we increase encodeCount
        if (deepFrame.mediaType == avp::VIDEO)
            encodeCount++;
        //ptlprintf("frame %d finish, encode time = %f\n", encodeCount, timerEncode.elapse());

        if (encodeCount % step == 0)
            finishPercent.store(double(encodeCount) / (validFrameCount > 0 ? validFrameCount : 100) * 100);
    }

    writer.close();

    finishPercent.store(100);

    ptlprintf("In %s, total encode %d\n", __FUNCTION__, encodeCount);
    ptlprintf("Thread %s [%8x] end\n", __FUNCTION__, id);
}

void CudaPanoramaLocalDiskTask::Impl::setAsyncErrorMessage(const std::string& message)
{
    std::lock_guard<std::mutex> lg(mtxAsyncErrorMessage);
    hasAsyncError = 1;
    asyncErrorMessage = message;
}

void CudaPanoramaLocalDiskTask::Impl::clearAsyncErrorMessage()
{
    std::lock_guard<std::mutex> lg(mtxAsyncErrorMessage);
    hasAsyncError = 0;
    asyncErrorMessage.clear();
}

CudaPanoramaLocalDiskTask::CudaPanoramaLocalDiskTask()
{
    ptrImpl.reset(new Impl);
}

CudaPanoramaLocalDiskTask::~CudaPanoramaLocalDiskTask()
{

}

bool CudaPanoramaLocalDiskTask::init(const std::vector<std::string>& srcVideoFiles, const std::vector<int> offsets, int audioIndex,
    const std::string& cameraParamFile, const std::string& customMaskFile, const std::string& dstVideoFile, int dstWidth, int dstHeight,
    int dstVideoBitRate, const std::string& dstVideoEncoder, const std::string& dstVideoPreset, 
    int dstVideoMaxFrameCount)
{
    return ptrImpl->init(srcVideoFiles, offsets, audioIndex, cameraParamFile, customMaskFile, dstVideoFile, dstWidth, dstHeight,
        dstVideoBitRate, dstVideoEncoder, dstVideoPreset, dstVideoMaxFrameCount);
}

bool CudaPanoramaLocalDiskTask::start()
{
    return ptrImpl->start();
}

void CudaPanoramaLocalDiskTask::waitForCompletion()
{
    ptrImpl->waitForCompletion();
}

int CudaPanoramaLocalDiskTask::getProgress() const
{
    return ptrImpl->getProgress();
}

void CudaPanoramaLocalDiskTask::cancel()
{
    ptrImpl->cancel();
}

void CudaPanoramaLocalDiskTask::getLastSyncErrorMessage(std::string& message) const
{
    ptrImpl->getLastSyncErrorMessage(message);
}

bool CudaPanoramaLocalDiskTask::hasAsyncErrorMessage() const
{
    return ptrImpl->hasAsyncErrorMessage();
}

void CudaPanoramaLocalDiskTask::getLastAsyncErrorMessage(std::string& message)
{
    return ptrImpl->getLastAsyncErrorMessage(message);
}