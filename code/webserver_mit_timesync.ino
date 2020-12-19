/*
  Reading CO2, humidity and temperature from the SCD30
  By: Florian von Bracht
  SparkFun Electronics
  Date: 14. November 2020

  This example prints the current CO2 level, relative humidity, and temperature in C.

  Hardware Connections:
  Attach RedBoard to computer using a USB cable.
  Connect SCD30 to RedBoard using Qwiic cable.
  Open Serial Monitor at 115200 baud.

  Note: 

*/

/* This program is distributed in the hope that it will be useful,
      but WITHOUT ANY WARRANTY; without even the implied warranty of
      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
      GNU General Public License for more details. */
#include <ESP8266WiFi.h>
#include <SparkFun_SCD30_Arduino_Library.h>
#include <Wire.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"
#include <Adafruit_GFX.h>
#include <Adafruit_IS31FL3731.h>

// calculated from DWD Weather station Aachen-Orsbach, because it is under 750m, the air pressure is based on sea level
#define SEALEVELPRESSURE_HPA (1025)


//########################################################
//
// INIT + SETTINGS
//
//########################################################

//int ut = 0;
String matrixausgabe_text  = " "; // Ausgabetext als globale Variable
volatile int matrixausgabe_index = 0;// aktuelle Position in Matrix

unsigned long est_start = 0; // estimated start time in milliseconds (synced timestamp - millies())

// file to save the current room as a string
// this information will be stored in a textfile and will be read after a restart
String room = " ";
String room_file = "/config/room.txt";

// file to save the start message for the LED screen
// this information will be stored in a textfile and will be read after a restart
String startmessage = "Hello World";
String startmessage_file = "/config/startmessage.txt";

// the file where the current count of logfiles will be saved
// after a restart the number will be increased by 1 until 9999
// then it will begin again at 1000
int filecounter = 0;
String counter_file = "/config/counter.txt";

// the folder where the logfiles are saved
String data_dir = "/data/";
String data_file = " ";
bool added_date = false;
int keep_files = 30; // how many of the last n files should remain

// SCD30 compensation values
float altitude_compensation = 210.0; // Aachen Eilendorf is around ~210, setup as 0 if you don't know
float temperature_compensation = 3.5; // just by try and error

//--------------------------------------------------------
// VARIABLES TO STORE THE CURRENT MEASURES
//--------------------------------------------------------

// values from the scd30
int current_co2 = 0;
int previous_co2 = 0;
float current_scd_temperature = 0;
float current_scd_humidity = 0;

// values from the bme680
float current_bme_temperature = 0;
float current_bme_humidity = 0;
float current_bme_pressure = 0;
float current_bme_gas = 0;
float current_bme_altitude = 0;

//--------------------------------------------------------
// VARIABLES USED FOR LED
//--------------------------------------------------------

// limits for CO2
int limit1 = 1000;
int limit2 = 2000;

// Values for intervals in the loop()
long previousMillisWrite = 0;
long intervalWrite = 30000;
long previousMillisLED = 0;
long intervalLED = 5000;
long previousMillisChange = 0;
long intervalChange = 1000;

// Values for controlling the animation steps and values shown on the LED Display
int stepLED = 0;    // The animation has different steps
int levelLED = 0;   // The stage (green/yellow/red)
bool animationDone = true;
int displayCO2 = 0;


//--------------------------------------------------------
// INIT FULL BREAKOUT LED MATRIX
//--------------------------------------------------------

Adafruit_IS31FL3731 ledmatrix = Adafruit_IS31FL3731();

uint8_t sweep[] = {1, 2, 3, 4, 6, 8, 10, 15, 20, 30, 40, 60, 60, 40, 30, 20, 15, 10, 8, 6, 4, 3, 2, 1};
static const uint8_t PROGMEM
  smile_green_bmp[] =
  { B00111100,
    B01000010,
    B10100101,
    B10000001,
    B10100101,
    B10011001,
    B01000010,
    B00111100 },
  smile_yellow_bmp[] =
  { B00111100,
    B01000010,
    B10100101,
    B10000001,
    B10000001,
    B10111101,
    B01000010,
    B00111100 },
  smile_red_bmp[] =
  { B00111100,
    B01000010,
    B10100101,
    B10000001,
    B10011001,
    B10100101,
    B01000010,
    B00111100 };


//--------------------------------------------------------
// INIT SCD30 - CO2 SENSOR
//--------------------------------------------------------

//Reading CO2, humidity and temperature from the SCD30 By: Nathan Seidle SparkFun Electronics 

//https://github.com/sparkfun/SparkFun_SCD30_Arduino_Library

SCD30 airSensorSCD30; // Objekt SDC30 Umweltsensor


//--------------------------------------------------------
// INIT BME680 - BOSCH SENSOR
//--------------------------------------------------------

Adafruit_BME680 bme; // I2C


//--------------------------------------------------------
// INIT WEBSERVER
//--------------------------------------------------------

IPAddress myOwnIP; // ownIP for mDNS 
ESP8266WebServer server(80);

String cal_passwort = "eilendorf"; // Kalibrierpasswort
String cal_message  = " ";      // Nachricht, it contains a space, because empty strings do cause problems with server.sendContent(cal_message);

const char* PARAM_INPUT_1 = "timestamp";


//########################################################
//
// HTML CODE
//
//########################################################

//--------------------------------------------------------
// MAIN HTML SNIPPETS
//--------------------------------------------------------

const char INDEX_HTML_START[] PROGMEM = R"rawliteral(
 <!DOCTYPE HTML>
  <html>
  <META HTTP-EQUIV="refresh" CONTENT="20">
   <head>
   <meta name="viewport" content="width = device-width, initial-scale = 1.0, maximum-scale = 1.0, user-scalable=0">
     <title>IoT-Werkstatt Umwelt-Campus Birkenfeld</title>
     <style>
     body {
          font: normal 14px "Courier New", Courier, monospace;
          /* font: normal 14px Arial, Helvetica, sans-serif; */
      }
      h1 { font-size: 36px; }
      table, th, td {
        border: 1px solid black; 
        border-collapse: collapse;
        font-size: 16px;
      } 
      td, th { padding: 10px; }
     </style>
   </head>
    <body>
    <CENTER>
)rawliteral";

const char INDEX_HTML_END[] PROGMEM = R"rawliteral( 
    </CENTER>
   </body>
  </html>
)rawliteral";


//--------------------------------------------------------
// HTML SCRIPTING PART
//--------------------------------------------------------

// The PROGMEM puts the text into flash and puts it into RAM when needed
// The R"()" allows raw output and allow something like R"(hello "world")" which outputs
// hello "world"
const char INDEX_HTML_SCRIPT[] PROGMEM = R"rawliteral(         
        <script>
            var ts = Math.floor(new Date().getTime() / 1000); // in milliseconds
            document.getElementById("timestamp").innerHTML = ts;
            fetch("/get?timestamp=" + ts)
                .then(function (response) {
                    return response;
                })
                .catch(function (error) {
                    console.log("Error: " + error);
                });
        </script>
)rawliteral";


//--------------------------------------------------------
// HTML FORMS
//--------------------------------------------------------

const char INDEX_HTML_SUBMIT[] PROGMEM = R"rawliteral(
     <FORM action="/" method="post">
     <P>
        Passwort: 
        <INPUT type="text" name="message">
        <INPUT type="submit" value="Kalibrieren">
      </P>
     </FORM>
)rawliteral";

const char INDEX_HTML_ROOM[] PROGMEM = R"rawliteral(
     <FORM action="/" method="post">
     <P>
        Raumname: 
        <INPUT type="text" name="room">
        <INPUT type="submit" value="Speichern">
      </P>
     </FORM>
)rawliteral";

const char INDEX_HTML_STARTMESSAGE[] PROGMEM = R"rawliteral(
     <FORM action="/" method="post">
     <P>
        Start-Nachricht: 
        <INPUT type="text" name="startmessage">
        <INPUT type="submit" value="Speichern">
      </P>
     </FORM>
)rawliteral";


//########################################################
//
// FUNCTIONS
//
//########################################################

//--------------------------------------------------------
// Sensirion SCD30
//--------------------------------------------------------

void CO2_Kalibrierfunktion(){ // Kalibrierfunktion

  // Forced Calibration Sensirion SCD 30
  Serial.print("Start SCD 30 calibration, please wait 30 s ...");
  delay(30000);
  airSensorSCD30.setAltitudeCompensation(altitude_compensation); // Altitude in m ü NN
  airSensorSCD30.setForcedRecalibrationFactor(400); // fresh air 
  Serial.println(" done");

}


//--------------------------------------------------------
// LED SCREEN
//--------------------------------------------------------

void showText(String message) {
  ledmatrix.setTextSize(1);
  ledmatrix.setTextWrap(false);  // we dont want text to wrap so it scrolls nicely
  ledmatrix.setTextColor(100);
  for (int8_t x=0; x>=-96; x--) {
    ledmatrix.clear();
    ledmatrix.setCursor(x,1);
    ledmatrix.print(message);
    delay(100);
  }
  ledmatrix.clear();
}


//--------------------------------------------------------
// TIME
//--------------------------------------------------------
 
long seconds() {
  return (millis() / 1000);
}

long unixtimestamp() {
  return ( est_start + seconds());
}


//--------------------------------------------------------
// FILEHANDLING
//--------------------------------------------------------

// Source: https://medium.com/@tafiaalifianty/input-data-to-html-form-with-esp32-200b3a43f0fc
// https://techtutorialsx.com/2019/02/23/esp32-arduino-list-all-files-in-the-spiffs-file-system/

String readFile(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\r\n", path);
  File file = fs.open(path, "r");
  if(!file || file.isDirectory()){
    Serial.println("- empty file or failed to open file");
    return String();
  }
  Serial.println("- read from file:");
  String fileContent;
  while(file.available()){
    fileContent+=String((char)file.read());
  }
  Serial.println(fileContent);
  return fileContent;
}

void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\r\n", path);
  File file = fs.open(path, "w");
  if(!file){
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
  file.close();
}

void appendFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("append file: %s\r\n", path);
  File file = fs.open(path, "a");
  if(!file){
    Serial.println("- failed to open file for appending");
    return;
  }
  if(file.print(message)){
    Serial.println("- file appended");
  } else {
    Serial.println("- write failed");
  }
  file.close();
}

// extracts the filename without the path
String getFilename(String path) {
  int last_slash = path.lastIndexOf('/')+1;
  String filename = path.substring(last_slash);
  return filename;
}

//--------------------------------------------------------
// OUTPUT FOR MEASURES IN A HTML TABLE
//--------------------------------------------------------

String messwertTabelle(){ 

  String html = "<table>\n";
  html += "<tr><th> </th> <th>SCD30</th><th>BME680</th><th>Einheit</th></tr>\n";
  html += "<tr><td>Temperatur:</td> <td>" + String(current_scd_temperature) + "</td><td>" + String(current_bme_temperature) + "</td><td>*C </td></tr>\n";
  html += "<tr><td>Feuchte:</td> <td>" + String(current_scd_humidity) + "</td><td>" + String(current_bme_humidity) + "</td><td>% </td></tr>\n";    
  html += "<tr><td>Druck:</td> <td> </td><td>" + String(current_bme_pressure / 100.0) + "</td><td>hPa </td></tr>\n";
  html += "<tr><td>Gas/VOC:</td> <td> </td><td>" + String(current_bme_gas / 1000.0) + "</td><td>KOhms </td></tr>\n";
  html += "<tr><td>H&ouml;he:</td> <td> </td><td>" + String(current_bme_altitude) + "</td><td>m </td></tr>\n";
  html+="</table>\n";
  
  return html;

}


//--------------------------------------------------------
// OUTPUT FOR ALL FILES IN A HTML TABLE
//--------------------------------------------------------

String FileTable() { 
  
  String html = "<table>\n";

  // List all available files
  Dir dir = SPIFFS.openDir(data_dir);
  
  html += "<tr><th>Pfad</th> <th>KB</th><th>Del</th><th>View</th><th>Download</th></tr>\n";

  // TODO [ ] Nachkommastellen für KB
  while (dir.next ()) {
    html += "<tr>";
    html += "<td>" + dir.fileName() + "</td>";
    html += "<td>" + String( dir.fileSize() / 1024 ) + "</td>";
    html += "<td><a href=\"/log?delete=" + dir.fileName() + "\">x</a></td>";
    html += "<td><a href=\"/log?view=" + dir.fileName() + "\">view</a></td>";    
    html += "<td><a href=\"/log?download=" + dir.fileName() + "\">DL</a></td>";
    html += "</tr>\n";
  }
  
  html+="</table>\n";
  
  return html;

}


//--------------------------------------------------------
// CREATE RED/YELLOW/GREEN CIRCLE
//--------------------------------------------------------

void serverSendFigure(){ 
  
  String unit ="ppm";
  String nam  ="CO2";
  int mess=current_co2;  //airSensorSCD30.getCO2();
  String light = " ";
  // lights with coloured circles
  /*  
  if (mess < limit1) { 
     light = R"rawliteral( 
                <svg width="300" height="300">
                  <ellipse cx="50%" cy="50%" ry="145" rx="145" id="path3713" style="opacity:1;fill:#00ff00;stroke:#000000;stroke-width:2.065;stroke-opacity:1;stroke-miterlimit:4;stroke-dasharray:none;fill-opacity:1" />
                </svg>
            )rawliteral";
  } else if (mess > limit2) { 
         light = R"rawliteral( 
                <svg width="300" height="300">
                  <ellipse cx="50%" cy="50%" ry="145" rx="145" id="path3713" style="opacity:1;fill:#ff0000;stroke:#000000;stroke-width:2.065;stroke-opacity:1;stroke-miterlimit:4;stroke-dasharray:none;fill-opacity:1" />
                </svg>
            )rawliteral";
  } else { 
     light = R"rawliteral( 
                <svg width="300" height="300">
                  <ellipse cx="50%" cy="50%" ry="145" rx="145" id="path3713" style="opacity:1;fill:#ffff00;stroke:#000000;stroke-width:2.065;stroke-opacity:1;stroke-miterlimit:4;stroke-dasharray:none;fill-opacity:1" />
                </svg>
            )rawliteral";
  }
  */
// lights with emojies
if (mess < limit1) { 
     light = R"rawliteral( 
              <svg width="300" height="300" viewbox="-129 -83 140 140"> <circle r="66.901787" cy="-12.780882" cx="-58.813072" id="path4518-3" style="opacity:1;fill:#00ff00;fill-opacity:1;stroke:#000000;stroke-width:2.06499982;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" /> <g transform="translate(-4.762481,-112.48568)" id="g4544-8"> <circle style="opacity:1;fill:#fffff8;fill-opacity:1;stroke:#000000;stroke-width:2.06499982;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" id="path4520-7" cx="-81.264885" cy="95.538681" r="18.520834" /> <circle style="opacity:1;fill:#000000;fill-opacity:1;stroke:#000000;stroke-width:2.06499982;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" id="path4522-6" cx="-78.619049" cy="98.940475" r="7.5595236" /> </g> <g transform="translate(-4.762481,-113.61961)" id="g4548-9"> <circle style="opacity:1;fill:#fffff8;fill-opacity:1;stroke:#000000;stroke-width:2.06499982;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" id="path4520-6-1" cx="-27.970238" cy="96.672615" r="18.520834" /> <circle style="opacity:1;fill:#000000;fill-opacity:1;stroke:#000000;stroke-width:2.06499982;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" id="path4522-4-5" cx="-25.324402" cy="100.07441" r="7.5595236" /> </g> <path sodipodi:nodetypes="cc" inkscape:connector-curvature="0" id="path4641-7" d="m -37.899846,23.936535 c -14.757813,11.58759 -28.895646,12.63546 -42.228558,0" style="fill:none;stroke:#000000;stroke-width:5.76499987;stroke-linecap:butt;stroke-linejoin:miter;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" /> <path sodipodi:nodetypes="cc" inkscape:connector-curvature="0" id="path4673-5-9" d="m -103.05102,-38.869639 c 3.659584,-8.0777 16.053195,-5.965633 31.749861,0" style="fill:none;stroke:#000000;stroke-width:2.38394141;stroke-linecap:butt;stroke-linejoin:miter;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" /> <path sodipodi:nodetypes="cc" inkscape:connector-curvature="0" id="path4673-5-9-9" d="m -17.944814,-38.072593 c -3.65959,-8.0777 -16.05327,-5.965633 -31.750004,0" style="fill:none;stroke:#000000;stroke-width:2.38394141;stroke-linecap:butt;stroke-linejoin:miter;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" /> </svg>
            )rawliteral";
  } else if (mess > limit2) { 
         light = R"rawliteral( 
              <svg width="300" height="300" viewbox="168 -83 140 140"> <g transform="translate(0,0)"> <circle r="66.901787" cy="-12.780882" cx="237.9142" id="path4518-39" style="opacity:1;fill:#ff0000;fill-opacity:1;stroke:#000000;stroke-width:2.06499982;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" /> <g transform="translate(291.96448,-112.48568)" id="g4544-0"> <circle style="opacity:1;fill:#fffff8;fill-opacity:1;stroke:#000000;stroke-width:2.06499982;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" id="path4520-0" cx="-81.264885" cy="95.538681" r="18.520834" /> <circle style="opacity:1;fill:#000000;fill-opacity:1;stroke:#000000;stroke-width:2.06499982;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" id="path4522-5" cx="-78.619049" cy="98.940475" r="7.5595236" /> </g> <g transform="translate(291.96448,-113.61961)" id="g4548-8"> <circle style="opacity:1;fill:#fffff8;fill-opacity:1;stroke:#000000;stroke-width:2.06499982;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" id="path4520-6-0" cx="-27.970238" cy="96.672615" r="18.520834" /> <circle style="opacity:1;fill:#000000;fill-opacity:1;stroke:#000000;stroke-width:2.06499982;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" id="path4522-4-1" cx="-25.324402" cy="100.07441" r="7.5595236" /> </g> <path sodipodi:nodetypes="cc" inkscape:connector-curvature="0" id="path4641" d="m 217.26447,29.414854 c 14.75782,-11.58759 28.89565,-12.63546 42.22858,0" style="fill:none;stroke:#000000;stroke-width:5.76499987;stroke-linecap:butt;stroke-linejoin:miter;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" /> <path inkscape:connector-curvature="0" id="path4673-5-0" d="m 198.33267,-48.497711 30.67687,8.18491" style="fill:none;stroke:#000000;stroke-width:2.38394141;stroke-linecap:butt;stroke-linejoin:miter;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" /> <path inkscape:connector-curvature="0" id="path4673-5-0-5" d="m 275.37139,-47.695903 -30.67687,8.18491" style="fill:none;stroke:#000000;stroke-width:2.38394141;stroke-linecap:butt;stroke-linejoin:miter;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" /> </g></svg>
            )rawliteral";
  } else { 
     light = R"rawliteral( 
              <svg width="300" height="300" viewbox="17 -83 140 140"> <circle r="66.901787" cy="-12.780882" cx="87.3125" id="path4518" style="opacity:1;fill:#ffff00;fill-opacity:1;stroke:#000000;stroke-width:2.06499982;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" /> <g transform="translate(141.3631,-112.48568)" id="g4544"> <circle style="opacity:1;fill:#fffff8;fill-opacity:1;stroke:#000000;stroke-width:2.06499982;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" id="path4520" cx="-81.264885" cy="95.538681" r="18.520834" /> <circle style="opacity:1;fill:#000000;fill-opacity:1;stroke:#000000;stroke-width:2.06499982;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" id="path4522" cx="-78.619049" cy="98.940475" r="7.5595236" /> </g> <g transform="translate(141.3631,-113.61961)" id="g4548"> <circle style="opacity:1;fill:#fffff8;fill-opacity:1;stroke:#000000;stroke-width:2.06499982;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" id="path4520-6" cx="-27.970238" cy="96.672615" r="18.520834" /> <circle style="opacity:1;fill:#000000;fill-opacity:1;stroke:#000000;stroke-width:2.06499982;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" id="path4522-4" cx="-25.324402" cy="100.07441" r="7.5595236" /> </g> <path inkscape:connector-curvature="0" id="path4673" d="M 66.901784,29.014879 H 109.6131" style="fill:none;stroke:#000000;stroke-width:5.76499987;stroke-linecap:butt;stroke-linejoin:miter;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" /> <path inkscape:connector-curvature="0" id="path4673-5" d="M 43.059557,-42.0574 H 74.809559" style="fill:none;stroke:#000000;stroke-width:2.38394141;stroke-linecap:butt;stroke-linejoin:miter;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" /> <path inkscape:connector-curvature="0" id="path4673-5-4" d="M 96.761904,-42.0574 H 128.51191" style="fill:none;stroke:#000000;stroke-width:2.38394141;stroke-linecap:butt;stroke-linejoin:miter;stroke-miterlimit:4;stroke-dasharray:none;stroke-opacity:1" /> </svg> 
            )rawliteral";
  }
  server.sendContent(light);
  String val = String("<h1>")+String(nam)+String(": ")+String(mess)+String(" ")+unit+ String("</h1>\n");
  server.sendContent(val);
  
 }

 
//--------------------------------------------------------
// SERVE HOMEPAGE
//--------------------------------------------------------

void serverHomepage() { 
  
 if (server.hasArg("message")) { // Wenn Kalibrierpasswort eingetroffen,
   String input = server.arg("message");     // dann Text vom Client einlesen
   if (input == cal_passwort) {
      CO2_Kalibrierfunktion(); // Kalibrierfunktion aufrufen 
      cal_message = "calibration done";
      // TODO [ ] save date of last calibration
   } else
      cal_message = "wrong password";
 } else
      cal_message = " ";
      
 if (server.hasArg("room")) { // room name via POST
    room = server.arg("room");
    writeFile(SPIFFS, room_file.c_str(), room.c_str());
 }

 if (server.hasArg("startmessage")) { // start message via POST
    startmessage = server.arg("startmessage");
    writeFile(SPIFFS, startmessage_file.c_str(), startmessage.c_str());
    showText(startmessage);
    //updateScreen();
    stepLED = 1;
 }
 
 server.setContentLength(CONTENT_LENGTH_UNKNOWN);
 server.send ( 200, "text/html", INDEX_HTML_START);
 if (room != " ") {
    server.sendContent("<h1>Raum \"" + room + "\"</h1><br/>");
 }
 serverSendFigure();             // Ampel integrieren
 server.sendContent(messwertTabelle());
 server.sendContent(INDEX_HTML_ROOM);
 server.sendContent(INDEX_HTML_SUBMIT);
 server.sendContent(INDEX_HTML_STARTMESSAGE);
 server.sendContent(cal_message);
 server.sendContent("<br/><a href=\"/log\">Log-Dateien</a><br/>");
 server.sendContent("<br/><a href=\"https://github.com/gitfvb/Oecher-CO2-Ampel/\" target=\"_blank\">Visit github project page</a><br/>");
 server.sendContent(F("<img src=' data:image/jpeg;base64,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' alt=''><br/>\n"));
 if (est_start != 0)
    server.sendContent("Timestamp Server = <span id=\"eststart\">" + String(unixtimestamp()) + "</span><br/>");
 server.sendContent("Timestamp Client = <span id=\"timestamp\"></span>");
 server.sendContent(INDEX_HTML_SCRIPT);
 server.sendContent(INDEX_HTML_END);
}

//--------------------------------------------------------
// RECEIVING DATA
//--------------------------------------------------------


void serverGet() { 
  String inputMessage;
  String inputParam;
  if(server.hasArg(PARAM_INPUT_1)) {

    // read data
    inputMessage = server.arg(PARAM_INPUT_1);
    inputParam = PARAM_INPUT_1;  
    
    // set start date
    est_start = inputMessage.toInt() - seconds();

    // rename data file
    if ( added_date == false ) {
        // rename current file if not done yet
        String old_data_file = data_file;
        data_file = data_dir + "log_" + filecounter + "_" + String(est_start) + ".csv";
        if(SPIFFS.rename(old_data_file,data_file)){
            Serial.println("Succesfully renamed");
            added_date = true; // set bool value
         }else{
            Serial.println("Couldn't rename file");
         } 
    }
    

    
  } else {
    inputMessage = "No message sent";
    inputParam = "none";
  }
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send ( 200, "text/html", "Thanks for input " + inputMessage);

  
  Serial.println("Input: " + inputMessage);
  Serial.println("Seconds since start: " + String(seconds()));
  Serial.println("Estimated Start: " + String(est_start));
  Serial.println("Estimated Time: " + String(unixtimestamp()));
  
}

//--------------------------------------------------------
// ACCESSING ALL FILES
//--------------------------------------------------------

void serverFiles() { 

  if (server.hasArg("view")) {
    String path = server.arg("view");
    File file = SPIFFS.open(path,"r");
    size_t sent = server.streamFile(file,"text/plain");
    file.close();
  } else if (server.hasArg("download")) {
    String path = server.arg("download");
    File file = SPIFFS.open(path,"r");
    server.sendHeader("Content-Disposition","attachment;filename=" + getFilename(path));
    size_t sent = server.streamFile(file,"application/octet-stream");
    file.close();
    
  } else {
      if (server.hasArg("delete")) {
        String file = server.arg("delete");
        SPIFFS.remove(file);
      }
      server.setContentLength(CONTENT_LENGTH_UNKNOWN);
      server.send( 200, "text/html", INDEX_HTML_START);
      server.sendContent(FileTable());
      server.sendContent("<br/><a href=\"/\">Hauptseite</a><br/>");
      server.sendContent("Hinweis: Die letzten " + String(keep_files) + " Dateien werden aufbewahrt");
      server.sendContent(INDEX_HTML_END);
  }
  
}


//########################################################
//
// SETUP
//
//########################################################

void setup(){ // Einmalige Initialisierung

  Serial.begin(115200);
  Serial.println("Hello World");
  
  //--------------------------------------------------------
  // INITIATE I2C BUS
  //--------------------------------------------------------

  Wire.begin(); // ---- Initialisiere den I2C-Bus 
  if (Wire.status() != I2C_OK) Serial.println("Something wrong with I2C");
  Wire.setClock(100000L);            // 100 kHz SCD30 
  Wire.setClockStretchLimit(200000L);// CO2-SCD30

  //--------------------------------------------------------
  // INITIATE BME680
  //--------------------------------------------------------

  if (!bme.begin()) {
    Serial.println("Could not find a valid BME680 sensor, check wiring!");
    while (1);
  }
  // Set up oversampling and filter initialization
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320*C for 150 ms

 if (! bme.performReading()) {
    Serial.println("Failed to perform reading BME680 :(");
  }

  // measure current pressure
  current_bme_pressure = bme.pressure;

  //--------------------------------------------------------
  // INITIATE SCD30
  //--------------------------------------------------------

  if (airSensorSCD30.begin() == false) {
    Serial.println("The SCD30 did not respond. Please check wiring.");
    while(1) {
      yield();
      delay(1);
    }
  }
  
  airSensorSCD30.setAutoSelfCalibration(false); // Sensirion no auto calibration
  airSensorSCD30.setMeasurementInterval(2);     //Change number of seconds between measurements: 2 to 1800 (30 minutes)
  airSensorSCD30.setAltitudeCompensation(altitude_compensation); //Set altitude of the sensor in m

  //Pressure in Boulder, CO is 24.65inHg or 834.74mBar
  airSensorSCD30.setAmbientPressure(current_bme_pressure); //Current ambient pressure in mBar: 700 to 1200

  float offset = airSensorSCD30.getTemperatureOffset();
  Serial.println("Current temp offset: ");
  Serial.print(offset, 2);
  Serial.println("C");
  airSensorSCD30.setTemperatureOffset(temperature_compensation); //Optionally we can set temperature offset to 5°C


  //--------------------------------------------------------
  // INITIATE FILE SYSTEM SPIFFS
  //--------------------------------------------------------

  // Mount file system SPIFFS
  if(!SPIFFS.begin()){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  // Load room data
  room = readFile(SPIFFS, room_file.c_str());
  Serial.println("Initiiert mit Raum " + room);

  // Load current log file counter
  filecounter = readFile(SPIFFS, counter_file.c_str()).toInt() + 1;
  Serial.println("Current counter " + room);

  // Load start message
  startmessage = readFile(SPIFFS, startmessage_file.c_str());
  Serial.println("Initiiert mit start message " + startmessage);

  // Save current counter and begin from 1 after 9999
  if ( filecounter >= 10000 ) {
    filecounter = 1000;
  }
  writeFile(SPIFFS, counter_file.c_str(), String(filecounter).c_str());

  // read in all data log files and count them
  Dir datadir = SPIFFS.openDir("/data/");
  int i = 0;
  while (datadir.next ()) {
    //filenames[i] = datadir.fileName();
    i += 1;
  }
  Serial.println("Anzahl Dateien:" + String(i) );

  // now go through the files again and remove older files
  datadir = SPIFFS.openDir("/data/");
  int j = 0;
  while (datadir.next() && j <= ( i - keep_files )) {
    Serial.println("removing " + datadir.fileName());
    SPIFFS.remove(datadir.fileName());
    j += 1;
  }
    
  // List all remaining files
  Dir dir = SPIFFS.openDir("");
  while (dir.next ()) {
    Serial.println (dir.fileName () + " " + dir.fileSize ());
  }


  //--------------------------------------------------------
  // INITIATE LOG FILE WITH SPIFFS
  //--------------------------------------------------------

  // TODO [ ] check preceeding zeros with printf("%04",filecounter)
  data_file = data_dir + "log_" + filecounter + ".csv";
  String header = "secondsSinceReset;";
      header += "unixtime;";
      header += "room;";
      header += "CO2;";
      header += "scd_temp;";
      header += "bme_temp;";
      header += "scd_humidity;";
      header += "bme_humidity;";
      header += "bme_pressure;";
      header += "bme_gas;";
      header += "bme_hoehe";      
      header += "\n";
  writeFile(SPIFFS, data_file.c_str(), header.c_str());


  //--------------------------------------------------------
  // INITIATE HTML SERVER
  //--------------------------------------------------------

  // TODO [ ] test calibration
    
  server.on("/", serverHomepage);
  server.on("/get",serverGet);
  server.on("/log",serverFiles);
  server.begin();// Server starten
  //ut = 0 ;
  

  //--------------------------------------------------------
  // INITIATE WIFI ACCESS POINT
  //--------------------------------------------------------
  
  WiFi.softAP("KoalaWLAN","co2ampel");
  Serial.print("\nAccessPoint SSID:"); Serial.print("KoalaWLAN");
  Serial.println ("  IP:"+ WiFi.softAPIP().toString());
  myOwnIP = WiFi.softAPIP();
  //matrixausgabe_text = String("Mein Netz:") + String("MeinOctiWLAN") + String( " IP:") + WiFi.softAPIP().toString();
  //matrixausgabe_index=0;


  //--------------------------------------------------------
  // TEST LED MATRIX
  //--------------------------------------------------------

  Serial.println("ISSI swirl test");

  if (! ledmatrix.begin()) {
    Serial.println("IS31 not found");
    while (1);
  }
  Serial.println("IS31 found!");

  ledmatrix.setRotation(0);
  // animate over all the pixels, and set the brightness from the sweep table
  for (uint8_t incr = 0; incr < 24; incr++)
    for (uint8_t x = 0; x < 16; x++)
      for (uint8_t y = 0; y < 9; y++)
        ledmatrix.drawPixel(x, y, sweep[(x+y+incr)%24]);
  ledmatrix.clear();
  delay(500);

  
  ledmatrix.clear();
  ledmatrix.drawBitmap(3, 0, smile_red_bmp, 8, 8, 150);
  delay(1000);
  
  ledmatrix.clear();
  ledmatrix.drawBitmap(3, 0, smile_yellow_bmp, 8, 8, 150);
  delay(1000);  

  ledmatrix.clear();
  ledmatrix.drawBitmap(3, 0, smile_green_bmp, 8, 8, 150);
  delay(3000);
  
  showText(startmessage);

  //--------------------------------------------------------
  // INITIATE CURRENT VALUES
  //--------------------------------------------------------

  current_co2 = (int)airSensorSCD30.getCO2();
  
}


//########################################################
//
// LOOP
//
//########################################################

void loop() { // Kontinuierliche Wiederholung 

  // save current milliseconds
  unsigned long currentMillis = millis();

  // Handle HTML service
  server.handleClient(); //Homepageanfragen versorgen
  delay(1);

  // write csv file
  if ( currentMillis - previousMillisWrite > intervalWrite ) {

      previousMillisWrite = currentMillis;

      Serial.println("Heartbeat at " + String(seconds()) + " seconds");

      int now = seconds();
      
      int current_time = 0;
      if ( est_start > 0 ) {
        current_time = est_start + now;
      }
      
      String line = String(now)+";";
      line += String(current_time)+";";
      line += "\"" + room + "\";";
      line += String(current_co2)+";";
      line += String(current_scd_temperature)+";";
      line += String(current_bme_temperature)+";";
      line += String((int)current_scd_humidity)+";";
      line += String((int)current_bme_humidity)+";";
      line += String((int)current_bme_pressure/100)+";";
      line += String((int)current_bme_gas/1000)+";";
      line += String((int)current_bme_altitude);      
      line += "\n";
      appendFile(SPIFFS, data_file.c_str(), line.c_str());
            
    }
  
    if ( currentMillis - previousMillisLED > intervalLED ) {
  
        previousMillisLED = currentMillis;
        
        // Trigger BME680
        if (! bme.performReading()) {
          Serial.println("Failed to perform reading BME680 :(");
        }
    
        // measure current values
        current_co2 = (int)airSensorSCD30.getCO2();
        Serial.println("Setting co2 to " + String(current_co2) + " at " + String(seconds()) + " seconds");
        current_scd_temperature = airSensorSCD30.getTemperature();
        current_bme_temperature = bme.temperature;
        current_scd_humidity = airSensorSCD30.getHumidity();
        current_bme_humidity = bme.humidity;
        current_bme_pressure = bme.pressure;
        current_bme_gas = bme.gas_resistance;
        current_bme_altitude = bme.readAltitude(SEALEVELPRESSURE_HPA);
    
        // compare co2 values    
        int short_previous_co2 = previous_co2/100;
        int short_current_co2 = current_co2/100;
        Serial.println("Previous " + String(short_previous_co2));
        Serial.println("Current " + String(short_current_co2));
    
        // update led matrix if co2 value divided by 100 changed
        if ( short_previous_co2 != short_current_co2 && animationDone == true) {
          
          // change the step greater than 0 so it will be picked up to update the screen
          stepLED = 1;

          // display value
          displayCO2 = short_current_co2;
          
          // if the values change in the meantime, the animation should not be finished or mixed up
          animationDone = false;
  
          // change the level that should now be shown
          // green
          if (current_co2 < limit1) { 
              levelLED = 0;        
          // red
          } else if (current_co2 > limit2) {
              levelLED = 2;
          // yellow
          } else {             
              levelLED = 1;
          }

          previous_co2 = current_co2;
          
        }
    }

    if ( stepLED >= 1 && ( currentMillis - previousMillisChange > intervalChange ) ) {

        previousMillisChange = currentMillis;
        
        // void drawBitmap(int16_t x, int16_t y, uint8_t *bitmap, int16_t w, int16_t h, uint16_t color);
        // show emoji first
        // then numbers
        
        ledmatrix.clear();

        if ( levelLED == 2 ) {
            if ( stepLED == 1 ) {
                ledmatrix.drawBitmap(3, 0, smile_red_bmp, 8, 8, 50);
                stepLED += 1;
            } else if ( stepLED == 2 ) {
                ledmatrix.drawBitmap(3, 0, smile_red_bmp, 8, 8, 255);
                stepLED += 1;
            } else if ( stepLED == 3 ) {
                ledmatrix.drawBitmap(3, 0, smile_red_bmp, 8, 8, 50);
                stepLED += 1;
            } else {
                ledmatrix.setTextColor(60); // brightness
                stepLED = 99;
            }
        } else if ( levelLED == 1 ) {
            if ( stepLED == 1 ) {
                ledmatrix.drawBitmap(3, 0, smile_yellow_bmp, 8, 8, 50);
                stepLED += 1;
            } else if ( stepLED == 2 ) {
                ledmatrix.drawBitmap(3, 0, smile_yellow_bmp, 8, 8, 255);
                stepLED += 1;
            } else if ( stepLED == 3 ) {
                ledmatrix.drawBitmap(3, 0, smile_yellow_bmp, 8, 8, 50);
                stepLED += 1;
            } else {
                ledmatrix.setTextColor(30); // brightness
                stepLED = 99;
            }
        } else {
            if ( stepLED <= 3 ) {
                ledmatrix.drawBitmap(3, 0, smile_green_bmp, 8, 8, 3);
                stepLED += 1;    
            } else {
                ledmatrix.setTextColor(3); // brightness
                stepLED = 99;
            }
        }

        // last step in animation
        if ( stepLED == 99 ) {
            
            // then update number
            ledmatrix.clear();
            ledmatrix.setTextSize(1);
            ledmatrix.setTextWrap(true);
            ledmatrix.setCursor(3,1);
            ledmatrix.print(String(displayCO2));

            // reset everything
            stepLED = 0;
            levelLED = 0;
            animationDone = true;
        }
      
    }
    
}
