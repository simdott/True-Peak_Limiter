#include <ladspa.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLUGIN_ID 15555
#define PLUGIN_NAME "True-Peak Limiter"
#define PLUGIN_LABEL "True-Peak Limiter"
#define PLUGIN_MAKER "Simon Delaruotte"
#define PLUGIN_URI "https://github.com/simdott/true-peak_limiter"
#define MAX_DELAY 8192

// ISP interpolation factor
#define ISP_INTERP 4

typedef struct {
    // Audio ports
    LADSPA_Data *input_left;
    LADSPA_Data *input_right;
    LADSPA_Data *output_left;
    LADSPA_Data *output_right;
    
    // Control ports
    LADSPA_Data *gain_in;
    LADSPA_Data *threshold;
    LADSPA_Data *release;
    LADSPA_Data *latency_out;

    unsigned long sample_rate;
    unsigned long lookahead_samples;
    unsigned long isp_latency_samples;
    unsigned long latency_samples;
    unsigned long buffer_size;
    
    // Left channel delay
    unsigned long write_index_l;
    unsigned long read_index_l;
    float *delay_buffer_l;
    
    // Right channel delay
    unsigned long write_index_r;
    unsigned long read_index_r;
    float *delay_buffer_r;

    // Shared gain state (linked stereo)
    float current_gain;
    float release_coeff;
    float threshold_linear;
    float gain_linear;
    
    int initialized;
} TruePeakLimiter;

static void instantiate(LADSPA_Handle handle, unsigned long sample_rate) {
    TruePeakLimiter *p = (TruePeakLimiter *)handle;
    if (!p) return;
    
    p->sample_rate = sample_rate;
    if (p->sample_rate == 0) p->sample_rate = 48000;
    
    p->lookahead_samples = (unsigned long)(64.0 * (double)sample_rate / 48000.0 + 0.5);
    if (p->lookahead_samples < 1) p->lookahead_samples = 1;
    if (p->lookahead_samples > MAX_DELAY) p->lookahead_samples = MAX_DELAY;
    
    p->isp_latency_samples = 6;
    p->latency_samples = p->lookahead_samples + p->isp_latency_samples;
    p->buffer_size = p->latency_samples + 1024;
    
    p->delay_buffer_l = calloc(p->buffer_size, sizeof(float));
    p->delay_buffer_r = calloc(p->buffer_size, sizeof(float));
    if (!p->delay_buffer_l || !p->delay_buffer_r) {
        p->initialized = 0;
        return;
    }
    
    p->current_gain = 1.0f;
    p->release_coeff = 0.0f;
    p->threshold_linear = 1.0f;
    p->gain_linear = 1.0f;
    
    p->write_index_l = p->latency_samples;
    p->read_index_l = 0;
    p->write_index_r = p->latency_samples;
    p->read_index_r = 0;
    
    p->input_left = NULL;
    p->input_right = NULL;
    p->output_left = NULL;
    p->output_right = NULL;
    p->gain_in = NULL;
    p->threshold = NULL;
    p->release = NULL;
    p->latency_out = NULL;
    p->initialized = 1;
}

static void connect_port(LADSPA_Handle handle, unsigned long port, LADSPA_Data *location) {
    TruePeakLimiter *p = (TruePeakLimiter *)handle;
    if (!p) return;
    
    switch (port) {
        case 0: p->input_left = location; break;
        case 1: p->input_right = location; break;
        case 2: p->output_left = location; break;
        case 3: p->output_right = location; break;
        case 4: p->gain_in = location; break;
        case 5: p->threshold = location; break;
        case 6: p->release = location; break;
        case 7: p->latency_out = location; break;
    }
}

static void activate(LADSPA_Handle handle) {
    TruePeakLimiter *p = (TruePeakLimiter *)handle;
    if (!p || !p->initialized) return;
    if (!p->delay_buffer_l || !p->delay_buffer_r) return;
    
    memset(p->delay_buffer_l, 0, p->buffer_size * sizeof(float));
    memset(p->delay_buffer_r, 0, p->buffer_size * sizeof(float));
    p->current_gain = 1.0f;
    p->write_index_l = p->latency_samples;
    p->read_index_l = 0;
    p->write_index_r = p->latency_samples;
    p->read_index_r = 0;
}

static inline float read_delay(float *buf, unsigned long *index, unsigned long size) {
    float sample = buf[*index];
    (*index)++;
    if (*index >= size) *index = 0;
    return sample;
}

static inline void write_delay(float *buf, unsigned long *index, unsigned long size, float sample) {
    buf[*index] = sample;
    (*index)++;
    if (*index >= size) *index = 0;
}

static float detect_isp_peak(float x0, float x1, float x2, float x3) {
    float max_val = fabsf(x1);
    if (fabsf(x2) > max_val) max_val = fabsf(x2);
    
    for (int i = 1; i < ISP_INTERP; i++) {
        float t = (float)i / ISP_INTERP;
        float t2 = t * t;
        float t3 = t2 * t;
        
        float a0 = -0.5f * x0 + 1.5f * x1 - 1.5f * x2 + 0.5f * x3;
        float a1 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
        float a2 = -0.5f * x0 + 0.5f * x2;
        float a3 = x1;
        
        float interp = a0 * t3 + a1 * t2 + a2 * t + a3;
        float abs_interp = fabsf(interp);
        if (abs_interp > max_val) max_val = abs_interp;
    }
    
    return max_val;
}

static float get_isp_peak(float *buf, unsigned long read_pos, unsigned long size) {
    unsigned long pos0 = (read_pos - 2 + size) % size;
    unsigned long pos1 = (read_pos - 1 + size) % size;
    unsigned long pos2 = read_pos;
    unsigned long pos3 = (read_pos + 1) % size;
    
    float x0 = buf[pos0];
    float x1 = buf[pos1];
    float x2 = buf[pos2];
    float x3 = buf[pos3];
    
    return detect_isp_peak(x0, x1, x2, x3);
}

static void run(LADSPA_Handle handle, unsigned long sample_count) {
    TruePeakLimiter *p = (TruePeakLimiter *)handle;
    
    if (!p || !p->initialized) return;
    if (sample_count == 0) return;
    if (!p->input_left || !p->input_right || !p->output_left || !p->output_right) return;
    if (!p->gain_in || !p->threshold || !p->release) return;
    if (!p->delay_buffer_l || !p->delay_buffer_r) return;
    
    if (p->latency_out) {
        *p->latency_out = (LADSPA_Data)p->latency_samples;
    }
    
    float *in_l = p->input_left;
    float *in_r = p->input_right;
    float *out_l = p->output_left;
    float *out_r = p->output_right;
    
    float gain_db = *p->gain_in;
    float threshold_db = *p->threshold;
    float release_ms = *p->release;
    
    if (release_ms < 10.0f) release_ms = 10.0f;
    if (release_ms > 200.0f) release_ms = 200.0f;
    if (p->sample_rate == 0) return;

    p->gain_linear = powf(10.0f, gain_db / 20.0f);
    p->threshold_linear = powf(10.0f, threshold_db / 20.0f);
    p->release_coeff = expf(-5000.0f / (p->sample_rate * release_ms));

    float current_gain = p->current_gain;
    float threshold = p->threshold_linear;
    float release_coeff = p->release_coeff;
    float gain_linear = p->gain_linear;
    unsigned long size = p->buffer_size;
    unsigned long read_pos_l;
    unsigned long read_pos_r;

    for (unsigned long i = 0; i < sample_count; i++) {
        float sample_l = in_l[i] * gain_linear;
        float sample_r = in_r[i] * gain_linear;
        
        write_delay(p->delay_buffer_l, &p->write_index_l, size, sample_l);
        write_delay(p->delay_buffer_r, &p->write_index_r, size, sample_r);
        
        float delayed_l = read_delay(p->delay_buffer_l, &p->read_index_l, size);
        float delayed_r = read_delay(p->delay_buffer_r, &p->read_index_r, size);
        
        read_pos_l = p->read_index_l;
        read_pos_r = p->read_index_r;
        
        float peak_l = get_isp_peak(p->delay_buffer_l, read_pos_l, size);
        float peak_r = get_isp_peak(p->delay_buffer_r, read_pos_r, size);
        
        float peak = peak_l > peak_r ? peak_l : peak_r;
        
        float target_gain = 1.0f;
        if (peak > threshold && peak > 0.0f) {
            target_gain = threshold / peak;
            if (target_gain > 1.0f) target_gain = 1.0f;
        }

        if (target_gain < current_gain) {
            current_gain = target_gain;
        } else {
            if (peak <= threshold) {
                current_gain = 1.0f - (1.0f - current_gain) * release_coeff;
                if (current_gain > 1.0f) current_gain = 1.0f;
                if (current_gain < 0.0f) current_gain = 0.0f;
            }
        }

        out_l[i] = delayed_l * current_gain;
        out_r[i] = delayed_r * current_gain;
    }

    p->current_gain = current_gain;
}

static LADSPA_Handle instantiate_handle(const LADSPA_Descriptor *desc, unsigned long sample_rate) {
    TruePeakLimiter *p = calloc(1, sizeof(TruePeakLimiter));
    if (!p) return NULL;
    instantiate(p, sample_rate);
    return (LADSPA_Handle)p;
}

static void cleanup_handle(LADSPA_Handle handle) {
    TruePeakLimiter *p = (TruePeakLimiter *)handle;
    if (p) {
        if (p->delay_buffer_l) free(p->delay_buffer_l);
        if (p->delay_buffer_r) free(p->delay_buffer_r);
        free(p);
    }
}

static const LADSPA_PortDescriptor ports[] = {
    LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
    LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
    LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
    LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
    LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL,
    LADSPA_PORT_OUTPUT | LADSPA_PORT_CONTROL
};

static const char *port_names[] = {
    "Input Left",
    "Input Right",
    "Output Left",
    "Output Right",
    "Input Gain (dB)",
    "Threshold (dBTP)",
    "Release (ms)",
    "latency"
};

static const LADSPA_PortRangeHint hints[] = {
    {0, 0.0f, 0.0f},
    {0, 0.0f, 0.0f},
    {0, 0.0f, 0.0f},
    {0, 0.0f, 0.0f},
    {LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_0,
     0.0f, 18.0f},
    {LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_0,
     -12.0f, 0.0f},
    {LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_100,
     10.0f, 200.0f},
    {LADSPA_HINT_INTEGER | LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE,
     0.0f, 8192.0f}
};

static const LADSPA_Descriptor descriptor = {
    .UniqueID = PLUGIN_ID,
    .Label = PLUGIN_LABEL,
    .Properties = LADSPA_PROPERTY_HARD_RT_CAPABLE,
    .Name = PLUGIN_NAME,
    .Maker = PLUGIN_MAKER,
    .Copyright = "Public Domain",
    .PortCount = 8,
    .PortDescriptors = ports,
    .PortNames = port_names,
    .PortRangeHints = hints,
    .instantiate = instantiate_handle,
    .connect_port = connect_port,
    .activate = activate,
    .run = run,
    .cleanup = cleanup_handle
};

const LADSPA_Descriptor *ladspa_descriptor(unsigned long index) {
    if (index == 0) return &descriptor;
    return NULL;
}

#ifdef __cplusplus
}
#endif
