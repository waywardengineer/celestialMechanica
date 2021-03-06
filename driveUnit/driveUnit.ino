#include <Arduino.h>

#include <EEPROM.h>
#include <QueueArray.h>
#include <string.h>


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
const int motorEncoderMinimumChangeTime = 500;
const int eepromMinimumSaveInterval = 5000;
const uint8_t speedAverageWeightingFactor = 6;
const uint8_t maxActuatorChangeQueueDepth = 50;

typedef struct {
	uint8_t currentState;
	unsigned long lastChangeTime;
	uint8_t outerSensorPin;
	uint8_t innerSensorPin;
	bool isMoving;
	bool isMovingInReverse;
	bool invertDirectionSense;
} Encoder;

typedef struct {
	char currentlyActiveActuator; 
	char movementQueue[maxActuatorChangeQueueDepth][2]; //{{actuator1, direction1}, {actuator2, direction2}, ...}
	uint8_t reverseRelayPin;
	unsigned long actuatorTimeOut;
	bool inReverse;
	uint8_t queueDepth;
} ActuatorGroup;

typedef struct {
	int instantaneousSpeed;
	int weightedAverageSpeed;
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
	bool actuatorMovePending;
	char lastActuatorChangeRequested;
	int speedAtActuatorChangeRequest;
} Planet;



Planet planets[8];
Encoder encoders[9];
ActuatorGroup actuatorGroups[2];


unsigned long estimatedAlignmentTimePoints[10];
uint8_t alignmentQueue[10][8];
int relativeEffectivenessFactors[8]; //For wind compenstion; how effective is releasing the brake per 8th orbit rotation; i think we will try to keep this summed up to 0
char commandBuffer[5];
unsigned long currentTime;
unsigned long lastMotorChangeTime;
unsigned long lastEepromSaveTime;
bool learningHomePosition = false;
bool currentDataSavedToEeprom = false;

// struct for queued commands
typedef struct {
	char opcode;
	uint8_t argLength;
	char args[8];
} Command;

QueueArray<Command> commandQueue;



void setup(){
	Serial.begin(9600);
	// Serial.println("Started.");
	currentTime = millis();
	for (uint8_t i=0; i<8; i++){
		unsigned long measurementIntervalx100 = 10000 / (speedIntervalDivisionsPerOrbit[i] * maxOrbitSpeeds[i]);
		planets[i].instantaneousSpeed = maxOrbitSpeeds[i];
		planets[i].weightedAverageSpeed = maxOrbitSpeeds[i];
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
		planets[i].encoder.lastChangeTime = currentTime;
		planets[i].actuatorGroup = actuatorGroups[i / 4];
		planets[i].actuatorRunningPin = iActuatorRunningPins[i];
		planets[i].actuatorRelayPin = oActuatorRelayPins[i];
		planers[i].actuatorMovePending = false;
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

void handleCommand(){
	if (commandQueue.isEmpty()) { return; }
	// Serial.println("Handling.");
	Command command = commandQueue.pop();
	// Do stuff with the command

	// Serial.println(command.opcode);
	// Serial.println(command.argLength);
	// for (int i=0; i < command.argLength; i++){ Serial.println(command.args[i]); }
	// etc.
}

void loop(){
	currentTime = millis();
	checkAndUpdateActuators();
	readPlanetEncoders();
	checkMotorDirectionAndSaveToEepromIfStopped();
	checkSerial();
	handleCommand();
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
			planets[planetIndex].actuatorMovePending = false;
			for (uint8_t j=1; j<actuatorGroups[i].queueDepth; j++){
				if (actuatorGroups[i].movementQueue[j][0] == planetIndex){
					planets[planetIndex].actuatorMovePending = true;
				}
			}
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
	if (planets[planetIndex].actuatorGroup.queueDepth < maxActuatorChangeQueueDepth){
		uint8_t queueDepth = planets[planetIndex].actuatorGroup.queueDepth++;
		planets[planetIndex].actuatorGroup.movementQueue[queueDepth][0] = planetIndex;
		planets[planetIndex].actuatorGroup.movementQueue[queueDepth][1] = changeDirection;
	}
}

int estimateTimeToPosition(uint8_t planetIndex, uint8_t angle){

}
int estimateTimeToAlignment(uint8_t angles[8]){

}

void addAlignmentToQueue(uint8_t angles[8]){

}

void setZeroPositions(){

}


void setTargetSpeeds(){

}


void adjustPlanetSpeeds(){
	for (uint8_t i=0; i<8; i++){
		if (planets[i].targetSpeed < planets[i].maxSpeed){
			if (! planets[i].actuatorMovePending){
				long differencePercent = ((planets[i].weightedAverageSpeed - planets[i].targetSpeed) * 100) / planets[i].targetSpeed;
				if (abs(differencePercent) > 10){
					int stepsToDo = abs(differencePercent) / planets[i].expectedChangeInSpeed[expectedChangeInSpeedIndex]
					char changeDirection = differencePrecent > 0 ? 1 : -1;
					for (uint8_t j=0; j < stepsToDo; j++){
						addActuatorChangeToQueue(changeDirection, i);
					}
					planets[i].lastActuatorChangeRequested = changeDirection * stepsToDo;
					planets[i].speedAtActuatorChangeRequest = planets[i].instantaneousSpeed;
					planets[i].targetSpeedAtActuatorChangeRequest = planets[i].targetSpeed;
				}
			}
		}
	}
}

void adjustExpectedChangesInSpeed(uint8_t planetIndex){
	int expectedChangeInSpeedIndex = planets[i].estimatedActuatorPosition * divisionsInEncoderRange / planets[i].estimatedActuatorStepsInRange;
	planets[i].expectedChangeInSpeed[expectedChangeInSpeedIndex] = (planets[i].expectedChangeInSpeed[expectedChangeInSpeedIndex] * expectedChangeInSpeedWeightingFactor + 
		(planets[i].targetSpeedAtActuatorChangeRequest - planets[i].speedAtActuatorChangeRequest) / (planets[i].targetSpeedAtActuatorChangeRequest - planets[i].instantaneousSpeed)) / 
		(expectedChangeInSpeedWeightingFactor + 1);

}

void learnActuatorSteps(){ //blocking function, don't count on accurate remembering of position after this
	for (uint8_t i=0; i < 8; i++){
		planets[i].estimatedActuatorStepsInRange = 0;
		resetActuatorGroup(i/4);
		addActuatorChangeToQueue(-2, i);
		while (planets[i].estimatedActuatorPosition > 0){
			checkAndUpdateActuators();
		}
		while (planets[i].estimatedActuatorStepsInRange == 0){
			if (planets[i].actuatorGroup.queueDepth == 0){
				addActuatorChangeToQueue(1, i);
			}
			checkAndUpdateActuators();
		}
		addActuatorChangeToQueue(-2, i);
		while (planets[i].estimatedActuatorPosition > 0){
			checkAndUpdateActuators();
		}
	}
}

void resetActuatorGroup(uint8_t actuatorGroupIndex){ //hard reset of actuator movements
	if (actuatorGroups[actuatorGroupIndex].queueDepth > 0){
		for (uint8_t i=0; i<(actuatorGroups[actuatorGroupIndex].queueDepth); i++){
			actuatorGroups[actuatorGroupIndex].movementQueue[i][0] = 0;
			actuatorGroups[actuatorGroupIndex].movementQueue[i][1] = 0;
		}
		actuatorGroups[actuatorGroupIndex].queueDepth = 0;
	}
	actuatorGroups[actuatorGroupIndex].currentlyActiveActuator = -1;
	for (uint8_t i=4*actuatorGroupIndex; i<4+4*actuatorGroupIndex; i++){
		digitalWrite(oActuatorRelayPins[i], LOW);
	}
}

void readPlanetEncoders(){
	for (uint8_t i=0; i<8; i++){
		uint8_t innerSensor = analogRead(planets[i].encoder.innerSensorPin) > photosensorOffThreshold ? 1 : 0;
		uint8_t outerSensor = analogRead(planets[i].encoder.outerSensorPin) > photosensorOffThreshold ? 2 : 0;
		uint8_t newSensorState = innerSensor & outerSensor;
		if (newSensorState != planets[i].encoder.currentState && !((planets[i].encoder.currentState == 1 || planets[i].encoder.currentState == 2) && newSensorState == 3)){
			//ignore transitions from one notch to 2 notches; those can only come from misalignment of the sensors
			if (newSensorState && planets[i].encoder.currentState){// 01 & 10 or 10 & 01 but not 00 & whatever
				if (newSensorState == 1){ // inner sensor
					planets[i].encoder.isMovingInReverse = true;
				}
				else if (newSensorState == 2){ // outer sensor
					planets[i].encoder.isMovingInReverse = false;
				}
			}
			planets[i].encoder.currentState = newSensorState;
			uint8_t directionFactor = planets[i].encoder.isMovingInReverse?-1:1;			
			if (newSensorState == 0 && planets[i].encoder.currentState == 3){//special notch indicating full rotation
				planets[i].currentEncoderRotations += directionFactor;
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
			else { //normal notch, only measure speed on these because spacing is irregular on the special notch
				unsigned long timeSinceLastChange = currentTime - planets[i].lastChangeTime
				planets[i].lastChangeTime = currentTime
				unsigned long instantaneousSpeed = 1000000 / timeSinceLastChange;
				instantaneousSpeed = instantaneousSpeed > 65535?65535:instantaneousSpeed; //should never exceed this but good to check before putting this into an int
				instantaneousSpeed *= directionFactor;
				planets[i].instantaneousSpeed = instantaneousSpeed;
				unsigned long weightedAverageSpeed = (planets[i].weightedAverageSpeed * speedAverageWeightingFactor + instantaneousSpeed) / (speedAverageWeightingFactor + 1);
				planets[i].weightedAverageSpeed = weightedAverageSpeed;
				planets[i].currentEncoderSteps += directionFactor;
			}
		}
	}
}

void checkMotorDirectionAndSaveToEepromIfStopped(){
	uint8_t innerSensor = digitalRead(encoders[8].innerSensorPin) ? 1 : 0;
	uint8_t outerSensor = digitalRead(encoders[8].outerSensorPin) ? 2 : 0;
	uint8_t newSensorState = innerSensor & outerSensor;
	if (newSensorState != encoders[8].currentState){
		encoders[8].isMoving = true;
		currentDataSavedToEeprom = false;
		lastMotorChangeTime = currentTime;
		if (newSensorState && encoders[8].currentState){// 01 & 10 or 10 & 01 but not 00 & whatever
			if (newSensorState == 1){ // inner sensor
				encoders[8].isMovingInReverse = true;
			}
			else if (newSensorState == 2){ // outer sensor
				encoders[8].isMovingInReverse = false;
			}
		}
	}
	if (currentTime > lastMotorChangeTime + motorEncoderMinimumChangeTime){
		if (currentTime > lastEepromSaveTime + eepromMinimumSaveInterval){
			encoders[8].isMoving = false;
			saveDataToEeprom();
			currentDataSavedToEeprom = true;
			lastEepromSaveTime = currentTime;
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

void parseAndQueueCommand(char buffer[]) {
	Serial.println("Parsing.");
	// Serial.println(buffer);
	
	char *saveptr;
	char *token = strtok_r(buffer, ",", &saveptr);
	char opcode = *token;
	Command command;
	command.opcode = opcode;
	uint8_t args[9];
	uint8_t i = 0;
	for (token = strtok_r(NULL, ",", &saveptr); token; token = strtok_r(NULL, ",", &saveptr) ) {
		command.args[i++] = *token;
	}
	command.argLength = i;

	// Push the command onto the queue.
	commandQueue.push(command);
}

uint8_t listeningState = 0;
uint8_t bufferCount = 0;
char buffer[20];

uint8_t checkSerial() {
	char incomingByte;
	
	while (Serial.available() > 0) {
		incomingByte = Serial.read();
		if (incomingByte == '!'){
			listeningState = 1;//listening to command
			Serial.println("Listening.");
			bufferCount = 0;
		}
		else if (listeningState == 1){
			// Serial.println(incomingByte);
			if (incomingByte == '.'){
				listeningState = 2;  // command complete

			}
			else {
				buffer[bufferCount++] = incomingByte;
				Serial.println(buffer);
			}
		}
		else if (listeningState == 2) {
			Serial.println("Done.");
			break;
		} else {
			// Serial.println(listeningState);
		}
	}
	if (listeningState == 2 && bufferCount > 0){
		/*
		result[0] = buffer[0]; //the single letter command
		uint8_t i = 1;
		resultLength = 1;
		while (i < bufferCount - 1){ //hex values, will be extedded to dealing with 2 and 3 digit values
			result[resultLength++] = hexToByte(buffer[i++]);
		}
		*/

		// null terminate the string
		buffer[bufferCount++] = NULL;
		 
		parseAndQueueCommand(buffer);
		bufferCount = 0;
		listeningState = 0;
	}
	return bufferCount;
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