#pragma once

#include <string>

void setTextLanguage(bool isChinese);

enum TextIndex
{
    TI_PERIOD,
    TI_COLON,
    TI_LINE_BREAK,
    TI_SPACE,

    TI_CONFIG_FILE_PARSE_FAIL,
    TI_PARAM_CHECK_FAIL,
    TI_STITCH_INIT_FAIL,
    TI_OPEN_VIDEO_FAIL,
    TI_CREATE_STITCH_VIDEO_FAIL,
    TI_WRITE_TO_VIDEO_FAIL_TASK_TERMINATE,
    TI_STITCH_FAIL_TASK_TERMINATE,

    TI_AUDIO_VIDEO_SOURCE_RUNNING_CLOSE_BEFORE_LAUNCH_NEW,
    TI_AUDIO_VIDEO_SOURCE_OPEN_FAIL,

    TI_VIDEO_SOURCE_RUNNING_CLOSE_BEFORE_LAUNCH_NEW,
    TI_VIDEO_SOURCE_EMPTY,
    TI_VIDEO_SOURCE_PROP_SHOULD_MATCH,
    TI_VIDEO_SOURCE_OPEN_FAIL,
    TI_VIDEO_SOURCE_OPEN_SUCCESS,
    TI_VIDEO_SOURCE_TASK_LAUNCH_SUCCESS,
    TI_VIDEO_SOURCE_TASK_FINISH,
    TI_VIDEO_SOURCE_CLOSE,

    TI_AUDIO_SOURCE_RUNNING_CLOSE_BEFORE_LAUNCH_NEW,
    TI_AUDIO_SOURCE_EMPTY,
    TI_AUDIO_SOURCE_OPEN_FAIL,
    TI_AUDIO_SOURCE_OPEN_SUCCESS,
    TI_AUDIO_SOURCE_TASK_LAUNCH_SUCCESS,
    TI_AUDIO_SOURCE_TASK_FINISH,
    TI_AUDIO_SOURCE_CLOSE,

    TI_SOURCE_NOT_OPENED_CANNOT_LAUNCH_STITCH,
    TI_STITCH_RUNNING_CLOSE_BEFORE_LAUNCH_NEW,
    TI_STITCH_INIT_SUCCESS,
    TI_STITCH_TASK_LAUNCH_SUCCESS,
    TI_STITCH_TASK_FINISH,

    TI_SOURCE_NOT_OPENED_CANNOT_LAUNCH_LIVE,
    TI_STITCH_NOT_RUNNING_CANNOT_LAUNCH_LIVE,
    TI_LIVE_RUNNING_CLOSE_BEFORE_LAUNCH_NEW,
    TI_LIVE_PARAM_ERROR_CANNOT_LAUNCH_LIVE,
    TI_SERVER_CONNECT_FAIL,
    TI_SERVER_CONNECT_SUCCESS,
    TI_LIVE_TASK_LAUNCH_SUCCESS,
    TI_LIVE_TASK_FINISH,
    TI_SERVER_DISCONNECT,

    TI_SOURCE_NOT_OPENED_CANNOT_LAUNCH_WRITE,
    TI_STITCH_NOT_RUNNING_CANNOT_LAUNCH_WRITE,
    TI_WRITE_RUNNING_CLOSE_BEFORE_LAUNCH_NEW,
    TI_WRITE_PARAM_ERROR_CANNOT_LAUNCH_WRITE,
    TI_WRITE_LAUNCH,
    TI_WRITE_FINISH,

    TI_SOURCE_NOT_OPENED_CANNOT_CORRECT,
    TI_STITCH_NOT_RUNNING_CANNOT_CORRECT,
    TI_LIVE_RUNNING_CLOSE_BEFORE_CORRECT,
    TI_WRITE_RUNNING_CLOSE_BEFORE_CORRECT,
    TI_CORRECT_FAIL,

    TI_ACQUIRE_VIDEO_SOURCE_FAIL_TASK_TERMINATE,
    TI_ACQUIRE_AUDIO_SOURCE_FAIL_TASK_TERMINATE,
    TI_LIVE_FAIL_TASK_TERMINATE,

    TI_FILE_OPEN_FAIL_TASK_TERMINATE,
    TI_BEGIN_WRITE,
    TI_END_WRITE,
    TI_WRITE_FAIL_TASK_TERMINATE,

    TI_TEXT_NUM
};

const std::string& getText(int index);