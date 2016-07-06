#include "Text.h"
#include <vector>

static int lang = 0;

void setLanguage(bool isChinese)
{
    lang = isChinese ? 0 : 1;
}

struct IndexTextsPair
{
    int index;
    std::string texts[2];
};

static IndexTextsPair indexTextsPairs[] =
{
    TI_PERIOD, { "��", "." },
    TI_COLON, { "��", ":" },
    TI_LINE_BREAK, { "\n", "\n" },
    TI_SPACE, { " ", " " },

    TI_PARAM_CHECK_FAIL, { "����У��ʧ��", "Parameters check failed" },
    TI_STITCH_INIT_FAIL, { "��Ƶƴ�ӳ�ʼ��ʧ��", "Could not initialize stitching" },
    TI_OPEN_VIDEO_FAIL, { "����Ƶʧ��", "Could not open videos" },
    TI_CREATE_STITCH_VIDEO_FAIL, { "�޷�����ȫ����Ƶ", "Could not create panorama video" },
    TI_WRITE_TO_VIDEO_FAIL_TASK_TERMINATE, { "д����Ƶʧ�ܣ�������ֹ", "Could not write panorama video, task terminated" },
    TI_STITCH_FAIL_TASK_TERMINATE, { "��Ƶƴ�ӷ�������������ֹ", "Could not stitch panorama frame, task terminated" },

    TI_AUDIO_VIDEO_SOURCE_RUNNING_CLOSE_BEFORE_LANCH_NEW, { "����ƵԴ�������������У��ȹرյ�ǰ���е������������µ�����", "Audio video sources task is running, termiate current task before lanching a new one" },

    TI_VIDEO_SOURCE_RUNNING_CLOSE_BEFORE_LAUNCH_NEW, { "��ƵԴ�������������У��ȹرյ�ǰ���е������������µ�����", "Video sources task is running, terminate current task before launching a new one" },
    TI_VIDEO_SOURCE_EMPTY, { "��ƵԴ��ַΪ�գ��������趨", "Video sources URLs are empty, please reset" },
    TI_VIDEO_SOURCE_PROP_SHOULD_MATCH, { "������ƵԴ��Ҫ��ͬ���ķֱ��ʺ�֡��", "All video sources should share the same resolution and the same frame rate" },
    TI_VIDEO_SOURCE_OPEN_FAIL, { "��ƵԴ��ʧ��", "Could not open video sources" },
    TI_VIDEO_SOURCE_OPEN_SUCCESS, { "��ƵԴ�򿪳ɹ�", "Successfully opened video sources" },
    TI_VIDEO_SOURCE_TASK_LAUNCH_SUCCESS, { "��ƵԴ��������", "Video sources task launched" },
    TI_VIDEO_SOURCE_TASK_FINISH, { "��ƵԴ�������", "Video sources task finished" },
    TI_VIDEO_SOURCE_CLOSE, { "��ƵԴ�ر�", "Video sources closed" },

    TI_AUDIO_SOURCE_RUNNING_CLOSE_BEFORE_LAUNCH_NEW, { "��ƵԴ�������������У��ȹرյ�ǰ���е������������µ�����", "Audio source task is running, terminate current task before launching a new one" },
    TI_AUDIO_SOURCE_EMPTY, { "��ƵԴ��ַΪ�գ��������趨", "Audio source URL is empty, please reset" },
    TI_AUDIO_SOURCE_OPEN_FAIL, { "��ƵԴ��ʧ��", "Could not open audio source" },
    TI_AUDIO_SOURCE_OPEN_SUCCESS, { "��ƵԴ�򿪳ɹ�", "Successfully opened audio source" },
    TI_AUDIO_SOURCE_TASK_LAUNCH_SUCCESS, { "��ƵԴ��������", "Audio source task launched" },
    TI_AUDIO_SOURCE_TASK_FINISH, { "��ƵԴ�������", "Audio source task finished" },
    TI_AUDIO_SOURCE_CLOSE, { "��ƵԴ�ر�", "Audio source closed" },

    TI_SOURCE_NOT_OPENED_CANNOT_LAUNCH_STITCH, { "��δ������ƵԴ���޷�����ƴ������", "Video (and audio) sources have not been opened, cannot launch stitching task" },
    TI_STITCH_RUNNING_CLOSE_BEFORE_LAUNCH_NEW, { "��Ƶƴ���������ڽ����У����ȹر�����ִ�е������������µ�����", "Stitching task is running, terminate current task before launching a new one" },
    TI_STITCH_INIT_SUCCESS, { "��Ƶƴ�ӳ�ʼ���ɹ�", "Successfull initialized stitching" },
    TI_STITCH_TASK_LAUNCH_SUCCESS, { "��Ƶƴ����������", "Stitching task launched" },
    TI_STITCH_TASK_FINISH, { "��Ƶƴ���������", "Stitching task finished" },

    TI_SOURCE_NOT_OPENED_CANNOT_LAUNCH_LIVE, { "��δ������ƵԴ���޷�����ֱ������", "Video (and audio) sources have not been opened, cannot launch live stream task" },
    TI_STITCH_NOT_RUNNING_CANNOT_LAUNCH_LIVE, { "��δ����ƴ�������޷�����ֱ������", "Stitching task has not been lanched, cannot launch live stream task" },
    TI_LIVE_RUNNING_CLOSE_BEFORE_LAUNCH_NEW, { "ֱ���������ڽ����У����ȹر�����ִ�е������������µ�����", "Live stream task is running, terminate current task before launching a new one" },
    TI_LIVE_PARAM_ERROR_CANNOT_LAUNCH_LIVE, { "���������޷�������������", "Invalid pamameters, cannot lanch live stream task" },
    TI_SERVER_CONNECT_FAIL, { "��ý�����������ʧ��", "Could not connect stream media server" },
    TI_SERVER_CONNECT_SUCCESS, { "��ý����������ӳɹ�", "Sucessfully connected stream media server" },
    TI_LIVE_TASK_LAUNCH_SUCCESS, { "ֱ����������", "Live stream task launched" },
    TI_LIVE_TASK_FINISH, { "ֱ���������", "Live stream task finished" },
    TI_SERVER_DISCONNECT, { "��ý����������ӶϿ�", "Disconnected stream media server" },

    TI_SOURCE_NOT_OPENED_CANNOT_LAUNCH_WRITE, { "��δ������ƵԴ���޷�������������", "Video (and audio) sources have not been opened, cannot launch saving to hard disk task" },
    TI_STITCH_NOT_RUNNING_CANNOT_LAUNCH_WRITE, { "��δ����ƴ�������޷�������������", "Stitching task has not been lanched, cannot launch saving to hard disk task" },
    TI_WRITE_RUNNING_CLOSE_BEFORE_LAUNCH_NEW, { "�����������ڽ����У����ȹر�����ִ�е������������µ�����", "Saving to hard disk task is running, terminate current task before launching a new one" },
    TI_WRITE_PARAM_ERROR_CANNOT_LAUNCH_WRITE, { "���������޷�������������", "Invalid pamameters, cannot lanch saving to hard disk task" },
    TI_WRITE_LAUNCH, { "������������", "Saving to hard disk task launched" },
    TI_WRITE_FINISH, { "�����������", "Saving to hard disk task finished" },

    TI_ACQUIRE_VIDEO_SOURCE_FAIL_TASK_TERMINATE, { "��ȡ��ƵԴ���ݷ�������������ֹ", "Could not acquire data from video source(s), all the running tasks terminated" },
    TI_ACQUIRE_AUDIO_SOURCE_FAIL_TASK_TERMINATE, { "��ȡ��ƵԴ���ݷ�������������ֹ", "Could not acquire data from audio source, all the running tasks terminated" },
    TI_LIVE_FAIL_TASK_TERMINATE, { "������������������ֹ", "Could not send data to stream media server, all the running tasks terminated" },

    TI_FILE_OPEN_FAIL_TASK_TERMINATE, { "�ļ��޷��򿪣�������ֹ", "file could not be opened, saving to hard disk task terminated" },
    TI_BEGIN_WRITE, { "��ʼд��", "began writing into this file" },
    TI_END_WRITE, { "д�����", "finished writing into this file" },
    TI_WRITE_FAIL_TASK_TERMINATE, { "д�뷢������������ֹ", "could not write into this file, saving to hard disk task terminated" },
};

struct IndexedTexts
{
    IndexedTexts()
    {
        pairs.resize(TI_TEXT_NUM);
        for (int i = 0; i < TI_TEXT_NUM; i++)
        {
            int index = TI_TEXT_NUM;
            for (int j = 0; j < TI_TEXT_NUM; j++)
            {
                if (indexTextsPairs[j].index == i)
                {
                    index = i;
                    break;
                }
            }
            if (index < TI_TEXT_NUM)
                pairs[i] = indexTextsPairs[index];
        }
    }
    std::vector<IndexTextsPair> pairs;
};

static IndexedTexts indexedTexts;

std::string emptyText = "";

const std::string& getText(int index)
{
    if (index < 0 || index >= TI_TEXT_NUM)
        return  emptyText;
    else
        return indexedTexts.pairs[index].texts[lang];
}