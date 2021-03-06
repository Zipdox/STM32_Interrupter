#include <MIDI.h>

MIDI_CREATE_DEFAULT_INSTANCE();

// Constants
#define sampling_rate 96000
#define tuning_pitch 440
#define max_playing_notes 5
#define max_pitch 98
#define min_on_time 6
#define max_on_time 100
//#define min_duty_cycle 0.005
//#define max_duty_cycle 0.025

// Lookup tables for fast calculation
uint16_t period_lut[128];
uint32_t period_x10000_lut[128];
uint8_t velocity_lut[128] = {
    200, 194, 188, 183, 178, 173, 168, 164, 160, 156, 152, 149, 145, 142, 139, 136,
    133, 130, 128, 125, 123, 120, 118, 116, 114, 112, 110, 108, 106, 105, 103, 101,
    100, 98,  97,  95,  94,  92,  91,  90,  89,  87,  86,  85,  84,  83,  82,  81,
    80,  79,  78,  77,  76,  75,  74,  73,  72,  72,  71,  70,  69,  68,  68,  67,
    66,  66,  65,  64,  64,  63,  62,  62,  61,  61,  60,  59,  59,  58,  58,  57,
    57,  56,  56,  55,  55,  54,  54,  53,  53,  53,  52,  52,  51,  51,  50,  50,
    50,  49,  49,  49,  48,  48,  47,  47,  47,  46,  46,  46,  45,  45,  45,  44,
    44,  44,  44,  43,  43,  43,  42,  42,  42,  42,  41,  41,  41,  41,  40,  40
};
uint16_t pitchbend_lut[256];


int note_period(byte pitch){
    while(pitch > max_pitch){
        pitch -= 12;
    }
    return period_lut[pitch];
}

typedef struct {
    byte channel;
    byte pitch;
    byte velocity;
    int on_time;
    int off_time;
    int been_on;
} Note;

// Arrays for playing notes and channel pitchbends
Note playing_notes[max_playing_notes];
int channels_pitchbend[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

void handleNoteOn(byte channel, byte pitch, byte velocity){
    for(int i = 0; i < max_playing_notes; i++){
        if(playing_notes[i].on_time != 0) continue;
        if(channels_pitchbend[channel-1] == 0){
            uint16_t period = note_period(pitch);
            int on_time = period/velocity_lut[velocity];
            if(on_time < min_on_time) on_time = min_on_time;
            if(on_time > max_on_time) on_time = max_on_time;
            playing_notes[i] = {channel, pitch, velocity, on_time, period-on_time, 0};
        }else{
            uint32_t note_period_x10000 = period_x10000_lut[pitch];
            uint16_t period = note_period_x10000/pitchbend_lut[(channels_pitchbend[channel-1]+8192)/64];
            int on_time = period/velocity_lut[velocity];
            if(on_time < min_on_time) on_time = min_on_time;
            if(on_time > max_on_time) on_time = max_on_time;
            playing_notes[i] = {channel, pitch, velocity, on_time, period-on_time, 0};
        }
        break;
    }
}

void handleNoteOff(byte channel, byte pitch, byte velocity){
    for(int i = 0; i < max_playing_notes; i++){
        if(playing_notes[i].pitch == pitch) playing_notes[i].on_time = 0;
    }
}

void handlePitchBend(byte channel, int bend){
    for(int i = 0; i < max_playing_notes; i++){
        channels_pitchbend[channel-1] = bend;

        if(playing_notes[i].on_time == 0) continue;
        if(playing_notes[i].channel != channel) continue;
        
        uint32_t note_period_x10000 = period_x10000_lut[playing_notes[i].pitch];
        uint16_t period = note_period_x10000/pitchbend_lut[(bend+8192)/64];
        
        int on_time = period/velocity_lut[playing_notes[i].velocity];
        
        if(on_time < min_on_time) on_time = min_on_time;
        if(on_time > max_on_time) on_time = max_on_time;
        
        playing_notes[i].on_time = on_time;
        playing_notes[i].off_time = period-on_time;
    }
}

void sample(void){
    // Increment the ontime for all playing notes
    for(int i = 0; i < max_playing_notes; i++){
        if(playing_notes[i].on_time != 0) playing_notes[i].been_on++;
    }
}

void handleStop(){
    // Turn off all notes when we receive a MIDI stop message
    for(int i = 0; i < max_playing_notes; i++){
        playing_notes[i].on_time = 0;
    }
}

void setup(){
    pinMode(LED_BUILTIN, OUTPUT);
    // Initialize output pin
    pinMode(PB9, OUTPUT);
    digitalWrite(PB9, LOW);

    // Generate lookup table for note periods
    for(int i = 0; i < 128; i++){
        float period = sampling_rate/(tuning_pitch*pow(2, (i-69)/12.0));
        period_lut[i] = period;
        period_x10000_lut[i] = period*10000;
    }

    // Generate lookup table for pitchbend
    for(int i = -128; i < 128; i++){
        float multiplier = pow(2, i/(64*12.0));
        pitchbend_lut[i+128] = multiplier*10000;
    }

    // fill playing note array with blank notes
    for(int i = 0; i<(sizeof(playing_notes)/sizeof(playing_notes[0])); i++){
        playing_notes[i] = {0, 0, 0, 0, 0, 0};
    }

    // Set incoming MIDI note handler functions
    MIDI.setHandleNoteOn(handleNoteOn);
    MIDI.setHandleNoteOff(handleNoteOff);
    MIDI.setHandlePitchBend(handlePitchBend);
    MIDI.setHandleStop(handleStop);
    MIDI.begin(MIDI_CHANNEL_OMNI);

    // Create sampler timer
    HardwareTimer *MyTim = new HardwareTimer(TIM2);
    MyTim->setOverflow(sampling_rate, HERTZ_FORMAT);
    MyTim->attachInterrupt(sample);
    MyTim->resume();
    digitalWrite(LED_BUILTIN, LOW);
}

void loop(){
    // Read MIDI data if available
    MIDI.read();
    // Iterate through notes to check if they should be switching to high or to low and check if any are high
    byte play = 0x0;
    for(int i = 0; i < max_playing_notes; i++){
        if(playing_notes[i].on_time == 0) continue;
        if(playing_notes[i].been_on > playing_notes[i].on_time) playing_notes[i].been_on = -playing_notes[i].off_time;
        if(playing_notes[i].been_on >= 0) play = 0x1;
    }
    // Write the output high if any of the pins were high, low if not
    digitalWrite(PB9, play);
}
