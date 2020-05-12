MIT License

Copyright (c) 2020 Simon Jansen
https://www.asciimation.co.nz

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

// Includes.
#include <FatReader.h>
#include <SdReader.h>
#include <avr/pgmspace.h>
#include <ServoTimer2.h>  
#include "WaveUtil.h"
#include "WaveHC.h"

// Defines.
#define MIN_PULSE  750        // Servo min pulse.
#define MAX_PULSE  2250       // Servo max pulse.
#define EYEMAXANGLE 140       // Eye servo max angle point.
#define EYEMINANGLE 22        // Eye servo min angle point.
#define EYEMIDANGLE 65        // Eye servo mid point.
#define MOUTHDELAY 5          // Controls how rapidly the mouth opens/closes.        
#define MOUTHTHRESHOLD 20     // Level of audio to trigger opening mouth.
#define MOUTHOPENMIN 50       // Minimum delay after mouth opens.
#define MOUTHOPENMAX 100      // Maximum delay after mouth opens.
#define SENSORTHRESHOLD 120   // Sensitivity for the scream sensor.
#define NUMBEROFCYCLES 5      // How many cycles of data should we look for before triggering.
#define SPEAKPERIOD 1500      // Period of the speak frequency.
#define EYESLEFTPERIOD 2000   // Period of the eyes left frequency.
#define EYESRIGHTPERIOD 3100  // Period of the eyes right frequency.
#define PERIODERROR 100       // How much error in the period can we tolerate.
#define EYEMOVE 5             // How much to move the eyes per increment.
#define SCREAMCHECKTIME 10    // Time in mS between checking the scream sensor.

SdReader card;     // This object holds the information for the card.
FatVolume vol;     // This holds the information for the partition on the card.
FatReader root;    // This holds the information for the filesystem on the card.
FatReader f;       // This holds the information for the file we're play.
dir_t dirBuf;      // Directory buffer.
WaveHC wave;       // This is the only wave (audio) object, since we will only play one at a time.
ServoTimer2 servo; // Eyes servo.

const int analogInSoundPin = 0;   // Analog input pin for audio to drive mouth.
const int analogInDetectPin = 1;  // Analog input pin for range detector to trigger scream.
const int digitalTriggerPin = 6;  // Digital input from receiver for the speech trigger.
const int mouthOutputPin = 7;     // Digital output pin for mouth solenoid.
const int servoPin = 8;           // Digital output for eyes servo.
const int button = 9;             // Digital output for button.

int sensorValue = 0;  // Value read from audio input.
int randNumber = 0;   // General random number variable.
int numFiles = 0;     // Number of wav fils on the SD card.
int eyePosition = 60; // Eye servo position.

// --------------------------------------------------------------------------------------
// Helper function to return free RAM available.
// --------------------------------------------------------------------------------------
int freeRam( void )
{
  extern int  __bss_end; 
  extern int  *__brkval; 
  int free_memory; 
  if( (int) __brkval == 0 ) 
  {
    free_memory = ( (int)&free_memory ) - ( (int)&__bss_end ); 
  }
  else 
  {
    free_memory = ( (int)&free_memory ) - ( (int)__brkval ); 
  }
  return free_memory; 
} 

// --------------------------------------------------------------------------------------
// SD card error handling.
// Just dump out the error and stop.
// --------------------------------------------------------------------------------------
void sdErrorCheck( void )
{
  if ( !card.errorCode() ) return;
  putstring( "\n\rSD I/O error: " );
  Serial.print( card.errorCode(), HEX );
  putstring( ", " );
  Serial.println( card.errorData(), HEX );
  while( 1 );
}

// --------------------------------------------------------------------------------------
// Map the servo angle.
// Map the given angle based on max and min pulse length.
// --------------------------------------------------------------------------------------
int servoAngle ( int angle )
{  
  int value = 0;
  return value = map( angle, 0, 180, MIN_PULSE , MAX_PULSE ); 
}

// --------------------------------------------------------------------------------------
// Should we scream or not?
// --------------------------------------------------------------------------------------
void CheckScream ( void )
{
  // Check the scream detector.
  sensorValue = analogRead( analogInDetectPin );  

  // Serial.print( "Detector: " ); 
  // Serial.println( sensorValue );  
  
  // Are we over the threshold to scream?
  if ( sensorValue > SENSORTHRESHOLD )
  {
    if ( wave.isplaying ) // Already playing something, so stop it.
    {
      wave.stop(); 
    }
    // Play the scream.
    playScream();
  }
}

// --------------------------------------------------------------------------------------
// Is the button pressed?
// --------------------------------------------------------------------------------------
bool IsButtonPressed (  )
{ 
  // Check the button state.
  return !digitalRead( button );
}

// --------------------------------------------------------------------------------------
// Plays a scream.
// --------------------------------------------------------------------------------------
void playScream() 
{
  // Play the file and open the mouth.
  playfile("scream.wav"); 
  openMouth(); 
  
  // While playing jiggle the eyes in an amusing/frightening manner.
  while ( wave.isplaying ) 
  {
      eyePosition = random( 30, 90 );
      servo.write(servoAngle(eyePosition));       
  }
  
  // Shut mouth.
  digitalWrite( mouthOutputPin, LOW ); 
  
  // Centre eyes.
  eyePosition = EYEMIDANGLE;
  servo.write(servoAngle(eyePosition));
  delay(100);
}
 
// --------------------------------------------------------------------------------------
// Should we trigger a play?
// --------------------------------------------------------------------------------------
unsigned long triggerPlay ()
{
  boolean bPlay = true;
  unsigned long time1;
  unsigned long time2;
  unsigned long initialPeriod;
  unsigned long period;
  unsigned long returnVal = 0;
   
  // Check for a signal from the receiver. 
  for ( int i = 0; i < NUMBEROFCYCLES; i++ )
  {  
    // Wait till the pin goes low. 
    while ( digitalRead( digitalTriggerPin ) != 0 ){} 
    // Get the time now. 
    time1 = micros();
    
    // Wait till the pin goes high again. 
    while ( digitalRead( digitalTriggerPin ) != 1 ){}
    
    // Wait till the pin goes low again. 
    while ( digitalRead( digitalTriggerPin ) != 0 ){} 
    // Get the time now. 
    time2 = micros();
    
    period = time2 - time1;
    if ( i == 0 )
    {
       initialPeriod = period; 
    }
    
    if ( (period < ( initialPeriod - PERIODERROR ) ) || (period > ( initialPeriod + PERIODERROR ) ) )
    {
      // The times we outside the bounds so break out of the for loop since we have no signal.
      returnVal = 0;
      break;
    }
    
    // If we haven't broken out then we much have the signal so return true.
    returnVal = initialPeriod;      
    
  } // End for.

  return returnVal;  
}

// --------------------------------------------------------------------------------------
// Plays a full file from beginning to end with no pause.
// --------------------------------------------------------------------------------------
void playcomplete( char *name ) 
{
  // Call our helper to find and play this name.
  putstring_nl( "Playing: " );
  Serial.println( name );
  
  // Play the file.
  playfile( name );
  
  // While playing animate!
  while ( wave.isplaying ) 
  {
    
    // Check the scream detector.
    CheckScream();
    
    // Read the analog in value.
    sensorValue = analogRead( analogInSoundPin );  
    
    //Serial.print( "Audio: " ); 
    //Serial.println( sensorValue );  
        
    // Check is we need to animate anything.
    if( sensorValue > MOUTHTHRESHOLD )
    {  
      // Open the mouth.
      openMouth();     
      
      // Leave mouth open for a bit. 
      randNumber = random( MOUTHOPENMIN, MOUTHOPENMAX );      
      delay( randNumber );   
      
      // Randomly move the eyes.
      randNumber = random( 30, 90 ); 
      if ( randNumber % 2 == 0 )
      {
        eyePosition = randNumber;
        servo.write(servoAngle(eyePosition));
      }        
    }
    else
    { 
      // Close the mouth again.      
      closeMouth();
      delay( 5 );                  
    }             
  } // End while.
       
  // Now its done playing close gob.
  digitalWrite( mouthOutputPin, LOW );      
  
  // Centre eyes.
  eyePosition = EYEMIDANGLE;
  servo.write(servoAngle(eyePosition));
  delay(100);  
}

// --------------------------------------------------------------------------------------
// Play the given wav file.
// --------------------------------------------------------------------------------------
void playfile( char *name ) 
{
  // See if the wave object is currently doing something.
  if ( wave.isplaying ) 
  {
    // Already playing something, so stop it.
    wave.stop(); 
  }
  
  // Look in the root directory and open the file.
  if ( !f.open( root, name ) ) 
  {
    putstring( "Couldn't open file " ); 
    Serial.print( name ); 
    return;
  }
  
  // Read the file and turn it into a wave object.
  if ( !wave.create( f ) ) 
  {
    putstring_nl( "Not a valid WAV" );
    return;
  }
  
  // Start playback.
  wave.play();

}

// --------------------------------------------------------------------------------------
// Sweep the eyes.
// --------------------------------------------------------------------------------------
void sweepEyes ()
{
  for ( int i = EYEMINANGLE; i <= EYEMAXANGLE; i = i + 10 )
  {    
    eyePosition = i;
    servo.write(servoAngle(eyePosition));
    delay(100);  
  }

  for ( int i = EYEMAXANGLE; i >= EYEMINANGLE; i = i - 10 )
  {
    eyePosition = i;
    servo.write(servoAngle(eyePosition));
    delay(100); 
  }  
  
  // Centre eyes.
  eyePosition = EYEMIDANGLE;
  servo.write(servoAngle(eyePosition));
  delay(100);  
}

// --------------------------------------------------------------------------------------
// Open the mouth.
// --------------------------------------------------------------------------------------
void openMouth ()
{  
  for ( int i = 1; i < MOUTHDELAY; i++ )
  {
    digitalWrite( mouthOutputPin, HIGH );
    delay( i );
    digitalWrite( mouthOutputPin, LOW );
    delay( MOUTHDELAY - i );    
  }
  
  digitalWrite( mouthOutputPin, HIGH );
}

// --------------------------------------------------------------------------------------
// Close the mouth.
// --------------------------------------------------------------------------------------
void closeMouth()
{  
  for ( int i = 1; i < MOUTHDELAY; i++ )
  {    
    digitalWrite( mouthOutputPin, LOW );
    delay( i );
    digitalWrite( mouthOutputPin, HIGH );  
    delay( MOUTHDELAY - i );   
  }
  
  digitalWrite( mouthOutputPin, LOW );
}

// --------------------------------------------------------------------------------------
// Main setup routine.
// --------------------------------------------------------------------------------------
void setup() 
{
  // Initialize serial communications at 9600 bps.
  Serial.begin( 9600 ); 

  putstring( "Free RAM: " );       // This can help with debugging, running out of RAM is bad
  Serial.println( freeRam() );      // if this is under 150 bytes it may spell trouble!

  // Set the output pins for the DAC control. This pins are defined in the library
  pinMode( 2, OUTPUT );
  pinMode( 3, OUTPUT );
  pinMode( 4, OUTPUT );
  pinMode( 5, OUTPUT );

  // Pin controlling mouth solenoid.
  pinMode( mouthOutputPin, OUTPUT );  
  
  // Pin controling the servo.
  servo.attach( servoPin ); 
  
  // Initialise the wave shield.
  //  if (!card.init(true)) { // Play with 4 MHz spi if 8MHz isn't working for you.
  if ( !card.init() )         // Play with 8 MHz spi (default faster!).  
  {
    putstring_nl( "Card init. failed!" );  
    sdErrorCheck();
    while( 1 );                           
  }
  
  // Enable optimize read - some cards may timeout. Disable if you're having problems.
  card.partialBlockRead( true );
 
  // Now we will look for a FAT partition.
  uint8_t part;
  for ( part = 0; part < 5; part++ ) // We have up to 5 slots to look in.
  {  
    if ( vol.init( card, part ) ) 
    break; // Found a slot so break.
  }
  if ( part == 5 ) // No slot found.
  { 
    putstring_nl( "No valid FAT partition!" );
    sdErrorCheck();      
    while( 1 );                           
  }
  
  // Lets tell the user about what we found.
  putstring( "Using partition " );
  Serial.print( part, DEC );
  putstring( ", type is FAT" );
  Serial.println( vol.fatType(), DEC );     // FAT16 or FAT32?
  
  // Try to open the root directory.
  if ( !root.openRoot( vol ) )
  {
    putstring_nl( "Can't open root dir!" ); 
    while( 1 );                            
  }
  
  root.ls();
  
  numFiles = 0;
  root.rewind();  
  while ( root.readDir( dirBuf ) > 0 ) 
  {    
    // Skip it if not a subdirectory and not a .WAV file
    if ( !DIR_IS_SUBDIR(dirBuf) && (strncmp_P((char *)&dirBuf.name[8], PSTR("WAV"), 3)) == 0 ) 
    {
      numFiles++;
    }
  }
    
  Serial.print( "Number of files: " );
  Serial.println( numFiles );

  // Whew! We got past the tough parts.
  putstring_nl( "Ready!" );
  
  // Test eyes and mouth.
  openMouth();
  sweepEyes();
  closeMouth();
  
  // Centre eyes.
  eyePosition = EYEMIDANGLE;
  servo.write(servoAngle(eyePosition));
  delay(100);
}

// --------------------------------------------------------------------------------------
// Main loop.
// --------------------------------------------------------------------------------------
void loop() 
{  
  
  //putstring_nl( "Main loop." );
  
  int randFile = random( 0, numFiles - 1 ); // Numfiles -1 to account for the scream wav file.
  //Serial.println( randFile );
  String fileNumber = String( randFile );
  String fileToPlay = String( fileNumber + ".wav" );
  //Serial.println( fileToPlay );
  char fileName[13];
  fileToPlay.toCharArray( fileName, 13 );
  unsigned long period = 0;
  unsigned long time = millis();
  
  // Look for a signal.
  while ( period == 0 )
  {
    
    // Check the scream sensor. We only do this every 100mS since it is a cycle intensive thing and
    // it messes up the rest of the loop!
    if ( millis() - time > SCREAMCHECKTIME )
    {
      CheckScream();  
      time = millis();
    }
  
    // Check the button next.
    if ( IsButtonPressed() )
    {
      // Play!    
      playcomplete( fileName ); 
      break;
    } 
    // Look for a remote signal.
    period = triggerPlay();
  }
  
  // Serial.print( "Period: " );
  // Serial.println( period );
  
  // Speak!
  if ( (period > (SPEAKPERIOD - PERIODERROR)) && (period < (SPEAKPERIOD + PERIODERROR)) ) 
  {
    // Play!    
    playcomplete( fileName );
  }
  
  // Eyes left.   
  else if ( (period > (EYESLEFTPERIOD - PERIODERROR)) && (period < (EYESLEFTPERIOD + PERIODERROR)) ) 
  {      
    eyePosition = eyePosition + EYEMOVE;
    if ( eyePosition > EYEMAXANGLE )
    {
      eyePosition = EYEMAXANGLE;
    }
    servo.write(servoAngle(eyePosition));
    delay(100);  
  }
  
  // Eyes right.
  else if ( (period > (EYESRIGHTPERIOD - PERIODERROR)) && (period < (EYESRIGHTPERIOD + PERIODERROR)) ) 
  {
    eyePosition = eyePosition - EYEMOVE;
    if ( eyePosition < EYEMINANGLE )
    {
      eyePosition = EYEMINANGLE;
    }
    servo.write(servoAngle(eyePosition));
    delay(100);  
  }
  
}
