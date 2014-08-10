#include <Arduino.h>

#include <EEPROM.h>


//pins
const uint8_t iInnerPhotoSensorAnalogPins[8] = {0, 1, 2, 3, 4, 5, 6, 7};
const uint8_t iOuterPhotoSensorAnalogPins[8] = {8, 9, 10, 11, 12, 13, 14, 15};
const uint8_t iActuatorRunningPins[8] = {3, 4, 5, 6, 7, 8, 9, 10};
const uint8_t oActuatorRelayPins[8] = {14, 15, 16, 17, 22, 23, 24, 25};
const uint8_t oActuatorReverseRelayPins[2] = {18, 26};
const uint8_t oSpareRelayPins[6] = {19, 20, 21, 27, 28, 29};
const uint8_t oIndicatorLedPins[4] = {32, 33, 34, 35};
const uint8_t iMotorPhotoSensorPins[2] = {51, 52};

// Physically defined stuff like gear ratios; speed stuff is in encoder ticks/1000 secs
const int maxOrbitSpeeds[8] = {11748, 4628, 2813, 1562, 632, 249, 90, 45};
const uint8_t totalEncoderSteps[8] = {59, 59, 59, 59, 149, 149, 149, 149};
const uint8_t totalEncoderRotations[8] = {3, 3, 3, 3, 4, 4, 4, 4};

// Weirder numbers that may need fiddling with
const uint8_t divisionsInEncoderRange = 5;
const int actuatorStepTime = 500;
const int actuatorOffTime = 100;
const int photosensorOffThreshold = 300;
const int speedIntervalDivisionsPerOrbit[8] = {6, 10, 10, 10, 50, 50, 50, 80};

typedef struct {
	uint8_t currentState;
	uint8_t lastState;
	uint8_t outerSensorPin;
	uint8_t innerSensorPin;
	bool isMoving;
	bool isMovingInReverse;
	bool invertDirectionSense;
} Encoder;

typedef struct {
	char currentlyActiveActuator; 
	char movementQueue[20][2]; //{{actuator1, direction1}, {actuator2, direction2}, ...}
	uint8_t reverseRelayPin;
	unsigned long actuatorTimeOut;
	bool inReverse;
	uint8_t queueDepth;
} ActuatorGroup;

typedef struct {
	int encoderTicksPerTimeInterval[10];
	int encoderSpeedMeasurementInterval;
	unsigned long encoderMeasurementIntervalRolloverTime;
	int averageSpeeds[3]; //average speeds grouped by short, medium, and long terms
	int targetSpeed; //target speed of planets, stays fixed per alignment target
	int maxSpeed; //max as determined by gearing
	int estimatedActuatorPosition; //how many "steps"(really just on pulses of a given duration) out are we from brake fully engaged
	uint8_t estimatedActuatorStepsInRange; //how many "steps" are there in full range of actuator
	int expectedChangeInSpeed[divisionsInEncoderRange]; //estimated effectiveness of releasing the brake by section of actuator steps range
	bool compensateForWind;
	uint8_t currentEncoderSteps;
	uint8_t currentEncoderRotations;
	uint8_t totalEncoderSteps;
	uint8_t totalEncoderRotations;
	ActuatorGroup actuatorGroup;
	uint8_t actuatorRunningPin;
	uint8_t actuatorRelayPin;
	uint8_t homeOffset;
	Encoder encoder;
} Planet;

Planet planets[8];
Encoder encoders[9];
ActuatorGroup actuatorGroups[2];

unsigned long estimatedAlignmentTimePoints[10];
uint8_t alignmentQueue[10][8];
int relativeEffectivenessFactors[8]; //For wind compenstion; how effective is releasing the brake per 8th orbit rotation; i think we will try to keep this summed up to 0
char commandBuffer[5];
unsigned long currentTime;
bool learningHomePosition = false;

void setup(){
	currentTime = millis();
	for (uint8_t i=0; i<8; i++){
		unsigned long measurementIntervalx100 = 10000 / (speedIntervalDivisionsPerOrbit[i] * maxOrbitSpeeds[i]);
		planets[i].encoderSpeedMeasurementInterval = measurementIntervalx100 / 100;
		planets[i].encoderMeasurementIntervalRolloverTime = currentTime;
		planets[i].targetSpeed = maxOrbitSpeeds[i];
		planets[i].maxSpeed = maxOrbitSpeeds[i];
		planets[i].estimatedActuatorPosition = 0;
		planets[i].estimatedActuatorStepsInRange = 0;
		planets[i].compensateForWind = i > 3;
		planets[i].totalEncoderSteps = totalEncoderSteps[i];
		planets[i].totalEncoderRotations = totalEncoderRotations[i];
		planets[i].encoder = encoders[i];
		planets[i].encoder.innerSensorPin = iInnerPhotoSensorAnalogPins[i];
		planets[i].encoder.outerSensorPin = iOuterPhotoSensorAnalogPins[i];
		planets[i].actuatorGroup = actuatorGroups[i / 4];
		planets[i].actuatorRunningPin = iActuatorRunningPins[i];
		planets[i].actuatorRelayPin = oActuatorRelayPins[i];
		pinMode(iActuatorRunningPins[i], INPUT);
		pinMode(oActuatorRelayPins[i], OUTPUT);
	}
	for (uint8_t i=0; i < 2; i++){
		pinMode(oActuatorReverseRelayPins[i], OUTPUT);
		pinMode(iMotorPhotoSensorPins[i], INPUT);
		actuatorGroups[i].reverseRelayPin = oActuatorReverseRelayPins[i];
		actuatorGroups[i].queueDepth = 0;
	}
	for (uint8_t i=0; i < 4; i++){
		pinMode(oIndicatorLedPins[i], OUTPUT);
	}
	for (uint8_t i=0; i < 6; i++){
		pinMode(oSpareRelayPins[i], OUTPUT);
	}
	loadDataFromEeprom();
}

void loop(){
	currentTime = millis();
	checkAndUpdateActuators();
	
}

void loadDataFromEeprom(){
	for (uint8_t i=0; i<8; i++){
		planets[i].currentEncoderSteps = EEPROM.read(i);
		planets[i].currentEncoderRotations = EEPROM.read(8 + i);
		planets[i].estimatedActuatorStepsInRange = EEPROM.read(16 + i);
		planets[i].homeOffset = EEPROM.read(24 + i);
		for (uint8_t j=0; j<divisionsInEncoderRange; j++){
			uint8_t address = 32 + i * j * 2;
			planets[i].expectedChangeInSpeed[j] = getIntFromEeprom(address);
		}
	}
}

void saveDataToEeprom(){
	for (uint8_t i=0; i<8; i++){
		EEPROM.write(i, planets[i].currentEncoderSteps);
		EEPROM.write(8 + i, planets[i].currentEncoderRotations);
		EEPROM.write(16 + i, planets[i].estimatedActuatorStepsInRange);
		EEPROM.write(24 + i, planets[i].homeOffset);
		for (uint8_t j=0; j<divisionsInEncoderRange; j++){
			uint8_t address = 32 + i * j * 2;
			saveIntToEeprom (address, planets[i].expectedChangeInSpeed[j]);
		}
	}
}

void startActuatorMove(char moveData[2]){
	char planetIndex = moveData[0];
	char dir = moveData[1];
	if (planets[planetIndex].actuatorGroup.currentlyActiveActuator == -1){
		if (dir < 0){
			digitalWrite(planets[planetIndex].actuatorGroup.reverseRelayPin, HIGH);
		}
		digitalWrite(planets[planetIndex].actuatorRelayPin, HIGH);
		if (dir == -2 or dir == 2){
			planets[planetIndex].actuatorGroup.actuatorTimeOut = 0;
		}
		else {
			planets[planetIndex].actuatorGroup.actuatorTimeOut = currentTime + actuatorStepTime;
		}
		planets[planetIndex].actuatorGroup.inReverse = dir < 0;
	}
}

void checkAndUpdateActuators(){
	for (int i=0; i<2; i++){
		bool stopActuator = false;
		uint8_t planetIndex;
		if (actuatorGroups[i].currentlyActiveActuator > -1){
			planetIndex = actuatorGroups[i].currentlyActiveActuator;
			if (actuatorGroups[i].actuatorTimeOut > 0 and currentTime >= actuatorGroups[i].actuatorTimeOut){
				stopActuator = true;
				planets[planetIndex].estimatedActuatorPosition += actuatorGroups[i].inReverse?-1:1;
			}
			if (not digitalRead(planets[actuatorGroups[i].currentlyActiveActuator].actuatorRunningPin)){//limits hit
				stopActuator = true;
				if (actuatorGroups[i].inReverse){
					planets[planetIndex].estimatedActuatorPosition = 0;
				}
				else {
					planets[planetIndex].estimatedActuatorStepsInRange = planets[planetIndex].estimatedActuatorPosition;
				}
			}
		}
		else if (actuatorGroups[i].currentlyActiveActuator == -2){
			if (currentTime > actuatorGroups[i].actuatorTimeOut){
				actuatorGroups[i].currentlyActiveActuator = -1;
			}
		}
		if (stopActuator){
			actuatorGroups[i].currentlyActiveActuator = -2;
			actuatorGroups[i].actuatorTimeOut = currentTime + actuatorOffTime; //an attemp to make actuator on times into a more measurable set of steps
			digitalWrite(planets[planetIndex].actuatorRelayPin, LOW);
			digitalWrite(actuatorGroups[i].reverseRelayPin, LOW);
		}
		if (actuatorGroups[i].currentlyActiveActuator == -1){
			doNextActuatorMoveInQueue(i);
		}
	}
}


void doNextActuatorMoveInQueue(uint8_t actuatorGroupIndex){
	if (actuatorGroups[actuatorGroupIndex].queueDepth > 0){
		for (uint8_t i=0; i<(actuatorGroups[actuatorGroupIndex].queueDepth - 1); i++){
			actuatorGroups[actuatorGroupIndex].movementQueue[i][0] = actuatorGroups[actuatorGroupIndex].movementQueue[i+1][0];
			actuatorGroups[actuatorGroupIndex].movementQueue[i][1] = actuatorGroups[actuatorGroupIndex].movementQueue[i+1][1];
		}
		actuatorGroups[actuatorGroupIndex].queueDepth--;
		startActuatorMove(actuatorGroups[actuatorGroupIndex].movementQueue[0]);
	}
}

void addActuatorChangeToQueue(char changeDirection, char planetIndex){
	uint8_t queueDepth = planets[planetIndex].actuatorGroup.queueDepth++;
	planets[planetIndex].actuatorGroup.movementQueue[queueDepth][0] = planetIndex;
	planets[planetIndex].actuatorGroup.movementQueue[queueDepth][1] = changeDirection;
}

int estimateTimeToPosition(uint8_t planetIndex, uint8_t angle){

}
int estimateTimeToAlignment(uint8_t angles[8]){

}

void addAlignmentToQueue(uint8_t angles[8]){

}

void setZeroPositions(){

}





int checkSpeedOfPlanet(uint8_t groupIndex, uint8_t planetIndex, int result[3]){
	//call readencoders and determine average speed at short, (maybe)medium, and long intervals. write to planetSpeeds.
}



void readEncoders(){
	for (uint8_t i=0; i<8; i++){
		uint8_t innerSensor = analogRead(planets[i].encoder.innerSensorPin) > photosensorOffThreshold ? 1 : 0;
		uint8_t outerSensor = analogRead(planets[i].encoder.outerSensorPin) > photosensorOffThreshold ? 2 : 0;
		uint8_t newSensorState = innerSensor & outerSensor;
		if (newSensorState != planets[i].encoder.currentState && !(planets[i].encoder.currentState & newSensorState)){
			if (newSensorState && planets[i].encoder.currentState){// 01 & 10 or 10 & 01 but not 00 & whatever
				if (newSensorState == 1){ // inner sensor
					planets[i].encoder.isMovingInReverse = true;
				}
				else if (newSensorState == 2){ // outer sensor
					planets[i].encoder.isMovingInReverse = false;
				}
			}
			planets[i].encoder.currentState = newSensorState;
			uint8_t changeIncrement = planets[i].encoder.isMovingInReverse?-1:1;
			if (newSensorState == 0 && planets[i].encoder.currentState == 3){
				planets[i].currentEncoderRotations += changeIncrement;
				if (planets[i].currentEncoderRotations < 0){
					planets[i].currentEncoderRotations = -((-planets[i].currentEncoderRotations) % planets[i].totalEncoderRotations);
				}
				else {
					planets[i].currentEncoderRotations = planets[i].currentEncoderRotations % planets[i].totalEncoderRotations;
				}
				if (learningHomePosition){
					planets[i].homeOffset = -planets[i].currentEncoderSteps;
				}
				planets[i].currentEncoderSteps = 0;
			}
			else {
				planets[i].currentEncoderSteps += changeIncrement;
			}
			planets[i].encoderTicksPerTimeInterval[0] += changeIncrement;
			if (currentTime > planets[i].encoderMeasurementIntervalRolloverTime){
				for (uint8_t j=9; j>=0; j--){
					planets[i].encoderTicksPerTimeInterval[j+1] = planets[i].encoderTicksPerTimeInterval[j];
				}
				planets[i].encoderTicksPerTimeInterval[0] = 0;
				planets[i].encoderMeasurementIntervalRolloverTime = currentTime + planets[i].encoderSpeedMeasurementInterval;
			}
		}
	}
}


int getIntFromEeprom(uint8_t address){
	int result = EEPROM.read(address) << 8;
	result += EEPROM.read(address + 1);
	return result;
}

void saveIntToEeprom(int value, uint8_t address){
	uint8_t b = value >> 8;
	EEPROM.write (address, b);
	address ++;
	b = value & 0xFF;
	EEPROM.write (address, b);
}


uint8_t checkSerial(char result[5]) {
	uint8_t bufferCount = 0;
	uint8_t listeningState = 0;
	char incomingByte;
	char buffer[10];
	uint8_t resultLength = 0;
	while (Serial.available() > 0) {
		incomingByte = Serial.read();
		if (incomingByte == '!'){
			listeningState = 1;//listening to command
		}
		else if (listeningState == 1){
			if (incomingByte == '.'){
				listeningState = 2;
			}
			else {
				buffer[bufferCount++] = incomingByte;
			}
		}
	}
	if (bufferCount > 0){
		result[0] = buffer[0]; //the single letter command
		uint8_t i = 1;
		resultLength = 1;
		while (i < bufferCount - 1){ //hex values, will be extedded to dealing with 2 and 3 digit values
			result[resultLength++] = hexToByte(buffer[i++]);
		}
	}
	return resultLength;
}

uint8_t hexToByte(char hi, char lo) {
	uint8_t result = hexToByte(hi);
	result = result << 4;
	result += hexToByte(lo);
	return result;
}
uint8_t hexToByte(char digit){
	digit = toupper(digit);
	if( isxdigit(digit) ) {
		if( digit > '9' ) digit -= 7; // software offset for A-F
		digit -= 0x30; // subtract ASCII offset
		return digit;
	}
	return 0;
}