// High level
// Initializes Audio Input, Initializes Audio Processing
// Listens for audio
// Computes MFCC
// Compares MFCC to known user
// Uses difference to determine if user is talking

// To install:
// Microphone device on pi
// Aubio installed
// Port audio installed
// It should run, might need to do some tweaking with mic

// Output:
// int user_detected
// running average of the difference between user MFCCs and live MFCCs


// Includes
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "portaudio.h"
#include <aubio/aubio.h>
#include <stdbool.h>

// Define magic number variable
#define ENERGY_THRESHOLD 0.0001f
#define N_COEFFS 13
#define DETECTION_ON_THRESHOLD 2.0f
#define DETECTION_OFF_THRESHOLD 2.5f
#define SMOOTH_WINDOW 5
#define UPDATE_SECONDS 1 //max time before an update is made to a speaker detection

// Define system testing or training mode
typedef enum{
	MODE_LEARN,
	MODE_DETECT
} system_mode_t;

volatile int user_detected = 0;
volatile system_mode_t system_mode = MODE_LEARN;

// Function Defs

// Define cumulative MFCC struct
typedef struct {
	float mfcc_sum[N_COEFFS];
	float mfcc_avg[N_COEFFS];
	int count;
} cumulative_mfcc;

// Define user struct
typedef struct {
	float user_mfcc[N_COEFFS];
} user_info;

//Define globals after types
float diff_history[SMOOTH_WINDOW] = {0}; // smooth out user dtetection
int diff_index = 0;	// counting index
int diff_count = 0;	//number of counts for smoothing
cumulative_mfcc cm;
user_info user;

int was_talking = 0;
float sum_energy = 0;
float total_diff_to_user = 0;

fvec_t *process_input;
aubio_pvoc_t *pv;
cvec_t *fftgrain;
aubio_mfcc_t *mfcc;
fvec_t *mfcc_out;
int sample_rate = 48000;
int frames_per_buffer = 1024;
int frames_until_update;
int frame_counter = 0;

// Struct specific helpers
// Reset accumulator
void reset_cumulative_mfcc (cumulative_mfcc *cm){
	int k = 0;
	for(k = 0; k < N_COEFFS; k++){
		cm->mfcc_avg[k] = 0;
		cm->mfcc_sum[k] = 0;
	}
	cm->count = 0;
}

//Compute mfcc average
void average_mfcc(cumulative_mfcc *cm){
	if(cm->count == 0) return;
	int k = 0;
	for(k = 0; k < N_COEFFS; k++){
		cm->mfcc_avg[k] = cm->mfcc_sum[k]/cm->count;
	}	
}

// Sum MFCCs during recording to be used for an average across frames after noise stops
void accumulate_mfcc(cumulative_mfcc *cm, fvec_t *mfcc_current){
	int k = 0;
	for(k = 0; k < N_COEFFS; k++){
		cm->mfcc_sum[k] = cm->mfcc_sum[k] + mfcc_current->data[k];
	}	
	cm->count++;
}

float smooth_diff(float new_diff){
	diff_history[diff_index] = new_diff;
	diff_index = (diff_index + 1) % SMOOTH_WINDOW; //continuously write/overwrite buffer of size SMOOTHIG_WINDOW
	if(diff_count < SMOOTH_WINDOW){
		diff_count++; //iterate count up to 5. Once at 5, hold
	}
	float sum = 0;
	for(int i = 0; i < diff_count; i++){
		sum += diff_history[i]; //accumulate diffs
	}
	
	if(diff_count == 0) return new_diff;
	return sum / diff_count;
}



float compute_sound_energy(int frames_per_buffer, short *buffer){
	float sum_energy = 0;
	float norm_buffer = 0;
	int i = 0;
	for (i = 0; i < frames_per_buffer; i++){
		norm_buffer = buffer[i] / 32768.0f;
		sum_energy += norm_buffer * norm_buffer;
	}
	sum_energy /= frames_per_buffer;
	return sum_energy;
}

// Compute MFCCs
void compute_mfcc(aubio_pvoc_t *pv, fvec_t *process_input, cvec_t *fftgrain, aubio_mfcc_t *mfcc, fvec_t *mfcc_out){
	aubio_pvoc_do(pv, process_input, fftgrain);
	aubio_mfcc_do(mfcc, fftgrain, mfcc_out);
}


// Compute Distance to User
float compute_mfcc_difference(float *user_mfccs, float *mfcc_avg, int n_coeffs){
	int k = 0;
	float diff_to_user = 0;
	float total_diff_to_user = 0;
	for(k = 0; k < n_coeffs; k++){
			diff_to_user = user_mfccs[k] - mfcc_avg[k];
			total_diff_to_user += diff_to_user * diff_to_user;
		}
	total_diff_to_user = sqrt(total_diff_to_user);
	return total_diff_to_user;
}

// Edn Port audio
void end_PA(PaStream* stream){
	//fclose(test_file_ptr);	//Close file
	Pa_StopStream(stream);	//Stop stream
	Pa_CloseStream(stream);	//Close stream
	Pa_Terminate(); //End port audio
}


// Move all voice processing to a single function outside main
void voice_process(short *buffer, int frames_per_buffer){

    sum_energy = compute_sound_energy(frames_per_buffer, buffer); 

    if (sum_energy > ENERGY_THRESHOLD){
        for (int k = 0; k < frames_per_buffer; k++){
            process_input->data[k] = buffer[k] / 32768.0f;
        }
        compute_mfcc(pv, process_input, fftgrain, mfcc, mfcc_out);
        accumulate_mfcc(&cm, mfcc_out);
        was_talking = 1;
    }

    frame_counter++;
    
    //Check mfccs during silence or after a set period of time of noise
    if ((was_talking && cm.count > 5) ||
        (frame_counter >= frames_until_update && cm.count > 0)){

        frame_counter = 0;

        average_mfcc(&cm);
        total_diff_to_user = compute_mfcc_difference(user.user_mfcc, cm.mfcc_avg, N_COEFFS);

        float smoothed_difference = smooth_diff(total_diff_to_user);

        if (user_detected == 0 && smoothed_difference < DETECTION_ON_THRESHOLD){
            user_detected = 1;
        }
        else if(user_detected == 1 && smoothed_difference > DETECTION_OFF_THRESHOLD){
            user_detected = 0;
        }
		if (user_detected == 1){
			printf("User Detected! \n");
		}
		else{
			printf("No user detected \n");
		}
        printf("Smoothed diff: %f\n\n", smoothed_difference);

        // reset
        was_talking = 0;
        reset_cumulative_mfcc(&cm);

        diff_index = 0;
        diff_count = 0;
        for(int i = 0; i < SMOOTH_WINDOW; i++){
            diff_history[i] = 0;
        }
    }
}



int main(void){
	
	// For Audio processing
	PaError pa_err;
	pa_err = Pa_Initialize();	//Init port audio	
	if(pa_err != paNoError){
		printf("PortAudio init error \n");
		return -1;
	}
	
	PaStream* stream; //Create Stream
	PaStreamParameters inputDevice = {0};
	short *buffer = (short*)malloc(sizeof(short) * frames_per_buffer);		//Buffer initialize, using short whule I have the mic input as int16. Must use Malloc for proper buffer handling.
	inputDevice.channelCount = 1;	//Device only has once channel
	inputDevice.device = Pa_GetDefaultInputDevice();	// Use the default input device. If this doesn't work, set your system default in OS > Settings
	//inputDevice.device = 1;	// If the device ID is known, you can manually enter it.
	inputDevice.hostApiSpecificStreamInfo = NULL;
	inputDevice.suggestedLatency = Pa_GetDeviceInfo(inputDevice.device)->defaultHighInputLatency;	//From device info, default low latency = 0.0080
	inputDevice.sampleFormat = paInt16; //Use int16 for now
	
	// For MFCC - Voice Recognition
	uint_t buf_size = frames_per_buffer; //buffer size
	uint_t hop_s = frames_per_buffer/1; // block size
	uint_t sample_rate_for_mfcc = sample_rate; // samplerate
	uint_t n_filters = 40; // number of filters
	//Note: workflow for MFCC input -> PVOC -> MFCC
	
	pv = new_aubio_pvoc(buf_size, hop_s);
	fftgrain = new_cvec(buf_size); // pvoc output / mfcc input /fft
	mfcc_out = new_fvec(N_COEFFS);   // mfcc output
	process_input = new_fvec(hop_s);
	mfcc = new_aubio_mfcc(buf_size, n_filters, N_COEFFS, sample_rate_for_mfcc); // init mfcc
	
	// PortAudio Implementation
	// FILE* test_file_ptr; //Test file to port audio data into	
	// test_file_ptr = fopen("test_file.raw", "wb");
	// if(test_file_ptr == NULL){
	//	printf("File Open Error \n");
	//	return -1;
	//}
	
	pa_err = Pa_OpenStream(&stream, &inputDevice, NULL, sample_rate, frames_per_buffer,paClipOff , NULL, NULL);	//Initialize stream
	if(pa_err != paNoError){
		printf("PortAudio Open Stream Error: %s\n", Pa_GetErrorText(pa_err));
		return -1;
	}
	//pa_err = Pa_StartStream(stream); //Start Stream	
		//if(pa_err != paNoError){
			//printf("PortAudio Start Stream Error: %s\n", Pa_GetErrorText(pa_err));
			//return -1;
		//}
	
	
	// Normal vars for calcs
	int secs_to_rec = 15;	//Seconds to record before ending
	//int user_training_status = 0;
	int buffers_to_rec = secs_to_rec * sample_rate / frames_per_buffer;	//Number of buffers that will be recorded
	int buffers_recorded = 0;	// init buffers recorded to increment as recoring proceeds
	reset_cumulative_mfcc(&cm);
	
	// User detection
	
	//Reload MFCCs if possible
	FILE *f = fopen("user_mfcc.bin", "rb");
	if (f == NULL){
		printf("No MFCC info found. Recommend running training mode \n");
	}
	else{
		fread(user.user_mfcc, sizeof(float), N_COEFFS, f);
		fclose(f);
		system_mode = MODE_DETECT;
		printf("Loaded Saved User MFCCs \n");
	}
	
	if(system_mode == MODE_LEARN){
		printf("Speaker voice characteristics recording beginning in 5 seconds.\n");
		Pa_Sleep(5000);
		printf("Read some sample text for the next %d seconds \n", secs_to_rec);
		pa_err = Pa_StartStream(stream); //Start Stream	
		if(pa_err != paNoError){
			printf("PortAudio Start Stream Error: %s\n", Pa_GetErrorText(pa_err));
			return -1;
		}
		while(buffers_recorded < buffers_to_rec){	//Continue recording until deisred buffers are recorded
		pa_err = Pa_ReadStream(stream, buffer, frames_per_buffer);	//Read (frames_per_buffer) samples from stream into buffer
		if (pa_err != 0){
			printf("PortAudio Read Stream Error: %s\n", Pa_GetErrorText(pa_err));
			break;
		}
		
		//Processing and MFCC
		sum_energy = compute_sound_energy(frames_per_buffer, buffer); 
		if (sum_energy > ENERGY_THRESHOLD){
			for (int k = 0; k < frames_per_buffer; k++){
				process_input->data[k] = buffer[k] / 32768.0f;
			}
			compute_mfcc(pv, process_input, fftgrain, mfcc, mfcc_out);	//compute mfccs
			accumulate_mfcc(&cm, mfcc_out); //Accumulate MFCCs for processing
		}
		buffers_recorded++;
	}
	if(cm.count > 0){
		average_mfcc(&cm);
	}
	for(int k = 0; k < N_COEFFS; k++){
		user.user_mfcc[k] = cm.mfcc_avg[k];
	}	
	system_mode = MODE_DETECT;
	buffers_recorded = 0;
	reset_cumulative_mfcc(&cm);
	// Save MFCCs
	FILE *f = fopen("user_mfcc.bin", "wb");
	if (f == NULL){
		printf("Cannot save MFCC\n");
	}
	else{
		fwrite(user.user_mfcc, sizeof(float), N_COEFFS, f);
		fclose(f);
	}
	
	printf("Training Complete \n");
}

	frames_until_update = UPDATE_SECONDS * (sample_rate / frames_per_buffer);
	while(1){	//Continue recording
		if (Pa_IsStreamStopped(stream)){
			pa_err = Pa_StartStream(stream);
			if (pa_err != 0){
				printf("PortAudio Start Stream Error: %s\n", Pa_GetErrorText(pa_err));
				break;
			}
		}			
		
		pa_err = Pa_ReadStream(stream, buffer, frames_per_buffer);	//Read (frames_per_buffer) samples from stream into buffer
		if (pa_err != 0){
			printf("PortAudio Read Stream Error: %s\n", Pa_GetErrorText(pa_err));
			break;
		}
		
		//fwrite(buffer, 2, frames_per_buffer, test_file_ptr);	//Write to .raw test_file. Writing "frames_per_buffer" samples into file, data size is 2 (2 bytes, small) 
		
		//Processing and MFCC
		
		voice_process(buffer, frames_per_buffer);
							
		buffers_recorded++;
	}
	
	// Clean up Aubio
	end_PA(stream);
	
	// End Port Audio
	free(buffer);
	return 0;
}
