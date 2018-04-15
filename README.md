# Lab Assignment 4

## Minimal ALSA lib usage example

This is the minimal code example for usage of the ALSA library to correctly configure the CODEC. See the TODOs in the code and follow the assignment on how to implement those.

```c
#include <alsa/asoundlib.h>

#define SND_CARD "default"

int configure_codec(unsigned int sample_rate, 
                    snd_pcm_format_t format, 
                    snd_pcm_t* handle,
                    snd_pcm_hw_params_t* params)
{
  int err;
  
  // initialize parameters 
  err = snd_pcm_hw_params_any(handle, params);
  if (err < 0)
  {
    // failed, handle and return...
  }
  
  // TODO: set format
  // NOTE: the codec only supports one audio format, this should be constant
  //       and not read from the WAVE file. You must convert properly to this 
  //       format, regardless of the format in your WAVE file 
  //       (bits per sample and alignment).
  
  // TODO: set channel count
  
  // TODO: set sample rate
  
  // TODO: write parameters to device
  
  return 0;
}

int main(void)
{
  snd_pcm_t *handle;
  snd_pcm_hw_params_t *hwparams;
  int err;
  // placeholder variables, use values you read from your WAVE file
  unsigned int sample_rate;
  snd_pcm_format_t sound_format;
  
  // TODO read WAVE file, find out parameters, etc (from pre-lab 4a)
  
  // allocate HW parameter data structures
  snd_pcm_hw_params_alloca(&hwparams);
  
  // open device (TX)
  err = snd_pcm_open(&handle, SND_CARD, SND_PCM_STREAM_PLAYBACK, 0);
  if (err < 0)
  {
    // failed, handle...
  }
  
  err = configure_codec(sample_rate, sound_format, handle, hwparams);
  if (err < 0)
  {
    // failed, handle...
  }
  
  // TODO do rest of initialization (from pre-lab 4a)
  // TODO play sound (from pre-lab 4a)
  
  snd_pcm_close(handle);
  
  // TODO do rest of cleanup
  return 0;
}
```
