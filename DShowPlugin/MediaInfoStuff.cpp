/********************************************************************************
 Copyright (C) 2012 Hugh Bailey <obs.jim@gmail.com>

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
********************************************************************************/


#include "DShowPlugin.h"


void WINAPI FreeMediaType(AM_MEDIA_TYPE& mt)
{
    if(mt.cbFormat != 0)
    {
        CoTaskMemFree((LPVOID)mt.pbFormat);
        mt.cbFormat = 0;
        mt.pbFormat = NULL;
    }

    SafeRelease(mt.pUnk);
}

HRESULT WINAPI CopyMediaType(AM_MEDIA_TYPE *pmtTarget, const AM_MEDIA_TYPE *pmtSource)
{
    if(!pmtSource || !pmtTarget) return S_FALSE;

    *pmtTarget = *pmtSource;

    if(pmtSource->cbFormat && pmtSource->pbFormat)
    {
        pmtTarget->pbFormat = (PBYTE)CoTaskMemAlloc(pmtSource->cbFormat);
        if(pmtTarget->pbFormat == NULL)
        {
            pmtTarget->cbFormat = 0;
            return E_OUTOFMEMORY;
        }
        else
            mcpy(pmtTarget->pbFormat, pmtSource->pbFormat, pmtTarget->cbFormat);
    }

    if(pmtTarget->pUnk != NULL)
        pmtTarget->pUnk->AddRef();

    return S_OK;
}


VideoOutputType GetVideoOutputTypeFromFourCC(DWORD fourCC)
{
    VideoOutputType type = VideoOutputType_None;

    // Packed RGB formats
    if(fourCC == '2BGR')
        type = VideoOutputType_RGB32;
    else if(fourCC == '4BGR')
        type = VideoOutputType_RGB24;
    else if(fourCC == 'ABGR')
        type = VideoOutputType_ARGB32;

    // Planar YUV formats
    else if(fourCC == '024I' || fourCC == 'VUYI')
        type = VideoOutputType_I420;
    else if(fourCC == '21VY')
        type = VideoOutputType_YV12;

    // Packed YUV formats
    else if(fourCC == 'UYVY')
        type = VideoOutputType_YVYU;
    else if(fourCC == '2YUY')
        type = VideoOutputType_YUY2;
    else if(fourCC == 'YVYU')
        type = VideoOutputType_UYVY;
    else if(fourCC == 'CYDH')
        type = VideoOutputType_HDYC;

    else if(fourCC == 'V4PM' || fourCC == '2S4M')
        type = VideoOutputType_MPEG2_VIDEO;

    else if(fourCC == '462H')
        type = VideoOutputType_H264;

    else if(fourCC == 'GPJM')
        type = VideoOutputType_MJPG;

    return type;
}


VideoOutputType GetVideoOutputType(const AM_MEDIA_TYPE &media_type)
{
    VideoOutputType type = VideoOutputType_None;

    if(media_type.majortype == MEDIATYPE_Video)
    {
        // Packed RGB formats
        if(media_type.subtype == MEDIASUBTYPE_RGB24)
            type = VideoOutputType_RGB24;
        else if(media_type.subtype == MEDIASUBTYPE_RGB32)
            type = VideoOutputType_RGB32;
        else if(media_type.subtype == MEDIASUBTYPE_ARGB32)
            type = VideoOutputType_ARGB32;

        // Planar YUV formats
        else if(media_type.subtype == MEDIASUBTYPE_I420)
            type = VideoOutputType_I420;
        else if(media_type.subtype == MEDIASUBTYPE_IYUV)
            type = VideoOutputType_I420;
        else if(media_type.subtype == MEDIASUBTYPE_YV12)
            type = VideoOutputType_YV12;

        else if(media_type.subtype == MEDIASUBTYPE_Y41P)
            type = VideoOutputType_Y41P;
        else if(media_type.subtype == MEDIASUBTYPE_YVU9)
            type = VideoOutputType_YVU9;

        // Packed YUV formats
        else if(media_type.subtype == MEDIASUBTYPE_YVYU)
            type = VideoOutputType_YVYU;
        else if(media_type.subtype == MEDIASUBTYPE_YUY2)
            type = VideoOutputType_YUY2;
        else if(media_type.subtype == MEDIASUBTYPE_UYVY)
            type = VideoOutputType_UYVY;

        else if(media_type.subtype == MEDIASUBTYPE_MPEG2_VIDEO)
            type = VideoOutputType_MPEG2_VIDEO;

        else if(media_type.subtype == MEDIASUBTYPE_H264)
            type = VideoOutputType_H264;

        else if(media_type.subtype == MEDIASUBTYPE_dvsl)
            type = VideoOutputType_dvsl;
        else if(media_type.subtype == MEDIASUBTYPE_dvsd)
            type = VideoOutputType_dvsd;
        else if(media_type.subtype == MEDIASUBTYPE_dvhd)
            type = VideoOutputType_dvhd;

        else if(media_type.subtype == MEDIASUBTYPE_MJPG)
            type = VideoOutputType_MJPG;

        else
            nop();
    }

    return type;
}

// 对应VideoOutputType，类型权重
const int inputPriority[] =
{
	1,		   // VideoOutputType_None,
	// RGB格式在shader中，直接取样就可以了，而YUV等格式
	// 需要转到RGB再返回，所以肯定是RGB格式效率高，所以
	// 这里就提高了RGB的权重值
	15,		   // VideoOutputType_RGB24,
	15,		   // VideoOutputType_RGB32,
	15,		   // VideoOutputType_ARGB32,

	12,		   // VideoOutputType_I420,
	12,		   // VideoOutputType_YV12,

	// -1表示不用
	-1,		   // VideoOutputType_Y41P,
	-1,		   // VideoOutputType_YVU9,

	13,		   // VideoOutputType_YVYU,
	13,		   // VideoOutputType_YUY2,
	13,		   // VideoOutputType_UYVY,
	13,		   // VideoOutputType_HDYC,

	5,		   // VideoOutputType_MPEG2_VIDEO,
	-1,		   // VideoOutputType_H264,

	10,		   // VideoOutputType_dvsl,
	10,		   // VideoOutputType_dvsd,
	10,		   // VideoOutputType_dvhd,

	9		   // VideoOutputType_MJPG
};

bool GetVideoOutputTypes(const List<MediaOutputInfo> &outputList, UINT width, UINT height, UINT64 frameInterval, List<VideoOutputType> &types)
{
    types.Clear();

    UINT64 closestIntervalDifference = 0xFFFFFFFFFFFFFFFFLL;
    UINT64 bestFrameInterval = 0;

    for(UINT i=0; i<outputList.Num(); i++)
    {
        MediaOutputInfo &outputInfo = outputList[i];
        //VIDEOINFOHEADER *pVih = reinterpret_cast<VIDEOINFOHEADER*>(outputInfo.mediaType->pbFormat);

        if( outputInfo.minCX <= width                    && outputInfo.maxCX >= width &&
            outputInfo.minCY <= height                   && outputInfo.maxCY >= height &&
            outputInfo.minFrameInterval <= frameInterval && outputInfo.maxFrameInterval >= frameInterval)
        {
            int priority = inputPriority[(UINT)outputInfo.videoType];
            if(priority == -1)
                continue;

            types.SafeAdd(outputInfo.videoType);
        }
    }

    return types.Num() != 0;
}

// preferredType变量有可能会传入-1，所以不能用UINT
MediaOutputInfo* GetBestMediaOutput(const List<MediaOutputInfo> &outputList, UINT width, UINT height, int preferredType, UINT64 &frameInterval)
{
    MediaOutputInfo *bestMediaOutput = NULL;
    int bestPriority = -1;
    UINT64 closestIntervalDifference = 0xFFFFFFFFFFFFFFFFLL;
    UINT64 bestFrameInterval = 0;

    bool bUsePreferredType = preferredType != -1;
	// 把变量移到循环外定义，详见《Effective C++》
	bool better;
	int priority;
	UINT64 curInterval;
	UINT64 intervalDifference;
	for (UINT i = 0; i < outputList.Num(); ++i)
	{
		MediaOutputInfo &outputInfo = outputList[i];
		//VIDEOINFOHEADER *pVih = reinterpret_cast<VIDEOINFOHEADER*>(outputInfo.mediaType->pbFormat);

		if (outputInfo.minCX <= width  && outputInfo.maxCX >= width &&
			outputInfo.minCY <= height && outputInfo.maxCY >= height)
		{
			priority = inputPriority[(UINT)outputInfo.videoType];
			if (priority == -1)
				continue;

			if (frameInterval > outputInfo.maxFrameInterval)
				curInterval = outputInfo.maxFrameInterval;
			else if (frameInterval < outputInfo.minFrameInterval)
				curInterval = outputInfo.minFrameInterval;
			else
				curInterval = frameInterval;

			intervalDifference = (UINT64)_abs64(INT64(curInterval) - INT64(frameInterval));

			if (intervalDifference > closestIntervalDifference)
				continue;

			if (!bUsePreferredType)
				better = priority > bestPriority || !bestMediaOutput || intervalDifference < closestIntervalDifference;
			else
				better = (UINT)outputInfo.videoType == preferredType && intervalDifference <= closestIntervalDifference;

			if (better)
			{
				closestIntervalDifference = intervalDifference;
				bestFrameInterval = curInterval;
				bestMediaOutput = &outputInfo;
				bestPriority = priority;
			}
		}
	}

    frameInterval = bestFrameInterval;
    return bestMediaOutput;
}
