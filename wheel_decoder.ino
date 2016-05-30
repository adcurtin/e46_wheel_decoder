#include <SPI.h>
#include <SdFat.h>
#include <TimeLib.h>

#define VERSION "0.2"

#define NEWLINE_CHAR "\n"

#define DEBUG 1
#define TIMESTAMPS 1
#define SDLOG 1

#define LED_PIN 13

#define EN_PIN 2 //for kline transciever

//portability, can easily change serial ports globally
HardwareSerial & kbus = Serial1;

#ifdef DEBUG
# ifdef SDLOG
const uint8_t chipSelect = 15;
SdFat sd;
SdFile logfile;
// Error messages stored in flash.
#define error(msg) sd.errorHalt(F(msg))
# else
usb_serial_class & logfile = Serial; //teensy 3
# endif
#endif


// HardwareSerial & bc127 = Serial1;


//teensy 2.0 needs rx and tx on 0,1,2,3,4,13,1415
// SoftwareSerial bc127(2, 3); // RX, TX teensy
//leonardo supports RX: 8, 9, 10, 11, 14 (MISO), 15 (SCK), 16 (MOSI).
// SoftwareSerial bc127(8, 9); // RX, TX leonardo
//teensy 3 serial2 is pins rx:9 and tx:10
//teensy 3 serial3 is pins rx:7 and tx:8
HardwareSerial & bc127 = Serial3;
//make sure in increase serial buffer size to catch startup! make it 255
// /Applications/Arduino.app/Contents/Java ./hardware/teensy/avr/cores/teensy3/serial3.c

byte music_playing = 0;

byte kbus_data[257] = { 0 }; //can be 0xFF + 2 long max
//on the teensy 3, we can afford the extra ram
//so the complexity of coding more bounds checking can be saved :)

IntervalTimer display_timer;

byte printMetadata = 1;

volatile byte clearToSend = 1;
IntervalTimer holdoffTimer, metaTimer;
volatile byte getMetadata = 0;

// LiquidCrystal lcd(18,17,16,15,14,13,12);

// String message = "adcurtin";
// String message = "123456789ab"; //max length message

String title = "";
String oldTitle = "";
String artist = "";
String oldArtist = "";
String album = "";

//read buffer
String bc127_buffer = "";

#define DISPLAY_BUFFER_SIZE 10
char display_buffer[DISPLAY_BUFFER_SIZE][12];
// String display_buffer[DISPLAY_BUFFER_SIZE] = { "" };
volatile int buf_i = 0;
volatile int buf_len = 0;
volatile int int_count = 0;

elapsedMillis tref = 0;//, kbus_timeout;//, bc127_timeout = 0;

void setup(){
    #ifdef TIMESTAMPS
    setSyncProvider(getTeensy3Time);
    delay(100);
    #endif

    //serial tx for kbus
    pinMode(1, INPUT_PULLUP);

    bc127.begin(57600);
    // bc127.attachRts(11);
    bc127.clear();
    bc127.print("RESET\r");

    //kbus holdoff interrupt
    pinMode(3, INPUT_PULLUP); // other pin has the pullup.
    attachInterrupt(digitalPinToInterrupt(3), startHoldoff, FALLING);

    pinMode(EN_PIN, OUTPUT);
    digitalWrite(EN_PIN, HIGH);

    kbus.begin(9600, SERIAL_8E1); //for leonardo and teensy 3
    // UCSR1C = 0x26; // manually set register to SERIAL_8E1 for teensy 2 (leonardo too)
    // kbus.begin(9600);


    #ifdef DEBUG
    # ifdef SDLOG
    //open new file on sdcard
    // Initialize the SD card at SPI_HALF_SPEED to avoid bus errors with long wires.
    // use SPI_FULL_SPEED for better performance.
    if (!sd.begin(chipSelect, SPI_HALF_SPEED)) {
        sd.initErrorHalt();
    }

    #  ifdef TIMESTAMPS
    String filename = F("radio-");
    int i;
    filename += year();
    filename += "-";
    i = month();
    if (i < 10) filename += "0";
    filename += i;
    filename += "-";
    i = day();
    if (i < 10) filename += "0";
    filename += i;
    filename += "_";
    i = hour();
    if (i < 10) filename += "0";
    filename += i;
    filename += "-";
    i = minute();
    if (i < 10) filename += "0";
    filename += i;
    filename += "-";
    i = second();
    if (i < 10) filename += "0";
    filename += i;
    filename += ".log";
    if (sd.exists(filename.c_str())){
        error("Can't create file name: file exists!");
    }
    if (!logfile.open(filename.c_str(), O_CREAT | O_WRITE | O_EXCL)) {
          error("file.open failed");
    }
    //set files creation / modified dates
    logfile.timestamp(T_CREATE | T_WRITE, year(), month(), day(), hour(), minute(), second());
    #  else /* TIMESTAMPS */
    char filename[] = "radio-notime-000.log";
    while (sd.exists(filename)) {
        if (filename[15] != '9') {
            filename[15]++;
        } else if (filename[14] != '9') {
            filename[15] = '0';
            filename[14]++;
        } else if (filename[13] != '9') {
            filename[15] = '0';
            filename[14] = '0';
            filename[13]++;
        } else {
            error("Can't create file name: too many files!");
        }
    }
    if (!logfile.open(filename, O_CREAT | O_WRITE | O_EXCL)) {
          error("file.open failed");
    }
    logfile.timestamp(T_CREATE | T_WRITE, 2016, 5, 16, 0, 0, 0);
    #  endif /* TIMESTAMPS */

    Serial.begin(57600); //for errors
    # else /* SDLOG */
    logfile.begin(57600);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    delay(5000);
    digitalWrite(LED_PIN, LOW);
    logfile.write(27);
    logfile.print(F("[2J")); // clear screen
    logfile.write(27); // ESC
    logfile.print(F("[H")); // cursor to home
    # endif /* SDLOG */
    #ifdef TIMESTAMPS
    if (timeStatus()!= timeSet) {
        logfile.print(F("Unable to sync with the internal RTC!\r" NEWLINE_CHAR));
    }
    printTime();
    #endif
    logfile.print(F("started " VERSION " compiled: " __DATE__ " " __TIME__ "\r" NEWLINE_CHAR));
    #endif

    tref = 0;

    // delay(1000);

    kbus_print("adcurtin");

    display_timer.begin(print_buffer, 3000000);

}

void loop(){

    // delay(1);

    int bytes_read = 0;
    bytes_read = read_kbus_packet();
    if(bytes_read > 0){
        #ifdef DEBUG
        // if(kbus_data[2] == 0x68 || kbus_data[2] == 0xC8) {
            // logfile.println("recd packet to radio or phone");
            // print_packet();
        // }
        #endif
        parse_packet();
    }

    if (getMetadata){
        bc127_command("AVRCP_META_DATA 11");
        getMetadata = 0;
    }

    //read bc127
    read_and_parse_bc127_packet();

    #ifdef SDLOG
    if (!logfile.sync() || logfile.getWriteError()) {
        error("write error");
    }
    #endif
}

/* check kbus for data. if any available, block until whole packet is read.
 * returns:
 *      int - number of bytes read.
 * updates:
 *      kbus_data[]     contains kbus packet
 */
int read_kbus_packet(){
    elapsedMillis kbus_timeout = 0;
    if(kbus.available() > 1){
        // kbus_timeout = 0;
        kbus_data[0] = kbus.read(); //first byte of message is ID of sending device
        kbus_data[1] = kbus.read(); //second byte is bytes remaining in packet
    } else {
        return 0; //nothing available, nothing read.
    }

    for(int i = 0; i < kbus_data[1]; i++){
        while(kbus_timeout < 500){
            if(kbus.available()){
                kbus_data[2 + i] = kbus.read();
                break; //out of while loop, read next data
            }
        } //wait for data to be available
    }
    if(kbus_timeout >= 500){
        #ifdef DEBUG
        # ifdef TIMESTAMPS
        printTime();
        # endif
        logfile.print(F("kbus timeout!\r" NEWLINE_CHAR));
        #endif
        kbus.clear(); //we might've gotten out of sync. throw everything away, hopefully not in the middle of a packet
    }

    //verify checksum
    byte crc = checksum(kbus_data, 2 + kbus_data[1]);

    if(kbus_data[1 + kbus_data[1]] != crc){
        kbus.clear(); //we might've gotten out of sync. throw everything away, hopefully not in the middle of a packet
        #ifdef DEBUG
        logfile.print(F("-------------------------\r" NEWLINE_CHAR));
        # ifdef TIMESTAMPS
        printTime();
        # endif
        logfile.print(F("Checksum mismatch! expected: "));
        logfile.print(crc, HEX);
        logfile.print(F(" got: "));
        logfile.println(kbus_data[1 + kbus_data[1]], HEX);
        print_packet();
        logfile.print(F("-------------------------x\r" NEWLINE_CHAR));
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
                    bc127_command("MUSIC 11 FORWARD");
                    metaTimer.begin(metadata_request, 5000000); // query the metadata in 5 seconds
                    break;
                case 0x11: //next held
                case 0x21: //next released
                    break;
                case 0x08: //previous pressed
                    bc127_command("MUSIC 11 BACKWARD");
                    metaTimer.begin(metadata_request, 5000000); // query the metadata in 5 seconds
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
                #ifdef DEBUG
                # ifdef TIMESTAMPS
                printTime();
                # endif
                logfile.print(F("printMetadata = 0\r" NEWLINE_CHAR));
                #endif
            }
            else
            {
                printMetadata = 1;
                #ifdef DEBUG
                # ifdef TIMESTAMPS
                printTime();
                # endif
                logfile.print(F("printMetadata = 1\r" NEWLINE_CHAR));
                #endif
                // bc127_command("AVRCP_META_DATA 11");// not in an interrupt
                getMetadata = 1;
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
                //the normal read will catch the source's response of AVRCP_PLAY/AVRCP_PAUSE and update music_playing
            } else if(kbus_data[4] == 0x90){ //held
                #ifdef DEBUG
                # ifdef TIMESTAMPS
                printTime();
                # endif
                //leave a breadcrumb in the log. It should be uncommon that I hold the phone button.
                logfile.print(F("breadcrumb\r" NEWLINE_CHAR));
                kbus_print("breadcrumb");
                #endif
            } else if(kbus_data[4] == 0xA0){ //released
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
    #ifdef TIMESTAMPS
    printTime();
    #endif
    logfile.print(F("recieved kbus packet: "));
    for(int i = 0; i < (2+kbus_data[1]); i++){
        logfile.print(kbus_data[i], HEX);
        logfile.print(F("."));
    }
    logfile.print(F("\r" NEWLINE_CHAR));
}

void print_part(int start){
    #ifdef TIMESTAMPS
    printTime();
    #endif
    for(int i = start; i < (2+kbus_data[1]); i++){
        logfile.print(kbus_data[i], HEX);
        logfile.print(F(":"));
    }
    logfile.print(F("\r" NEWLINE_CHAR));
}
#endif

//takes a string and prints the first 11 characters to the display
//runs in interrupt, don't use String!
void kbus_print(const char message[]){
    int i = 0;
    int len = strlen(message);
    if (len > 11) len = 11;

    byte out_data[20] = { 0 };
    out_data[0] = 0xC8; //sending from phone
    out_data[1] = 4 + len + 1; // length: 4 static bytes, the message, and the checksum
    out_data[2] = 0x80; //sending to radio
    out_data[3] = 0x23;
    out_data[4] = 0x42;
    out_data[5] = 0x32;

    for(i=0;i<len;i++){
        out_data[6+i] = message[i];
    }

    out_data[ 1 + out_data[1] ] = checksum(out_data, 2 + out_data[1]);

    int out_len = 2 + out_data[1]; //will always transmit right number of bytes

    //wait until it's clear to send the message
    byte waited = 0;
    tref = 0;
    unsigned long wait_duration = 0;
    while (clearToSend == 0) {
        waited = 1; //just set this to true to keep this wait loop as short and uninterrupted as possible
    }
    wait_duration = tref;
    if (clearToSend)
    {
        // kbus.clear(); //there shouldn't be anything on new on the bus if we're clear to send
        kbus.write(out_data, out_len);
        //we should get this data right back
    }
    // delay(2); //low priority messages, idle for 2ms after transmission
    #ifdef DEBUG
    if (waited == 1)
    {
        # ifdef TIMESTAMPS
        printTime();
        # endif
        logfile.print(F("waited for kbus holdoff ~"));
        logfile.print(wait_duration);
        logfile.print(F(" ms\r" NEWLINE_CHAR));
        tref = 0;
        waited = 0;
    }

    // logfile.print("kbus send data: ");
    // for(i=0;i<out_len;i++){
    //     logfile.print(out_data[i], HEX);
    //     logfile.print(":");
    // }
    // logfile.print("\r" NEWLINE_CHAR);

    # ifdef TIMESTAMPS
    printTime();
    # endif
    logfile.print(F("kbus_print: "));
    logfile.print(message);
    logfile.print(F("\r" NEWLINE_CHAR));
    #endif

    return;
}

//print the next message in the buffer
//runs in interrupt
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
    }

    if(int_count >= 10 && music_playing){ //since we're only resetting the display on new data, try every time
        // bc127.clear();
        // bc127_command("AVRCP_META_DATA 11");
        getMetadata = 1;
        int_count = 0;
    }

    int_count++;
}

//reset true if we should empty the buffer and start over
//does not run in interrupt, but updates stuff that is used there
void update_display_buffer(String message, byte reset)
{
    noInterrupts();
    int i = 0;
    if (reset == 1 || buf_len > DISPLAY_BUFFER_SIZE)
    {
        buf_len = 0;
        buf_i = 0;
        for (i = 0; i < DISPLAY_BUFFER_SIZE; i++)
        {
            memset(display_buffer[i], 0, 12);
            // display_buffer[i][0] = NULL; //null the first byte to end the strings
        }
    }

    if (message.length() > 55) message = message.substring(0,55); //limit each data to display cycles so whole message can be displayed

    while (message.length() > 11)
    {
        //copy first 11 characters of message to display buffer
        strncpy(display_buffer[buf_len], message.c_str(),  11);
        // display_buffer[buf_len] = message.substring(0, 11);
        buf_len++;
        //remove first 11 chars from message (0 indexed)
        message = message.substring(11, message.length());
    }

    //message is now 11 or less characters. copy whole message to buffer
    strncpy(display_buffer[buf_len], message.c_str(), message.length());
    // display_buffer[buf_len] = message;
    buf_len++;
    interrupts();
    return;
}

/* runs a command and waits for result of command
 * returns status: 1 for success, 0 for error
 */
byte bc127_command(const char message[])
{
    #ifdef DEBUG
    #ifdef TIMESTAMPS
    printTime();
    #endif
    logfile.print(F("bc127 command: "));
    logfile.print(message);
    logfile.print(F("\r" NEWLINE_CHAR));
    #endif
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
            // logfile.print(tmp);
        }
        if (bc127_timeout > 2000) //timeout after 200ms
        {
            #ifdef DEBUG
            #ifdef TIMESTAMPS
            printTime();
            #endif
            logfile.print(F("timeout reading bc127 command response!\r" NEWLINE_CHAR));
            logfile.print(F("read: \'"));
            logfile.print(bc127_buffer.c_str());
            logfile.print(F("\r" NEWLINE_CHAR));
            #endif
            bc127_buffer = "";
            bc127_timeout = 0;
            break; //out of while loop
        }
    }
    //finished reading a line: bc127_buffer ends with \r

    #ifdef DEBUG
    #ifdef TIMESTAMPS
    printTime();
    #endif
    logfile.print(F("bc127 rx response: "));
    logfile.print(bc127_buffer);
    logfile.print(F(NEWLINE_CHAR));
    #endif
    //interpret data
    if (bc127_buffer.startsWith("ER"))
    {
            #ifdef DEBUG
            #ifdef TIMESTAMPS
            printTime();
            #endif
            logfile.print(F("bc127 command error\r" NEWLINE_CHAR));
            #endif
            return 0;
    }
    if (bc127_buffer.startsWith("OK")) return 1;

    //should never get here
    return 0;
}

void read_and_parse_bc127_packet()
{
    if(!bc127.available()) return;
    bc127_buffer = "";
    elapsedMillis bc127_timeout = 0;
    while(!bc127_buffer.endsWith("\r"))
    {
        if (bc127.available())
        {
            char tmp = bc127.read();
            bc127_buffer += (char) tmp; //bc127.read();
            // logfile.print(tmp);
        }
        if (bc127_timeout > 3000) //timeout after 200ms
        {
            #ifdef DEBUG
            #ifdef TIMESTAMPS
            printTime();
            #endif
            logfile.print(F("read: \'"));
            logfile.print(bc127_buffer.c_str());
            logfile.print(F("\'\r" NEWLINE_CHAR));
            logfile.print(F("timeout reading bc127 packet: "));
            logfile.print(bc127_timeout);
            logfile.print(F("\r" NEWLINE_CHAR));
            #endif
            bc127_buffer = "";
            bc127_timeout = 0;
            return;
        }
    }
    //finished reading a line: bc127_buffer ends with \r

    #ifdef DEBUG
    #ifdef TIMESTAMPS
    printTime();
    #endif
    logfile.print(F("bc127 rx: "));
    logfile.print(bc127_buffer);
    logfile.print(F(NEWLINE_CHAR));
    #endif
    //interpret data
    if (bc127_buffer.startsWith("AVRCP_PLAY 11") ||
        bc127_buffer.startsWith("A2DP_STREAM_START 10")
       )
    {
        if(!music_playing)
        {
            metaTimer.begin(metadata_request, 5000000);// query the metadata in 5 seconds.
        }
        music_playing = 1;
        #ifdef DEBUG
        #ifdef TIMESTAMPS
        printTime();
        #endif
        logfile.print(F("music_playing = 1\r" NEWLINE_CHAR));
        #endif
    }
    else if (bc127_buffer.startsWith("AVRCP_PAUSE 11") ||
             bc127_buffer.startsWith("A2DP_STREAM_SUSPEND 10") ||
             bc127_buffer.startsWith("CLOSE_OK 11 AVRCP") ||
             bc127_buffer.startsWith("LINK_LOSS 10 1")
            )
    {
        music_playing = 0;
        #ifdef DEBUG
        #ifdef TIMESTAMPS
        printTime();
        #endif
        logfile.print(F("music_playing = 0\r" NEWLINE_CHAR));
        #endif
    }
    else if (bc127_buffer.startsWith("AVRCP_MEDIA TITLE: "))
    {
        //0xc6 is a speaker in the e46 business cd
        title = (char) 0xc6 + bc127_buffer.substring(19, bc127_buffer.length() - 1);  // length() - 1 to trim the /r
        //fix UTF encodings
        sanitize(title);

    }
    else if (bc127_buffer.startsWith("AVRCP_MEDIA ARTIST: "))
    {
        //0xc4 is a music note in the e46 business cd
        artist = (char) 0xc4 + bc127_buffer.substring(20, bc127_buffer.length() - 1);  // length() - 1 to trim the /r
        //fix UTF encodings
        sanitize(artist);
    }
    else if (bc127_buffer.startsWith("AVRCP_MEDIA ALBUM: "))
    {
        album = bc127_buffer.substring(19, bc127_buffer.length() - 1); // length() - 1 to trim the /r
        // update_display_buffer(album, 0);
        //fix UTF encodings
        sanitize(album);

        if ( title.compareTo(oldTitle) != 0 || artist.compareTo(oldArtist) != 0)
        {
            update_display_buffer(title, 1);
            update_display_buffer(artist, 0);
            oldTitle = title;
            oldArtist = artist;
            #ifdef DEBUG
            // logfile.print("metadata changed. updated display and copied new to old\r" NEWLINE_CHAR);
            #endif
        }
        //we've finished reading in the metadata we care about, print the buffer
        //could move this to "AVRCP_MEDIA PLAYING_TIME(MS): " clause
    }
    else if (bc127_buffer.startsWith("OPEN_OK 11 AVRCP"))
    {
        //automatically start music on powerup / connection
        bc127_command("MUSIC 11 PLAY");

        //this probably shouldn't go here, it could be called twice. instead, always run it and just dont' print before music is playing
        // display_timer.begin(print_buffer, 3000000);
    }
    bc127_buffer = "";
}

//runs in interrupt
void startHoldoff()
{
    // if clear to send is false, already in holdoff period
    if(clearToSend){
        clearToSend = 0;
        holdoffTimer.begin(endHoldoff, 2000);
        holdoffTimer.priority(64);
        // logfile.print("start holdoff. cts = 0\r" NEWLINE_CHAR);
    }
}

//runs in interrupt
void endHoldoff()
{
    clearToSend = 1;
    holdoffTimer.end(); //just a one shot timer
    // logfile.print("end holdoff. cts = 1\r" NEWLINE_CHAR);
}

//runs in interrupt
void metadata_request()
{
    metaTimer.end(); //single shot
    #ifdef DEBUG
    #ifdef TIMESTAMPS
    printTime();
    #endif
    logfile.print(F("meta request timer\r" NEWLINE_CHAR));
    #endif
    // bc127_command("AVRCP_META_DATA 11");
    getMetadata = 1;
}

void sanitize(String & str){
    int i;

    i = str.indexOf(F("\xe2"));
    while(i != -1){
        //following is for determining exactly what special chars are
        #ifdef DEBUG
        #ifdef TIMESTAMPS
        printTime();
        #endif
        logfile.print(F("sanitize chars :"));
        for (int j = 0; j < 3; j++){
            logfile.print(str.charAt(i+j), HEX);
            logfile.print(F(":"));
        }
        logfile.print(F("\r" NEWLINE_CHAR));
        #endif


        String c = "";
        String unicode = str.substring(i+1, i+3);
        if (unicode == "\x80\x99") c = "'"; //"\xe2\x80\x99" is the unicode ' char, aka â€™
        else if (unicode == "\x80\x9c" || unicode == "\x80\x9d") c = "\"";
        // else if (unicode == "\x80\xa2") c = "\xc3"; //hopefully this won't mess up below. It's the dot in the e46

        if (c.length() > 0) str = str.substring(0,i) + c + str.substring(i+3);
        else str = str.substring(0,i) + str.substring(i+2);
        i = str.indexOf(F("\xe2"));
    }

    i = str.indexOf(F("\xc3"));
    while(i != -1){
        #ifdef DEBUG
        #ifdef TIMESTAMPS
        printTime();
        #endif
        logfile.print(F("sanitize chars :"));
        for (int j = 0; j < 2; j++){
            logfile.print(str.charAt(i+j), HEX);
            logfile.print(F(":"));
        }
        logfile.print(F("\r" NEWLINE_CHAR));
        #endif
        String c = "";
        switch (str.charAt(i+1)) {
            case 0xa6: //æ
                c = "ae";
                break;
            case 0xa9: //é
                c = "e";
                break;
            case 0xad: //æ
                c = "i";
                break;
            case 0xb3: //ó
                c = "o";
                break;
            case 0xb6: //ö
                c = "\xa5"; //this one's in the radio :)
                break;
            default:
                c = "";
                break;
        }
        if (c.length() > 0) str = str.substring(0,i) + c + str.substring(i+2);
        // else str = str.substring(0,i) + str.substring(i+1);
        i = str.indexOf(F("\xc3"));
    }
}

#ifdef TIMESTAMPS
time_t getTeensy3Time()
{
  return Teensy3Clock.get();
}

void printTime(){
    int i;
    logfile.print(F("["));
    logfile.print(year());
    logfile.print(F("-"));
    i = month();
    if (i < 10) logfile.print(F("0"));
    logfile.print(i);
    logfile.print(F("-"));
    i = day();
    if (i < 10) logfile.print(F("0"));
    logfile.print(i);
    logfile.print(F(" "));
    i = hour();
    if (i < 10) logfile.print(F("0"));
    logfile.print(i);
    logfile.print(F(":"));
    i = minute();
    if (i < 10) logfile.print(F("0"));
    logfile.print(i);
    logfile.print(F(":"));
    i = second();
    if (i < 10) logfile.print(F("0"));
    logfile.print(i);
    logfile.print(F("."));
    //stuff to get the last 3 digits of millis, including any leading zeros. probably better than % 1000 and other work for leading 0s
    String ms = millis();
    i = ms.length();
    logfile.print(ms.substring(i-3,i));
    logfile.print(F("] "));
}
#endif
