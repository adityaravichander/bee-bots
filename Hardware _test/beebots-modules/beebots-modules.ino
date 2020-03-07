#include <PID_v1.h>
#include <Arduino.h>
#include <Servo.h>

#include "src/motor.h"
#include "src/encoders.h"

#define RADIUS 0.036
#define LENGTH 0.325
#define TICKS_PER_REV 3690.0
#define ENCODER_LEFT_PIN 19
#define ENCODER_RIGHT_PIN 21
#define POSITION_COMPUTE_INTERVAL 150

bool set_;
double set = 150;
double pidLeftCorrection, pidRightCorrection;
volatile unsigned int leftTicks, righTicks;
unsigned long prevPositionComputeTime = 0;

/*Position control*/
float posX = 0, posY = 0, orientation = 0;
float targetPosX = 0, targetPosY = 0, targetOrientation = 0, finalOrientation = 0;
float currentDistance = 0, targetDistance = 0;

Servo arm;
Motor motor;


enum State
{
  IDLE,
  TURNING_TO_FACE_TARGET,
  MOVING_TOWARDS_TARGET
}

currentState = IDLE;

/*Control Parameters */
float Kpl = 6, Kil = 0.00, Kdl = 0;        // left motor pid tuning
float Kpr = 6, Kir = 0.00, Kdr = 0;        // right motor pid tuning

void pulseLeft();
void pulseRight();

Encoders encoders(pulseLeft, pulseRight);

void pulseLeft()
{
  encoders.incrementleftticks();
}
void pulseRight()
{
  encoders.incrementrightticks();
}

PID pidLeft(&encoders.lrpm, &pidLeftCorrection, &set, Kpl, Kil, Kdl, DIRECT);
PID pidRight(&encoders.rrpm, &pidRightCorrection, &set, Kpr, Kir, Kdr, DIRECT);

char states[][10]
{
  "IDLE",
  "TURNING",
  "MOVING"
};

void configureSET()
{
  Serial.println("Enter SET: ");
  while (!Serial.available());
  set = Serial.readString().toFloat();
}

void configureRightPID()
{
  Serial.println("Enter KPR: ");
  while (!Serial.available());
  Kpr = Serial.readString().toFloat();

  Serial.println("Enter KDR: ");
  while (!Serial.available());
  Kdr = Serial.readString().toFloat();

  Serial.println("Enter KIR: ");
  while (!Serial.available());
  Kir = Serial.readString().toFloat();

  Serial.print("Setting values: ");
  Serial.print(Kpr);
  Serial.print(", ");
  Serial.print(Kdr);
  Serial.print(", ");
  Serial.print(Kir);
  Serial.print("\n");

  pidRight.SetTunings(Kpr, Kdr, Kir);
}

void configureLeftPID()
{
  Serial.println("Enter KPL: ");
  while (!Serial.available());
  Kpl = Serial.readString().toFloat();

  Serial.println("Enter KDL: ");
  while (!Serial.available());
  Kdl = Serial.readString().toFloat();

  Serial.println("Enter KIL: ");
  while (!Serial.available());
  Kil = Serial.readString().toFloat();

  Serial.print("Setting values: ");
  Serial.print(Kpl);
  Serial.print(", ");
  Serial.print(Kdl);
  Serial.print(", ");
  Serial.print(Kil);
  Serial.print("\n");

  pidLeft.SetTunings(Kpl, Kdl, Kil);
}

void setup()
{
  Serial.begin(9600);
  float flusher = 3.14;
  // configureRightPID();

  pidRight.SetMode(AUTOMATIC);
  pidLeft.SetMode(AUTOMATIC);

  arm.attach(2);
  arm.write(0);
}

void updatePosition()
{
  float wheelRadius = RADIUS;
  float axleLength = LENGTH;

  static float prevTime = micros();
  float deltaTime = (micros() - prevTime) / 1000000.0f;
  float rpm = (encoders.lrpm + encoders.rrpm) / 2;

  float distanceTravelled = (motor.lpos + motor.rpos) / 2 * deltaTime * rpm * wheelRadius * 2 * PI;
  float angleTurned = (motor.rpos - motor.lpos) / 2 * deltaTime * rpm * 2 * PI / 60;
  float angle = angleTurned * wheelRadius * 2 / axleLength;
  currentDistance += distanceTravelled;

  orientation += angle;
  if (orientation > 2 * PI)
    orientation -= 2 * PI;
  else if (orientation < 0)
    orientation += 2 * PI;
  posX += cos(orientation) * distanceTravelled;
  posY += sin(orientation) * distanceTravelled;

  prevTime = micros();
}

void printPosition()
{
  Serial.print(states[currentState]);
  Serial.print(":\t");
  Serial.print(posX);
  Serial.print("\t");
  Serial.print(posY);
  Serial.print("\t");
  Serial.println(orientation * 180 / PI);
}

void findAngle()
{
  targetOrientation = atan((targetPosY - posY) / (targetPosX - posX));

  if ((targetPosY - posY) == 0) //X-Axis
  {
    if (targetPosX - posX > 0)
      targetOrientation = 0;
    else
      targetOrientation = PI;
  }
  else if ((targetPosX - posX) == 0) //Y-Axis
  {
    if (targetPosY - posY > 0)
      targetOrientation = PI / 2;
    else
      targetOrientation = 0.75 * PI;
  }
  else if (((targetPosY - posY) > 0) && ((targetPosX - posX) > 0)) // 1st QUADRANT
    targetOrientation = targetOrientation;
  else if (((targetPosY - posY) > 0) && ((targetPosX - posX) < 0)) // 2nd QUADRANT
    targetOrientation += PI;
  else if (((targetPosY - posY) < 0) && ((targetPosX - posX) < 0)) // 3rd QUADRANT
    targetOrientation += PI;
  else if (((targetPosY - posY) < 0) && ((targetPosX - posX) > 0)) // 4th QUADRANT
    targetOrientation += 2 * PI;

  if (targetOrientation > 2 * PI)
    targetOrientation -= 2 * PI;

  if (targetOrientation < 0)
    targetOrientation += 2 * PI;
}

void setTarget(float x, float y, float o)
{
  targetPosX = x;
  targetPosY = y;
  finalOrientation = o;
  findAngle();
  targetDistance = distanceToTarget();
  currentDistance = 0;
  currentState = TURNING_TO_FACE_TARGET;
}

float distanceToTarget()
{
  return sqrt(square(posY - targetPosY) - square(posX - targetPosX));
}

void loop()
{
  if  (currentState != IDLE)
  {
    pidRight.Compute();
    pidLeft.Compute();

    pidRightCorrection = round(pidRightCorrection);
    pidLeftCorrection = round(pidLeftCorrection);

    motor.setleftspeed(pidLeftCorrection);
    motor.setrightspeed(pidRightCorrection);
  }

  if (millis() - prevPositionComputeTime > POSITION_COMPUTE_INTERVAL)
  {
    encoders.computeRPM();
    prevPositionComputeTime = millis();
  }

  updatePosition();
  printPosition();

  switch (currentState)
  {
    case TURNING_TO_FACE_TARGET:

      Serial.print(orientation);
      Serial.print("\t");
      Serial.println(targetOrientation);

      if (abs(orientation - targetOrientation) > 0.1)
      {
        motor.leftturn();
      }

      else
      {
        motor.brake();
        currentState = MOVING_TOWARDS_TARGET;
      }
      break;

    case MOVING_TOWARDS_TARGET:

      if (currentDistance < targetDistance)
      {
        motor.forward();
      }
      else
      {
        motor.brake();
        Serial.print("brake");
        currentState = IDLE;
      }
      break;
  }

  static int angle = 0;

  if (Serial.available())
  {
    char c = Serial.read();
    switch (c)
    {
      case '1':
        setTarget(0, 100, 0);
        break;
      case '0':
        angle = max(angle - 10, 0);
        arm.write(angle);
        break;
      case '2':
        angle = min(angle + 10, 180);
        arm.write(angle);
        break;
    }
  }

  Serial.print("lrpm: ");
  Serial.print(encoders.lrpm);
  Serial.print("\t\t");
  Serial.print("rrpm: ");
  Serial.print(encoders.rrpm);
  Serial.print("\t\t");

  // Serial.print("\t");
  // Serial.print(leftTicks);
  // Serial.print("\t");
  // Serial.println(pidRightCorrection);
  // Serial.println(angle);
  //Serial.print(states[currentState]);
  //Serial.print("\t");
  //Serial.print(orientation);
  //Serial.print("/");
  //Serial.print(targetOrientation);
  //Serial.print("\t");
  //Serial.print(currentDistance);
  //Serial.print("/");
  //Serial.print(targetDistance);
  //Serial.print("rrpm:");
  //Serial.println(encoders.rrpm );
  //Serial.print("lrpm");
  //Serial.println(encoders.lrpm );
  delay(10);
}
