#include <LiquidCrystal.h>
#include <SoftwareSerial.h>


#define DEBUG 1

#define EN_PIN 2

//portability, can easily change serial ports globally
HardwareSerial & kbus = Serial1;

// HardwareSerial & usb = Serial;
usb_serial_class & usb = Serial;

// HardwareSerial & bc127 = Serial1;


//teensy 2.0 needs rx and tx on 0,1,2,3,4,13,1415
SoftwareSerial bc127(2, 3); // RX, TX teensy
//leonardo supports RX: 8, 9, 10, 11, 14 (MISO), 15 (SCK), 16 (MOSI).
// SoftwareSerial bc127(8, 9); // RX, TX leonardo

byte kbus_data[32] = { 0 };

// LiquidCrystal lcd(18,17,16,15,14,13,12);

String message = "adcurtin";
// String message = "123456789ab"; //max length message

String title = "";
String artist = "";
String album = "";

//read buffer
String bc127_buffer = "";

String display_buffer[8] = { "" };
int buf_i = 0;
int buf_len = 0;

unsigned long tref = 0, pref = 0;

void setup(){

    //serial tx for kbus
    pinMode(0, INPUT_PULLUP);

#ifdef DEBUG
    usb.begin(9600);
    pinMode(11, OUTPUT);
    digitalWrite(11, HIGH);
    delay(5000);
    digitalWrite(11, LOW);
    usb.println("started");
#endif

    bc127.begin(9600);

    bc127_command("RESET");
    tref = millis();

    // lcd.begin(20, 2);
    // lcd.setCursor(0,1);
    // lcd.print("           xxxxxxxxx");

    delay(1000);

    pinMode(EN_PIN, OUTPUT);
    digitalWrite(EN_PIN, HIGH);

    kbus.begin(9600, SERIAL_8E1);
    kbus_print(message);

}

void loop(){

    delay(10);


    int bytes_read = 0;
    bytes_read = read_kbus_packet();
    if(bytes_read > 0){
        // if(kbus_data[2] == 0x68 || kbus_data[2] == 0xC8) {
        //     usb.println("recd packet to radio or phone");
        //     print_packet();
        // }
        parse_packet();
    }

    //don't reset buffer between reads //not thread safe
    //read bc127
    while(bc127.available())
    {
        char tmp = bc127.read();
        bc127_buffer += tmp;
        if (bc127_buffer.endsWith('\r')) //finished reading a line
        {
            usb.print("bc127 rx: ");
            usb.print(bc127_buffer);
            //interpret data
            if (bc127_buffer.startsWith("AVRCP_MEDIA TITLE: "))
            {
                //0xc6 is a speaker in the e46 business cd
                title = (char) 0xc6 + bc127_buffer.substring(19, bc127_buffer.length() - 1);  // length() - 1 to trim the /r
                update_buffer(title, 1);
            }
            else if (bc127_buffer.startsWith("AVRCP_MEDIA ARTIST: "))
            {
                //0xc4 is a music note in the e46 business cd
                artist = (char) 0xc4 + bc127_buffer.substring(20, bc127_buffer.length() - 1);  // length() - 1 to trim the /r
                update_buffer(artist, 0);
            }
            else if (bc127_buffer.startsWith("AVRCP_MEDIA ALBUM: "))
            {
                album = bc127_buffer.substring(19, bc127_buffer.length() - 1); // length() - 1 to trim the /r
                // update_buffer(album, 0);

                //we've finished reading in the metadata we care about, print the buffer
                //could move this to "AVRCP_MEDIA PLAYING_TIME(MS): " clause
                // print_buffer();
            }
            else if (bc127_buffer.startsWith("OPEN_OK 11 AVRCP"))
            {
                bc127_command("AVRCP_META_DATA 11");
                tref = millis();
            }
            bc127_buffer = "";
        }
    }

    //print new line of display every 3 seconds
    if (pref + 3000 < millis())
    {
        print_buffer();
    }



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

//interprets a kbus packet and performs actions based on the results
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
                // digitalWrite(PLAY_PIN, HIGH);
                // usb.println("play held");
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

// #ifdef DEBUG
// void print_packet(){
//     usb.print("recieved kbus packet: ");
//     for(int i = 0; i < (2+kbus_data[1]); i++){
//         usb.print(kbus_data[i], HEX);
//         usb.print(".");
//     }
//     usb.println("");
// }

// void print_part(int start){
//     for(int i = start; i < (2+kbus_data[1]); i++){
//         usb.print(kbus_data[i], HEX);
//         usb.print(":");
//     }
//     usb.println("");
// }
// #endif

//takes a string and prints the first 11 characters to the display
//TODO: need to fix timing / bus policy!
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

// emulate what the radio can print, only 11 characters and 1 line
// void radio_print(String message)
// {
//     lcd.setCursor(0,0);
//     lcd.print("           "); //only blank 11 spaces to preserve overwrites
//     lcd.setCursor(0,0);
//     lcd.print(message.c_str()); //print whole string, this function shouldn't be gien a string longer than 11 chars.
// }

//print the next message in the buffer
void print_buffer()
{
    kbus_print(display_buffer[buf_i]);
    buf_i++;
    if (buf_i >= buf_len) buf_i = 0;
    pref = millis();
}

//reset true if we should empty the buffer and start over
void update_buffer(String message, byte reset)
{
    int i = 0;
    if (reset == 1)
    {
        buf_len = 0;
        for (i = 0; i < 8; i++)
        {
            display_buffer[i] = "";
        }
    }

    //"∫Andrew McM" "ahon In The" " Wilderness"
    while (message.length() > 11)
    {
        //copy first 11 characters of message to display buffer (+ null byte?)
        // message.toCharArray(display_buffer[buf_len], 12);
        display_buffer[buf_len] = message.substring(0, 11);

        buf_len++;
        //remove first 11 chars from message (0 indexed)
        message = message.substring(11, message.length());
    }

    //message is now 11 or less characters. copy whole message to buffer
    // message.toCharArray(display_buffer[buf_len], message.length() + 1); //+1 for null byte
    display_buffer[buf_len] = message.substring(0, message.length());
    buf_len++;

    //bounds check on buffer
    if (buf_len >= 8) buf_len = 0;

    return;
}

/* runs a command and waits for result of command
 *
 */
void bc127_command(char message[])
{
    bc127.print(message);
    bc127.print("\r");
    usb.print("bc127 tx: ");
    usb.print(message);
    usb.print("\r");



    if (_serialPort->available() > 0) buffer.concat(char(_serialPort->read()));

    if (buffer.endsWith(EOL))
    {
      if (buffer.startsWith("ER")) return MODULE_ERROR;
      if (buffer.startsWith("OK")) return SUCCESS;
      buffer = "";
    }    
}
