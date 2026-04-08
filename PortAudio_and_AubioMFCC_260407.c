// Includes
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "portaudio.h"
#include <aubio/aubio.h>

// Define variable
#define ENERGY_THRESHOLD 0.0001f
#define N_COEFFS 13
#define DETECTION_THRESHOLD 2.0f

// Function Defs
// Compute sound energy, needed to detect when sound is present

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
	int sample_rate = 48000;	//Default sample rate from device
	int frames_per_buffer = 1024;	//try 256 for now
	short buffer[frames_per_buffer];		//Buffer initialize, using short whule I have the mic input as int16
	inputDevice.channelCount = 1;	//Device only has once channel
	inputDevice.device = 1;	//
	inputDevice.hostApiSpecificStreamInfo = NULL;
	inputDevice.suggestedLatency = Pa_GetDeviceInfo(inputDevice.device)->defaultHighInputLatency;	//From device info, default low latency = 0.0080
	inputDevice.sampleFormat = paInt16; //Use int16 for now
	int user_detected = 0;
	
	// For MFCC - Voice Recognition
	uint_t buf_size = frames_per_buffer; //buffer size
	uint_t hop_s = frames_per_buffer/1; // block size
	uint_t sample_rate_for_mfcc = sample_rate; // samplerate
	uint_t n_filters = 40; // number of filters
	//Noete: workflow for MFCC input -> PVOC -> MFCC
	fvec_t *process_input = new_fvec(hop_s);       // phase vocoder input
	aubio_pvoc_t *pv = 0;	//phase vector
	pv = new_aubio_pvoc(buf_size, hop_s);
	cvec_t *fftgrain = new_cvec(buf_size); // pvoc output / mfcc input /fft
	aubio_mfcc_t *mfcc = 0;	//mfcc object
	fvec_t *mfcc_out = new_fvec(N_COEFFS);   // mfcc output
	mfcc = new_aubio_mfcc(buf_size, n_filters, N_COEFFS, sample_rate_for_mfcc); // init mfcc
	
	// PortAudio Implementation
	FILE* test_file_ptr; //Test file to port audio data into	
	test_file_ptr = fopen("test_file.raw", "wb");
	if(test_file_ptr == NULL){
		printf("File Open Error \n");
		return -1;
	}
	pa_err = Pa_OpenStream(&stream, &inputDevice, NULL, sample_rate, frames_per_buffer,paClipOff , NULL, NULL);	//Initialize stream
	if(pa_err != paNoError){
		printf("PortAudio Open Stream Error: %s\n", Pa_GetErrorText(pa_err));
		return -1;
	}
	
	// Normal vars for calcs
	int secs_to_rec = 30;	//Seconds to record before ending
	user_info user;
	int user_training_status = 0;
	int buffers_to_rec = secs_to_rec * sample_rate / frames_per_buffer;	//Number of buffers that will be recorded
	int buffers_recorded = 0;	// init buffers recorded to increment as recoring proceeds
	cumulative_mfcc cm;
	reset_cumulative_mfcc(&cm);
	int k = 0; //just another counter
	int was_talking = 0;
	float sum_energy = 0;
	
	// User detection
	float total_diff_to_user = 0;
	
	if(user_training_status != 1){
		printf("Speaker voice characteristics recording beginning in 5 seconds.\n");
		Pa_Sleep(5000);
		printf("Read some sample text for the next 30 seconds\n");
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
			for (k = 0; k < frames_per_buffer; k++){
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
	for(k = 0; k < N_COEFFS; k++){
		user.user_mfcc[k] = cm.mfcc_avg[k];
	}	
	user_training_status = 1;
	buffers_recorded = 0;
	reset_cumulative_mfcc(&cm);
	printf("Training Complete \n");
}
	
	while(1){	//Continue recording until deisred buffers are recorded
		pa_err = Pa_ReadStream(stream, buffer, frames_per_buffer);	//Read (frames_per_buffer) samples from stream into buffer
		if (pa_err != 0){
			printf("PortAudio Read Stream Error: %s\n", Pa_GetErrorText(pa_err));
			break;
		}
		
		//fwrite(buffer, 2, frames_per_buffer, test_file_ptr);	//Write to .raw test_file. Writing "frames_per_buffer" samples into file, data size is 2 (2 bytes, small) 
		
		//Processing and MFCC
		sum_energy = compute_sound_energy(frames_per_buffer, buffer); 
		if (sum_energy > ENERGY_THRESHOLD){
			for (k = 0; k < frames_per_buffer; k++){
				process_input->data[k] = buffer[k] / 32768.0f;
			}
			compute_mfcc(pv, process_input, fftgrain, mfcc, mfcc_out);	//compute mfccs
			accumulate_mfcc(&cm, mfcc_out);
			was_talking = 1;
		}
		else{
			if (was_talking && cm.count > 5){
				average_mfcc(&cm);
				total_diff_to_user = compute_mfcc_difference(user.user_mfcc, cm.mfcc_avg, N_COEFFS);
					
				if (total_diff_to_user < DETECTION_THRESHOLD){
					printf("User Detected!\n");
					printf("Diff to user: %f \n\n", total_diff_to_user);
					user_detected = 1;
				}
				else{
					printf("No Detection.\n");
					printf("Diff to user: %f \n\n", total_diff_to_user);
					user_detected = 0;
				}
				// reset in between audio detection
				was_talking = 0;
				reset_cumulative_mfcc(&cm);				
				}
			}					
		buffers_recorded++;
	}
	
	// Clean up Aubio
	del_aubio_pvoc(pv);
	del_aubio_mfcc(mfcc);
	del_fvec(process_input);
	del_fvec(mfcc_out);
	del_cvec(fftgrain);
	
	// End Port Audio
	fclose(test_file_ptr);	//Close file
	Pa_StopStream(stream);	//Stop stream
	Pa_CloseStream(stream);	//Close stream
	Pa_Terminate(); //End port audio

	return 0;
}
