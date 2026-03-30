// Includes
#include <stdio.h>
#include <math.h>
#include "portaudio.h"
#include <aubio/aubio.h>

int main(void){
	
	// For Audio processing
	Pa_Initialize();	//Init port audio	
	PaStream* stream; //Create Stream
	PaStreamParameters inputDevice = {0};
	int sample_rate = 48000;	//Default sample rate from device
	int frames_per_buffer = 256;	//try 256 for now
	short buffer[frames_per_buffer];		//Buffer initialize, using short whule I have the mic input as int16
	inputDevice.channelCount = 1;	//Device only has once channel
	inputDevice.device = 1;			//Device is on channel index 1
	inputDevice.hostApiSpecificStreamInfo = NULL;
	inputDevice.suggestedLatency = 0.0080;	//From device info, default low latency = 0.0080
	inputDevice.sampleFormat = paInt16; //Use int16 for now
	
	// For MFCC - Voice Recognition
	uint_t buf_size = frames_per_buffer; //buffer size
	uint_t hop_s = frames_per_buffer; // block size
	uint_t sample_rate_for_mfcc = sample_rate; // samplerate
	uint_t n_filters = 40; // number of filters
	uint_t n_coeffs = 13; // number of coefficients
	//Noete: workflow for MFCC input -> PVOC -> MFCC
	fvec_t *process_input = new_fvec(hop_s);       // phase vocoder input
	aubio_pvoc_t *pv = 0;	//phase vector
	pv = new_aubio_pvoc(buf_size, hop_s);
	cvec_t *fftgrain = new_cvec(buf_size); // pvoc output / mfcc input /fft
	aubio_mfcc_t *mfcc = 0;	//mfcc object
	fvec_t *mfcc_out = new_fvec(n_coeffs);   // mfcc output
	float float_buffer[frames_per_buffer];	//init buffer for MFCC
	mfcc = new_aubio_mfcc(buf_size, n_filters, n_coeffs, sample_rate_for_mfcc); // init mfcc
	
	
	// PortAudio Implementation
	FILE* test_file_ptr; //Test file to port audio data into
	
	Pa_OpenStream(&stream, &inputDevice, NULL, sample_rate, frames_per_buffer,paNoFlag , NULL, NULL);	//Initialize stream

	Pa_StartStream(stream); //Start Stream
	
	test_file_ptr = fopen("test_file.raw", "wb");
	
	int i = 0;	//Counter to be used later for buffer counting, Obsolete, replaced with buffers_recorded
	int secs_to_rec = 10;	//Seconds to record before ending
	int buffers_to_rec = secs_to_rec * sample_rate / frames_per_buffer;	//Number of buffers that will be recorded
	int buffers_recorded = 0;	// init buffers recorded to increment as recoring proceeds
	
	while(buffers_recorded < buffers_to_rec){	//Continue recording until deisred buffers are recorded
		Pa_ReadStream(stream, buffer, frames_per_buffer);	//Read (frames_per_buffer) samples from stream into buffer
		fwrite(buffer, 2, frames_per_buffer, test_file_ptr);	//Write to .raw test_file. Writing "frames_per_buffer" samples into file, data size is 2 (2 bytes, small) 
		
		//Processing and MFCC
		long long  sumergy = 0;
		for (i = 0; i < frames_per_buffer; i++){
			sumergy += (long long) buffer[i] * buffer[i];
		}
		sumergy /= frames_per_buffer;
		if (sumergy > 99999){
			//printf("Noise!\n")
			for (i = 0; i < frames_per_buffer; i++){
				process_input->data[i] = buffer[i] / 32768.0f;
			}
			aubio_pvoc_do(pv, process_input, fftgrain);
			aubio_mfcc_do(mfcc, fftgrain, mfcc_out);
			fvec_print(mfcc_out);
			printf("\n");
		}
		else{
		//printf("Silence.\n");
		}
		//printf("%lld\n", sumergy);
		
		// End processing and MFCC. Move all this to a function
		
		buffers_recorded++;	// Increment buffers
	}
	
	fclose(test_file_ptr);	//Close file
		
	Pa_StopStream(stream);	//Stop stream
	
	Pa_CloseStream(stream);	//Close stream
	
	Pa_Terminate(); //End port audio

	return 0;
}
