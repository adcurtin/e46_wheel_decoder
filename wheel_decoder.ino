// #include <LiquidCrystal.h>
// #include <SoftwareSerial.h>
// #include <TimerThree.h>


// #define SERIAL_RX_BUFFER_SIZE 256 //for leonardo


#define DEBUG 1

#define LED_PIN 13

#define EN_PIN 2
//portability, can easily change serial ports globally
HardwareSerial & kbus = Serial1;

// Serial_ & usb = Serial; //leonardo
usb_serial_class & usb = Serial; //teensy 2 and 3

// HardwareSerial & bc127 = Serial1;


//teensy 2.0 needs rx and tx on 0,1,2,3,4,13,1415
// SoftwareSerial bc127(2, 3); // RX, TX teensy
//leonardo supports RX: 8, 9, 10, 11, 14 (MISO), 15 (SCK), 16 (MOSI).
// SoftwareSerial bc127(8, 9); // RX, TX leonardo
//teensy 3 serial2 is pins rx:9 and tx:10
HardwareSerial & bc127 = Serial2;

byte music_playing = 0;

byte kbus_data[32] = { 0 };

IntervalTimer display_timer;

byte printMetadata = 1;

volatile byte clearToSend = 1;
IntervalTimer holdoffTimer, metaTimer;

// LiquidCrystal lcd(18,17,16,15,14,13,12);

// String message = "adcurtin";
// String message = "123456789ab"; //max length message

String title = "";
String artist = "";
String album = "";

//read buffer
String bc127_buffer = "";

#define DISPLAY_BUFFER_SIZE 10
String display_buffer[DISPLAY_BUFFER_SIZE] = { "" };
volatile int buf_i = 0;
volatile int buf_len = 0;
volatile int int_count = 0;

elapsedMillis tref = 0, dot;//, bc127_timeout = 0;

void setup(){

    //serial tx for kbus
    pinMode(1, INPUT_PULLUP);

    bc127.begin(57600);

#ifdef DEBUG
    usb.begin(57600);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    delay(5000);
    digitalWrite(LED_PIN, LOW);
    usb.write(27);
    usb.print("[2J"); // clear screen
    usb.write(27); // ESC
    usb.print("[H"); // cursor to home
    usb.print("started\r");
#endif

    // bc127.attachRts(11);
    bc127.clear();
    // bc127_command("RESET");
    bc127.print("RESET\r");

    tref = 0;

    //kbus holdoff
    pinMode(3, INPUT_PULLUP); // other pin has the pullup.
    attachInterrupt(digitalPinToInterrupt(3), startHoldoff, FALLING);


    // lcd.begin(20, 2);
    // lcd.setCursor(0,1);
    // lcd.print("           xxxxxxxxx");


    delay(1000);

    pinMode(EN_PIN, OUTPUT);
    digitalWrite(EN_PIN, HIGH);

    kbus.begin(9600, SERIAL_8E1); //for leonardo and teensy 3
    // UCSR1C = 0x26; // manually set register to SERIAL_8E1 for teensy 2 (leonardo too)
    // kbus.begin(9600);
    kbus_print("adcurtin");
    // update_display_buffer("adcurtin", 1);

}

void loop(){

    // delay(1);

    int bytes_read = 0;
    bytes_read = read_kbus_packet();
    if(bytes_read > 0){
        // if(kbus_data[2] == 0x68 || kbus_data[2] == 0xC8) {
            // usb.println("recd packet to radio or phone");
            // print_packet();
        // }
        parse_packet();
    }


    //read bc127
    read_and_parse_bc127_packet();
    // if(dot > 250)
    // {
    //     usb.print('.');
    //     dot = 0;
    // }

    //print new line of display every 3 seconds
    // if (pref + 3000 < millis())
    // {
    //     print_buffer();
    // }



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
        usb.print("-------------------------\r");
        usb.print("Checksum mismatch! expected: ");
        usb.print(crc, HEX);
        usb.print(" got: ");
        usb.println(kbus_data[1 + kbus_data[1]], HEX);
        print_packet();
        usb.print("-------------------------x\r");
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
                    // press(NEXT_PIN);
                    // usb.println("next pressed");
                    bc127_command("MUSIC 11 FORWARD");
                    break;
                case 0x11: //next held
                case 0x21: //next released
                    break;
                case 0x08: //previous pressed
                    // press(PREV_PIN);
                    // usb.println("previous pressed");
                    bc127_command("MUSIC 11 BACKWARD");
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
            if(printMetadata){
                printMetadata = 0;
            }
            else
            {
                printMetadata = 1;
                bc127_command("AVRCP_META_DATA 11");
            }

        } else if(kbus_data[3] == 0x3B){ // phone button
            if(kbus_data[4] == 0x80){ // pressed
                //need to handle holding of this.
                if (music_playing){
                    bc127_command("MUSIC 11 PAUSE");
                }
                else {
                    bc127_command("MUSIC 11 PLAY");
                }
                //the normal read will catch the source's response of AVRCP_PLAY \ AVRCP_PAUSE and update music_playing
            } else if(kbus_data[4] == 0x90){ //held
                //if held, this is hold the play button, the press function will
                //release it when the button is released.
                // digitalWrite(PLAY_PIN, HIGH);
                // usb.println("play held");
            } else if(kbus_data[4] == 0xA0){ //released
                // press(PLAY_PIN);
                // usb.println("play pressed");
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
    usb.print("\r");
}

void print_part(int start){
    for(int i = start; i < (2+kbus_data[1]); i++){
        usb.print(kbus_data[i], HEX);
        usb.print(":");
    }
    usb.print("\r");
}
#endif

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

    //wait until it's clear to send the message
    while (clearToSend == 0) {
        //TODO remove this for production
        #ifdef DEBUG
        usb.print("waiting for kbus holdoff\r");
        #endif
    }
    if(clearToSend) kbus.write(out_data, out_len);
    // delay(2); //low priority messages, idle for 2ms after transmission
    // usb.print("kbus send data: ");
    // for(i=0;i<out_len;i++){
    //     usb.print(out_data[i], HEX);
    //     usb.print(":");
    // }
    // // usb.println("");

    usb.print("kbus_print: ");
    usb.print(message.c_str());
    usb.print("\r");
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
    if (music_playing && printMetadata)
    {
        kbus_print(display_buffer[buf_i]);
        buf_i++;
    }

    //buf_len = 9
    if (buf_i >= buf_len){
        buf_i = 0;
        if(int_count >= 10){ //only get new data when done printing a message
            // bc127.clear();
            bc127_command("AVRCP_META_DATA 11");
            int_count = 0;
        }
    }

    int_count++;
}

//reset true if we should empty the buffer and start over
void update_display_buffer(String message, byte reset)
{
    int i = 0;
    if (reset == 1)
    {
        buf_len = 0;
        buf_i = 0;
        for (i = 0; i < 8; i++)
        {
            display_buffer[i] = "";
        }
    }

    if (message.length() > 55) message = message.substring(0,55); //limit each data to display cycles so whole message can be displayed

    //"∫Andrew McM" "ahon In The" " Wilderness"
    while (message.length() > 11)
    {
        //copy first 11 characters of message to display buffer (+ null byte?)
        // message.toCharArray(display_buffer[buf_len], 12);
        display_buffer[buf_len] = message.substring(0, 11);

        buf_len++;
        if (buf_len > DISPLAY_BUFFER_SIZE) buf_len = 0; //don't handle gracefully
        //remove first 11 chars from message (0 indexed)
        message = message.substring(11, message.length());
    }

    //message is now 11 or less characters. copy whole message to buffer
    // message.toCharArray(display_buffer[buf_len], message.length() + 1); //+1 for null byte
    display_buffer[buf_len] = message.substring(0, message.length());
    buf_len++;

    // //bounds check on buffer
    // if (buf_len >= 8) buf_len = 0;

    return;
}

/* runs a command and waits for result of command
 * returns status: 1 for success, 0 for error
 */
byte bc127_command(const char message[])
{
    usb.print("bc127 command: ");
    usb.print(message);
    usb.print("\r");
    bc127.print(message);
    bc127.print("\r");

    bc127_buffer = "";
    elapsedMillis bc127_timeout = 0;
    while(!bc127_buffer.endsWith("\r"))
    {
        if (bc127.available())
        {
            char tmp = bc127.read();
            bc127_buffer += (char) tmp; //bc127.read();
            // usb.print(tmp);
        }
        if (bc127_timeout > 2000) //timeout after 200ms
        {
            usb.print("timeout reading bc127 command response!\r");
            usb.print("read: \'");
            usb.print(bc127_buffer.c_str());
            usb.print("\r");
            bc127_buffer = "";
            bc127_timeout = 0;
        }
    }
    //finished reading a line: bc127_buffer ends with \r

    usb.print("bc127 rx response: ");
    usb.print(bc127_buffer);
    //interpret data
    if (bc127_buffer.startsWith("ER"))
    {
            usb.print("bc127 command error\r");
            return 0;
    }
    if (bc127_buffer.startsWith("OK")) return 1;
    bc127_buffer = "";

}

void read_and_parse_bc127_packet()
{
    if(!bc127.available()) return;
    bc127_buffer = "";
    elapsedMillis bc127_timeout = 0;
    // usb.print("bc127_timeout:");
    // usb.print(bc127_timeout);
    // usb.print("\r");
    while(!bc127_buffer.endsWith("\r"))
    {
        if (bc127.available())
        {
            char tmp = bc127.read();
            bc127_buffer += (char) tmp; //bc127.read();
            // usb.print(tmp);
        }
        else
        {
            // usb.print("asdlkjh\r");
        }
        if (bc127_timeout > 3000) //timeout after 200ms
        {
            usb.print("read: \'");
            usb.print(bc127_buffer.c_str());
            usb.print("\'\r");
            usb.print("timeout reading bc127 packet: ");
            usb.print(bc127_timeout);
            usb.print("\r");
            bc127_buffer = "";
            bc127_timeout = 0;
            return;
        }
    }
    //finished reading a line: bc127_buffer ends with \r

    usb.print("bc127 rx: ");
    usb.print(bc127_buffer);
    //interpret data
    if (bc127_buffer.startsWith("AVRCP_PLAY 11") || bc127_buffer.startsWith("A2DP_STREAM_START 10"))
    {
        if(!music_playing)
        {
            metaTimer.begin(metadata_request, 5000000);
        }
        music_playing = 1;
        usb.print("music playing\r");
    }
    else if (bc127_buffer.startsWith("AVRCP_PAUSE 11") || bc127_buffer.startsWith("A2DP_STREAM_SUSPEND 10"))
    {
        music_playing = 0;
        usb.print("music paused\r");
    }
    else if (bc127_buffer.startsWith("AVRCP_MEDIA TITLE: "))
    {
        //0xc6 is a speaker in the e46 business cd
        title = (char) 0xc6 + bc127_buffer.substring(19, bc127_buffer.length() - 1);  // length() - 1 to trim the /r
        update_display_buffer(title, 1);
        // usb.print("parsed title\r");
    }
    else if (bc127_buffer.startsWith("AVRCP_MEDIA ARTIST: "))
    {
        //0xc4 is a music note in the e46 business cd
        artist = (char) 0xc4 + bc127_buffer.substring(20, bc127_buffer.length() - 1);  // length() - 1 to trim the /r
        update_display_buffer(artist, 0);
        // usb.print("parsed artist\r");
    }
    else if (bc127_buffer.startsWith("AVRCP_MEDIA ALBUM: "))
    {
        album = bc127_buffer.substring(19, bc127_buffer.length() - 1); // length() - 1 to trim the /r
        // update_display_buffer(album, 0);
        // usb.print("parsed album\r");
        // bc127.clear(); //clear the rest of the buffer, usually "AVRCP_MEDIA PLAYING_TIME(MS):"" which often times out

        //we've finished reading in the metadata we care about, print the buffer
        //could move this to "AVRCP_MEDIA PLAYING_TIME(MS): " clause
        // print_buffer();
    }
    else if (bc127_buffer.startsWith("OPEN_OK 11 AVRCP"))
    {
        //automatically start music. make sure to do this after getting metadata so we can read the response
        bc127_command("MUSIC 11 PLAY");
        // read_and_parse_bc127_packet(); //catch the response to command and update music_playing

        // bc127_command("AVRCP_META_DATA 11");
        // tref = 0;
        // Timer3.initialize(3000000);
        // Timer3.attachInterrupt(print_buffer);
        display_timer.begin(print_buffer, 3000000);
    }
    bc127_buffer = "";
}

void startHoldoff()
{
    // if clear to send is false, already in holdoff period
    if(clearToSend){
        clearToSend = 0;
        holdoffTimer.begin(endHoldoff, 2000);
        holdoffTimer.priority(64);
        // usb.print("start holdoff. cts = 0\r");
    }
}

void endHoldoff()
{
    clearToSend = 1;
    holdoffTimer.end(); //just a one shot timer
    // usb.print("end holdoff. cts = 1\r");
}

void metadata_request()
{
    metaTimer.end(); //single shot
    usb.print("meta request timer\r");
    bc127_command("AVRCP_META_DATA 11");
}
