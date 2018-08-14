/**
 * am_codec_interface.cpp
 *
 * History:
 *  2013/11/18 - [Zhi He] create file
 *
 * Copyright (c) 2015 Ambarella, Inc.
 *
 * This file and its contents ("Software") are protected by intellectual
 * property rights including, without limitation, U.S. and/or foreign
 * copyrights. This Software is also the confidential and proprietary
 * information of Ambarella, Inc. and its licensors. You may not use, reproduce,
 * disclose, distribute, modify, or otherwise prepare derivative works of this
 * Software or any portion thereof except pursuant to a signed license agreement
 * or nondisclosure agreement with Ambarella, Inc. or its authorized affiliates.
 * In the absence of such an agreement, you agree to promptly notify and return
 * this Software to Ambarella, Inc.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF NON-INFRINGEMENT,
 * MERCHANTABILITY, AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL AMBARELLA, INC. OR ITS AFFILIATES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; COMPUTER FAILURE OR MALFUNCTION; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "stdlib.h"
#include "stdio.h"
#include "string.h"


#include "am_playback_new_if.h"

#include "am_native.h"
#include "am_native_log.h"

#include "am_internal.h"

#include "am_codec_interface.h"

extern SCodecVideoH264 *gfGetVideoCodecH264Parser(TU8 *p_data, TMemSize data_size, EECode &ret);

SCodecVideoCommon *gfGetVideoCodecParser(TU8 *p_data, TMemSize data_size, StreamFormat format, EECode &ret)
{
  DASSERT(p_data);
  DASSERT(data_size);
  if (DLikely(p_data && data_size)) {
    switch (format) {
      case StreamFormat_H264:
        return (SCodecVideoCommon *)gfGetVideoCodecH264Parser(p_data, data_size, ret);
      default:
        LOG_ERROR("gfGetVideoCodecParser, unsupported format=%d\n", format);
        return NULL;
    }
  } else {
    LOG_ERROR("invalid params, p_data=%p data_size=%lu\n", p_data, data_size);
    return NULL;
  }
}

void gfReleaseVideoCodecParser(SCodecVideoCommon *parser)
{
  if (DLikely(parser)) {
    free(parser);
  } else {
    LOG_FATAL("NULL parser\n");
  }
}

TU32 gfGetADTSFrameLength(TU8 *p)
{
  return (((TU32)(p[3] & 0x3)) << 11) | (((TU32)p[4]) << 3) | (((TU32)p[5] >> 5) & 0x7);
}

EECode gfParseADTSHeader(TU8 *p, SADTSHeader *header)
{
  if (DUnlikely((!p) || (!header))) {
    LOG_ERROR("NULL parameters in gfParseADTSHeader\n");
    return EECode_BadParam;
  }
  if (DUnlikely((0xff != p[0]) || (0xf0 != (p[1] & 0xf0)))) {
    LOG_ERROR("not find sync byte in gfParseADTSHeader\n");
    return EECode_DataCorruption;
  }
  header->ID = (p[1] >> 3) & 0x01;
  header->layer = (p[1] >> 1) & 0x03;
  header->protection_absent = p[1] & 0x01;
  header->profile = (p[2] >> 6) & 0x03;
  header->sampling_frequency_index = (p[2] >> 2) & 0x0f;
  header->private_bit = (p[2] >> 1) & 0x01;
  header->channel_configuration = ((p[2] & 0x01) << 2) | ((p[3] >> 6) & 0x3);
  header->original_copy = (p[3] >> 5) & 0x01;
  header->home = (p[3] >> 4) & 0x01;
  header->copyright_identification_bit = (p[3] >> 3) & 0x01;
  header->copyright_identification_start = (p[3] >> 2) & 0x01;
  header->aac_frame_length = (((TU16)p[3] & 0x03) << 11) | ((TU16)p[4] << 3) | (((TU16)p[5] >> 5) & 0x7);
  header->adts_buffer_fullness = (((TU16)p[5] & 0x1f) << 6) | (((TU16)p[6] >> 2) & 0x3f);
  header->number_of_raw_data_blocks_in_frame = p[6] & 0x03;
  return EECode_OK;
}

EECode gfBuildADTSHeader(TU8 *p,
  TU8 ID, TU8 layer, TU8 protection_absent, TU8 profile, TU8 sampling_frequency_index, TU8 private_bit, TU8 channel_configuration,
  TU8 original_copy, TU8 home, TU8 copyright_identification_bit, TU8 copyright_identification_start,
  TU32 frame_length, TU16 adts_buffer_fullness, TU8 number_of_raw_data_blocks_in_frame)
{
  if (DUnlikely(!p)) {
    LOG_ERROR("NULL parameters in gfBuildADTSHeader\n");
    return EECode_BadParam;
  }
  p[0] = 0xFF;
  p[1] = (0xF0) | ((ID & 0x01) << 3) | ((layer & 0x03) << 1) | (protection_absent & 0x01);
  p[2] = ((profile & 0x03) << 6) | ((sampling_frequency_index & 0x0f) << 2) | ((private_bit & 0x01) << 1) | ((channel_configuration >> 2) & 0x01);
  p[3] = ((channel_configuration & 0x03) << 6) | ((original_copy & 0x01) << 5) | ((home & 0x01) << 4) | ((copyright_identification_bit & 0x01) << 3)
    | ((copyright_identification_start & 0x01) << 3) | ((frame_length >> 11) & 0x03);
  p[4] = (frame_length >> 3) & 0xFF;
  p[5] = ((frame_length & 0x07) << 5) | ((adts_buffer_fullness >> 6) & 0x1F);
  p[6] = ((adts_buffer_fullness & 0x3f) << 2) | (number_of_raw_data_blocks_in_frame & 0x03);
  return EECode_OK;
}

TU32 gfGetADTSSamplingFrequency(TU8 sampling_frequency_index)
{
  switch (sampling_frequency_index) {
    case EADTSSamplingFrequency_96000:
      return 96000;
      break;
    case EADTSSamplingFrequency_88200:
      return 88200;
      break;
    case EADTSSamplingFrequency_64000:
      return 64000;
      break;
    case EADTSSamplingFrequency_48000:
      return 48000;
      break;
    case EADTSSamplingFrequency_44100:
      return 44100;
      break;
    case EADTSSamplingFrequency_32000:
      return 32000;
      break;
    case EADTSSamplingFrequency_24000:
      return 24000;
      break;
    case EADTSSamplingFrequency_22050:
      return 22050;
      break;
    case EADTSSamplingFrequency_16000:
      return 16000;
      break;
    case EADTSSamplingFrequency_12000:
      return 12000;
      break;
    case EADTSSamplingFrequency_11025:
      return 11025;
      break;
    case EADTSSamplingFrequency_8000:
      return 8000;
      break;
    case EADTSSamplingFrequency_7350:
      return 7350;
      break;
    default:
      break;
  }
  return 0;
}

