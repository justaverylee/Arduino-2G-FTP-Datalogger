// this config is for the original v1 setup

// this ID is used to identify the unit when uploading files over FTP
#define UNIT_ID                20 // this number must be in range 0 <= ID < 100 
    // because I haven't fixed it to work with numbers more then 2 digits. LMK if this is something I should rework

/////////////////////////////////////////////////////////////////////////////////////////////
////////////                    What it does settings                       /////////////////
/////////////////////////////////////////////////////////////////////////////////////////////

// commenting these lines will disable the respective debug outputs
#define SERIAL_DEBUG           true // send debug data over the USB serial bus
#define SERIAL_BAUD            115200 // the baud rate for the USB serial connection
#define SD_DEBUG               true // create a DEBUG.log file on the SD card
#define SD_LOG_FILENAME        "debug.log" // name of file to write debug data to
#define SD_ERR_FILENAME        "error.log" // name of file to write error logs to
// if set to true, upload important status messages (such as boots and errors)

/////////////////////////////////////////////////////////////////////////////////////////////
////////////                    Network/upload settings                     /////////////////
/////////////////////////////////////////////////////////////////////////////////////////////


// LOCAL filenames
// a conditional statement that when evaluated to true will create a log file and allow checking if it is time to upload
#define LOG_DATA_CONDITION     (now.minute() % 2 == 0) && (now.minute() != lastMinuteLogged) // now.minute() = current minutes, "lastMinuteLogged" = last minute this condition was true
// a conditional statement that when true will trigger an ftp upload of the last days file
// note: there may be some down time in recording during upload
#define UPLOAD_DATA_CONDITION  (now.minute() + (now.hour()*60) == UNIT_ID) // implies upload UNIT_ID minutes after midnight
//#define UPLOAD_DATA_CONDITION (now.minute() == 1) // for debug (upload every hour)
// a formula (should likely use the DateTime object named "now" as well as the "paddedString(int)" class defined below this header)
// anytime LOG_DATA_CONDITION is true and FILENAME_FORMULA creates a new string will lead to an upload next time UPLOAD_DATA_CONDITION
#define FILENAME_FORMULA       paddedString(now.year() - 2000) + paddedString(now.month()) + paddedString(now.day()) + paddedString(UNIT_ID) + ".DAT"
// #define FILENAME_FORMULA       paddedString(now.month()) + paddedString(now.day()) + paddedString(now.hour()) + ".debug" // for testing
// length of filename must be recorded to allocate the space in EEPROM to store the names
#define FILENAME_MAX_LENGTH    16 // please add at least 1 for the null char

// FTP credentials and paths
// will upload FILENAME_FORMULA to FTP_FILENAME whenever UPLOAD_DATA_CONDITION is true
// ... and FILENAME_FORMULA has created a different file
#define FTP_ADDRESS            "ip-address-goes-here"
#define FTP_PORT               "21" // note: this must be quoted as a String
#define FTP_USERNAME           "ftp-username-goes-here"
#define FTP_PASSWORD           "ftp-password-goes-here"
#define FTP_PATH               "ftp-upload-path-goes-here" // this should be the path to a directory (ends in "/") // was "/shared/data/"
//#define FTP_FILENAME           data // this will upload to UPLOAD_DATA_CONDITION (see top of source file)
#define FTP_FILENAME           STR(UNIT_ID) ".data" // file on server to upload data to. If constant, it will append to the same file
// below is for uploading the boot log (whenever device is booted or specific errors)
#define FTP_LOGGING            true // see FTP_LOG_PATH and FTP_LOG_FILE defined below
#define FTP_LOG_PATH           "ftp-log-upload-path-goes-here" // path to a directory (ending in "/") // was "/shared/log/"
#define FTP_LOG_FILE           STR(UNIT_ID) ".log" // path to report boots too
#define FTP_ERR_FILE           STR(UNIT_ID) ".err" // path to report errors to
// I considered allowing upload of debug.log file on bootup but that would
// require a large transmission of data which I would prefer not to do.
// If you'd like this feature, let me know.

// apn definition -> identification for what the SIM should register to
// depends on your SIM card/plan
#define FONA_APN               "m2mglobal"
#define FONA_USERNAME          ""
#define FONA_PASSWORD          ""
// this is a value that determines the minimum RSSI value that data will be transfered using.
// the value is not directly the RSSI. See the FONA AT guide page 94 under AT+CSQ. higher values
// mean it requires a stronger signal to make a transmission
// this must be in range 0-31 inclusive, but should likely not be near either end.
#define RSSI_THRESHOLD         11 // default 11 (completely arbitrary)
#define ENABLE_ROAMING         true // default true


// this will cause RTC time to be updated to FONA time whenever FONA is booted
#define RESET_RTC_TIME         true // default true

// this is the maximum number of times to try before accepting failure
// in theory this should never be compared to, but anything that is being waited
// on will only be checked MAX_RETRIES times before moving on or reporting error
#define MAX_RETRIES            10
// some AT commands being sent to the FONA are generated dynamically. These commands
// must be generated into a buffer before being sent to the FONA. the following sets
// the size of this buffer. If the filenames or paths you upload to are long, you may
// need to make this longer. Larger values will use more memory in some methods
#define COMMAND_BUFFER_SIZE    70
// time in seconds before rechecking network signal/status
// note: the total time before assuming no network available is
//  NETWORK_REG_DELAY * MAX_RETRIES
#define NETWORK_REG_DELAY      10 // units: seconds

/////////////////////////////////////////////////////////////////////////////////////////////
////////////                    Hardware specifications                     /////////////////
/////////////////////////////////////////////////////////////////////////////////////////////

// port definitions
#define FONA_RX                2  // the rx pin on FONA
#define FONA_TX                11 // the tx pin on FONA
#define FONA_RST               4 // the rst pin on FONA
#define FONA_PWR_STATUS        7 // the pwr pin on FONA
#define FONA_PWR_CONTROL       6 // the key pin on FONA
#define SD_CS_PIN              // purposefully blank (change it if you connect CS pin elsewhere)
#define LED                    8

// battery monitoring
#define ANALOG_BATTERY_PIN     A0
// a formula to convert from the A0 pin voltage to
// the real voltage that the value v should represent
#define A0_TO_VOLTAGE_FORMULA  (v / 10) * 15.70588 * 1.1 / 1024 // use "v" to represent a0's pin value in formula


// flow meter monitoring/settings
// if USE_INTERRUPTS is true, FLOW_METER must be on a pin that allows interupts
// see table at bottom of link for what pins allow interrupts
// https://www.arduino.cc/reference/en/language/functions/external-interrupts/attachinterrupt/
#define FLOW_METER             9 // wired to 19 on v2
// a formula to convert from the number of pulses from the flowmeter
// to the number of gallons that should represent over a 2 minute period
#define METER_TO_GALS_FORMULA  meterReadCounter * 5 // use "meterReadCounter" as the number of pulses from flowmeter
// use interrupts to monitor the flowmeter - will result in less monitoring downtime
#define USE_INTERRUPTS         false // default true
// METER_MIN_PULSE is the minimum length that the meter must read a HIGH
// signal before it returning to a LOW signal will count as a meter read
#define METER_MIN_PULSE        100 // number of milliseconds a pulse must last

// the library to use for the onboard RTC interface
#define RTC_LIBRARY            RTC_DS3231
// the condition that implies the RTC is active in the above library
#define RTC_CONDITION          !rtc.lostPower()
