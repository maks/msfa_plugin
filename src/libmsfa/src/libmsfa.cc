/*
 * Copyright 2022 Maksim Lin.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <iostream>
#include <fstream>
#include <cstdlib>

#include "synth.h"
#include "synth_unit.h"
#include "module.h"
#include "freqlut.h"
#include "sin.h"
#include "sawtooth.h"

#include "../include/libmsfa.h"

//for initial onlytesting
#include "patch.h"

#define MINIAUDIO_IMPLEMENTATION
#include "../include/miniaudio.h"

#define MAX_ENGINES 16

double sample_rate = 44100.0;

RingBuffer *ring_buffer[MAX_ENGINES];
SynthUnit *synth_unit[MAX_ENGINES];
u_short engine_id_count = 0;

ma_device *device;

void data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount) {
  // Use MSFA frames
  // iterate through all channels and get samples and mix them together
  int16_t *buffer = (int16_t *)malloc(frameCount * sizeof(int16_t));
  for (int channel = 0; channel < engine_id_count; channel++) {
    synth_unit[channel]->GetSamples(frameCount, buffer);

    /* Mix the frames together. */
    for (int iSample = 0; iSample < frameCount; iSample++) {
      ((int16_t *)pOutput)[iSample] += buffer[iSample];
    }
  }
  free(buffer);

  (void)pInput;
}

msfa_result initEngine() {
  ma_result result;
  ma_device_config deviceConfig;

  // Init MSFA engine
  createSynth();

  if (device == nullptr) {
    device = (ma_device *)malloc(sizeof(ma_device));
    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_s16;
    deviceConfig.playback.channels = 1;
    deviceConfig.sampleRate = ma_standard_sample_rate_44100;
    deviceConfig.dataCallback = data_callback;

    if (ma_device_init(NULL, &deviceConfig, device) != MA_SUCCESS) {
      // printf("Failed to open playback device.\n");
      return MSFA_AUDIO_OPEN_FAILED;
    }

    if (ma_device_start(device) != MA_SUCCESS) {
      // printf("Failed to start playback device.\n");
      ma_device_uninit(device);
      return MSFA_AUDIO_START_FAILED;
    }
  }
  return MSFA_SUCCESS;
}

void createSynth() {
  SynthUnit::Init(sample_rate);
  auto rb = ring_buffer[engine_id_count] = new RingBuffer();
  synth_unit[engine_id_count] = new SynthUnit(rb);
  printf("MSFA synth engine %d initialized\n", engine_id_count);
  engine_id_count++;
}

void sendMidi(const uint8_t *bytes, int size) {
  short channel = 0;
  uint8_t midiCommand = bytes[0] & 0xF0; // mask off all but top 4 bits
  if (midiCommand >= 0x80 && midiCommand <= 0xE0) {
    // it's a voice message
    // find the channel by masking off all but the low 4 bits
    uint8_t midiChannel = bytes[0] & 0x0F;
    channel = midiChannel;
  }

  if (channel < engine_id_count) {
    printf("[%d] send to channel: %X,%X\n", channel, bytes[0], bytes[1]);
    ring_buffer[channel]->Write(bytes, size);
  } else {
    printf("Channel %d not found\n", channel);
  }
}

void sendMidiToChannel(uint8_t channel, const uint8_t *bytes, int size) {
  if (channel < engine_id_count) {
    printf("[%d] send to channel: %X,%X\n", channel, bytes[0], bytes[1]);
    ring_buffer[channel]->Write(bytes, size);
  } else {
    printf("Channel %d not found\n", channel);
  }
}

uint8_t parseChannelFromMessage(const uint8_t midiStatus) {
  uint8_t midiCommand = midiStatus & 0xF0; // mask off all but top 4 bits

  if (midiCommand >= 0x80 && midiCommand <= 0xE0) {
    // it's a voice message
    // find the channel by masking off all but the low 4 bits
    uint8_t midiChannel = midiStatus & 0x0F;
    return midiChannel;
  } else {
    return 0;
  }
}

void shutdownEngine() {
  ma_device_uninit(device);
  free(device);
}