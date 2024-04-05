int futureOCR1A = 100;

void setup() {
  Serial.begin(9600);
  TCCR1A = 0;
  TCCR1B = bit(WGM12) | bit(CS10);
  TIMSK1 = bit (OCIE1A);
  Serial.println("start");
  Serial.println("start2");
  futureOCR1A = 10;  // MAGIC LINE, IF COMMENTED OUT IT WORKS, OTHERWISE PREVIOUS LINE FAILS
  Serial.println("finish");
}  

void loop() { 
  delay(100);
}

ISR(TIMER1_COMPA_vect){
  OCR1A = futureOCR1A;
}