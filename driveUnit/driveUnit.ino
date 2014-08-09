

//pins
const uint8_t iInnerPhotoSensorAnalogPins[8] = {0, 1, 2, 3, 4, 5, 6, 7};
const uint8_t iOuterPhotoSensorAnalogPins[8] = {8, 9, 10, 11, 12, 13, 14, 15};
const uint8_t iActuatorLimitsPins[8] = {3, 4, 5, 6, 7, 8, 9, 10};
const uint8_t oActuatorRelayPins[8] = {14, 15, 16, 17, 22, 23, 24, 25};
const uint8_t oActuatorReverseRelayPins[2] = {18, 26};
const uint8_t oSpareRelayPins[6] = {19, 20, 21, 27, 28, 29};
const uint8_t oIndicatorLedPins[4] = {32, 33, 34, 35};
const uint8_t iMotorPhotoSensorPins[2] = {51, 52};

// Physically defined stuff like gear ratios; speed stuff is in encoder ticks/1000 secs
const int maxOrbitSpeeds[8] = {11748, 4628, 2813, 1562, 632, 249, 90, 45};
const uint8_t totalEncoderSteps[8] = {59, 59, 59, 59, 149, 149, 149, 149};
const uint8_t totalEncoderRotations[8] = {3, 3, 3, 3, 4, 4, 4, 4};

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
	uint8_t currentlyActiveActuator; //1-indexed
	unsigned long actuatorTimeOut;
	uint8_t movementQueue[20][2]; //{{actuator1, direction1}, {actuator2, direction2}, ...}
} ActuatorGroup;

typedef struct {
	int averageSpeeds[3]; //average speeds grouped by short, medium, and long terms
	int targetSpeed; //target speed of planets, stays fixed per alignment target
	int maxSpeed; //max as determined by gearing
	int estimatedActuatorPositions; //how many "steps"(really just on pulses of a given duration) out are we from brake fully engaged
	int estimatedActuatorStepsInRange; //how many "steps" are there in full range of actuator
	int expectedChangeInSpeed[5]; //estimated effectiveness of releasing the brake by section of actuator steps range
	bool compensateForWind;
	uint8_t currentEncoderSteps;
	uint8_t currentEncoderRotations;
	uint8_t totalEncoderSteps;
	uint8_t totalEncoderRotations;
	Encoder encoder;
} Planet;

Planet planets[8];
Encoder encoders[9];
ActuatorGroup actuatorgroups[2];

unsigned long estimatedAlignmentTimePoints[10];
uint8_t alignmentQueue[10][8];
int relativeEffectivenessFactors[8]; //For wind compenstion; how effective is releasing the brake per 8th orbit rotation; i think we will try to keep this summed up to 0
char commandBuffer[5];

void setup(){
	for (uint8_t i=0; i<8; i++){
		planets[i].averageSpeeds[0] = maxOrbitSpeeds[i];
		planets[i].averageSpeeds[1] = maxOrbitSpeeds[i];
		planets[i].averageSpeeds[2] = maxOrbitSpeeds[i];
		planets[i].targetSpeed = maxOrbitSpeeds[i];
		planets[i].maxSpeed = maxOrbitSpeeds[i];
		planets[i].estimatedActuatorPositions = 0;
		planets[i].estimatedActuatorStepsInRange = 0;
		//planets[i].expectedChangeInSpeed[5]//load from eeprom
		planets[i].compensateForWind = i > 3;
		//planets[i].currentEncoderSteps //load from eeprom
		//planets[i].currentEncoderRotations//load from eeprom
		planets[i].totalEncoderSteps = totalEncoderSteps[i];
		planets[i].totalEncoderRotations = totalEncoderRotations[i];
		planets[i].encoder = encoders[i];
	}
}

void loop(){

}


void engageBrake(uint8_t planetIndex){
	//put the brake to the out limit and reset estimatedActuatorPositions
	
}
void disengageBrake(uint8_t planetIndex){
	//pull the brake in till release, and if starting from engages note the number of steps
}


void addActuatorChangeToQueue(uint8_t changeDirection, uint8_t planetIndex){

}

int estimateTimeToPosition(uint8_t planetIndex, uint8_t angle){

}
int estimateTimeToAlignment(uint8_t angles[8]){

}

void addAlignmentToQueue(uint8_t angles[8]){

}

void setZeroPositions(){

}

void savePositionsToEeprom(){

}


void doActuatorChanges(){
	// check if any actuators are ready to turn off, turn on next one, 
	//and remove the change request from the queue. Only one actator per group of 4 can be on at a time.
	//actuators controlled by pulses of a set length of time
}

int checkSpeedOfPlanet(uint8_t groupIndex, uint8_t planetIndex, int result[3]){
	//call readencoders and determine average speed at short, (maybe)medium, and long intervals. write to planetSpeeds.
}



void readEncoders(){

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