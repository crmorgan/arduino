#define trigger 2
#define echo 3
#define beep 9
#define beep_start 100
#define min_distance 5
#define c 0.0343

long tempo;
float space;

void setup() {
  pinMode(trigger, OUTPUT);
  pinMode(echo, INPUT);
  pinMode(beep, OUTPUT);

  Serial.begin(9600);
}

void loop() {
  digitalWrite(trigger, LOW);
  delayMicroseconds(5);

  // Send pule for 10 mico seconds
  digitalWrite(trigger, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigger, LOW);

  // Read echo and calculate distance in cm
  tempo = pulseIn(echo, HIGH) / 2;
  space = tempo * c;

  Serial.println("Distance = " + String(space, 1) + " cm");

  // Emit sound if disance in range
  if(space < beep_start) {
    Serial.println("Beep");
    tone(beep, 1000);
    delay(40);

    // Emit continous sound if inside min distance
    if(space > min_distance) {
      Serial.println("Tone");
      noTone(beep);
      delay(space * 4);
    }
  }
  delay(50);
}
