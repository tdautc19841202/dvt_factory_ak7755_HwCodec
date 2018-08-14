/*******************************************************************************
 * am_audio_codec_opus.cpp
 *
 * History:
 *   2014-11-10 - [ccjing] created file
 *
 * Copyright (c) 2016 Ambarella, Inc.
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
 *
 ******************************************************************************/

#include "am_base_include.h"
#include "am_define.h"
#include "am_log.h"

#include "am_audio_define.h"
#include "am_audio_codec_if.h"
#include "am_audio_codec.h"
#include "am_audio_codec_opus.h"
#include "am_audio_codec_opus_config.h"

#include "am_audio_utility.h"

AMIAudioCodec* get_audio_codec(const char *config)
{
  return AMAudioCodecOpus::create(config);
}

AMIAudioCodec* AMAudioCodecOpus::create(const char* config)
{
  AMAudioCodecOpus *opus = new AMAudioCodecOpus();
  if (AM_UNLIKELY(opus && !opus->init(config))) {
    delete opus;
    opus = nullptr;
  }
  return ((AMIAudioCodec*) opus);
}

void AMAudioCodecOpus::destroy()
{
  inherited::destroy();
}

bool AMAudioCodecOpus::initialize(AM_AUDIO_INFO *srcAudioInfo,
                                  AM_AUDIO_CODEC_MODE mode)
{
  do {
    if (AM_UNLIKELY(!srcAudioInfo)) {
      ERROR("Invalid audio info, finalize!");
      finalize();
      m_is_initialized = false;
      memset(m_src_audio_info, 0, sizeof(*m_src_audio_info));
      break;
    }
    if (AM_UNLIKELY(m_is_initialized &&
                    (mode == AM_AUDIO_CODEC_MODE_ENCODE) &&
                    (srcAudioInfo->sample_rate ==
                        m_audio_info[AUDIO_INFO_ENCODE].sample_rate) &&
                        (srcAudioInfo->channels ==
                            m_audio_info[AUDIO_INFO_ENCODE].channels))) {
      NOTICE("Audio encode opus is already initialized and "
          "the audioInfo is not changed. Do not reinitialize.");
      break;
    } else if (AM_UNLIKELY(m_is_initialized)) {
      NOTICE("Codec %s is already initialized to %s mode, re-initializing!",
             m_name.c_str(),
             codec_mode_string().c_str());
      finalize();
    }
    memcpy(m_src_audio_info, srcAudioInfo, sizeof(*m_src_audio_info));
    memcpy(&m_audio_info[AUDIO_INFO_ENCODE],
           srcAudioInfo,
           sizeof(*srcAudioInfo));
    memcpy(&m_audio_info[AUDIO_INFO_DECODE],
           srcAudioInfo,
           sizeof(*srcAudioInfo));
    m_mode = mode;
    switch (m_mode) {
      case AM_AUDIO_CODEC_MODE_ENCODE: {
        int error;
        AM_AUDIO_INFO *audioInfo = &m_audio_info[AUDIO_INFO_ENCODE];
        switch(audioInfo->sample_format) {
          case AM_SAMPLE_U8:
          case AM_SAMPLE_S16LE:
          case AM_SAMPLE_S16BE:
          case AM_SAMPLE_S24LE:
          case AM_SAMPLE_S24BE:
          case AM_SAMPLE_S32LE:
          case AM_SAMPLE_S32BE: {
            uint32_t lsb_depth = (audioInfo->sample_size > 3) ? 24 :
                audioInfo->sample_size * 8;
            audioInfo->type = AM_AUDIO_OPUS;
            m_encoder = opus_encoder_create(audioInfo->sample_rate,
                                            audioInfo->channels,
                                            OPUS_APPLICATION_AUDIO,
                                            &error);
            m_encode_frame_size = m_opus_config->encode.enc_frame_time_length
                * audioInfo->sample_rate / 1000;
            m_encode_frame_bytes = m_encode_frame_size * audioInfo->channels
                * audioInfo->sample_size;
            if (AM_LIKELY(m_encoder)) {
              uint32_t &bitrate = m_opus_config->encode.opus_avg_bitrate;
              uint32_t &complexity = m_opus_config->encode.opus_complexity;
              int ret = 0;
              do {
                ret = opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(bitrate));
                if (AM_UNLIKELY(OPUS_OK != ret)) {
                  ERROR("Failed to set bitrate to %u: %s",
                        bitrate, opus_strerror(ret));
                  break;
                }

                ret = opus_encoder_ctl(m_encoder,
                                       OPUS_SET_COMPLEXITY(complexity));
                if (AM_UNLIKELY(OPUS_OK != ret)) {
                  ERROR("Failed to set complexity to %u: %s",
                        complexity,
                        opus_strerror(ret));
                  break;
                }

                ret = opus_encoder_ctl(
                    m_encoder,
                    OPUS_SET_LSB_DEPTH(lsb_depth));
                if (AM_UNLIKELY(OPUS_OK != ret)) {
                  ERROR("Failed to set LSB depth to %u: %s",
                        lsb_depth,
                        opus_strerror(ret));
                  break;
                }

                m_repacketizer = opus_repacketizer_create();
                if (AM_UNLIKELY(!m_repacketizer)) {
                  ERROR("Failed to create OPUS repacketizer!");
                  break;
                }
                m_enc_output_buf =
                    new uint8_t[m_opus_config->encode.enc_output_size];
                if (AM_UNLIKELY(!m_enc_output_buf)) {
                  ERROR("Failed to allocate enc_output_buf error!");
                  m_enc_output_buf = nullptr;
                  break;
                }
                m_is_initialized = true;
              }while(0);
            } else {
              ERROR("Failed to create OPUS encoder:%s!", opus_strerror(error));
            }
          }break;
          default: {
            ERROR("Invalid input audio format! "
                  "U8/S16LE/S24LE audio is required!");
          }break;
        }
      }break;
      case AM_AUDIO_CODEC_MODE_DECODE: {
        int error;
        AM_AUDIO_INFO* audioInfo = &m_audio_info[AUDIO_INFO_DECODE];
        audioInfo->sample_rate = m_opus_config->decode.dec_output_sample_rate;
        audioInfo->channels =
            (m_opus_config->decode.dec_output_channel != 0) ?
                m_opus_config->decode.dec_output_channel :
                audioInfo->channels;
        audioInfo->sample_format = AM_SAMPLE_F32LE;
        audioInfo->sample_size = sizeof(float);
        NOTICE("\nInput Audio Parameter:"
               "\n   Sample Rate: %u"
               "\n       Channel: %u"
               "\nRequested Decoded Audio Format:"
               "\n Sample Format: %s"
               "\n   Sample Rate: %u"
               "\n       Channel: %u",
            audioInfo->sample_rate,
            audioInfo->channels,
            smp_fmt_to_str(AM_AUDIO_SAMPLE_FORMAT(audioInfo->sample_format)),
            m_opus_config->decode.dec_output_sample_rate,
            (m_opus_config->decode.dec_output_channel == 0) ?
                audioInfo->channels :
                m_opus_config->decode.dec_output_channel);
        m_decoder = opus_decoder_create(audioInfo->sample_rate,
                                        audioInfo->channels,
                                        &error);
        m_is_initialized = (m_decoder != nullptr);
        if (AM_UNLIKELY(!m_decoder)) {
          ERROR("Failed to create OPUS decoder: %s!", opus_strerror(error));
        }
      } break;
      default: {
        ERROR("Invalid opus codec mode!");
      } break;
    }
  } while (0);

  if (AM_UNLIKELY(!m_is_initialized)) {
    m_mode = AM_AUDIO_CODEC_MODE_NONE;
  }
  NOTICE("Audio codec %s is initialized to mode %s",
         m_name.c_str(), codec_mode_string().c_str());

  return m_is_initialized;
}

bool AMAudioCodecOpus::finalize()
{
  if (AM_LIKELY(m_encoder)) {
    opus_encoder_destroy(m_encoder);
    m_encoder = nullptr;
  }
  if (AM_LIKELY(m_decoder)) {
    opus_decoder_destroy(m_decoder);
    m_decoder = nullptr;
  }
  if (AM_LIKELY(m_repacketizer)) {
    opus_repacketizer_destroy(m_repacketizer);
    m_repacketizer = nullptr;
  }
  delete[] m_enc_output_buf;
  m_enc_output_buf = nullptr;
  m_is_initialized = false;
  m_mode = AM_AUDIO_CODEC_MODE_NONE;
  return true;
}

AM_AUDIO_INFO* AMAudioCodecOpus::get_codec_audio_info()
{
  AM_AUDIO_INFO *info = nullptr;
  switch (m_mode) {
    case AM_AUDIO_CODEC_MODE_ENCODE: {
      info = &m_audio_info[AUDIO_INFO_ENCODE];
    }
      break;
    case AM_AUDIO_CODEC_MODE_DECODE: {
      info = &m_audio_info[AUDIO_INFO_DECODE];
    }
      break;
    default:
      break;
  }

  return info;
}

uint32_t AMAudioCodecOpus::get_codec_output_size()
{
  uint32_t size = 0;
  switch (m_mode) {
    case AM_AUDIO_CODEC_MODE_ENCODE: {
      size = m_opus_config->encode.enc_output_size;
    }
      break;
    case AM_AUDIO_CODEC_MODE_DECODE: {
      size = m_opus_config->decode.dec_output_max_time_length
          * m_audio_info[AUDIO_INFO_DECODE].sample_rate / 1000
          * sizeof(uint16_t) * m_audio_info[AUDIO_INFO_DECODE].channels;
    }
      break;
    default:
      break;
  }

  return size;
}

bool AMAudioCodecOpus::check_encode_src_parameter(AM_AUDIO_INFO &info)
{
  bool ret = false;
  do {
    if (AM_UNLIKELY((info.sample_format != AM_SAMPLE_U8) &&
                    (info.sample_format != AM_SAMPLE_S16LE) &&
                    (info.sample_format != AM_SAMPLE_S16BE) &&
                    (info.sample_format != AM_SAMPLE_S24LE) &&
                    (info.sample_format != AM_SAMPLE_S24BE) &&
                    (info.sample_format != AM_SAMPLE_S32LE) &&
                    (info.sample_format != AM_SAMPLE_S32BE))) {
      break;
    }

    if (AM_UNLIKELY((info.sample_rate != 48000) &&
                    (info.sample_rate != 16000) &&
                    (info.sample_rate != 8000))) {
      break;
    }

    if (AM_UNLIKELY((info.channels != 1) && (info.channels != 2))) {
      break;
    }
    ret = true;
  }while(0);

  if (AM_UNLIKELY(!ret)) {
    WARN("\nWrong source audio parameters:"
         "\n Sample Format: %s"
         "\n   Sample Rate: %u"
         "\n      Channels: %u"
         "\nRequired source audio parameters:"
         "\n Sample Format: s16le | s16be | s24le | u8"
         "\n   Sample Rate: 48000 | 16000 | 8000"
         "\n      Channels: 1 | 2",
         smp_fmt_to_str(AM_AUDIO_SAMPLE_FORMAT(info.sample_format)),
         info.sample_rate,
         info.channels);
  }

  return ret;
}

uint32_t AMAudioCodecOpus::get_encode_required_chunk_size(AM_AUDIO_INFO &info)
{
  /* Opus's max frame length should not exceeds 120ms */
  uint32_t base = 20 * info.sample_rate / 1000 *
                  info.channels * info.sample_size;
  uint32_t max_chunk = m_opus_config->encode.enc_frame_time_length *
      info.sample_rate / 1000 * info.channels * info.sample_size;
  uint32_t chunk_size = 0;
  while ((chunk_size + base) <= max_chunk) {
    chunk_size += base;
  }

  return chunk_size;
}

uint32_t AMAudioCodecOpus::encode(uint8_t *input,
                                  uint32_t in_data_size,
                                  uint8_t *output,
                                  uint32_t *out_data_size)
{
  return m_opus_config->encode.repacketize ?
      encode_repackeize(input, in_data_size, output, out_data_size) :
      encode_single(input, in_data_size, output, out_data_size);
}

uint32_t AMAudioCodecOpus::decode(uint8_t *input,
                                  uint32_t in_data_size,
                                  uint8_t *output,
                                  uint32_t *out_data_size)
{
  AM_AUDIO_INFO &audioInfo = m_audio_info[AUDIO_INFO_DECODE];
  uint32_t totalFrames = opus_packet_get_nb_frames(input, in_data_size);
  uint32_t totalSamples =
      (totalFrames > 0) ?
          totalFrames
              * opus_packet_get_samples_per_frame(input,
                                                  audioInfo.sample_rate) :
          (uint32_t) -1;
  uint32_t ret = (uint32_t) -1;

  uint32_t &dec_output_max_time_length =
      m_opus_config->decode.dec_output_max_time_length;
  uint32_t &dec_output_sample_rate = audioInfo.sample_rate;
  if (AM_LIKELY((totalSamples >= 120) &&
                (totalSamples <=
                 dec_output_max_time_length * dec_output_sample_rate / 1000))) {
#if 0
    float decoded_float[totalSamples * sizeof(float)];
#endif
    *out_data_size = 0;
    while (totalSamples > 0) {
      int decoded_sample = opus_decode_float(m_decoder,
                                             (const unsigned char*)input,
                                             (opus_int32)in_data_size,
                                             (float*)(output + *out_data_size),
#if 0
                                             decoded_float,
#endif
                                             dec_output_max_time_length *
                                             dec_output_sample_rate / 1000,
                                             0);
      if (AM_LIKELY(decoded_sample > 0)) {
        totalSamples -= decoded_sample;
#if 0
        float_to_int(decoded_float,
                     (uint8_t*)(output + *out_data_size),
                     (uint32_t)decoded_sample * audioInfo.channels,
                     AM_AUDIO_SAMPLE_FORMAT(audioInfo.sample_format));
#endif
        *out_data_size += decoded_sample *
                          audioInfo.sample_size *
                          audioInfo.channels;
      } else {
        ret = (uint32_t)-1;
        break;
      }
    }
    if (AM_LIKELY(totalSamples == 0)) {
      ret = in_data_size;
    }
  }

  return ret;
}

AMAudioCodecOpus::AMAudioCodecOpus() :
    inherited(AM_AUDIO_CODEC_OPUS, "opus")
{
}

AMAudioCodecOpus::~AMAudioCodecOpus()
{
  finalize();
  delete m_config;
}

bool AMAudioCodecOpus::init(const char* config)
{
  bool ret = true;
  std::string conf_file(config);
  do {
    ret = inherited::init();
    if (AM_UNLIKELY(!ret)) {
      ERROR("inherited init failed!");
      break;
    }
    m_config = new AMAudioCodecOpusConfig();
    if (AM_UNLIKELY(!m_config)) {
      ret = false;
      ERROR("Failed to create opus codec config module!");
      break;
    }
    m_opus_config = m_config->get_config(conf_file);
    if (AM_UNLIKELY(!m_opus_config)) {
      ERROR("Failed to get opus codec config!");
      ret = false;
      break;
    }
  } while (0);

  return ret;
}

uint32_t AMAudioCodecOpus::encode_repackeize(uint8_t *input,
                                             uint32_t in_data_size,
                                             uint8_t *output,
                                             uint32_t *out_data_size)
{
  *out_data_size = 0;
  /* Opus Raw data separator */
  output[0] = 'O';
  output[1] = 'P';
  output[2] = 'U';
  output[3] = 'S';
  /* Opus Raw data size */
  output[4] = 0;
  output[5] = 0;
  output[6] = 0;
  output[7] = 0;
  if (AM_LIKELY(0 == (in_data_size % m_encode_frame_bytes))) {
    bool is_ok = true;
    uint8_t *out = m_enc_output_buf;
    uint32_t &enc_output_size = m_opus_config->encode.enc_output_size;
    uint32_t count = in_data_size / m_encode_frame_bytes;
    AM_AUDIO_INFO &ainfo = m_audio_info[AUDIO_INFO_ENCODE];
    uint32_t sample_count = m_encode_frame_bytes / ainfo.sample_size;
    float out_data[sample_count];

    m_repacketizer = opus_repacketizer_init(m_repacketizer);
    for (uint32_t i = 0; i < count; i ++) {
      uint8_t *in_data = (input + i * m_encode_frame_bytes);
      int ret = 0;
      int_to_float(in_data, out_data, sample_count,
                   AM_AUDIO_SAMPLE_FORMAT(ainfo.sample_format));
      ret = opus_encode_float(m_encoder,
                              (const float*)out_data,
                              m_encode_frame_size,
                              out,
                              enc_output_size);
      if (AM_LIKELY(ret > 0)) {
        int retval = opus_repacketizer_cat(m_repacketizer, out, ret);
        if (AM_UNLIKELY(retval != OPUS_OK)) {
          ERROR("Opus repacketizer error: %s", opus_strerror(retval));
          is_ok = false;
          break;
        }
        out += ret;
      } else {
        ERROR("Opus encode error: %s", opus_strerror(ret));
        is_ok = false;
        break;
      }
    }
    if (AM_LIKELY(is_ok)) {
      int ret = opus_repacketizer_out(m_repacketizer, output + 8,
                                      enc_output_size);
      if (AM_LIKELY(ret > 0)) {
        output[4] = (ret & 0xff000000) >> 24;
        output[5] = (ret & 0x00ff0000) >> 16;
        output[6] = (ret & 0x0000ff00) >>  8;
        output[7] = (ret & 0x000000ff);
        *out_data_size = ret + 8;
      } else {
        ERROR("Opus repacketizer error: %s", opus_strerror(ret));
      }
    }
  } else {
    ERROR("Invalid input data length: %u, must be multiple of %u!",
          in_data_size,
          m_encode_frame_bytes);
  }
  return *out_data_size;
}

uint32_t AMAudioCodecOpus::encode_single(uint8_t *input,
                                         uint32_t in_data_size,
                                         uint8_t *output,
                                         uint32_t *out_data_size)
{
  uint8_t *out = output;
  *out_data_size = 0;

  if (AM_LIKELY(0 == (in_data_size % m_encode_frame_bytes))) {
    uint32_t &enc_output_size = m_opus_config->encode.enc_output_size;
    uint32_t count = in_data_size / m_encode_frame_bytes;
    AM_AUDIO_INFO &ainfo = m_audio_info[AUDIO_INFO_ENCODE];
    uint32_t sample_count = m_encode_frame_bytes / ainfo.sample_size;
    float data[sample_count];

    for (uint32_t i = 0; i < count; i ++) {
      uint8_t *in_data = input + i * m_encode_frame_bytes;
      int ret = 0;
      int_to_float(in_data, data, sample_count,
                   AM_AUDIO_SAMPLE_FORMAT(ainfo.sample_format));
      ret = opus_encode_float(m_encoder,
                              (const float*)data,
                              m_encode_frame_size,
                              (out + 8), /* Skip out header */
                              enc_output_size);
      if (AM_LIKELY(ret > 0)) {
        /* Opus Raw data separator */
        out[0] = 'O';
        out[1] = 'P';
        out[2] = 'U';
        out[3] = 'S';
        /* Opus Raw data size */
        out[4] = (ret & 0xff000000) >> 24;
        out[5] = (ret & 0x00ff0000) >> 16;
        out[6] = (ret & 0x0000ff00) >>  8;
        out[7] = (ret & 0x000000ff);
        out += (ret + 8);
        *out_data_size += (ret + 8);
      } else {
        ERROR("Opus encode error: %s", opus_strerror(ret));
        break;
      }
    }
  } else {
    ERROR("Invalid input data length: %u, must be multiple of %u!",
          in_data_size,
          m_encode_frame_bytes);
  }
  return *out_data_size;
}