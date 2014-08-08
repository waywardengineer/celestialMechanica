char commandBuffer[5];
const uint8_t iInnerPhotoSensorAnalogPins[8] = {0, 1, 2, 3, 4, 5, 6, 7};
const uint8_t iOuterPhotoSensorAnalogPins[8] = {8, 9, 10, 11, 12, 13, 14, 15};
const uint8_t iActuatorLimitsPins[8] = {3, 4, 5, 6, 7, 8, 9, 10};
const uint8_t oActuatorRelayPins[8] = {14, 15, 16, 17, 22, 23, 24, 25};
const uint8_t oActuatorReverseRelayPins[2] = {18, 26};
const uint8_t oSpareRelayPins[6] = {19, 20, 21, 27, 28, 29};
const uint8_t oIndicatorLedPins[4] = {32, 33, 34, 35};
const uint8_t iMotorPhotoSensorPins = {51, 52};


// speed stuff in encoder ticks/10 mins. or something, we'll see
int planetSpeeds[8][3]; //average speeds grouped by planet and short, medium, and long terms
int targetPlanetSpeeds[8]; //target speeds of planets, stay fixed per alignment target
const int maxPlanetSpeeds[8]; //max as determined by gearing
int estimatedActuatorPositions[8]; //how many "steps"(really just on pulses of a given duration) out are we from brake fully engaged
int estimatedActuatorStepsInRange[8]; //how many "steps" are there in full range of actuator
int expectedChangeInSpeed[8][5][4]; //estimated effectiveness of releasing the brake by planet, section of actuator steps range, quadrant of orbit(to try to compensate for wind)
unsigned long estimatedAlignmentTimePoints[10];
uint8_t alignmentQueue[10][8];

void engageBrake(planetIndex){
	//put the brake to the out limit and reset estimatedActuatorPositions
}
void disengageBrake(planetIndex){
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

void doActuatorChanges(){
	// check if any actuators are ready to turn off, turn on next one, 
	//and remove the change request from the queue. Only one actator per group of 4 can be on at a time.
	//actuators controlled by pulses of a set length of time
}

int checkSpeedOfPlanet(uint8_t groupIndex, uint8_t planetIndex, int result[3]){
	//read sensors and determine average speed at short, (maybe)medium, and long intervals. write to planetSpeeds.
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