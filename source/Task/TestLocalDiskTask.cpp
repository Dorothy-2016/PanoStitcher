#include "PanoramaTask.h"
#include "PanoramaTaskUtil.h"
#include "AudioVideoProcessor.h"
#include "Log.h"
#include "Tool/Timer.h"
#include <fstream>
#include <thread>

#define TEST_RICOH 0

static void parseVideoPathsAndOffsets(const std::string& infoFileName, std::vector<std::string>& videoPath, std::vector<int>& offset)
{
    videoPath.clear();
    offset.clear();

    std::ifstream fstrm(infoFileName);
    std::string line;
    while (!fstrm.eof())
    {
        std::getline(fstrm, line);
        if (line.empty())
            continue;

        std::string::size_type pos = line.find(',');
        if (pos == std::string::npos)
            continue;

        videoPath.push_back(line.substr(0, pos));
        offset.push_back(atoi(line.substr(pos + 1).c_str()));
    }
}

static void cancelTask(PanoramaLocalDiskTask* task)
{
    std::this_thread::sleep_for(std::chrono::seconds(10));
    if (task)
        task->cancel();
}

int main(int argc, char* argv[])
{
    const char* keys =
        "{project_file           |             | project file path}"
        "{pano_width             | 2048        | pano picture width}"
        "{pano_height            | 1024        | pano picture height}"
        "{pano_video_name        | panogpu.mp4 | xml param file path}"
        "{use_cuda               | false       | use gpu to accelerate computation}";

    cv::CommandLineParser parser(argc, argv, keys);

    //initLog("", "");
    //avp::setFFmpegLogCallback(bstLogVlPrintf);
    //avp::setLogCallback(bstLogVlPrintf);
    //setPanoTaskLogCallback(bstLogVlPrintf);
    setLanguage(false);
    setCPUMultibandBlendMultiThread(true);

    cv::Size srcSize, dstSize;
    std::vector<std::string> srcVideoNames;
    std::vector<int> offset;
    std::string panoVideoName;

    dstSize.width = parser.get<int>("pano_width");
    dstSize.height = parser.get<int>("pano_height");

    panoVideoName = parser.get<std::string>("pano_video_name");

    std::string projFileName;
    projFileName = parser.get<std::string>("project_file");
#if !TEST_RICOH
    projFileName = "F:\\panovideo\\test\\test1\\haiyangguansimple.xml";
    //projFileName = "F:\\panovideo\\test\\outdoor\\outdoor.pvs";
    //projFileName = "F:\\panovideo\\test\\test6\\zhanxiang.xml";
    loadVideoFileNamesAndOffset(projFileName, srcVideoNames, offset);
#endif

#if TEST_RICOH
    srcVideoNames.clear();

    //projFileName = "F:\\panovideo\\ricoh\\paramricoh.xml";
    //srcVideoNames.push_back("F:\\panovideo\\ricoh\\R0010113.MP4");

    projFileName = "F:\\panovideo\\vrdlc\\201610271307.xml";    
    srcVideoNames.push_back("F:\\panovideo\\vrdlc\\2016_1017_195821_006A.MP4");

    offset.clear();
    offset.push_back(0);
#endif

    std::unique_ptr<PanoramaLocalDiskTask> task;
    if (parser.get<bool>("use_cuda"))
        task.reset(new CudaPanoramaLocalDiskTask);
    else
        task.reset(new CPUPanoramaLocalDiskTask);

    avp::setDumpInput(false);
    
    panoVideoName = "qsv_h264_linear.mp4";
    std::string logoFileName = ""/*"F:\\image\\Earth_global.png"*/;
    int fov = 45;
#if TEST_RICOH
    bool ok = task->init(srcVideoNames, offset, 0, PanoStitchTypeRicoh, projFileName, projFileName, projFileName, 
        logoFileName, fov, 0, 25, panoVideoName, dstSize.width, dstSize.height, 8000000, "h264", "medium", 40 * 48);
#else
    bool ok = task->init(srcVideoNames, offset, 0, PanoStitchTypeMISO, projFileName, projFileName, projFileName, 
        logoFileName, fov, 0, 25, panoVideoName, dstSize.width, dstSize.height, 48000000, "h264_qsv", "medium", 20 * 48);
#endif
    if (!ok)
    {
        printf("Could not init panorama local disk task\n");
        std::string msg;
        task->getLastSyncErrorMessage(msg);
        printf("Error message: %s\n", msg.c_str());
        return 0;
    }

    //std::thread t1(cancelTask, task.get());
    ztool::Timer timer;
    task->start();
    int progress;
    while (true)
    {
        progress = task->getProgress();
        printf("percent %d\n", progress);
        if (progress == 100)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    task->waitForCompletion();
    printf("percent 100\n");
    timer.end();
    printf("%f\n", timer.elapse());
    //t1.join();

    /*ok = task->init(srcVideoNames, offset, 0, projFileName, projFileName, panoVideoName,
        dstSize.width, dstSize.height, 8000000, "h264", "medium", 40 * 48);
    if (!ok)
    {
        printf("Could not init panorama local disk task\n");
        std::string msg;
        task->getLastSyncErrorMessage(msg);
        printf("Error message: %s\n", msg.c_str());
        return 0;
    }

    std::thread t2(cancelTask, task.get());
    timer.start();
    task->start();
    while (true)
    {
        progress = task->getProgress();
        printf("percent %d\n", progress);
        if (progress == 100)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    task->waitForCompletion();
    printf("percent 100\n");
    timer.end();
    printf("%f\n", timer.elapse());
    t2.join();*/

    //t.join();

    return 0;
}