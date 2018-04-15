
#include <alsa/asoundlib.h>

#define SND_CARD "default"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <endian.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

// NOTE use sizes from STDINT
// NOTE verify data alignment!
struct wave_header
{
  // RIFF Chunk descriptor
  uint32_t chunk_id;        // B   "RIFF" 0x52494646 BE
  uint32_t chunk_size;      // L   36 + SubChunk2Size. Entire file - 8 bytes
  uint32_t format;          // B   "WAVE" 0x57415645 BE
  // FMT sub-chunk
  uint32_t subchunk_1_id;   // B   "fmt " 0x666d7420 BE
  uint32_t subchunk_1_size; // L   16 for PCM
  uint16_t audio_format;    // L   PCM = 1, else Compressed
  uint16_t num_channels;    // L   1 = Mono, 2 = Stereo, etc
  uint32_t sample_rate;     // L   8000, 44100, etc
  uint32_t byte_rate;       // L   SampleRate * Num channels * Bits per sample / 8
  uint16_t block_align;     // L   Num Channels * bits per samlple/8. Bytes per sample inclusive of channels
  uint16_t bits_per_sample; // L   8, 16, etc
  // DATA sub-chunk
  uint32_t subchunk_2_id;   // B   "data" 0x64617461 BE
  uint32_t subchunk_2_size; // L   num samples * num channels * bits per sample/8
} __attribute__((aligned(4)));

#define CHUNK_ID      be32toh(0x52494646)
#define FORMAT        be32toh(0x57415645)
#define SUBCHUNK1_ID  be32toh(0x666d7420)
#define SUBCHUNK2_ID  be32toh(0x64617461)
#define WAVE_HEADER_SIZE sizeof(struct wave_header)

void pr_usage(char* pname)
{
  printf("usage: %s WAV_FILE\n", pname);
}

/* @brief Read WAVE header
   @param fp file pointer
   @param dest destination struct
   @return 0 on success, < 0 on error */
int read_wave_header(FILE* fp, struct wave_header* dest)
{
  int size = sizeof(struct wave_header);
  size_t ret;

  if (!dest || !fp)
    {
      return -ENOENT;
    }

  // NOTE do not assume file pointer is at its starting point
  if(fseek(fp, 0, SEEK_SET))
    return errno;

  printf("Seeked to beginning of file\n");

  // read 44 bytes from file and store in wave header ptr
  ret = fread(dest, 1, size, fp);

  printf("Read %d bytes from file\n", (int)ret);

  if(ret != size)
    return -ENODATA;

  return 0;
}

/* @brief Parse WAVE header and print parameters
   @param hdr a struct wave_header variable
   @return 0 on success, < 0 on error or if not WAVE file*/
int parse_wave_header(struct wave_header hdr)
{
  // verify that this is a RIFF file header
  if(hdr.chunk_id != CHUNK_ID)
  {
    printf("Header not RIFF !\n");
    return 1;
  }

  printf("Found RIFF Header\n");

  // verify that this is WAVE file
  if(hdr.format != FORMAT)
  {
    printf("File format not WAVE!\n");
    return 1;
  }

  printf("File format: WAVE\n");
  printf("\tWAV File size: %d\n", hdr.chunk_size + 8);

  if((hdr.subchunk_1_id != SUBCHUNK1_ID) ||
     (hdr.subchunk_1_size != 16) ||
     (hdr.audio_format != 1))
  {
    printf("Audio format not PCM!\n");
    printf("Subchunk 1 id: %x\n", be32toh(hdr.subchunk_1_id));
    printf("subchunk 1 size: %u\n", hdr.subchunk_1_size);
    printf("Audio format: %u\n", hdr.audio_format);
    return 1;
  }

  printf("Audo format: PCM\n");

  // print out information: number of channels, sample rate, total size
  printf("\tNumber of channels: %u\n", hdr.num_channels);
  printf("\tSample rate: %d Hz\n", hdr.sample_rate);
  printf("\tByte Rate: %d Hz\n", hdr.byte_rate);
  printf("\tBlock align: %d byte(s)\n", hdr.block_align);
  printf("\tBits per sample: %d bits\n", hdr.bits_per_sample);

  if(hdr.subchunk_2_id != SUBCHUNK2_ID)
  {
    printf("Subchunk 2 ID invalid!\n");
    return 1;
  }

  printf("Data section information:\n");
  printf("\tBytes in data section: %d\n", hdr.subchunk_2_size);
  if(hdr.subchunk_2_size + 36 != hdr.chunk_size)
  {
    printf("Something wrong with chunk sizes\n");
    //return 1;
  }

  return 0;
}

/* @brief Transmit a word (put into FIFO)
 * @param hdr wave_header struct for sample_rate
   @param word a 32-bit word */
void fifo_transmit_word(FILE * chardev, uint32_t word)
{
	int ret;
	// write to kernel module
	// Lab 4.4.1) Fwrite is buffered, and will store a page at a time to write. write will write
	// exactly the number of bytes its told to write. Using write would cause a billion system calls
	// and be slightly slower on average
	ret = fwrite((void *)&word, 4, 1, chardev);

	if(ret == 0)
	{
		printf("Error = %d\n", errno);
	}
}

/* @brief Build a 32-bit audio word from a buffer
   @param hdr WAVE header
   @param buf a byte array
   @return 32-bit word */
uint32_t audio_word_from_buf(struct wave_header hdr, uint8_t* buf)
{
  // build word depending on bits per sample, etc
  // only supports up to 32 bits per sample
  uint32_t audio_word = 0;
  int32_t shift_amt = shift_amt = 32 - hdr.bits_per_sample;;

  if(hdr.bits_per_sample <= 8){
    audio_word = buf[0];
  }
  else if(hdr.bits_per_sample <= 16){
    audio_word = buf[0] | (buf[1] << 8);
  }
  else if(hdr.bits_per_sample <= 24){
    audio_word = buf[0] | (buf[1] << 8) | (buf[2] << 16);
  }
  else{
    audio_word = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
  }

  audio_word = audio_word << shift_amt;

  return audio_word;
}

/* @brief Play sound samples
   @param fp file pointer
   @param hdr WAVE header
   @param sample_count how many samples to play or -1 plays to end of file
   @param start starting point in file for playing
   @return 0 if successful, < 0 otherwise */
int play_wave_samples(FILE* fp,
					  FILE* chardev,
                      struct wave_header hdr,
                      int sample_count,
                      unsigned int start)
{
  unsigned int start_byte = WAVE_HEADER_SIZE + (start * hdr.block_align);
  uint32_t sample;
  // Note: this gets really annoying if we're at for example 24 bits per sample.
  // Worry about that later
  uint32_t bytes_per_sample = hdr.bits_per_sample/8;
  size_t samples_read;

  if (!fp)
  {
    return -EINVAL;
  }

  // NOTE reject if number of channels is not 1 or 2
  if(hdr.num_channels != 1 && hdr.num_channels != 2)
  {
    printf("Number of channels: (%u) is invalid!", hdr.num_channels);
    return -EINVAL;
  }

  //calculate starting point and move there
  if(fseek(fp, start_byte, SEEK_SET))
    return errno;

  // continuously read frames/samples and use fifo_transmit_word to
  // simulate transmission
  while (sample_count > 0)
    {
      // read chunk (whole frame)
      samples_read = fread(&sample, bytes_per_sample, 1, fp);

      fifo_transmit_word(chardev, audio_word_from_buf(hdr, (uint8_t *)&sample));

      if(samples_read != 1){
    	 //printf("LEFT: Tried to read %d bytes but read %d instead!\n", bytes_per_sample, bytes_read);
        return -ENODATA;
      }

      // write samples properly independently if file is mono or stereo
      if(hdr.num_channels == 2)
      {
        samples_read = fread(&sample, bytes_per_sample, 1, fp);
        if(samples_read != 1)
        {
            return -ENODATA;
        }
      }

      fifo_transmit_word(chardev, audio_word_from_buf(hdr, (uint8_t *)&sample));

      sample_count--;
    }

  return 0;
}

int i2s_enable_tx(void)
{
	ssize_t numwritten;
	char buf = '1';
	int fd = open("/sys/devices/soc0/amba_pl/77600000.axi_i2s_adi/tx_enabled", O_WRONLY);

	if(fd == -1)
		return errno;

	numwritten = write(fd, &buf, sizeof(char));

	if(numwritten != sizeof(char))
		return errno;

	if(!close(fd))
		return errno;

	return 0;
}

int i2s_disable_tx(void)
{
	ssize_t numwritten;
	char buf = '0';
	int fd = open("/sys/devices/soc0/amba_pl/77600000.axi_i2s_adi/tx_enabled", O_WRONLY);

	if(fd == -1)
		return errno;

	numwritten = write(fd, &buf, sizeof(char));

	if(numwritten != sizeof(char))
		return errno;

	if(!close(fd))
		return errno;

	return 0;
}

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
	printf("PANIC\n");
    return -1;
  }

  // set format
  // NOTE: the codec only supports one audio format, this should be constant
  //       and not read from the WAVE file. You must convert properly to this
  //       format, regardless of the format in your WAVE file
  //       (bits per sample and alignment).
  err = snd_pcm_hw_params_set_format(handle, params, format);
  if (err < 0)
  {
	  printf("PANIC 2\n");
	  return -1;
  }

  // set channel count
  err = snd_pcm_hw_params_set_channels(handle, params, 2);
  if (err < 0)
  {
  	  printf("PANIC 3\n");
  	  return -1;
  }

  // set sample rate
  printf("attempting to set sample rate %u\n", sample_rate);
  err = snd_pcm_hw_params_set_rate_near(handle, params, &sample_rate, 0);

  printf("Sample rate set to %u\n", sample_rate);

  if (err < 0)
  {
	  printf("PANIC 4\n");
      return -1;
  }


  // write parameters to device
  err = snd_pcm_hw_params(handle, params);
  if (err < 0)
  {
	printf("PANIC 5\n");
	return -1;
  }

  return 0;
}

int main(int argc, char **argv)
{
  snd_pcm_t *handle;
  snd_pcm_hw_params_t *hwparams;
  int err;
  // placeholder variables, use values you read from your WAVE file
  unsigned int sample_rate;
  snd_pcm_format_t sound_format = SND_PCM_FORMAT_S32_LE;

  FILE* fp;
  FILE* chardev;
  struct wave_header hdr;
  int ret;

  printf("Size of wave header: %lu\n", sizeof(struct wave_header));

  // check number of arguments
  if (argc < 2)
  {
      // fail, print usage
      pr_usage(argv[0]);
      return 1;
  }

  // allocate HW parameter data structures
  snd_pcm_hw_params_alloca(&hwparams);

  // open device (TX)
  err = snd_pcm_open(&handle, SND_CARD, SND_PCM_STREAM_PLAYBACK, 0);
  if (err < 0)
  {
    printf("PAINC 6\n");
    return -1;
  }

  // do rest of initialization (from pre-lab 4a)
  // play sound (from pre-lab 4a)

  // open file
  fp = fopen(argv[1], "r");

  if(!fp)
  {
    printf("Could not open file %s for reading\n", argv[1]);
    snd_pcm_close(handle);
    return errno;
  }

 // initalize AXI FIFO, map /dev/mem offset for the AXI control registers
  chardev = fopen("/dev/zedaudio0", "w");
  if(!chardev)
  {
    printf("Failed to open kernel module %d:\n", errno);
    fclose(fp);
    snd_pcm_close(handle);
    return errno;
  }

  // read file header
  ret = read_wave_header(fp, &hdr);
  if(ret)
  {
    printf("Could not read wave header from %s\n", argv[1]);
    fclose(chardev);
    fclose(fp);
    snd_pcm_close(handle);
    return ret;
  }

  // parse file header, verify that is wave
  ret = parse_wave_header(hdr);
  if(ret)
  {
    printf("Error parsing wave header file %s\n", argv[1]);
    fclose(chardev);
    fclose(fp);
    snd_pcm_close(handle);
    return ret;
  }

  sample_rate = hdr.sample_rate;

  err = configure_codec(sample_rate, sound_format, handle, hwparams);
  if (err < 0)
  {
    printf("PANIC 7\n");
    fclose(chardev);
    fclose(fp);
    snd_pcm_close(handle);
    return -1;
  }

  err = i2s_enable_tx();
  if (err < 0)
  {
	  printf("PANIC 8\n");
	  fclose(chardev);
	  fclose(fp);
	  snd_pcm_close(handle);
	  return -1;
  }

  // play entire file
  int sample_count = hdr.subchunk_2_size/hdr.block_align;

  ret = play_wave_samples(fp, chardev, hdr, sample_count,  0);
  if(ret)
  {
    printf("Error playing wave file %s\n", argv[1]);
    printf("Tried to play %d samples\n", sample_count);
    printf("Return code %d\n", ret);
    fclose(chardev);
    fclose(fp);
    snd_pcm_close(handle);
    i2s_disable_tx();
    return ret;
  }
/*
  // play last one second of file
  ret = play_wave_samples(fp, hdr, hdr.sample_rate, hdr.sample_rate);
  if(ret)
  {
    printf("Error playing wave file %s\n", argv[1]);
    (void)munmap(pFIFO, 0x1000);
    close(fd);
    fclose(fp);
    snd_pcm_close(handle);
    i2s_disable_tx();
    return ret;
  }
*/
  // do rest of cleanup
  fclose(chardev);
  fclose(fp);
  snd_pcm_close(handle);
  i2s_disable_tx();
  return 0;
}
