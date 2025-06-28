#ifdef __APPLE__
#ifdef TTS

#undef MIDI  // Having MIDI defined conflicts with Apple stuff

#include <Foundation/Foundation.h>
#include <AVFoundation/AVFoundation.h>

#include "message.h"

//
// Allocate/init the speech synthesizer once and keep
// that single instance alive (as long as piHPSDR runs)
// such that one can terminate an on-going speech (from
// the previous invocation) if a new one is due.
//
static AVSpeechSynthesizer *synth = NULL;

void MacTTS(char *text) {
  //
  // Convert C string to a NSString and init an AVSpeechUtterance instance
  // with English language
  //
  NSString* str = [NSString stringWithUTF8String:text];

  //
  // Create the synthesizer instance upon the first call to MacTTS,
  // then keep it.
  //
  if (synth == NULL) {
   t_print("Creating the MacOS Speech Synthesizer Instance\n");
   synth = [[AVSpeechSynthesizer alloc] init];
  }

  AVSpeechUtterance *utter = [[AVSpeechUtterance alloc] initWithString:str];
  AVSpeechSynthesisVoice *voice = [AVSpeechSynthesisVoice voiceWithLanguage:@"en-GB"];
  [utter setVoice:voice];

  //
  // If the previous text is not yet completely spoken,
  // abort such that the new text does not have to wait
  //
  if ([synth isSpeaking]) {
    [synth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate ];
  }

  //
  // Put the text into the queue of the synthesizer
  // and return. The synthesizer will be busy with speaking
  // for some more time, we do not wait for the speech being
  // complete. Hopefully, utter will then be free'd once it
  // is no longer needed.
  //
  [synth speakUtterance:utter];
  [utter release];
}

#endif
#endif
