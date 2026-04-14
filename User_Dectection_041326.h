#ifndef SPEAKER_MFCC_H
#define SPEAKER_MFCC_H

#include <stdbool.h>
#include <aubio/aubio.h>
#include "portaudio.h"

#ifdef __cplusplus
extern "C" {
#endif

// ===================== CONFIG =====================
#define N_COEFFS 13

// Detection tuning
#define ENERGY_THRESHOLD 0.0001f
#define DETECTION_ON_THRESHOLD 2.0f
#define DETECTION_OFF_THRESHOLD 2.5f

// ===================== STATE =====================
typedef enum {
    MODE_LEARN,
    MODE_DETECT
} system_mode_t;

typedef struct {
    float mfcc_sum[N_COEFFS];
    float mfcc_avg[N_COEFFS];
    int count;
} cumulative_mfcc;

typedef struct {
    float user_mfcc[N_COEFFS];
} user_info;

// ===================== PUBLIC STATE =====================
extern volatile int user_detected;
extern volatile system_mode_t system_mode;

// ===================== API =====================

// Lifecycle
int speaker_init(int sample_rate, int frames_per_buffer);
void speaker_start(void);
void speaker_stop(void);

// Training
void speaker_train(int seconds);

// Detection (call continuously in loop)
void speaker_process(short *buffer);

// Access results
int speaker_is_detected(void);

// Persistence
int speaker_load_model(const char *path);
int speaker_save_model(const char *path);

#ifdef __cplusplus
}
#endif

#endif