#ifndef _MST_TEMPO_H_
#define _MST_TEMPO_H_

#if 0
typedef int             MS_S32;
typedef short           MS_S16;
typedef char            MS_S8;
typedef unsigned  int   MS_U32;
typedef unsigned  short MS_U16;
typedef unsigned  char  MS_U8;
#endif

int mst_tempo_get_sram_size(int sample_rate);
void * mst_tempo_init (unsigned char *heap, int sample_rate, int *in_frame_size, int tempo_rate);
void mst_tempo_reset(void *  itempo, float tempo_rate, int sample_rate, int *in_frame_size);
int mst_tempo_process(void *  itempo, short * in, int inputSize, short *  out,  int *outputSize);
void mst_tempo_framesize(int samplerate, int *framesize, int *overlaysize);

#endif //_MST_TEMPO_H_
