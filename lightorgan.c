#include <alsa/asoundlib.h>
#include <wiringPi.h>
#include <limits.h>
#include <unistd.h>
#include <math.h>
static snd_seq_t *seq_handle;
static int in_port;

////////////////////////////////////////////////////////////////////////////
//Example setup: There are 12 melody channels. Each index is mapped to 
// the corresponding Wiring Pi valued channel in the array below.
//
//////////////////////////////////////////////////////////////////

int pinMapping[] = {
0, //0
1, //1
2, //2
3, //3
4, //4
5, //5
6, //6
7, //7
8, //8
9, //9
10,//10
11 //11
};

#define TOTAL_PINS sizeof(pinMapping) / sizeof(int)
#define THRUPORTCLIENT 14
#define THRUPORTPORT 0

void midi_open(void)
{
    snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_INPUT, 0);

    snd_seq_set_client_name(seq_handle, "LightOrgan");
    in_port = snd_seq_create_simple_port(seq_handle, "listen:in",
                      SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
                      SND_SEQ_PORT_TYPE_APPLICATION);
 
    if( snd_seq_connect_from(seq_handle, in_port, THRUPORTCLIENT, THRUPORTPORT) == -1) {
       perror("Can't connect to thru port");
       exit(-1);
    } 

}


snd_seq_event_t *midi_read(void)
{
    snd_seq_event_t *ev = NULL;
    snd_seq_event_input(seq_handle, &ev);
    return ev;
}


//Currently playing note, by pin
int pinNotes[TOTAL_PINS];

//Currently playing channel, by pin
int pinChannels[TOTAL_PINS];

//Enabled channels
int playChannels[16];


void clearPinNotes() {
   int i;
   for(i=0; i< TOTAL_PINS; i++) {
      pinNotes[i] = -1;
   }
}

void myDigitalWrite(int pinIdx, int val) {
     val  ?  printf("%i (%i) ON\n", pinIdx, pinMapping[pinIdx])  : printf("%i (%i) OFF\n", pinIdx, pinMapping[pinIdx]);
     digitalWrite( pinMapping[pinIdx], val );
}


void clearPinChannels() {
   int i;
   for(i=0; i< TOTAL_PINS; i++) {
      pinChannels[i] = INT_MAX;
   }
}

void clearPinsState() {
  clearPinNotes();
  clearPinChannels();
}

void pinsOn() {
   int i;
   for(i=0; i< TOTAL_PINS; i++) { 
      myDigitalWrite(i, 1); 
   }
}

void pinsOff() {
   int i;
   for(i=0; i< TOTAL_PINS; i++) {
      myDigitalWrite(i, 1); 
   }
}


void setChannelInstrument(int channel, int instr) {
  printf("setting channel %i to instrument %i\n", channel, instr);
  playChannels[channel] = instr;  
}


int isPercussion(int instrVal) {
  return instrVal >= 8 && instrVal <= 15;
}

int isPercussionChannel(int channel) {
  int instr = playChannels[channel];
  return isPercussion(instr);
}


int isBase(int instrVal) {
  return instrVal >= 32 && instrVal <= 39;
}
int isSynth(int instrVal) {
  return instrVal >= 88 && instrVal <= 103;
}



int choosePinIdx(int note, int channel) {
   //Return the note modulated by the number of melody pins
   int val = note  % (TOTAL_PINS * 2);
   return val / 2;
}


void midi_process(snd_seq_event_t *ev)
{
    
    //If this event is a PGMCHANGE type, it's a request to map a channel to an instrument
    if( ev->type == SND_SEQ_EVENT_PGMCHANGE )  {
       //printf("PGMCHANGE: channel %2d, %5d, %5d\n", ev->data.control.channel, ev->data.control.param,  ev->data.control.value);

       //Clear pins state, this is probably the beginning of a new song
       clearPinsState();
       
       setChannelInstrument(ev->data.control.channel, ev->data.control.value);
    }

    //Note on/off event
    else if ( ((ev->type == SND_SEQ_EVENT_NOTEON)||(ev->type == SND_SEQ_EVENT_NOTEOFF)) ) {
        
  
        //choose the output pin based on the pitch of the note
        int pinIdx = choosePinIdx(ev->data.note.note, ev->data.note.channel);


        if(!isPercussionChannel(ev->data.note.channel) ) { 
           int isOn = 1;
           //Note velocity == 0 means the same thing as a NOTEOFF type event
           if( ev->data.note.velocity == 0 || ev->type == SND_SEQ_EVENT_NOTEOFF) {
              isOn = 0;
           }


           //If pin is set to be turned on
           if( isOn ) {
              //If pin is currently available to play a note, or if currently playing channel can be overriden due to higher priority channel
              if( pinNotes[pinIdx] == -1 || pinChannels[pinIdx] > ev->data.note.channel )  {
                      
                 if( (pinChannels[pinIdx] > ev->data.note.channel ) && pinNotes[pinIdx] != -1)  {
                    //printf("OVERRIDING CHANNEL %i for %i\n", pinChannels[pinIdx], ev->data.note.channel);
                 }
                 //Write to the pin, save the note to pinNotes
                 //printf("Pin %i - %s %i %i \n", pinIdx, isOn ? "on" : "off", ev->data.note.note, ev->data.note.channel);       
                 myDigitalWrite(pinIdx, 1); 
                 pinNotes[pinIdx] = ev->data.note.note;
                 pinChannels[pinIdx] =  ev->data.note.channel;
              }
           }
           
           //Pin is to be turned off
           else {
              //If this is the note that turned the pin on..
              if( pinNotes[pinIdx] == ev->data.note.note && pinChannels[pinIdx] == ev->data.note.channel ) {
                 //Write to the pin, indicate that pin is available
                 //printf("Pin %i - %s %i %i \n", pinIdx, isOn ? "on" : "off", ev->data.note.note, ev->data.note.channel);       
                 myDigitalWrite(pinIdx, 0); 
                 pinNotes[pinIdx] = -1;
                 pinChannels[pinIdx] = INT_MAX;
              }
           }
       }

    }
    
    else {
       printf("Unhandled event %2d\n", ev->type);
   
    }

    snd_seq_free_event(ev);
}


int main()
{

    //Setup wiringPi
    if( wiringPiSetup() == -1) {
      exit(1);
    }
   
    //Setup all the pins to use OUTPUT mode
    int i=0;
    for(i=0; i< TOTAL_PINS; i++) {
      pinMode( pinMapping[i], OUTPUT);
    }


    clearPinsState();

    //Open a midi port, connect to thru port also
    midi_open();

    //Process events forever
    while (1) {
       midi_process(midi_read());
    }

    return -1;
}
