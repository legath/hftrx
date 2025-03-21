/*
 * FT8 implementation given from https://github.com/kgoba/ft8_lib,
 * adapted to Storch by RA4ASN
 */

#include "hardware.h"
#include "formats.h"
#include "board.h"

#if WITHFT8

#include "ft8.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdbool.h>

#include "ft8/unpack.h"
#include "ft8/ldpc.h"
#include "ft8/decode.h"
#include "ft8/constants.h"
#include "ft8/encode.h"
#include "ft8/pack.h"
#include "ft8/crc.h"

#include "speex/kiss_fftr.h"

/// Configuration options for FT4/FT8 monitor
typedef struct
{
    float f_min;             ///< Lower frequency bound for analysis
    float f_max;             ///< Upper frequency bound for analysis
    int sample_rate;         ///< Sample rate in Hertz
    int time_osr;            ///< Number of time subdivisions
    int freq_osr;            ///< Number of frequency subdivisions
    ftx_protocol_t protocol; ///< Protocol: FT4 or FT8
} monitor_config_t;

/// FT4/FT8 monitor object that manages DSP processing of incoming audio data
/// and prepares a waterfall object
typedef struct
{
    float symbol_period; ///< FT4/FT8 symbol period in seconds
    int block_size;      ///< Number of samples per symbol (block)
    int subblock_size;   ///< Analysis shift size (number of samples)
    int nfft;            ///< FFT size
    float fft_norm;      ///< FFT normalization factor
    float* window;       ///< Window function for STFT analysis (nfft samples)
    float* last_frame;   ///< Current STFT analysis frame (nfft samples)
    waterfall_t wf;      ///< Waterfall object
    float max_mag;       ///< Maximum detected magnitude (debug stats)

    // KISS FFT housekeeping variables
    void* fft_work;        ///< Work area required by Kiss FFT
    kiss_fftr_cfg fft_cfg; ///< Kiss FFT housekeeping object
} monitor_t;

#if ! LINUX_SUBSYSTEM
	ft8_t ft8;
#endif /* ! LINUX_SUBSYSTEM */

static timestamp_t ts1, ts2;
uint32_t bufind1 = 0, bufind2 = 0;
uint8_t fill_ft8_buf1 = 0, fill_ft8_buf2 = 0, ft8_enable = 0;
const uint32_t bufsize = ft8_sample_rate * ft8_length;
uint8_t ft8_tx = 0;
uint32_t bufind = 0;
uint8_t ft8_mox_request = 0;
volatile uint8_t ft8_encode_req = 0;
static IRQLSPINLOCK_t ft8bufflock /* = IRQLSPINLOCK_INIT */;

static subscribefloat_t ft8_outregister;

const int kMin_score = 10; // Minimum sync score threshold for candidates
const int kMax_candidates = 120;
const int kLDPC_iterations = 20;
const int num_samples = ft8_length * ft8_sample_rate;

const int kMax_decoded_messages = 50;

const int kFreq_osr = 2;
const int kTime_osr = 2;

const float kFSK_dev = 6.25f; // tone deviation in Hz and symbol rate

static float hann_i(int i, int N)
{
    float x = sinf((float)M_PI * i / N);
    return x * x;
}

static float hamming_i(int i, int N)
{
    const float a0 = (float)25 / 46;
    const float a1 = 1 - a0;

    float x1 = cosf(2 * (float)M_PI * i / N);
    return a0 - a1 * x1;
}

static float blackman_i(int i, int N)
{
    const float alpha = 0.16f; // or 2860/18608
    const float a0 = (1 - alpha) / 2;
    const float a1 = 1.0f / 2;
    const float a2 = alpha / 2;

    float x1 = cosf(2 * (float)M_PI * i / N);
    float x2 = 2 * x1 * x1 - 1; // Use double angle formula

    return a0 - a1 * x1 + a2 * x2;
}

void waterfall_init(waterfall_t* me, int max_blocks, int num_bins, int time_osr, int freq_osr)
{
    size_t mag_size = max_blocks * time_osr * freq_osr * num_bins * sizeof(me->mag[0]);
    me->max_blocks = max_blocks;
    me->num_blocks = 0;
    me->num_bins = num_bins;
    me->time_osr = time_osr;
    me->freq_osr = freq_osr;
    me->block_stride = (time_osr * freq_osr * num_bins);
    me->mag = (uint8_t *) malloc(mag_size);
//    PRINTF("Waterfall size = %zu\n", mag_size);
}

void waterfall_free(waterfall_t* me)
{
    free(me->mag);
}

void monitor_init(monitor_t* me, const monitor_config_t* cfg)
{
    float slot_time = (cfg->protocol == PROTO_FT4) ? FT4_SLOT_TIME : FT8_SLOT_TIME;
    float symbol_period = (cfg->protocol == PROTO_FT4) ? FT4_SYMBOL_PERIOD : FT8_SYMBOL_PERIOD;
    // Compute DSP parameters that depend on the sample rate
    me->block_size = (int)(cfg->sample_rate * symbol_period); // samples corresponding to one FSK symbol
    me->subblock_size = me->block_size / cfg->time_osr;
    me->nfft = me->block_size * cfg->freq_osr;
    me->fft_norm = 2.0f / me->nfft;
    // const int len_window = 1.8f * me->block_size; // hand-picked and optimized

    me->window = (float *)malloc(me->nfft * sizeof(me->window[0]));
    for (int i = 0; i < me->nfft; ++i)
    {
        // window[i] = 1;
        me->window[i] = hann_i(i, me->nfft);
        // me->window[i] = blackman_i(i, me->nfft);
        // me->window[i] = hamming_i(i, me->nfft);
        // me->window[i] = (i < len_window) ? hann_i(i, len_window) : 0;
    }
    me->last_frame = (float *)malloc(me->nfft * sizeof(me->last_frame[0]));

    size_t fft_work_size;
    kiss_fftr_alloc(me->nfft, 0, 0, &fft_work_size);

//    PRINTF("Block size = %d\n", me->block_size);
//    PRINTF("Subblock size = %d\n", me->subblock_size);
//    PRINTF("N_FFT = %d\n", me->nfft);
//    PRINTF("FFT work area = %zu\n", fft_work_size);

    me->fft_work = malloc(fft_work_size);
    me->fft_cfg = kiss_fftr_alloc(me->nfft, 0, me->fft_work, &fft_work_size);

    const int max_blocks = (int)(slot_time / symbol_period);
    const int num_bins = (int)(cfg->sample_rate * symbol_period / 2);
    waterfall_init(&me->wf, max_blocks, num_bins, cfg->time_osr, cfg->freq_osr);
    me->wf.protocol = cfg->protocol;
    me->symbol_period = symbol_period;

    me->max_mag = -120.0f;
}

void monitor_free(monitor_t* me)
{
    waterfall_free(&me->wf);
    free(me->fft_work);
    free(me->last_frame);
    free(me->window);
}

// Compute FFT magnitudes (log wf) for a frame in the signal and update waterfall data
void monitor_process(monitor_t* me, const float* frame)
{
    // Check if we can still store more waterfall data
    if (me->wf.num_blocks >= me->wf.max_blocks)
        return;

    int offset = me->wf.num_blocks * me->wf.block_stride;
    int frame_pos = 0;

    // Loop over block subdivisions
    for (int time_sub = 0; time_sub < me->wf.time_osr; ++time_sub)
    {
        kiss_fft_scalar timedata[me->nfft];
        kiss_fft_cpx freqdata[me->nfft / 2 + 1];

        // Shift the new data into analysis frame
        for (int pos = 0; pos < me->nfft - me->subblock_size; ++pos)
        {
            me->last_frame[pos] = me->last_frame[pos + me->subblock_size];
        }
        for (int pos = me->nfft - me->subblock_size; pos < me->nfft; ++pos)
        {
            me->last_frame[pos] = frame[frame_pos];
            ++frame_pos;
        }

        // Compute windowed analysis frame
        for (int pos = 0; pos < me->nfft; ++pos)
        {
            timedata[pos] = me->fft_norm * me->window[pos] * me->last_frame[pos];
        }

        kiss_fftr(me->fft_cfg, timedata, freqdata);

        // Loop over two possible frequency bin offsets (for averaging)
        for (int freq_sub = 0; freq_sub < me->wf.freq_osr; ++freq_sub)
        {
            for (int bin = 0; bin < me->wf.num_bins; ++bin)
            {
                int src_bin = (bin * me->wf.freq_osr) + freq_sub;
                float mag2 = (freqdata[src_bin].i * freqdata[src_bin].i) + (freqdata[src_bin].r * freqdata[src_bin].r);
                float db = 10.0f * log10f(1E-12f + mag2);
                // Scale decibels to unsigned 8-bit range and clamp the value
                // Range 0-240 covers -120..0 dB in 0.5 dB steps
                int scaled = (int)(2 * db + 240);

                me->wf.mag[offset] = (scaled < 0) ? 0 : ((scaled > 255) ? 255 : scaled);
                ++offset;

                if (db > me->max_mag)
                    me->max_mag = db;
            }
        }
    }

    ++me->wf.num_blocks;
}

void monitor_reset(monitor_t* me)
{
    me->wf.num_blocks = 0;
    me->max_mag = 0;
}

void ft8_decode_buf(float * signal, timestamp_t ts)
{
	// Compute FFT over the whole signal and store it
    monitor_t mon;
    monitor_config_t mon_cfg = {
        .f_min = 100,
        .f_max = 3000,
        .sample_rate = ft8_sample_rate,
        .time_osr = kTime_osr,
        .freq_osr = kFreq_osr,
        .protocol = PROTO_FT8,
    };
    monitor_init(&mon, &mon_cfg);
	memset(ft8.rx_text, '\0', ft8_text_records * ft8_text_length);

	for (int frame_pos = 0; frame_pos + mon.block_size <= num_samples; frame_pos += mon.block_size)
	{
		// Process the waveform data frame by frame - you could have a live loop here with data from an audio device
		monitor_process(&mon, signal + frame_pos);
	}
//	PRINTF("Waterfall accumulated %d symbols\n", mon.wf.num_blocks);
//	PRINTF("Max magnitude: %.1f dB\n", mon.max_mag);

	// Find top candidates by Costas sync score and localize them in time and frequency
	candidate_t candidate_list[kMax_candidates];
	int num_candidates = ft8_find_sync(&mon.wf, kMax_candidates, candidate_list, kMin_score);

	// Hash table for decoded messages (to check for duplicates)
	int num_decoded = 0;
	message_t decoded[kMax_decoded_messages];
	message_t* decoded_hashtable[kMax_decoded_messages];

	// Initialize hash table pointers
	for (int i = 0; i < kMax_decoded_messages; ++i)
	{
		decoded_hashtable[i] = NULL;
	}

	// Go over candidates and attempt to decode messages
	for (int idx = 0; idx < num_candidates; ++idx)
	{
		const candidate_t* cand = &candidate_list[idx];
		if (cand->score < kMin_score)
			continue;

		float freq_hz = (cand->freq_offset + (float)cand->freq_sub / mon.wf.freq_osr) / mon.symbol_period;
		float time_sec = (cand->time_offset + (float)cand->time_sub / mon.wf.time_osr) * mon.symbol_period;

		message_t message;
		decode_status_t status;
		if (!ft8_decode(&mon.wf, cand, &message, kLDPC_iterations, &status))
		{
			// printf("000000 %3d %+4.2f %4.0f ~  ---\n", cand->score, time_sec, freq_hz);
			if (status.ldpc_errors > 0)
			{
//				PRINTF("LDPC decode: %d errors\n", status.ldpc_errors);
			}
			else if (status.crc_calculated != status.crc_extracted)
			{
//				PRINTF("CRC mismatch!\n");
			}
			else if (status.unpack_status != 0)
			{
//				PRINTF("Error while unpacking!\n");
			}
			continue;
		}

//		PRINTF("Checking hash table for %4.1fs / %4.1fHz [%d]...\n", time_sec, freq_hz, cand->score);
		int idx_hash = message.hash % kMax_decoded_messages;
		bool found_empty_slot = false;
		bool found_duplicate = false;
		do
		{
			if (decoded_hashtable[idx_hash] == NULL)
			{
//				PRINTF("Found an empty slot\n");
				found_empty_slot = true;
			}
			else if ((decoded_hashtable[idx_hash]->hash == message.hash) && (0 == strcmp(decoded_hashtable[idx_hash]->text, message.text)))
			{
//				PRINTF("Found a duplicate [%s]\n", message.text);
				found_duplicate = true;
			}
			else
			{
//				PRINTF("Hash table clash!\n");
				// Move on to check the next entry in hash table
				idx_hash = (idx_hash + 1) % kMax_decoded_messages;
			}
		} while (!found_empty_slot && !found_duplicate);

		if (found_empty_slot)
		{
			// Fill the empty hashtable slot
			memcpy(&decoded[idx_hash], &message, sizeof(message));
			decoded_hashtable[idx_hash] = &decoded[idx_hash];

			// Fake WSJT-X-like output for now
			int snr = cand->score > 160 ? 160 : cand->score;
			snr = (snr - 160) / 6;
//			PRINTF("%02d%02d%02d %4.0f %02d %s", ts.hour, ts.minute, ts.second, freq_hz, snr, message.text);

			local_snprintf_P(ft8.rx_text [num_decoded], ft8_text_length, "%02d%02d%02d %4.0f %02d %s",
					ts.hour, ts.minute, ts.second, freq_hz, snr, message.text);

			++num_decoded;
		}
	}
	PRINTF("decoded %d messages\n", num_decoded);
	ft8.decoded_messages = num_decoded;
	monitor_free(&mon);
#if ! LINUX_SUBSYSTEM
	xcz_ipi_sendmsg_c0(FT8_MSG_DECODE_DONE);
#endif /* ! LINUX_SUBSYSTEM */
}

// *** Encode *********************************************************************

#define FT8_SYMBOL_BT 2.0f ///< symbol smoothing filter bandwidth factor (BT)
#define FT4_SYMBOL_BT 1.0f ///< symbol smoothing filter bandwidth factor (BT)

#define GFSK_CONST_K 5.336446f ///< == pi * sqrt(2 / log(2))

void gfsk_pulse(int n_spsym, float symbol_bt, float* pulse)
{
    for (int i = 0; i < 3 * n_spsym; ++i)
    {
        float t = i / (float)n_spsym - 1.5f;
        float arg1 = GFSK_CONST_K * symbol_bt * (t + 0.5f);
        float arg2 = GFSK_CONST_K * symbol_bt * (t - 0.5f);
        pulse[i] = (erff(arg1) - erff(arg2)) / 2;
    }
}

void synth_gfsk(const uint8_t* symbols, int n_sym, float f0, float symbol_bt, float symbol_period, int signal_rate, float* signal)
{
    int n_spsym = (int)(0.5f + signal_rate * symbol_period); // Samples per symbol
    int n_wave = n_sym * n_spsym;                            // Number of output samples
    float hmod = 1.0f;

//    PRINTF("n_spsym = %d\n", n_spsym);
    // Compute the smoothed frequency waveform.
    // Length = (nsym+2)*n_spsym samples, first and last symbols extended
    float dphi_peak = 2 * M_PI * hmod / n_spsym;
    float dphi[n_wave + 2 * n_spsym];

    // Shift frequency up by f0
    for (int i = 0; i < n_wave + 2 * n_spsym; ++i)
    {
        dphi[i] = 2 * M_PI * f0 / signal_rate;
    }

    float pulse[3 * n_spsym];
    gfsk_pulse(n_spsym, symbol_bt, pulse);

    for (int i = 0; i < n_sym; ++i)
    {
        int ib = i * n_spsym;
        for (int j = 0; j < 3 * n_spsym; ++j)
        {
            dphi[j + ib] += dphi_peak * symbols[i] * pulse[j];
        }
    }

    // Add dummy symbols at beginning and end with tone values equal to 1st and last symbol, respectively
    for (int j = 0; j < 2 * n_spsym; ++j)
    {
        dphi[j] += dphi_peak * pulse[j + n_spsym] * symbols[0];
        dphi[j + n_sym * n_spsym] += dphi_peak * pulse[j] * symbols[n_sym - 1];
    }

    // Calculate and insert the audio waveform
    float phi = 0;
    for (int k = 0; k < n_wave; ++k)
    { // Don't include dummy symbols
        signal[k] = sinf(phi);
        phi = fmodf(phi + dphi[k + n_spsym], 2 * M_PI);
    }

    // Apply envelope shaping to the first and last symbols
    int n_ramp = n_spsym / 8;
    for (int i = 0; i < n_ramp; ++i)
    {
        float env = (1 - cosf(2 * M_PI * i / (2 * n_ramp))) / 2;
        signal[i] *= env;
        signal[n_wave - 1 - i] *= env;
    }
}

void ft8_encode_buf(float * signal, char * message, float frequency)
{
	uint8_t packed[FTX_LDPC_K_BYTES];
	int num_tones = FT8_NN;
	float symbol_period = FT8_SYMBOL_PERIOD;
	float symbol_bt = FT8_SYMBOL_BT;
	uint8_t tones[num_tones]; // Array of 79 tones (symbols)

	PRINTF("tx message: %s\n", message);
	pack77(message, packed);
	ft8_encode(packed, tones);
	synth_gfsk(tones, num_tones, frequency, symbol_bt, symbol_period, ft8_sample_rate, signal);
}

// ********************************************************************************

#if ! LINUX_SUBSYSTEM
void ft8_irqhandler_core0(void)
{
	uint8_t msg = ft8.int_core0;

	if (msg == FT8_MSG_START_FILL)
	{
		ft8_start_fill();
	}
	else if (msg == FT8_MSG_DECODE_DONE)
	{
		hamradio_gui_parse_ft8buf();
	}
	else if (msg == FT8_MSG_ENCODE_DONE)
	{
		PRINTF("transmit message...\n");
		ft8_tx = 1;
		ft8_mox_request = 1;
	}
}

void ft8_irqhandler_core1(void)
{
	uint8_t msg = ft8.int_core1;
//	PRINTF("ft8_irqhandler_core1 CPU%u\n", (unsigned) (__get_MPIDR() & 0x03));

	if (msg == FT8_MSG_DECODE_1) // start decode
	{
		board_rtc_cached_gettime(& ts1.hour, & ts1.minute, & ts1.second);
		ft8_decode_buf(ft8.rx_buf1, ts1);
	}
	else if (msg == FT8_MSG_DECODE_2) // start decode
	{
		board_rtc_cached_gettime(& ts2.hour, & ts2.minute, & ts2.second);
		ft8_decode_buf(ft8.rx_buf1, ts2);
	}
	else if (msg == FT8_MSG_ENCODE)  // transmit message
	{
		ft8_stop_fill();
		ft8_encode_req = 1;
		memset(ft8.tx_buf, 0, sizeof(float) * ft8_sample_rate * ft8_length);
		ft8_encode_buf(ft8.tx_buf, ft8.tx_text, ft8.tx_freq);
		xcz_ipi_sendmsg_c0(FT8_MSG_ENCODE_DONE);
	}
	else if (msg == FT8_MSG_ENABLE)	// enable ft8
	{
		ft8_enable = 1;
		PRINTF("core %d: ft8 enabled\n", (unsigned) (__get_MPIDR() & 0x03));
	}
	else if (msg == FT8_MSG_DISABLE)  // disable ft8
	{
		ft8_enable = 0;
		ft8_stop_fill();
		PRINTF("core %d: ft8 disabled\n", (unsigned) (__get_MPIDR() & 0x03));
	}
	else if (msg == FT8_MSG_TX_DONE)
	{
		ft8_encode_req = 0;
		xcz_ipi_sendmsg_c0(FT8_MSG_START_FILL);
	}
}

void ft8_walkthrough_core0(uint_fast8_t rtc_seconds)
{
	static uint_fast8_t old_s = 99;

	if (rtc_seconds == 0 || rtc_seconds == 15 || rtc_seconds == 30 || rtc_seconds == 45)
	{
		if ((ft8_enable && ! ft8_encode_req) && (old_s != rtc_seconds))
		{
			xcz_ipi_sendmsg_c0(FT8_MSG_START_FILL);
			old_s = rtc_seconds;
		}
	}

#if WITHTX
	if (ft8_mox_request)
	{
		ft8_mox_request = 0;
		hamradio_moxmode(1);
	}
#endif /* WITHTX */
}

void ft8_txfill(float * sample)
{
	const uint32_t bufsize = ft8_sample_rate * ft8_length;

	if (ft8_tx && hamradio_get_tx())
	{
		* sample = ft8.tx_buf [bufind];
		bufind ++;
		if (bufind >= bufsize || ft8.tx_buf [bufind] == 0)
		{
			ft8_tx = 0;
			bufind = 0;
			ft8_mox_request = 1;
			xcz_ipi_sendmsg_c1(FT8_MSG_TX_DONE);
		}
	}
}

static void ft8fill(void * ctx, FLOAT_t ch0, FLOAT_t ch1)
{
	if (fill_ft8_buf1 == 1)
	{
		IRQL_t oldIrql;
		IRQLSPIN_LOCK(& ft8bufflock, & oldIrql);
		ASSERT(bufind1 < bufsize);
		ft8.rx_buf1 [bufind1] = ch0;
		bufind1 ++;
		if (bufind1 >= bufsize)
		{
			fill_ft8_buf1 = 0;
			bufind1 = 0;
			xcz_ipi_sendmsg_c1(FT8_MSG_DECODE_1);
		}
		IRQLSPIN_UNLOCK(& ft8bufflock, oldIrql);
	}

	if (fill_ft8_buf2 == 1)
	{
		IRQL_t oldIrql;
		IRQLSPIN_LOCK(& ft8bufflock, & oldIrql);
		ASSERT(bufind2 < bufsize);
		ft8.rx_buf2 [bufind2] = ch0;
		bufind2 ++;
		if (bufind2 >= bufsize)
		{
			fill_ft8_buf2 = 0;
			bufind2 = 0;
			xcz_ipi_sendmsg_c1(FT8_MSG_DECODE_2);
		}
		IRQLSPIN_UNLOCK(& ft8bufflock, oldIrql);
	}
}

void ft8_start_fill(void)
{
	IRQL_t oldIrql;
	IRQLSPIN_LOCK(& ft8bufflock, & oldIrql);
	if (fill_ft8_buf1)
	{
		PRINTF("ft8: start fill 2\n");
		fill_ft8_buf2 = 1;
	}
	else
	{
		PRINTF("ft8: start fill 1\n");
		fill_ft8_buf1 = 1;
	}
	IRQLSPIN_UNLOCK(& ft8bufflock, oldIrql);
}

void ft8_stop_fill(void)
{
	IRQL_t oldIrql;
	IRQLSPIN_LOCK(& ft8bufflock, & oldIrql);

	fill_ft8_buf1 = 0;
	fill_ft8_buf1 = 0;
	bufind1 = 0;
	bufind2 = 0;

	IRQLSPIN_UNLOCK(& ft8bufflock, oldIrql);
}


void ft8_set_state(uint8_t v)
{
	ft8_enable = v != 0;
	xcz_ipi_sendmsg_c1(FT8_MSG_DISABLE + ft8_enable);
}

uint8_t ft8_get_state(void)
{
	return ft8_enable;
}

void ft8_do_encode(void)
{
	xcz_ipi_sendmsg_c1(FT8_MSG_ENCODE);
}

void ft8_initialize(void)
{
	IRQLSPINLOCK_INITIALIZE(& ft8bufflock, IRQL_REALTIME);

	arm_hardware_set_handler(ft8_interrupt_core0, ft8_irqhandler_core0, ARM_SYSTEM_PRIORITY, TARGETCPU_CPU0);
	arm_hardware_set_handler(ft8_interrupt_core1, ft8_irqhandler_core1, ARM_SYSTEM_PRIORITY, TARGETCPU_CPU1);

	ft8.int_core0 = 0;
	ft8.int_core1 = 0;

	subscribefloat(& speexoutfloat, & ft8_outregister, NULL, ft8fill);
}

#endif /* ! LINUX_SUBSYSTEM */
#endif /* WITHFT8 */
