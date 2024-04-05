/*
Overview:
For nasal birdle project, for glueing the inserter handle in, we need to apply a fixed ammount of glue.
When button pressed, it delivers a dose of glue. This dose is settable.
Modified version of tube chamfer code, which in turn was a modification of Davids linear motor code for flow setting.

Linear motor:
The arduino uses a hardware timer (timer 1) that counts upwards.
When the timer reaches the value stored in the OCR1A register it resets the timer to 0 and fires off an interrupt.
If the motor should be moving the interrupt toggles the output line and increments / decrements the position counter.
The position counter therefore counts twice as fast as the pulses sent to the motor driver.
*/

const byte permissionPin = 13;
const byte pulsePin = 11; // if you change this you need to change the ISR code manually
const byte directionPin = 10;
int val = 0; // reading of if tube is
int inpin = 8;
int tubein = 0;  // if the tube is in =1, if not in =0
unsigned long lastLoopTime = 0;
// the system has three modes: 
    // it can be stopped (0). It can go forwards (1). It can go backwards (-1).
volatile char mode = 0;

// position has units of counts
volatile long currentPosition = 0;
long targetPosition = 0;
// speed has units of counts per second
// the main loop in this program stops responding if the speed goes much above this.
const long hardwareTopSpeed = 600000;
// on startup use the maximum speed allowed
long commandTopSpeed = hardwareTopSpeed;
long speed = 0;
int min_speed = 200;  //needed to ensure that speed keeps getting updated. (if speed=1, would only update speed once per second, causing a potential 0.9s update delay)
float decelerationConstant = 40000;
// this has units of (counts per second) per time through the main loop (processor speed dependent) 
int accelerationConstant = 2;
const unsigned long clockSpeed = 16000000;
const unsigned long maxOCRA = 0xFFFF;
// futureOCR1A is only assigned to OCR1A during the ISR to avoid a race condition where OCR1A
// could jump below the counter so the counter would have to rise all the way to 0xFFFF
// this produced an a brief, audiable pause in the motor movement
unsigned long futureOCR1A = 0xFFFF;
// use a pre-scalar  of 1 and clear the timer counter on matching the OCR1A register
unsigned char futureTCCR1B = bit(WGM12) | bit(CS10);
// used for a custom version of delay as normal delay is problematic
long sleep = 0;
long start_time = 0;
int dispense_ammount = 1000;
int backlash = 500;


void setup() {
  Serial.begin(9600); //115200 or 9600
  pinMode(pulsePin, OUTPUT);   
  pinMode(directionPin, OUTPUT);
  pinMode(permissionPin, OUTPUT);
  pinMode(inpin, INPUT_PULLUP);
  // the register documentation can be found at: http://www.atmel.com/Images/doc8161.pdf
  // look on page 134 for the section that describes the registers
  // note that registers TCCR1A and TCCR1B both contain setting bits for the same timer
  // I use timer 1
  TCCR1A = 0;          
  // use a pre-scalar  of 1 and clear the timer counter on matching the OCR1A register
  TCCR1B = bit(WGM12) | bit(CS10);    
  // interrupt on match with register OCR1A 
  TIMSK1 = bit (OCIE1A);              
  pinMode(13, OUTPUT);
}  


void loop() { 
  digitalWrite(permissionPin, HIGH);
  val = digitalRead(inpin);
  //Serial.println(val);
  if (millis() < lastLoopTime + 2000){     // I want to make a delay at the end of loop, but "delay(1000)" kept failing. No delay between loop causes repeated chamfer.
    delay(1);
  }
  else if (tubein == 1){
    if (val == LOW){  // if the stick was in but is no longer in, remove the stick
      delay(100);
      if (val == LOW){
        tubein = 0;
      }
    }
  }
  else if (val == HIGH) {  // if the stick was out but now a stick is in
    tubein = 1;  // remember that a tube is in
    delay(200);             // waits for the tube to go fully in.
    if (val == HIGH) {           // check tube is still in, then run chamfer code.
      Serial.println("start cycle");
      chamferTube();
      Serial.println("finished cycle");
      lastLoopTime = millis();
      //Serial.println("program loop");
    }
  }
}


void chamferTube(){
  currentPosition = currentPosition - dispense_ammount;
  commandTopSpeed = 1000; targetPosition = backlash; // move 1000 = move 5mm.
  do{                                                    // move in
    updateSpeed();
  }while (mode != 0);
  digitalWrite(permissionPin, HIGH);
  delay(1);
  Serial.println(currentPosition);
  commandTopSpeed = 1000; targetPosition = 0;
  do{                                                        // move back
    updateSpeed();
  }while (mode != 0);
  Serial.println(currentPosition);
  digitalWrite(permissionPin, HIGH);
  //delay(2000);
  //Serial.println("end");
}


void updateSpeed() { 
  unsigned long potentialOCRA;
  long distanceToGo = targetPosition - currentPosition;
  // topSpeed should have the same sign as the direction I am going in
  // if needed I flip the topSpeed to have the appropriate sign
  if (distanceToGo>0 == commandTopSpeed<0){
    commandTopSpeed = -commandTopSpeed; 
  }
  // check if destination has been reached
  if (abs(distanceToGo) < 5){  // gives small window of locations to prevent hunting behaviour
    speed = 0;
  }
  //else if (targetPosition == 0){  // experiment to try and fix problem where system hunts after reach end (thus target position 0)
  //  speed = 0;
  //}
  // check if I an near enough to the destination to need to decelerate
  else if ((distanceToGo < 0) && (speed < -sqrt(-decelerationConstant * distanceToGo))){
    speed = -max(min_speed, sqrt(-decelerationConstant * distanceToGo)); 
  }
  else if ((distanceToGo > 0) && (speed > sqrt(decelerationConstant * distanceToGo))){
    speed = max(min_speed, sqrt(decelerationConstant * distanceToGo));
  }
  // check if I need to snap to the commandTopSpeed
  else if (abs(speed-commandTopSpeed)<accelerationConstant){
    speed = commandTopSpeed;
  }
  // check if I need to be accelerating forwards
  else if (speed<commandTopSpeed){
    speed += accelerationConstant;
    speed = max(speed, min_speed);
  }
  // check if I need to be accelerating backwards
  else if (speed>commandTopSpeed){
    speed -= accelerationConstant;
    speed = min(speed, min_speed);
  }
  // speed is now set
  if (speed > 0){
    digitalWrite(permissionPin, LOW);
    potentialOCRA = (clockSpeed/speed)-1;
    digitalWrite(directionPin, HIGH);
    mode = 1;
  }    
  else if (speed < 0){
    digitalWrite(permissionPin, LOW);
    potentialOCRA = (clockSpeed/(-speed))-1;
    digitalWrite(directionPin, LOW);
    mode = -1;
  }
  else{
    // speed is therefore 0
    mode = 0;
    // for the sake of removing ambiguity and history dependence I set the directionPin
    digitalWrite(directionPin, HIGH);
    digitalWrite(permissionPin, HIGH);
    // I also make the timer count quickly
    potentialOCRA = 100;
  }
  if (potentialOCRA > maxOCRA * 256){
    // use a pre-scalar  of 1024
    potentialOCRA /= 1024;
    // if potentialOCRA will not fit in 16 bits use maxOCRA instead
    futureOCR1A = (potentialOCRA < maxOCRA) ? potentialOCRA : maxOCRA;
    futureTCCR1B = bit(WGM12) | bit(CS12) | bit(CS10);
    Serial.println("This should never be executed.");
    Serial.println("PotentialOCRA should never get high enough");
    Serial.println(currentPosition);
  }
  else if(potentialOCRA > maxOCRA * 64){
    // use a pre-scaler  of 256  
    futureOCR1A = potentialOCRA/256;
    futureTCCR1B = bit(WGM12) | bit(CS12);
    //Serial.println("256");
    //Serial.println(currentPosition);
  }
  else if(potentialOCRA > maxOCRA * 8){
    // use a pre-scaler  of 64  
    futureOCR1A = potentialOCRA/64;
    futureTCCR1B = bit(WGM12) | bit(CS11) | bit(CS10);
    //Serial.println("64");
    //Serial.println(currentPosition);
  }
  else if(potentialOCRA > maxOCRA * 1){
    // use a pre-scalar  of 8
    futureOCR1A = potentialOCRA/8;
    futureTCCR1B = bit(WGM12) | bit(CS11);
    //Serial.println("8");
    //Serial.println(currentPosition);
  }
  else{
    // use a pre-scalar  of 1
    futureOCR1A = potentialOCRA;
    futureTCCR1B = bit(WGM12) | bit(CS10);
    //Serial.println("1");
    //Serial.println(currentPosition);
  }
  // the interrupts per second is = clockSpeed / (prescaler*(OCR1A + 1))
  // for an OCR1A value of 3 there will be 4 clock ticks per repeat. (012301230123)  
  // OCR1A must be truncated to fit within a 16 bit register.
  // This imposes a minimum speed of:
  //    16,000,000/1024/65536 = 0.2384 interrupts (counts) per second
  // The "speed" variable being an integer imposes a minimum speed of 1 count per second
}



void custom_delay(long sleep){
  // inbuilt function delay seems intermitent. this is a stupid atempt to work around that.
  //attempt 1: failded due to high variation (30,000 sometimes gave 11s sometimes 23s)
  //start_time = millis();
  //while (millis() < lastLoopTime + sleep){
  //    delay(1);
  //}

  while (sleep>0){
    sleep=sleep-1;
    delay(1);
  }
}


ISR(TIMER1_COMPA_vect){
  // this is the interrupt service routine
  if (mode){
    PORTB ^= 8;  // flips output bit 2  
    currentPosition += mode; // increment or decrement the motor position as appropriate
  }
  TCCR1B = futureTCCR1B;
  OCR1A = futureOCR1A;
}
