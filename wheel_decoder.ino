

#define DEBUG 1

#define EN_PIN 2

#define PLAY_PIN 5
#define NEXT_PIN 6
#define PREV_PIN 7

//portability, can easily change serial ports globally
HardwareSerial & kbus = Serial1;
HardwareSerial & usb = Serial;

byte kbus_data[32] = { 0 };

String message = "adcurtin";
// String message = "123456789ab"; //max length message


void setup(){

    pinMode(PLAY_PIN, OUTPUT);
    pinMode(NEXT_PIN, OUTPUT);
    pinMode(PREV_PIN, OUTPUT);
    digitalWrite(PLAY_PIN, LOW);
    digitalWrite(NEXT_PIN, LOW);
    digitalWrite(PREV_PIN, LOW);

    pinMode(0, INPUT_PULLUP);

#ifdef DEBUG
    usb.begin(57600);
    pinMode(13, OUTPUT);
    digitalWrite(13, HIGH);
    delay(5000);
    digitalWrite(13, LOW);
    usb.println("started");
#endif


    pinMode(EN_PIN, OUTPUT);
    digitalWrite(EN_PIN, HIGH);

    kbus.begin(9600, SERIAL_8E1);
    kbus_print(message);

}

void loop(){

    delay(10);

    // press(PLAY_PIN);
    // usb.println("pressed play");
    // delay(2000);

    // press(PLAY_PIN);
    // usb.println("pressed play");

    // delay(10000);
    // press(NEXT_PIN);
    // usb.println("pressed next");


    int bytes_read = 0;
    bytes_read = read_kbus_packet();
    if(bytes_read > 0){
        // if(kbus_data[2] == 0x68 || kbus_data[2] == 0xC8) {
        //     usb.println("recd packet to radio or phone");
        //     print_packet();
        // }
        parse_packet();
    }
    // delay(200);


}

/* check kbus for data. if any available, block until whole packet is read.
 * returns:
 *      int - number of bytes read.
 * updates:
 *      kbus_data[]     contains kbus packet
 */
int read_kbus_packet(){
    if(kbus.available() > 1){
        kbus_data[0] = kbus.read(); //first byte of message is ID of sending device
        kbus_data[1] = kbus.read(); //second byte is bytes remaining in packet
        // usb.println("read 2 bytes from kbus packet");
    } else {
        return 0; //nothing available, nothing read.
    }

    for(int i = 0; i < kbus_data[1]; i++){
        while(kbus.available() == 0){} //wait for data to be available
        kbus_data[2 + i] = kbus.read();
    }

    //verify checksum
    byte crc = checksum(kbus_data, 2 + kbus_data[1]);

    if(kbus_data[1 + kbus_data[1]] != crc){
#ifdef DEBUG
        usb.println("-------------------------");
        usb.print("Checksum mismatch! expected: ");
        usb.print(crc, HEX);
        usb.print(" got: ");
        usb.println(kbus_data[1 + kbus_data[1]], HEX);
        print_packet();
        usb.println("-------------------------x");
#endif
        return 0; //no valid bytes read. can't really handle errors.
    }

    return 2 + kbus_data[1];
}

void parse_packet(){
    if(kbus_data[2] == 0x68){ //command sent to radio
        if(kbus_data[3] == 0x32) { //volume button
            if(kbus_data[4] == 0x11){ //volume up

            } else if(kbus_data[4] == 0x10){ //volume down

            }
        } else if (kbus_data[3] == 0x3B){ //track button
            switch (kbus_data[4]) {
                case 0x01: //next pressed
                    press(NEXT_PIN);
                    usb.println("next pressed");
                    break;
                case 0x11: //next held
                case 0x21: //next released
                    break;
                case 0x08: //previous pressed
                    press(PREV_PIN);
                    usb.println("previous pressed");
                    break;
                case 0x18: //previous held
                case 0x28: //previous released
                    break;
                default:
                    break;
            }
        }
    } else if (kbus_data[2] == 0xC8){ //command send to phone unit
        if(kbus_data[3] == 0x01){ // R/T button pressed
        } else if(kbus_data[3] == 0x3B){ // phone button
            if(kbus_data[4] == 0x80){ // pressed
                //need to handle holding of this.

            } else if(kbus_data[4] == 0x90){ //held
                //if held, this is hold the play button, the press function will
                //release it when the button is released.
                digitalWrite(PLAY_PIN, HIGH);
                usb.println("play held");
            } else if(kbus_data[4] == 0xA0){ //released
                press(PLAY_PIN);
                usb.println("play pressed");
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

//packet length includes the checksum
byte checksum(byte data[], int packet_length){
    byte crc = 0;
    //the last byte of the packet is the checksum, don't include the previous checksum in the calculation
    for(int i=0; i < (packet_length - 1); i++){
        crc ^= data[i];
    }
    return crc;
}

#ifdef DEBUG
void print_packet(){
    usb.print("recieved kbus packet: ");
    for(int i = 0; i < (2+kbus_data[1]); i++){
        usb.print(kbus_data[i], HEX);
        usb.print(".");
    }
    usb.println("");
}

void print_part(int start){
    for(int i = start; i < (2+kbus_data[1]); i++){
        usb.print(kbus_data[i], HEX);
        usb.print(":");
    }
    usb.println("");
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
    delay(2); //low priority messages, idle for 2ms after transmission
    usb.print("kbus send data: ");
    for(i=0;i<out_len;i++){
        usb.print(out_data[i], HEX);
        usb.print(":");
    }
    usb.println("");
    return;
}

//press a button for 200ms
void press(int pin){
    digitalWrite(pin, HIGH);
    delay(200);
    digitalWrite(pin, LOW);
}
