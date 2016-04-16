

#define DEBUG 1

#define EN_PIN 2

#define PLAY_PIN 5
#define NEXT_PIN 6
#define PREV_PIN 7

HardwareSerial & kbus = Serial1;

int kbus_from = 0;
int kbus_length = 0;
int kbus_to = 0;
int kbus_data[32] = { 0 };

// String message = "adcurtin";
String message = "123456789abcdef";


void setup(){

    pinMode(PLAY_PIN, OUTPUT);
    pinMode(NEXT_PIN, OUTPUT);
    pinMode(PREV_PIN, OUTPUT);
    digitalWrite(PLAY_PIN, LOW);
    digitalWrite(NEXT_PIN, LOW);
    digitalWrite(PREV_PIN, LOW);

#ifdef DEBUG
    Serial.begin(57600);
    pinMode(13, OUTPUT);
    digitalWrite(13, HIGH);
    delay(5000);
    digitalWrite(13, LOW);
    Serial.println("started");
#endif


    pinMode(EN_PIN, OUTPUT);
    digitalWrite(EN_PIN, HIGH);

    kbus.begin(9600, SERIAL_8E1);

}

void loop(){

    // kbus_print2(message);
    // delay(1000);
    // kbus_print(message);
    delay(5000);

    // press(PLAY_PIN);
    // Serial.println("pressed play");
    // delay(2000);

    // press(PLAY_PIN);
    // Serial.println("pressed play");

    // delay(10000);
    // press(NEXT_PIN);
    // Serial.println("pressed next");


    int bytes_read = 0;
    // bytes_read = read_kbus_packet();
    if(bytes_read > 0){
        // if(kbus_data[0] == 0x68) print_packet();
        // parse_packet();
    }
    delay(2000);


}

/* check kbus for data. if any available, block until whole packet is read.
 * returns number of bytes read.
 * updates:
 *      kbus_from       ID of sending device
 *      kbus_length     length of rest of message
 *      kbus_data[]     rest of message (first byte destination ID)
 */
int read_kbus_packet(){
    if(kbus.available() > 1){
        kbus_from = kbus.read();   //first byte of message is ID of sending device
        kbus_length = kbus.read(); //second byte is bytes remaining in packet
    } else {
        return 0; //nothing available, nothing read.
    }

    for(int i = 0; i < kbus_length; i++){
        while(kbus.available() == 0){} //wait for data to be available
        kbus_data[i] = kbus.read();
    }
    kbus_to = kbus_data[0];

    //verify checksum
    byte crc = kbus_from ^ kbus_length;
    for(int i = 0; i < (kbus_length - 1); i++){
        crc ^= kbus_data[i];
    }

    if(kbus_data[kbus_length-1] != crc){
#ifdef DEBUG
        Serial.print("Checksum mismatch! expected: ");
        Serial.print(crc, HEX);
        Serial.print(" got: ");
        Serial.println(kbus_data[kbus_length-1], HEX);
        print_packet();
#endif
        return 0; //no valid bytes read. can't really handle errors.
    }

    return kbus_length + 2;
}

void parse_packet(){
    if(kbus_data[0] == 0x68){ //command sent to radio
        if(kbus_data[1] == 0x32) { //volume button
            if(kbus_data[2] == 0x11){ //volume up

            } else if(kbus_data[2] == 0x10){ //volume down

            }
        } else if (kbus_data[1] == 0x3B){ //track button
            switch (kbus_data[2]) {
                case 0x01: //next pressed
                case 0x11: //next held
                case 0x21: //next released
                    break;
                case 0x08: //previous pressed
                case 0x18: //previous held
                case 0x28: //previous released
                    break;
                default:
                    break;
            }
        }
    } else if (kbus_data[0] == 0xC8){ //command send to phone unit
        if(kbus_data[1] == 0x01){ // R/T button pressed
        } else if(kbus_data[1] == 0x3B){ // phone button
            if(kbus_data[2] == 0x27){ // pressed
            } else if(kbus_data[2] == 0x37){ //held
            } else if(kbus_data[2] == 0x07){ //released
            }
        }
    } else {
        return;
    }
}

// volume up             0x68, 0x32, ? ? Volume: 11:1F:

// volume down           0x68, 0x32, ? ? Volume: 10:1E:


// up button pressed     0x68, 0x3B, 0x01, 0x06
// up button held        0x68, 0x3B, 0x11, 0x16
// up button released    0x68, 0x3B, 0x21, 0x26

// down button pressed   0x68, 0x3B, 0x08, 0x0F
// down button held      0x68, 0x3B, 0x18, 0x1F
// down button released  0x68, 0x3B, 0x28, 0x2F


// R/T button            0xC8, 0x01, 0x9A        R/T button:01:9A:
// phone button          0xC8, 0x3B
                        // press                3B:80:27:
                        // hold                3B:90:37:
                        // release                3B:A0:07:

byte checksum(byte data[], int length){
    byte crc = 0;
    for(int i=0;i<length;i++){
        crc ^= data[i];
    }
    return crc;
}

#ifdef DEBUG
void print_packet(){
    Serial.println("kbus packet: ");
    Serial.print("sender ID: ");
    Serial.println(kbus_from, HEX);
    Serial.print("message length: ");
    Serial.println(kbus_length, HEX);
    Serial.print("receiver ID: ");
    Serial.println(kbus_to, HEX);
    Serial.println("message: ");
    for(int i = 1; i < kbus_length; i++){
        Serial.println(kbus_data[i], HEX);
    }
}

void print_part(int start){
    for(int i = start; i < kbus_length; i++){
        Serial.print(kbus_data[i], HEX);
        Serial.print(":");
    }
    Serial.println("");
}
#endif


//takes a string and prints the first 11 characters to the display
//need to fix scrolling later
void kbus_print(String message){
    int i = 0;
    int len = message.length();
    if (len > 11) len = 11;

    byte out_data[20] = { 0 };
    out_data[0] = 0xC8; //sending from phone
    out_data[1] = 4 + len + 1; // length: 4 static bytes, the message, and the checksum
    out_data[2] = 0x80; //sending to radio
    out_data[3] = 0x23;
    out_data[4] = 0x42;
    out_data[5] = 0x32;

    for(i=0;i<len;i++){
        out_data[6+i] = message.charAt(i);
    }

    out_data[ 1 + out_data[1] ] = checksum(out_data, 2 + out_data[1]);

    int out_len = 2 + out_data[1]; //will always transmit right number of bytes
    kbus.write(out_data, out_len);
    delay(200); //low priority messages, idle for 200ms after transmission
    Serial.print("kbus send data: ");
    for(i=0;i<out_len;i++){
        Serial.print(out_data[i], HEX);
        Serial.print(":");
    }
    Serial.println("");
    return;
}



//press a button for 200ms
void press(int pin){
    digitalWrite(pin, HIGH);
    delay(200);
    digitalWrite(pin, LOW);
}
