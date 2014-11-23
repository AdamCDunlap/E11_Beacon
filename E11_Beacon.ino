// Writen by Adam Dunlap, 24 October 2014
// Some gold code code from Patrick McKeen

// This library is used for asyncronous gold code flashing, and must be downloaded
#include "TimerOne.h"

const int VERSION = 4;

// Declare what pins are what
const uint8_t bonusOrNormalPin = 2;
const uint8_t flashOrBumpPin = 3;
const uint8_t flashLEDPin = 4;
const uint8_t numSeedSwitchPins = 4;
const uint8_t seedSwitchPins[numSeedSwitchPins] = { 5, 6, 7, 8 }; // MSB to LSB
const uint8_t flashRecvPin = 17;
const uint8_t redLEDPin = 14;
const uint8_t whiteLEDPin = 15;
const uint8_t greenLEDPin = 16;
const uint8_t bumpPin = 19;

// Declare some other constants
// Define the feedbacks for the gold codes
// The first number is the length of the array
const uint8_t feedback1[] = { 5,2,3,4,5 };
const uint8_t feedback2[] = { 3,3,5 };
// The threshold for saying if two gold codes are the same
const uint8_t sameGCThresh = 22;
const unsigned long epsiloniumStabilizationTime = 30000; // 30 seconds
const unsigned long cooldownTime = 2000; // 2 seconds
const unsigned long debounceTime = 10; // 10ms
const int8_t startTeams[16] = { 0, 0, 0, 1, 0, -1, 1, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
const uint8_t unclaimedOffset = 10; // Offset of the seed for an unclaimed beacon
const bool testBoard = false; // Change to true to read all switches and flash LEDs

// Function prototypes
void flash_GC_async();
void readGC_async();
uint32_t goldCode(const uint8_t feedback1[], const uint8_t feedback2[], uint8_t seed2);
uint32_t shiftRegister(uint32_t seed, const uint8_t feedbackList[]);
uint32_t nextStep(uint32_t a, const uint8_t feedbackList[]) ;
int dotProduct(uint32_t gc1, uint32_t gc2);
int sameGC(uint32_t gc1, uint32_t gc2);

// Declare global variables about what kind of beacon this is
bool bonus; // Is this beacon a bonus beacon?
bool flasher; // Does this beacon receive flashes or bumps?
uint8_t seed; // seed for gold code
uint32_t unclaimedGC; // Gold code to flash at start. This is the given seed + 10
uint32_t normalGC; // Gold code to flash after it's been claimed at least once

// Declare global variables about the state of the beacon
int8_t owner; // 0 is unclaimed, -1 is green, 1 is white
unsigned long lastClaimTime;

// Variable which keeps track if we are in debug mode.
bool debug = false;

void setup() {
    Serial.begin(115200);
    Serial.println("Good morning");
    Serial.print("I was compiled at "__TIME__" on "__DATE__". Version # is ");
    Serial.println(VERSION);
    Serial.println("Enter anything into the terminal to go into debug mode");

    // Set the random seed
    randomSeed(  analogRead(A0)
               ^ analogRead(A1)
               ^ analogRead(A2)
               ^ analogRead(A3)
               ^ analogRead(A4)
               ^ analogRead(A5));

    // Declare pins and enable internal pullup resistors
    pinMode(bonusOrNormalPin, INPUT);
    digitalWrite(bonusOrNormalPin, HIGH); 
    pinMode(flashOrBumpPin, INPUT);
    digitalWrite(flashOrBumpPin, HIGH); 
    pinMode(flashLEDPin, OUTPUT);
    for(int i=0; i<numSeedSwitchPins; ++i) {
        pinMode(seedSwitchPins[i], INPUT);
        digitalWrite(seedSwitchPins[i], HIGH); 
    }
    pinMode(flashRecvPin, INPUT);
    pinMode(redLEDPin, OUTPUT);
    pinMode(whiteLEDPin, OUTPUT);
    pinMode(greenLEDPin, OUTPUT);
    pinMode(bumpPin, INPUT);
    digitalWrite(bumpPin, HIGH); 

    // Figure out what kind of beacon we are
    bonus = digitalRead(bonusOrNormalPin);
    flasher = !digitalRead(flashOrBumpPin);

    // Read the seed in binary
    seed = 0;
    for (int i=0; i<numSeedSwitchPins; ++i) {
        seed <<= 1;
        seed |= !digitalRead(seedSwitchPins[i]);
    }

    // Now calculate the gold codes
    normalGC = goldCode(feedback1, feedback2, seed);
    unclaimedGC = goldCode(feedback1, feedback2, seed+unclaimedOffset);

    Serial.print("Seed is ");
    Serial.println(seed);
    Serial.println("Gold codes:");
    Serial.print("normal:    ");
    Serial.println(normalGC, 2);
    Serial.print("Unclaimed: ");
    Serial.println(unclaimedGC, 2);

    // Figure out what team we should start on via lookup table
    owner = startTeams[seed];

    if (testBoard) {
        Serial.println("Reading input pins. Send any key to stop");
        while(Serial.read() == -1) {
            uint8_t digpins[7] = { bonusOrNormalPin, flashOrBumpPin, seedSwitchPins[0], seedSwitchPins[1], seedSwitchPins[2], seedSwitchPins[3], bumpPin };
            for (int i=0; i<7; ++i) {
                Serial.print(digitalRead(digpins[i]));
                Serial.print(" ");
            }
            Serial.print(analogRead(flashRecvPin));
            Serial.println();
            delay(100);
        }
        Serial.println("Now flashing one pin every second");
        const uint8_t numOutPins = 4;
        uint8_t outPins[numOutPins] = { flashLEDPin, redLEDPin, whiteLEDPin, greenLEDPin };
        for (int i=0; i<numOutPins; ++i) {
            digitalWrite(outPins[i], HIGH);
            delay(1000);
            digitalWrite(outPins[i], LOW);
            delay(500);
        }
        Serial.println("Done with tests. Halting.");
        while(1) ;
    }


    // Start flashing the gold code, asyncronously!
    Timer1.initialize(250);
    Timer1.attachInterrupt(flash_GC_async);
}

void loop() {
    // See if we should go into debug mode
    if (Serial.read() != -1) {
       debug = true;
       Serial.println("Debug mode enabled");
    }
    // Set the appropriate LED
    digitalWrite(greenLEDPin, owner < 0);
    digitalWrite(whiteLEDPin, owner > 0);
    digitalWrite(redLEDPin,   owner == 0);

    if (flasher) {
        readGC_async();
    } else {
        // Read button and update accordingly
        static bool wasPressed;
        static unsigned long lastChangeTime;
        unsigned long curTime = millis();
        bool isPressed = !digitalRead(bumpPin);

        // If we're just getting pressed and nothing has changed in a while,
        //  switch owner.
        if (isPressed && !wasPressed && 
            curTime - lastChangeTime > debounceTime &&
            curTime - lastClaimTime > cooldownTime) {
            lastClaimTime = curTime;
            // If we were previously unassigned, go to a random team
            if (owner == 0) {
                owner = random(2)*2-1;
            }
            // Otherwise, switch teams
            owner = -owner;
        }

        if (isPressed != wasPressed) lastChangeTime = curTime;
        wasPressed = isPressed;
    }

    // If we're the bonus beacon, if we're held continuously by the same team
    //  for 30 seconds, revert back to being unclaimed
    if (bonus && owner != 0 &&
          millis() - lastClaimTime > epsiloniumStabilizationTime) {
        owner = 0;
    }
}


/***********************************************************************
 *****            Asyncronous Gold Code Flashing/Reading           *****
 ***********************************************************************/

// Must be called precisely every 250 microseconds
void flash_GC_async() {
    //unsigned long curTime = micros();
    //if (curTime - flashPos.lastTime < 250) return false;
    //flashPos.lastTime = curTime;
    static uint8_t pos = 0;
    uint32_t gcToFlash = owner == 0? unclaimedGC : normalGC;
    digitalWrite(flashLEDPin, (owner<0) ^ !!(gcToFlash & (1UL << (30 - pos))));

    ++pos;
    if (pos >= 31) {
        pos = 0;
    }
}

void readGC_async() {
    static unsigned long lastReadTime = 0;
    static unsigned int vals[31];
    static unsigned long times[31];
    static int pos = 0;

    unsigned long curTime = micros();
    
    if (curTime - lastReadTime < 250) {
        return;
    }
    if (curTime - lastReadTime > 1000) lastReadTime = curTime;
    else lastReadTime += 250;

    vals[pos] = analogRead(flashRecvPin);
    times[pos] = curTime;
    pos++;
    
    if (pos >= 31) {
        pos = 0;
        
        unsigned int sum = 0;
        for (int i=0; i<31; i++) {
            sum += vals[i];
        }
        unsigned int avg = sum/31;

        uint32_t gc = 0;
        
        for (int i=0; i < 31; i++) {
            // For each reading, convert it to binary by comparing to the average, then 
            //  OR it into the GC backwards, with the first element of the array
            //  going in the 30th position, etc
            gc |= (uint32_t)(vals[30-i] < avg) << i;
        }

        // Check the gold code against the "normal" gold code and change the
        //  owner if there's a strong correlation
        int same = sameGC(gc, normalGC);
        if (debug){
          unsigned int maxVal = 0, minVal = 1023;
          unsigned int closeToAvg = 0;
          for (int i = 0; i<31; i++) {
            if (vals[i] < minVal) minVal = vals[i];
            if (vals[i] > maxVal) maxVal = vals[i];
          }
          for (int i = 0; i<31; i++) 
            if (abs(vals[i]-avg) < (maxVal-minVal)/10) closeToAvg++;
            
          Serial.println("gold code recieved: ");
          Serial.println(gc,BIN);
          Serial.println("Correlation");
          Serial.println(same); 
          Serial.print("Time per sample ");
          Serial.println((times[30]-times[0])/30);
          Serial.print("Min, Max, Avg, close to avg: ");
          Serial.print(minVal); Serial.print(" ");
          Serial.print(maxVal); Serial.print(" ");
          Serial.print(avg); Serial.print(" ");
          Serial.println(closeToAvg); 
          Serial.print("raw data: ");
          for(int i = 0; i < 31; i++){
            Serial.print(vals[i]);  Serial.print(" ");
 //           Serial.println(times[i]);
          }
          Serial.println();
          Serial.println();
          
            
        }
        if (same > 0 && owner != 1) {
            owner = 1;
            lastClaimTime = millis();
        } else if (same < 0 && owner != -1) {
            owner = -1;
            lastClaimTime = millis();
        }
    }
}

/***********************************************************************
 *****                    Gold Code Utilities                      *****
 ***********************************************************************/


// Returns the numerical value of the gold code
uint32_t goldCode(const uint8_t feedback1[], const uint8_t feedback2[], uint8_t seed2)
{
    uint32_t reg1=shiftRegister(1, feedback1);
    uint32_t reg2=shiftRegister(seed2, feedback2);
    return reg1^reg2; // XORs the 2 outputs of the LFSRs together
}

// Finds the shift register for the LFSR
uint32_t shiftRegister(uint32_t seed, const uint8_t feedbackList[])
{
    uint32_t reg = 0;
    uint32_t b = seed;
    for (int i=0; (b-seed) != 0 || i == 0 ; i++) {
        // Takes the rightmost digit of each iteration and adds it to the binary string
        reg |= (b&1L) << (30-i);
        b = nextStep(b, feedbackList); //iterates to the next step
    }
    return reg;
}

// Finds the next value output of the LFSR
uint32_t nextStep(uint32_t a, const uint8_t feedbackList[]) 
{
    uint32_t r= a >> 1;//shifts one place to the right
    bool addOne = false;
    for (int i=1; i<feedbackList[0];i++)
    {
        // Tests if the feedback terminals are outputting 1s
        bool n = ((1L << (5 - feedbackList[i])) & a) != 0;
        addOne^=n; //XORs the outputs of the feedback terminals together
    }
    return (addOne << 4 ) | r; //generates next iteration
}


int dotProduct(uint32_t gc1, uint32_t gc2) {
    // This code works, but is slow

    //int score = 0;
    //for (int i=0; i<31; i++) {
    //    // If the ith bit is the same in each one, add 1
    //    // gc1 & (1UL << i) is the ith bit (although still kept in the same position)
    //    if ((gc1 & (1UL << i)) == (gc2 & (1UL << i))) score++;
    //    else                                          score--;
    //}
    //return score;

    // This code is much faster and also works

    // xor one GC with the NOT of the other, then make the top bit 0
    uint32_t v = (gc1 ^ ~gc2) & ~0x80000000;

    // now count the bits in v, with code
    // From http://graphics.stanford.edu/~seander/bithacks.html

    // count bits set in this (32-bit value)
    uint32_t c; // store the total here
    
    c = v - ((v >> 1) & 0x55555555);
    c = ((c >> 2) & 0x33333333) + (c & 0x33333333);
    c = ((c >> 4) + c) & 0x0F0F0F0F;
    c = ((c >> 8) + c) & 0x00FF00FF;
    c = ((c >> 16) + c) & 0x0000FFFF;
    return c*2 - 31;
}

// Returns a positive number if they're the same, a negative number if they're
//  inverted, or 0 if they're not the same.
// The number returned is the dot product of the gold codes
int sameGC(uint32_t gc1, uint32_t gc2) {

    int score = dotProduct(gc1, gc2);
    if (abs(score) >= sameGCThresh) return score;
    for (int i=0; i<31; i++) {
        gc2 = ((gc2 & 1) << 30) | gc2 >> 1;
        score = dotProduct(gc1, gc2);
        if (abs(score) >= sameGCThresh) {
            if (debug) {
                Serial.println("gold code ACTUAL  : ");
                Serial.println(gc2,BIN);
            }
            return score;
        }
    }
    // If none of them have a good enough match, return 0
    return 0;
}


