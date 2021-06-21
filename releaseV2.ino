#include <Wire.h> //added by Gerry
#include "RTClib.h" // comes from the package RTClib - using version 1.13.0
#include <SD.h>
// this is NOT the typical AdaFruit_FONA library.
// it is the AdaFruit_FONA library with a few private methods made public (because I couldn't find an alternate)
#include "Adafruit_FONA.h"
#include <SoftwareSerial.h>
// see this file for most configuration options
#include "configHW2.h"

// Adafruit FONA AT guide: https://www.elecrow.com/download/SIM800%20Series_AT%20Command%20Manual_V1.09.pdf

// all non-arbitrary configuration has been moved to configHW#.h

// error levels (These must be different values)
// 1 - info - something that the code can work around
// 2 - ignorable trouble - something that should get fixed, but isn't a problem on its own
// 3 - retry-able trouble - something that has been stopped, but will be tried again later
// 4 - fatal error - something that forces a full system stop (most likely never to occur)
#define ERR_INFO               1 // do not change!
#define ERR_TROUBLE            2 // do not change!
#define ERR_RETRY              3 // do not change!
#define ERR_FATAL              4 // do not change!

// ignore the next 2 lines, they help convert int define statements to Strings
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define OK_REPLY               F("OK")
#define QUEUE_OVERFLOW         "\0"

// create a connection to the SIM chip over softwareserial
SoftwareSerial fonaSS = SoftwareSerial(FONA_TX, FONA_RX);
SoftwareSerial *fonaSerial = &fonaSS;

/////////////////////////////////////////////////////////////
// End of configuration //// beginning of global variables //
/////////////////////////////////////////////////////////////

// initialize the Adafruit_FONA.h library by passing it a reset pin
Adafruit_FONA fona = Adafruit_FONA(FONA_RST);

// initialize the RTC library/chip
// you CAN change the library to another type of RTC, but the enabled condition likely will also change
RTC_LIBRARY rtc;

// global variable for tracking the last minute the log file was written
uint8_t lastMinuteLogged;

// tracks the last high reading from the flowmeter to detect edges in signal
volatile unsigned long meterWasHigh = 0;

// tracks the number of pulses since lastMinuteLogged
volatile unsigned int meterReadCounter = 0;

// tracks the total number of pulses that have been recorded while awake
unsigned uint32_t linesLogged = 0;

// tracks number of retry-able trouble errors recorded, if more then MAX_RETRIES
// we will reboot the device, hoping for improvement.
uint8_t errorsCaught = 0;

// uploadQueue - files not yet successfully uploaded will be in this array
// the first spot stores the working file. The second spot and beyond is the upload-queue/backlog
String uploadQueue[1+FILE_BACKLOG_SIZE];

////////////////////////////////////////////////////////////////
// End of global variables //// beginning of helper functions //
////////////////////////////////////////////////////////////////

// calling this function will reset everything (I believe), at least it will rerun setup
void (*resetFunc) (void) = 0; // a reset function located at position 0 in memory

// pad an integer into a 2 digit String -> if <10, will add a zero.
String paddedString(uint8_t inputNum) {
  String out;
  if (inputNum < 10) {
    out = "0";
  } else {
    out = "";
  }
  out += String(inputNum);
  return out;
}

// attempt to read the RTC clock time into a string
// if not initialized, returns an empty string
// reads from the RTC a DateTime then turns it into a string
// in format yy/MM/dd,hh:mm:ss
String getDateString() {
  if (RTC_CONDITION) {
    DateTime now = rtc.now();
    char dString[20];
    dString[0] = now.year() / 1000 + '0';
    dString[1] = now.year() % 1000 / 100 + '0';
    dString[2] = now.year() % 100 / 10 + '0';
    dString[3] = now.year() % 10 + '0';
    dString[4] = '/';
    dString[5] = now.month() / 10 + '0';
    dString[6] = now.month() % 10 + '0';
    dString[7] = '/';
    dString[8] = now.day() / 10 + '0';
    dString[9] = now.day() % 10 + '0';
    dString[10] = ' ';
    dString[11] = now.hour() / 10 + '0';
    dString[12] = now.hour() % 10 + '0';
    dString[13] = ':';
    dString[14] = now.minute() / 10 + '0';
    dString[15] = now.minute() % 10 + '0';
    dString[16] = ':';
    dString[17] = now.second() / 10 + '0';
    dString[18] = now.second() % 10 + '0';
    dString[19] = '\0';
    return String(dString);
    //
    // String date = String();
    // date += String(now.year()); // I don't padd this as it comes padded by default
    // date += '/';
    // date += paddedString(now.month());
    // date += '/';
    // date += paddedString(now.day());
    // date += ' ';
    // date += paddedString(now.hour());
    // date += ':';
    // date += paddedString(now.minute());
    // date += ':';
    // date += paddedString(now.second());
    // return date;
  }
  // returns a blank string if rtc not ready
  return String();
}

// read a string in format "yy/MM/dd,hh:mm:ss±zz" into separate integer variables
// note that year is uint16_t and others are uint8_t
// allows us to set the RTC from the FONA's network time (which comes in this format)
void parseDateString(char dateArray[], uint8_t *year, uint8_t *month, uint8_t *day, uint8_t *hour, uint8_t *min, uint8_t *sec) {
  *year = (10 * (dateArray[1] - '0')) + (dateArray[2] - '0');
  *month = (10 * (dateArray[4] - '0')) + (dateArray[5] - '0');
  *day = (10 * (dateArray[7] - '0')) + (dateArray[8] - '0');
  *hour = (10 * (dateArray[10] - '0')) + (dateArray[11] - '0');
  *min = (10 * (dateArray[13] - '0')) + (dateArray[14] - '0');
  *sec = (10 * (dateArray[16] - '0')) + (dateArray[17] - '0');
}

// takes a String (or similar) and moves it into a buffer using the first size positions
char* stringify(String input, char outputBuffer[], uint8_t size) {
  input.toCharArray(outputBuffer, size);
  return outputBuffer;
}

// in an appropriate manner report/log an error of level level. See above for log levels
void reportError(int level, const __FlashStringHelper* message) {
  Serial.print(F("Fona recent response: "));
  Serial.println(fona.replybuffer);

  for (uint8_t i = 0; i < level; i++) {
    debug(F("ERROR!!"));
  }
  Serial.print(F("LEVEL: "));
  Serial.println(level);

  Serial.print(F("Error message: "));
  Serial.println(message);

  if (level >= ERR_TROUBLE) {
    File err = SD.open(SD_ERR_FILENAME, FILE_WRITE);
    if (err) {
      err.print(getDateString());
      err.print(F(": "));
      err.println(message);
    }
    err.close();
  }
  if (level >= ERR_RETRY) {
    errorsCaught++;
    debug(String("This is error #") + errorsCaught + String(". Will reboot after ") + MAX_RETRIES);
    if (errorsCaught > MAX_RETRIES) {
      debug(F("REBOOTING due to repeated errors"));
      delay(30000);
      resetFunc();
    }
  }
  if (level >= ERR_FATAL) {
    debug(F("FATAL ERROR!!!"));
    debug(F("REBOOTING due to fatal error"));
    delay(30000);
    resetFunc();
  }
}

// print toPrint out to both Serial and SD as appropriate
void debug(String toPrint) {
  #ifdef SERIAL_DEBUG || SD_DEBUG
    String date = getDateString();

    #if SERIAL_DEBUG
      if (Serial) { // if the serial port is open/ready
        Serial.println(date + ": " + toPrint);
      }
    #endif

    #if SD_DEBUG
      File debug = SD.open(SD_LOG_FILENAME, FILE_WRITE);
      if (debug) { // if opened successfully
          debug.println(date + ": " + toPrint);
          debug.close();
      } //else { // this wouldn't be good, but what else are we going to do?
        // Serial.println(F("SD - failed to open "SD_LOG_FILENAME));
    //}
    #endif
  #endif
}

// // this is a macro that is used within uploadFile
// // the macro takes an AT command as argument
// // and sends it. It verifies the response is OK_REPLY and
// // if it is not reports an INFO level error and returns false
// // uploadFile returning false implies failed to upload
// #define FONA_SEND_IMPORTANT(x)\
// if (!fona.sendCheckReply(F(#x)), OK_REPLY) {\
//   reportError(ERR_INFO, F("Failed to send \"" #x "\", aborting file upload"));\
//   return false;\
// }

// reads and uploads sdFilename to ftpPath/ftpFilename returns true only if success
boolean uploadFile(String sdFilename, FONAFlashStringPtr ftpPath, String ftpFilename) {
  if (digitalRead(FONA_PWR_STATUS) != HIGH) { // if FONA is asleep
    debug("FONA offline. Booting");
    if (!bootFonaTest()) {// start the FONA
      return false;
    }
  }
  
  // start FTP
  debug("Uploading " + sdFilename + " to " + ftpPath + ftpFilename);
  File toSend = SD.open(sdFilename, FILE_READ);
  if (toSend) { // toSend is now open
    if (!fona.sendCheckReply(F("AT+SAPBR=0,1"), OK_REPLY)) {
      // close past connections before starting a new one
      reportError(1, F("preparing GPRS connection: AT+SAPBR=0,1 failed"));
    }
    if (!fona.sendCheckReply(F("AT+SAPBR=1,1"), OK_REPLY)) {
      reportError(2, F("opening GPRS connection: AT+SAPBR=1,1 failed"));
      return false;
    }
    if (!fona.sendCheckReply(F("AT+FTPCID=1"), OK_REPLY)) {
      reportError(2, F("preparing to upload: AT+FTPCID=1 reported an error"));
      // return false;
    }
    if (!fona.sendCheckReply(F("AT+FTPSERV=\"" FTP_ADDRESS "\""), OK_REPLY)) {
      reportError(2, F("preparing to upload: AT+FTPSERV=\"" FTP_ADDRESS "\" reported an error"));
      return false;
    }
    if (!fona.sendCheckReply(F("AT+FTPPORT=" FTP_PORT), OK_REPLY)) {
      reportError(2, F("preparing to upload: AT+FTPPORT=" FTP_PORT " reported an error"));
      return false;
    }
    if (!fona.sendCheckReply(F("AT+FTPUN=\"" FTP_USERNAME "\""), OK_REPLY)) {
      reportError(2, F("preparing to upload: AT+FTPUN=\"xxxx\" reported an error"));
      return false;
    }
    if (!fona.sendCheckReply(F("AT+FTPPW=\"" FTP_PASSWORD "\""), OK_REPLY)) {
      reportError(2, F("preparing to upload: AT+FTPPW=\"****\" reported an error"));
      return false;
    }
    if (!fona.sendCheckReply(F("AT+FTPPUTOPT=\"APPE\""), OK_REPLY)) {// set to append mode
      reportError(2, F("preparing to upload: AT+FTPPUTOPT=\"APPE\" reported an error"));
      return false;
    }

    if (!fona.sendCheckReplyQuoted(F("AT+FTPPUTPATH="), ftpPath, OK_REPLY)) {
      reportError(2, F("preparing to upload: AT+FTPPUTPATH=\"....\" reported an error"));
      return false;
    }

    char dynCommandBuffer[COMMAND_BUFFER_SIZE];
    // apply the filename
    if (!fona.sendCheckReply(stringify("AT+FTPPUTNAME=\"" + ftpFilename + "\"", dynCommandBuffer, COMMAND_BUFFER_SIZE), OK_REPLY)) {
      reportError(2, F("preparing to upload: AT+FTPPUTNAME=\"....\" reported an error"));
      return false;
    }

    // open the connection
    if (!fona.sendCheckReply(F("AT+FTPPUT=1"), OK_REPLY)) {
      reportError(2, F("opening FTP connection: AT+FTPPUT=1 reported an error"));
      return false;
    }

    // AT+FTPPUT=1 responds with OK and follows that with a useful number
    // that I want to grab (It's the limit on upload sizes)
    // this takes a minute sometimes to respond, thus the timeout being huge.
    fona.readline(NETWORK_REG_DELAY * 1000, true); // read the actual response to above

    uint8_t offset = 0;
    // find the first + that leads the response by looking through up to COMMAND_BUFFER_SIZE
    while (fona.replybuffer[offset++] != '+' && offset < COMMAND_BUFFER_SIZE);

    if (offset >= COMMAND_BUFFER_SIZE) {
      reportError(2, F("opening FTP connection: did not receive valid response"));
      return false;
    }

    // if we find this, then we have opened a successful FTP connection
    char replyHeader[12] = "FTPPUT: 1,1,";

    // check that the + found above is followed by FTPPUT: 1,1,xxxx
    // after this, the next chars are the value we hope to snag (where the xs)
    uint8_t i;
    for (i = offset; i < offset+12; i++) {
      if (replyHeader[i - offset] != fona.replybuffer[i]) {
        reportError(2, F("opening FTP connection: did not parse valid response"));
        return false;
      }
    }

    // starting at i+offset, read the number that follows (and is followed by a newline)
    int maxSize = 0;
    while (isDigit(fona.replybuffer[i])) {
      maxSize *= 10;
      maxSize += fona.replybuffer[(i++)] - '0';
    }

    // now we need to upload in chunks of size maxSize
    int fileSize = toSend.size();

    Serial.print(F("File size: "));
    Serial.print(fileSize);
    Serial.print(F(" - Batch size: "));
    Serial.println(maxSize);

    char alpha = ' ';

    while (fileSize > 0) {
      int sendSize = min(maxSize, fileSize);

      Serial.print(F("Sending: "));
      Serial.println(sendSize);

      // now we need to figure out the response we want
      fona.sendCheckReply(F("AT+FTPPUT=2,"), sendSize, OK_REPLY); //we don't really check the response because I believe this command doesn't respond consistently
      // char dynCommandBuffer2[COMMAND_BUFFER_SIZE];
      // if (!fona.sendCheckReply(stringify("AT+FTPPUT=2," + String(fileSize), dynCommandBuffer, COMMAND_BUFFER_SIZE),
      //   stringify("+FTPPUT: 2," + String(fileSize), dynCommandBuffer2, COMMAND_BUFFER_SIZE))) return false;

      fileSize -= sendSize; // set fileSize to zero to escape the loop
      while (toSend.available() && sendSize-- > 0) {
        alpha = toSend.read();
        fona.write((char) alpha);
        // this next line will print out data as it is uploaded
        // Serial.print((char) alpha);
      }
      // check for "+FTPUT: 1,1,maxSize" as response
    }
    if (!fona.sendCheckReply(F("AT+FTPPUT=2,0"), OK_REPLY)) {
      reportError(2, F("closing FTP upload: AT+FTPPUT=2,0 reported an error"));
      // if this errors, I will accept that the upload has occured -> possibly an issue
    }

    return true;
  } else {
    reportError(3, F("Failed to open file for uploading"));
    return false;
  }
}

void clearQueue(int position) {
  uploadQueue[position] = "";
}

void pushQueue(String name) {
  for (int i = FILE_BACKLOG_SIZE; i > 0; i--) {
    uploadQueue[i] = uploadQueue[i - 1]; // shift right
  }
  // now uploadQueue[0] = uploadQueue[1] so we can update uploadQueue[0] to filename
  uploadQueue[0] = name;
}

// gets a value from the Queue
// returns QUEUE_OVERFLOW if position is past end of queue
String getQueue(int position) {
  if (position > FILE_BACKLOG_SIZE || position < 0)
    return String(QUEUE_OVERFLOW);
  return uploadQueue[position];
}

// note: should only be called following bootFonaTest()
// reads uploadQueue and uploads appropriate files over ftp using uploadFile
// does not upload current file (uploadQueue[0])
void uploadFilesFromQueue() {
  int i = FILE_BACKLOG_SIZE; // start at the oldest un-uploaded file and work to newest
  String data;
  do {
    data = getQueue(i--); // retrieves last item, then decrements
    if (data != "" && data != QUEUE_OVERFLOW) {
      if (uploadFile(data, F(FTP_PATH), FTP_FILENAME)) {
        clearQueue(i); // successfull upload, remove it from the list
        debug(F("Successfull upload"));
      } else {
        debug(F("Incomplete upload. Will try again later"));
      }  
        // wait before uploading next file
      delay(NETWORK_REG_DELAY * 1000);
    }
  } while (i > 0); // stop at i=1 as i=0 may still be a work in progress
 
  #if FTP_LOGGING
    if (SD.exists(SD_ERR_FILENAME)) {
      if (uploadFile(SD_ERR_FILENAME, F(FTP_LOG_PATH), FTP_ERR_FILE)) {
        SD.remove(SD_ERR_FILENAME);
      }
    }
  #endif
}

// writes to the file at filename on the SD card and append data followed by newline
void writeToFile(String filename, String data) {
  // write the line to filename
//  Serial.print("Writing to file: ");
//  Serial.println(filename);
  File file = SD.open(filename, FILE_WRITE);
  if (!file) {
    reportError(3, F("SD - failed to open & write data"));
  } else {
    file.println(data);
  }
  file.close();
}

// helper functions for loop
// called only when LOG_DATA_CONDITION is true
// calculates metrics, calls writeToFile on today's file
void logData() {
  unsigned int v;
  for (int x = 0; x < 10; x++) // analogRead 10 times to balance for funky reads
    v += analogRead(A0);

  float voltage = A0_TO_VOLTAGE_FORMULA;
  int gals = METER_TO_GALS_FORMULA;

  // get the date
  DateTime now = rtc.now();

  // writes to file on SD of name "yymmddxx.DAT" where xx is UNIT_ID of this arduino
  // it will append a string of the following format (quotes around date included)
  // ""yyyy-mm-dd hh:mm:ss",lineNum,battVoltage,pulseCounts,gals" -- uses writeToFile()
  String filename = FILENAME_FORMULA; // actually defined above in header

  // make sure filename is a piece of uploadQueue for later upload
  if (filename != getQueue(0)) {
    pushQueue(filename);
  }

  // create the line
  String line = "\""; // surround timestamp with quotes
  line += String(now.year()) + "-" + paddedString(now.month()) + "-" + paddedString(now.day()) + " "; // the date
  line += paddedString(now.hour()) + ":" + paddedString(now.minute()) + ":" + paddedString(now.second()) + "\","; // the time
  line += String(++linesLogged) + "," + String(voltage) + "," + String(meterReadCounter) + "," + String(gals); // the data

  // log a line to the file
  writeToFile(filename, line); // write line to file
  Serial.println(line);

  // reset meterReadCounter for next set of 2 minutes
  meterReadCounter = 0;
}

boolean bootFonaTest() {
  debug(F("FONA - starting"));
  // wait for the FONA to signal it is powered up
  uint8_t failCounter = 0;
  while (digitalRead(FONA_PWR_STATUS) != HIGH && failCounter++ < MAX_RETRIES) { // toggle FONA key pin to wake it up
    digitalWrite(FONA_PWR_CONTROL, LOW);
    delay(2000);
    digitalWrite(FONA_PWR_CONTROL, HIGH);
    delay(2000);
  }
  if (failCounter >= MAX_RETRIES) {
    // if we tried MAX_RETRIES times, we failed to boot fona, just stop here
    reportError(3, F("FONA - Unable to power on"));
    return false;
  }

  fonaSerial->begin(4800); // open up the connection to the FONA
  if (!fona.begin(*fonaSerial)) {
    reportError(3, F("FONA - Unable to open serial connection"));
    return false;
  }

  // this just enables the FONA RTC's time sync
  #if RESET_RTC_TIME
    // enable both Network time sync and NTP time sync
    // I use bitwise & so as to force both to be computed, but still apply an and gate (prevents short circuit)
    if (!fona.enableNetworkTimeSync(true) & !fona.enableNTPTimeSync(true, F("pool.ntp.org"))) { // if both fail, then crash. Otherwise, continue
      reportError(1, F("FONA - Failed network time sync"));
    } // continue by reading the FONA time into the RTC
  #endif
  
  // give the FONA network connection
  fona.setGPRSNetworkSettings(F(FONA_APN), F(FONA_USERNAME), F(FONA_PASSWORD));

  uint8_t signal = fona.getRSSI();
  failCounter = 0;

  // wait for RSSI to reach a level
  // signal == 99 means it doesn't know the signal
  // signal < RSSI_THRESHOLD is weak signal reception, and 12 is an arbitrary value chosen
  while(signal < RSSI_THRESHOLD && signal != 99 && failCounter++ < MAX_RETRIES) {
    delay(NETWORK_REG_DELAY * 1000); // delay NETWORK_REG_DELAY seconds before checking again
    signal = fona.getRSSI();
    debug("RSSI read as : " + String(signal));
  }
  if (failCounter >= MAX_RETRIES || signal == 99) {
    reportError(2, F("FONA - Failed to establish a satisfactory RSSI - try lowering the threshold or improving the antenna"));
    return false;
  }

  Serial.println(F("FONA - Waiting for network"));
  Serial.println(F("     Network registration: "));
  Serial.println(F("       0 Not registered, not searching"));
  Serial.println(F("       1 Registered, home network"));
  Serial.println(F("       2 Not registered, searching"));
  Serial.println(F("       3 Registration denied"));
  Serial.println(F("       4 Unknown"));
  Serial.println(F("       5 Registered, roaming"));
  Serial.println(F("Roaming Enabled: " STR(ENABLE_ROAMING)));

  // wait for network registration: (AT+CREG?) = 1 or 5 (1 = registered, 5 = roaming)
  signal = fona.getNetworkStatus(); // reset this before the following checks
  failCounter = 0; // reset before the following

  #ifdef ENABLE_ROAMING
    // case 1) ENABLE_ROAMING != true: loop until signal == 1. case 2) ENABLE_ROAMING = true: loop until signal = 1 or 5
    while ((!(signal == 1 || (ENABLE_ROAMING && signal == 5))) && failCounter++ < MAX_RETRIES)
  #else
    while (signal != 1 && signal != 5 && failCounter++ < MAX_RETRIES) // ENABLE_ROAMING assumed as false so case 1 from above
  #endif
  {
    delay(NETWORK_REG_DELAY * 1000); // delay NETWORK_REG_DELAY seconds before checking again
    signal = fona.getNetworkStatus();
    debug("FONA network registration: " + String(signal));
  }

  // check for failure - depending on ENABLE_ROAMING state
  #ifdef ENABLE_ROAMING
    if (failCounter >= MAX_RETRIES || (!(signal == 1 || (ENABLE_ROAMING && signal == 5)))) {
      reportError(2, F("FONA - Failed to register to network"));
      if (signal == 5) debug(F("FONA - Registered to ROAMING with ROAMING disabled"));
      return false;
    }
  #else
    if (failCounter >= MAX_RETRIES || (!(signal == 1 || (signal == 5)))) {
      reportError(2, F("FONA - Failed to register to network"));
      return false;
    }
  #endif

  ////////////////////////////////////////
  ///////////// testing FONA /////////////
  ////////////////////////////////////////
  // get the network time
  debug(F("FONA - Testing connection"));

  // enabling 2G network connection (the data is over GPRS)
  failCounter = 0; // reset before the following
  while (!fona.enableGPRS(true) && failCounter++ < MAX_RETRIES) {
    reportError(1, F("FONA - Failed to enable GPRS"));
    fona.enableGPRS(false); // reset
    delay(NETWORK_REG_DELAY * 1000);
  }
  if (failCounter >= MAX_RETRIES) {
    reportError(2, F("FONA - Failed to enable GPRS connection"));
  }

  debug(F("FONA - Ready"));
  return true;
}

void sleepFona() {
  uint8_t failCounter = 0;
  while (digitalRead(FONA_PWR_STATUS) != LOW && failCounter < MAX_RETRIES) { // toggle FONA key pin to wake it up
    digitalWrite(FONA_PWR_CONTROL, LOW);
    delay(2000);
    digitalWrite(FONA_PWR_CONTROL, HIGH);
    delay(3000);
  }

  // if we tried MAX_RETRIES times, we failed to sleep the fona, just stop here
  if (failCounter >= MAX_RETRIES) {
    reportError(1, F("FONA - Power Save FAILED!"));
  } else {
    debug(F("FONA - Powered OFF"));
  }
}

void syncRTC() {
  debug(F("FONA - Syncing FONA RTC with onboard RTC"));
  // the actual RTC date sync occurs here
  char dateArray[23]; // buffer for time string
  fona.getTime(dateArray, 23); // read time into buffer

  // convert into integers from the getTime string response
  uint8_t year, month, day, hour, min, sec = 0;
  parseDateString(dateArray, &year, &month, &day, &hour, &min, &sec);

  // set the RTC with the time parsed above
  rtc.adjust(DateTime(year, month, day, hour, min, sec));
  debug(F("FONA - Time reset to network time"));
}

void announceBoot() {
  #if FTP_LOGGING
    debug(F("FONA - Uploading BOOT signal"));
    debug(F("Creating message file"));
    writeToFile(FTP_LOG_FILE, STR(UNIT_ID) + String(" booted at ") + getDateString() + String(" with current RSSI of: ") + String(fona.getRSSI()));
    if (uploadFile(String(FTP_LOG_FILE), F(FTP_LOG_PATH), String(FTP_LOG_FILE))) {
      debug(F("FONA - Successfully sent boot signal"));
      // debug(F("Uploading debug.log"));
      // uploadFile(SD_FILENAME, FTP_LOG_PATH, SD_FILENAME);
    }
    SD.remove(FTP_LOG_FILE); // clear the file for next time
  #endif
}
//////////////////////////////////////////////////////////////
// End of helper functions //// beginning of body functions //
//////////////////////////////////////////////////////////////

void setup() {
  ////////////////////////////////////////
  //////// open input/output pins ////////
  ////////////////////////////////////////
  pinMode(LED, OUTPUT); // allow LED toggling
  digitalWrite(LED, LOW); // make sure LED is off
  pinMode(SD_CS_PIN,OUTPUT);//added by Gerry
  pinMode(FLOW_METER, INPUT_PULLUP); // flowmeter connection

  pinMode(FONA_PWR_STATUS, INPUT); // read FONA state (on/off)
  pinMode(FONA_PWR_CONTROL, OUTPUT); // control the FONA state

  analogReference(INTERNAL1V1); // added as part of battery measuring system

  ////////////////////////////////////////
  /////////// open serial port ///////////
  ////////////////////////////////////////
  Serial.begin(SERIAL_BAUD);
  Serial.println(F(": Serial - bus opened"));

  ////////////////////////////////////////
  ///////// connect the SD card //////////
  ////////////////////////////////////////
  // always initialize the sd output -- because we will use it for data storage as well

  if (!SD.begin(SD_CS_PIN)) {
    reportError(4, F("SD Card - could not initialize"));
  } else {
    debug(F("SD card - Initialized"));
  }
  // print out if SD debug is enabled
  Serial.println(F(": SD card logging: " STR(SD_DEBUG)));

  ////////////////////////////////////////
  //////////// open RTC port /////////////
  ////////////////////////////////////////
  if (!rtc.begin()) {
    reportError(4, F("RTC - could not initialize"));
  } else {
    debug(F("RTC - Initialized"));
  }

  ////////////////////////////////////////
  ///////////// booting FONA /////////////
  ////////////////////////////////////////

  // power the FONA power controller pins
  digitalWrite(FONA_PWR_CONTROL, HIGH);
  delay(2000); // give FONA a moment to wake up as it may or may not do so here

  if (bootFonaTest()) {// this runs a test on the fona while booting it up
    #if FTP_LOGGING
      announceBoot();
    #endif
    #if RESET_RTC_TIME
      syncRTC();
    #endif
    uploadFilesFromQueue();
  }

  lastMinuteLogged = rtc.now().minute(); // record the startup time

  sleepFona(); // put the FONA to sleep

  debug(F("RTC - testing tick")); // this test blinks the onboard LED for ~10 seconds
  uint8_t stopSeconds = (rtc.now().second() + 10) % 60;
  digitalWrite(LED, HIGH);
  int counter = 0;
  while (rtc.now().second() != stopSeconds && counter < MAX_RETRIES * 100) {
    counter++;
    delay(100);
  } // wait till RTC ticks 10 seconds
  // this allows it to take MAX_RETRIES "arduino seconds" for the RTC to tick 10 seconds.
  // this means that if the arduino "clock" is MAX_RETRIES fast, it will still not fail
  if (counter >= MAX_RETRIES * 10*10) {
    reportError(3, F("RTC - did not tick"));
  }
  digitalWrite(LED, LOW);

  #if USE_INTERRUPTS
    // enable interupts
    debug(F("Enabling Flowmeter interupts"));
    attachInterrupt(digitalPinToInterrupt(FLOW_METER), meterPulse, CHANGE);
    interrupts();
  #endif

  debug("UNIT_ID: " + UNIT_ID);
  delay(10);
  debug("I will upload to ftp://" FTP_ADDRESS FTP_PATH " " + paddedString(UNIT_ID) + " minutes after midnight");
  delay(10);
  debug(F("Compile Date: " __DATE__ " " __TIME__));
  delay(10);

  debug(F("Initialization completed"));
  debug(F("----------------------------------------------------"));
}

void loop() {
  // poll the time
  DateTime now = rtc.now();

  // call meterPulse every loop if interupts are disabled
  #if USE_INTERRUPTS
  #else // if USE_INTERUPTS is not defined, implies false
    meterPulse();
  #endif

  // try to log
  if (LOG_DATA_CONDITION) { // condition defined above
    lastMinuteLogged = now.minute();
    logData(); // no args and no return because everything is global variables
  }

  // try to upload date if appropriate
  if (UPLOAD_DATA_CONDITION) {
    debug(F("Upload Data Condition met"));
    if (bootFonaTest())
      uploadFilesFromQueue();
    sleepFona();
  }

  //////////////////////////////////////////////////
  #if SERIAL_DEBUG
    if (Serial.available()) {
      // flush serial pipe
      // while (Serial.available()) Serial.read();
      // while (!Serial.available());

      char command = Serial.read();

      String fileUploader;
      int i;
      String data;
      switch (command) {
        case 'a':
          Serial.println("Enter AT command: ");
          while (Serial.available()) Serial.read();
          while (!Serial.available());
          delay(50);
          fileUploader = "";
          while (Serial.available()) {
            fileUploader += (char) Serial.read();
          }
          char dynCommandBuffer[COMMAND_BUFFER_SIZE];
          fona.sendCheckReply(stringify(fileUploader, dynCommandBuffer, COMMAND_BUFFER_SIZE), OK_REPLY);
          break;
        case 'd':
          Serial.println("Enter filename: ");
          while (Serial.available()) Serial.read();
          while (!Serial.available());
          delay(50);
          fileUploader = "";
          while (Serial.available())
            fileUploader += (char) Serial.read();
          if (SD.exists(fileUploader)) {
            SD.remove(fileUploader);
            Serial.println("File deleted");
          } else {
            Serial.println("File does not exist");
          }
          break;
        case 'i':
          Serial.println("Enter filename: ");
          while (Serial.available()) Serial.read();
          while (!Serial.available());
          delay(50);
          fileUploader = "";
          while (Serial.available())
            fileUploader += (char) Serial.read();
          if (SD.exists(fileUploader)) {
            File t;
            t = SD.open(fileUploader);
            Serial.print("File size: ");
            Serial.println(t.size());
            t.close();
          } else {
            Serial.println("File does not exist");
          }
          break;
        case 'p':
          Serial.println("Enter filename: ");
          while (Serial.available()) Serial.read();
          while (!Serial.available());
          delay(50);
          fileUploader = "";
          while (Serial.available())
            fileUploader += (char) Serial.read();
          if (SD.exists(fileUploader)) {
            File t;
            t = SD.open(fileUploader);
            Serial.println("---------------");
            while (t.available()) {
              Serial.print((char) t.read());
            }
            Serial.println("---------------");
            t.close();
          } else {
            Serial.println("File does not exist");
          }
          break;
        case 'u':
          Serial.println("Enter filename to upload: ");
          while (Serial.available()) Serial.read();
          while (!Serial.available());
          delay(50);
          fileUploader = "";
          while (Serial.available()) {
            fileUploader += (char) Serial.read();
          }
          if (uploadFile(fileUploader, F("/debug/"), fileUploader))
            Serial.println("Upload completed to /debug/");
          else
            Serial.println("Upload failed");
          break;
        case 't':
          uploadFilesFromQueue();
          Serial.println("Uploads completed");
          break;
        case 'f':
          sleepFona();
          Serial.println("FONA OFF");
          break;
        case 'm':
          Serial.print("Current meter edge count: ");
          Serial.println(meterReadCounter);
          break;
        case 'o':
          boolean stateMachine;
          stateMachine = bootFonaTest();
          Serial.print("Received State: ");
          Serial.println(stateMachine);
          break;
        case 'q':
          Serial.println("Current upload QUEUE: ");

          i = 0; // don't upload position 0 (the working file)
          data = QUEUE_OVERFLOW;
          do {
            data = getQueue(i++);
            Serial.println("\"" + data + "\"");
            // if (data != "") {
              // uploadFile(data, FTP_PATH, data);
            // }
          } while (data != QUEUE_OVERFLOW);

          Serial.println();
          break;
        case 'r':
          Serial.println("RESTARTING!");
          delay(1000);
          resetFunc();
          break;
        case 's':
          Serial.print("Current meter pin state: ");
          Serial.println(digitalRead(FLOW_METER));
          break;
        case 'x':
          Serial.println("Filename to insert: ");
          while (Serial.available()) Serial.read();
          while (!Serial.available());
          delay(50);
          fileUploader = "";
          while (Serial.available())
            fileUploader += (char) Serial.read();
          pushQueue(fileUploader);
          break;
        default:
          Serial.println("--------------------------");
          Serial.println("Enter a debug command: ");
          Serial.println("send AT command - a");
          Serial.println("delete file - d");
          Serial.println("size file - i");
          Serial.println("print file - p");
          Serial.println("upload file - u");
          Serial.println("upload queue - t");
          Serial.println("Disable FONA - f");
          Serial.println("show meter counts - m");
          Serial.println("Boot and Test Fona - o");
          Serial.println("Print uploadQueue - q");
          Serial.println("restart - r");
          Serial.println("show meter state - s");
          Serial.println("insert into upload Queue - x");
          Serial.println("--------------------------");
          break;
      }

      // flush serial pipe
      while (Serial.available()) Serial.read();
    }
  #endif
}

// ISR for whenever the meter sends an edge to the arduino
// will increment meterReadCounter if appropriate
void meterPulse() {
  int meterReading = digitalRead(FLOW_METER); // read from the flow meter pin
  if (meterReading == HIGH) {
    meterWasHigh = millis();
    return;
  }

  if ((meterReading == LOW) && ((meterWasHigh + METER_MIN_PULSE) < millis()) && (meterWasHigh != 0)) { // a falling edge detected
    meterWasHigh = 0;
    meterReadCounter++;
     digitalWrite(LED, HIGH); //blink when pulse detected --Added by Gerry
     delay(500);// this is a terrible idea. It should not be here, I'll leave it for now, but am very likely to move it
     // in a future version.
     // This function (which is called by interrupts) should be as short as possible (and not have delays/writes)
      digitalWrite(LED, LOW); //--Added by Gerry
    return;
  }
}

///////////////////////////
// End of body functions //
///////////////////////////
