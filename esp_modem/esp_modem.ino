/*
   WiFi SIXFOUR (OLED Edition) - A virtual WiFi modem based on the ESP 8266 chipset
   Copyright (C) 2017 Ilu <superilu@yahoo.com>
   Added I2C OLED display code
   Removed a lot af functions I didn't need
   
   base on
   https://github.com/jsalin/esp8266_modem
   and
   https://github.com/RolandJuno/esp8266_modem
   
   Copyright (C) 2016 Paul Rickards <rickards@gmail.com>
   Added EEPROM read/write, status/help pages, busy answering of incoming calls
   uses the readily available Sparkfun ESP8266 WiFi Shield which has 5v level
   shifters and 3.3v voltage regulation present-- easily connect to a C64
   https://www.sparkfun.com/products/13287

   based on
   ESP8266 based virtual modem
   Copyright (C) 2016 Jussi Salin <salinjus@gmail.com>

   https://github.com/jsalin/esp8266_modem

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266mDNS.h>

#include <Wire.h>  
#include "SSD1306.h"

#define VERSIONA 0
#define VERSIONB 1
#define VERSION_ADDRESS 0    // EEPROM address
#define VERSION_LEN     2    // Length in bytes
#define SSID_ADDRESS    2
#define SSID_LEN        32
#define PASS_ADDRESS    34
#define PASS_LEN        63
#define IP_TYPE_ADDRESS 97   // for future use
#define STATIC_IP_ADDRESS 98 // length 4, for future use
#define STATIC_GW       102  // length 4, for future use
#define STATIC_DNS      106  // length 4, for future use
#define STATIC_MASK     110  // length 4, for future use
#define BAUD_ADDRESS    111
#define ECHO_ADDRESS    112
#define AUTO_ANSWER_ADDRESS 115 // 1 byte
#define TELNET_ADDRESS  116     // 1 byte
#define VERBOSE_ADDRESS 117
#define DIAL0_ADDRESS   200
#define DIAL1_ADDRESS   250
#define DIAL2_ADDRESS   300
#define DIAL3_ADDRESS   350
#define DIAL4_ADDRESS   400
#define DIAL5_ADDRESS   450
#define DIAL6_ADDRESS   500
#define DIAL7_ADDRESS   550
#define DIAL8_ADDRESS   600
#define DIAL9_ADDRESS   650
#define LAST_ADDRESS    780

// Global variables
String build = "20160621182048";
String cmd = "";           // Gather a new AT command to this string from serial
bool cmdMode = true;       // Are we in AT command mode or connected mode
bool callConnected = false;// Are we currently in a call
bool telnet = false;       // Is telnet control code handling enabled
bool verboseResults = false;
//#define DEBUG 1          // Print additional debug information to serial channel
#undef DEBUG

#define RING_INTERVAL 3000 // How often to print RING when having a new incoming connection (ms)
unsigned long lastRingMs = 0; // Time of last "RING" message (millis())
//long myBps;                // What is the current BPS setting
#define MAX_CMD_LENGTH 256 // Maximum length for AT command
char plusCount = 0;        // Go to AT mode at "+++" sequence, that has to be counted
unsigned long plusTime = 0;// When did we last receive a "+++" sequence
#define TX_BUF_SIZE 256    // Buffer where to read from serial before writing to TCP
// (that direction is very blocking by the ESP TCP stack,
// so we can't do one byte a time.)
uint8_t txBuf[TX_BUF_SIZE];
const int speedDialAddresses[] = { DIAL0_ADDRESS, DIAL1_ADDRESS, DIAL2_ADDRESS, DIAL3_ADDRESS, DIAL4_ADDRESS, DIAL5_ADDRESS, DIAL6_ADDRESS, DIAL7_ADDRESS, DIAL8_ADDRESS, DIAL9_ADDRESS };
String speedDials[10];
const int bauds[] = { 300, 1200, 2400, 4800, 9600 };
byte serialspeed;
bool echo = true;
String ssid, password;
byte ringCount = 0;
String resultCodes[] = { "OK", "CONNECT", "RING", "NO CARRIER", "ERROR", "", "NO DIALTONE", "BUSY", "NO ANSWER" };
enum resultCodes_t { R_OK, R_CONNECT, R_RING, R_NOCARRIER, R_ERROR, R_NONE, R_NODIALTONE, R_BUSY, R_NOANSWER };
unsigned long connectTime = 0;
bool hex = false;

// display
#define ADDRESS 0x3c
#define SDA 0
#define SDC 2
SSD1306  display(ADDRESS, SDA, SDC);

int ind_dIn = 0;
int ind_dOut = 0;

int lastChar = 0;


// Telnet codes
#define DO 0xfd
#define WONT 0xfc
#define WILL 0xfb
#define DONT 0xfe

WiFiClient tcpClient;
MDNSResponder mdns;

String connectTimeString() {
  unsigned long now = millis();
  int secs = (now - connectTime) / 1000;
  int mins = secs / 60;
  int hours = mins / 60;
  String out = "";
  if (hours < 10) out.concat("0");
  out.concat(String(hours));
  out.concat(":");
  if (mins % 60 < 10) out.concat("0");
  out.concat(String(mins % 60));
  out.concat(":");
  if (secs % 60 < 10) out.concat("0");
  out.concat(String(secs % 60));
  return out;
}

void writeSettings() {
  setEEPROM(ssid, SSID_ADDRESS, SSID_LEN);
  setEEPROM(password, PASS_ADDRESS, PASS_LEN);

  EEPROM.write(BAUD_ADDRESS, serialspeed);
  EEPROM.write(ECHO_ADDRESS, byte(echo));
  EEPROM.write(TELNET_ADDRESS, byte(telnet));
  EEPROM.write(VERBOSE_ADDRESS, byte(verboseResults));

  for (int i = 0; i < 10; i++) {
    setEEPROM(speedDials[i], speedDialAddresses[i], 50);
  }
  EEPROM.commit();
}

void readSettings() {
  echo = EEPROM.read(ECHO_ADDRESS);
  // serialspeed = EEPROM.read(BAUD_ADDRESS);

  ssid = getEEPROM(SSID_ADDRESS, SSID_LEN);
  password = getEEPROM(PASS_ADDRESS, PASS_LEN);
  telnet = EEPROM.read(TELNET_ADDRESS);
  verboseResults = EEPROM.read(VERBOSE_ADDRESS);

  for (int i = 0; i < 10; i++) {
    speedDials[i] = getEEPROM(speedDialAddresses[i], 50);
  }
}

void defaultEEPROM() {
  EEPROM.write(VERSION_ADDRESS, VERSIONA);
  EEPROM.write(VERSION_ADDRESS + 1, VERSIONB);

  setEEPROM("", SSID_ADDRESS, SSID_LEN);
  setEEPROM("", PASS_ADDRESS, PASS_LEN);
  setEEPROM("d", IP_TYPE_ADDRESS, 1);

  EEPROM.write(BAUD_ADDRESS, 0x02);
  EEPROM.write(ECHO_ADDRESS, 0x01);
  EEPROM.write(AUTO_ANSWER_ADDRESS, 0x01);
  EEPROM.write(TELNET_ADDRESS, 0x00);
  EEPROM.write(VERBOSE_ADDRESS, 0x01);

  setEEPROM("bbs.fozztexx.com:23", speedDialAddresses[0], 50);
  setEEPROM("cottonwoodbbs.dyndns.org:6502", speedDialAddresses[1], 50);
  setEEPROM("borderlinebbs.dyndns.org:6400", speedDialAddresses[2], 50);
  setEEPROM("particlesbbs.dyndns.org:6400", speedDialAddresses[3], 50);
  setEEPROM("reflections.servebbs.com:23", speedDialAddresses[4], 50);
  setEEPROM("heatwavebbs.com:9640", speedDialAddresses[5], 50);

  for (int i = 5; i < 10; i++) {
    setEEPROM("", speedDialAddresses[i], 50);
  }

  EEPROM.commit();
}

String getEEPROM(int startAddress, int len) {
  String myString;

  for (int i = startAddress; i < startAddress + len; i++) {
    if (EEPROM.read(i) == 0x00) {
      break;
    }
    myString += char(EEPROM.read(i));
  }
  return myString;
}

void setEEPROM(String inString, int startAddress, int maxLen) {
  for (int i = startAddress; i < inString.length() + startAddress; i++) {
    EEPROM.write(i, inString[i - startAddress]);
  }
  // null pad the remainder of the memory space
  for (int i = inString.length() + startAddress; i < maxLen + startAddress; i++) {
    EEPROM.write(i, 0x00);
  }
}

void sendResult(int resultCode) {
  Serial.print("\r\n");
  if (verboseResults == 0) {
    Serial.println(resultCode);
    return;
  }
  if (resultCode == R_CONNECT) {
    Serial.print(String(resultCodes[R_CONNECT]) + " " + String(bauds[serialspeed]));
  } else if (resultCode == R_NOCARRIER) {
    Serial.print(String(resultCodes[R_NOCARRIER]) + " (" + connectTimeString() + ")");
  } else {
    Serial.print(String(resultCodes[resultCode]));
  }
  Serial.print("\r\n");
}

void sendString(String msg) {
  Serial.print("\r\n");
  Serial.print(msg);
  Serial.print("\r\n");
}

void connectWiFi() {
  if (ssid == "" || password == "") {
    Serial.println("CONFIGURE SSID AND PASSWORD. TYPE AT? FOR HELP.");
    return;
  }
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.print("\nCONNECTING TO SSID "); Serial.print(ssid);
  uint8_t i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 30) {
    delay(1000);
    Serial.print(".");
    updateState();
    delay(1000);
    Serial.print("o");
    updateState();
  }
  Serial.println();
  if (i == 31) {
    Serial.print("COULD NOT CONNET TO "); Serial.println(ssid);
    WiFi.disconnect();
  } else {
    Serial.print("CONNECTED TO "); Serial.println(WiFi.SSID());
    Serial.print("IP ADDRESS: "); Serial.println(WiFi.localIP());
  }
  updateState();
}

void disconnectWiFi() {
  WiFi.disconnect();
}

void setBaudRate(int inSpeed) {
  if (inSpeed == 0) {
    sendResult(R_ERROR);
    return;
  }
  int foundBaud = -1;
  for (int i = 0; i < sizeof(bauds); i++) {
    if (inSpeed == bauds[i]) {
      foundBaud = i;
      break;
    }
  }
  // requested baud rate not found, return error
  if (foundBaud == -1) {
    sendResult(R_ERROR);
    return;
  }
  if (foundBaud == serialspeed) {
    sendResult(R_OK);
    return;
  }
  Serial.print("SWITCHING SERIAL PORT TO ");
  Serial.print(inSpeed);
  Serial.println(" IN 5 SECONDS");
  delay(5000);
  Serial.end();
  delay(200);
  Serial.begin(bauds[foundBaud]);
  serialspeed = foundBaud;
  delay(200);
  sendResult(R_OK);
}

void displayNetworkStatus() {
  Serial.print("WIFI STATUS: ");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("CONNECTED");
  }
  if (WiFi.status() == WL_IDLE_STATUS) {
    Serial.println("OFFLINE");
  }
  if (WiFi.status() == WL_CONNECT_FAILED) {
    Serial.println("CONNECT FAILED");
  }
  if (WiFi.status() == WL_NO_SSID_AVAIL) {
    Serial.println("SSID UNAVAILABLE");
  }
  if (WiFi.status() == WL_CONNECTION_LOST) {
    Serial.println("CONNECTION LOST");
  }
  if (WiFi.status() == WL_DISCONNECTED) {
    Serial.println("DISCONNECTED");
  }
  if (WiFi.status() == WL_SCAN_COMPLETED) {
    Serial.println("SCAN COMPLETED");
  }
  yield();

  Serial.print("SSID.......: "); Serial.println(WiFi.SSID());

  byte mac[6];
  WiFi.macAddress(mac);
  Serial.print("MAC ADDRESS: ");
  Serial.print(mac[0], HEX);
  Serial.print(":");
  Serial.print(mac[1], HEX);
  Serial.print(":");
  Serial.print(mac[2], HEX);
  Serial.print(":");
  Serial.print(mac[3], HEX);
  Serial.print(":");
  Serial.print(mac[4], HEX);
  Serial.print(":");
  Serial.println(mac[5], HEX);
  yield();

  Serial.print("IP ADDRESS.: "); Serial.println(WiFi.localIP()); yield();
  Serial.print("GATEWAY....: "); Serial.println(WiFi.gatewayIP()); yield();
  Serial.print("SUBNET MASK: "); Serial.println(WiFi.subnetMask()); yield();
  Serial.print("CALL STATUS: "); yield();
  if (callConnected) {
    Serial.print("CONNECTED TO "); Serial.println(ipToString(tcpClient.remoteIP())); yield();
    Serial.print("CALL LENGTH: "); Serial.println(connectTimeString()); yield();
  } else {
    Serial.println("NOT CONNECTED");
  }
}

void displayCurrentSettings() {
  Serial.println("ACTIVE PROFILE:"); yield();
  Serial.print("BAUD: "); Serial.println(bauds[serialspeed]); yield();
  Serial.print("SSID: "); Serial.println(ssid); yield();
  if (verboseResults == 0) {
    Serial.println("PASS: ******"); yield();
  } else {
    Serial.print("PASS: "); Serial.println(password); yield();
  }
  Serial.print("E"); Serial.print(echo); Serial.print(" "); yield();
  Serial.print("V"); Serial.print(verboseResults); Serial.print(" "); yield();
  Serial.print("NET"); Serial.print(telnet); Serial.print(" "); yield();
  Serial.println(); yield();

  Serial.println("SPEED DIAL:");
  for (int i = 0; i < 10; i++) {
    Serial.print(i); Serial.print(": "); Serial.println(speedDials[i]);
    yield();
  }
  Serial.println();
}

void displayStoredSettings() {
  Serial.println("STORED PROFILE:"); yield();
  Serial.print("BAUD: "); Serial.println(bauds[EEPROM.read(BAUD_ADDRESS)]); yield();
  Serial.print("SSID: "); Serial.println(getEEPROM(SSID_ADDRESS, SSID_LEN)); yield();
  if (verboseResults == 0) {
    Serial.println("PASS: ******"); yield();
  } else {
    Serial.print("PASS: "); Serial.println(getEEPROM(PASS_ADDRESS, PASS_LEN)); yield();
  }
  Serial.print("E"); Serial.print(EEPROM.read(ECHO_ADDRESS)); Serial.print(" "); yield();
  Serial.print("V"); Serial.print(EEPROM.read(VERBOSE_ADDRESS)); Serial.print(" "); yield();
  Serial.print("NET"); Serial.print(EEPROM.read(TELNET_ADDRESS)); Serial.print(" "); yield();
  Serial.println(); yield();

  Serial.println("STORED SPEED DIAL:");
  for (int i = 0; i < 10; i++) {
    Serial.print(i); Serial.print(": "); Serial.println(getEEPROM(speedDialAddresses[i], 50));
    yield();
  }
  Serial.println();
}

void waitForSpace() {
  char c = 0;
  Serial.print("PRESS SPACE");
  while (c != ' ') {
    if (Serial.available() > 0) {
      c = Serial.read();
    }
  }
  Serial.print("\r\n");
}

void displayHelp() {
  welcome();
  Serial.println("AT COMMAND SUMMARY:"); yield();
  Serial.println("DIAL HOST.....: ATDTHOST:PORT"); yield();
  Serial.println("SPEED DIAL....: ATDSN (N=0-9)"); yield();
  Serial.println("SET SPEED DIAL: AT&ZN=HOST:PORT (N=0-9)"); yield();
  Serial.println("HANDLE TELNET.: ATNETN (N=0,1)"); yield();
  Serial.println("NETWORK INFO..: ATI"); yield();
  Serial.println("HTTP GET......: ATGET<URL>"); yield();
  Serial.println("LOAD NVRAM....: ATZ"); yield();
  Serial.println("SAVE TO NVRAM.: AT&W"); yield();
  Serial.println("SHOW SETTINGS.: AT&V"); yield();
  Serial.println("FACT. DEFAULTS: AT&F"); yield();
  Serial.println("ECHO OFF/ON...: ATE0 / ATE1"); yield();
  Serial.println("VERBOSE OFF/ON: ATV0 / ATV1"); yield();
  Serial.println("SET SSID......: AT$SSID=WIFISSID"); yield();
  Serial.println("SET PASSWORD..: AT$PASS=WIFIPASSWORD"); yield();
  Serial.println("SET BAUD RATE.: AT$SB=N (3,12,24,48,96"); yield();
  waitForSpace();
  Serial.println("WIFI OFF/ON...: ATC0 / ATC1"); yield();
  Serial.println("HANGUP........: ATH"); yield();
  Serial.println("REBOOT........: AT$RB"); yield();
  Serial.println("ENTER CMD MODE: +++"); yield();
  Serial.println("EXIT CMD MODE.: ATO"); yield();
  Serial.println("QUERY MOST COMMANDS FOLLOWED BY '?'"); yield();
}

void storeSpeedDial(byte num, String location) {
  //if (num < 0 || num > 9) { return; }
  speedDials[num] = location;
  //Serial.print("STORED "); Serial.print(num); Serial.print(": "); Serial.println(location);
}

void welcome() {
  Serial.println();
  Serial.println("Ilu's ESP8266-01_MODEM");
  Serial.println("BASE ON WIFI SIXFOUR BUILD " + build + " BY @PAULRICKARDS");
  Serial.println("BASED ON GITHUB.COM/JSALIN/ESP8266_MODEM");
}

/**
   Arduino main init function
*/
void setup() {
  displayInit();
  drawC64Logo();
  
  EEPROM.begin(LAST_ADDRESS + 1);
  delay(10);

  if (EEPROM.read(VERSION_ADDRESS) != VERSIONA || EEPROM.read(VERSION_ADDRESS + 1) != VERSIONB) {
    defaultEEPROM();
  }

  readSettings();
  // Fetch baud rate from EEPROM
  serialspeed = EEPROM.read(BAUD_ADDRESS);
  // Check if it's out of bounds-- we have to be able to talk
  if (serialspeed < 0 || serialspeed > sizeof(bauds)) {
    serialspeed = 2;
  }

  Serial.begin(bauds[serialspeed]);

  char c;
  while (c != 0x0a && c != 0x0d) {
    if (Serial.available() > 0) {
      c = Serial.read();
    }
    yield();
  }

  drawWifiLogo();
  welcome();

  WiFi.mode(WIFI_STA);
  connectWiFi();
  sendResult(R_OK);

  mdns.begin("C64WiFi", WiFi.localIP());
}

void hangUp() {
  tcpClient.stop();
  callConnected = false;
  sendResult(R_NOCARRIER);
  connectTime = 0;
}

String ipToString(IPAddress ip) {
  char s[16];
  sprintf(s, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return s;
}

void dialOut(String upCmd) {
  // Can't place a call while in a call
  if (callConnected) {
    sendResult(R_ERROR);
    return;
  }
  String host, port;
  int portIndex;
  // Dialing a stored number
  if (upCmd.indexOf("ATDS") == 0) {
    byte speedNum = upCmd.substring(4, 5).toInt();
    portIndex = speedDials[speedNum].indexOf(':');
    if (portIndex != -1) {
      host = speedDials[speedNum].substring(0, portIndex);
      port = speedDials[speedNum].substring(portIndex + 1);
    } else {
      port = "23";
    }
  } else {
    // Dialing an ad-hoc number
    int portIndex = cmd.indexOf(":");
    if (portIndex != -1)
    {
      host = cmd.substring(4, portIndex);
      port = cmd.substring(portIndex + 1, cmd.length());
    }
    else
    {
      host = cmd.substring(4, cmd.length());
      port = "23"; // Telnet default
    }
  }
  host.trim(); // remove leading or trailing spaces
  port.trim();
  Serial.print("DIALING "); Serial.print(host); Serial.print(":"); Serial.println(port);
  char *hostChr = new char[host.length() + 1];
  host.toCharArray(hostChr, host.length() + 1);
  int portInt = port.toInt();
  tcpClient.setNoDelay(true); // Try to disable naggle
  if (tcpClient.connect(hostChr, portInt))
  {
    tcpClient.setNoDelay(true); // Try to disable naggle
    sendResult(R_CONNECT);
    connectTime = millis();
    cmdMode = false;
    Serial.flush();
    callConnected = true;
  }
  else
  {
    sendResult(R_NOANSWER);
    callConnected = false;
  }
  delete hostChr;
}

/**
   Perform a command given in command mode
*/
void command()
{
  cmd.trim();
  if (cmd == "") return;
  Serial.println();
  String upCmd = cmd;
  upCmd.toUpperCase();

  /**** Just AT ****/
  if (upCmd == "AT") sendResult(R_OK);

  /**** Dial to host ****/
  else if ((upCmd.indexOf("ATDT") == 0) || (upCmd.indexOf("ATDP") == 0) || (upCmd.indexOf("ATDI") == 0) || (upCmd.indexOf("ATDS") == 0))
  {
    dialOut(upCmd);
  }

  /**** Change telnet mode ****/
  else if (upCmd == "ATNET0")
  {
    telnet = false;
    sendResult(R_OK);
  }
  else if (upCmd == "ATNET1")
  {
    telnet = true;
    sendResult(R_OK);
  }

  else if (upCmd == "ATNET?") {
    Serial.println(String(telnet));
    sendResult(R_OK);
  }

  /**** Display Help ****/
  else if (upCmd == "AT?" || upCmd == "ATHELP") {
    displayHelp();
    sendResult(R_OK);
  }

  /**** Reset, reload settings from EEPROM ****/
  else if (upCmd == "ATZ") {
    readSettings();
    sendResult(R_OK);
  }

  /**** Disconnect WiFi ****/
  else if (upCmd == "ATC0") {
    disconnectWiFi();
    sendResult(R_OK);
  }

  /**** Connect WiFi ****/
  else if (upCmd == "ATC1") {
    connectWiFi();
    sendResult(R_OK);
  }

  /**** Control local echo in command mode ****/
  else if (upCmd.indexOf("ATE") == 0) {
    if (upCmd.substring(3, 4) == "?") {
      sendString(String(echo));
      sendResult(R_OK);
    }
    else if (upCmd.substring(3, 4) == "0") {
      echo = 0;
      sendResult(R_OK);
    }
    else if (upCmd.substring(3, 4) == "1") {
      echo = 1;
      sendResult(R_OK);
    }
    else {
      sendResult(R_ERROR);
    }
  }

  /**** Control verbosity ****/
  else if (upCmd.indexOf("ATV") == 0) {
    if (upCmd.substring(3, 4) == "?") {
      sendString(String(verboseResults));
      sendResult(R_OK);
    }
    else if (upCmd.substring(3, 4) == "0") {
      verboseResults = 0;
      sendResult(R_OK);
    }
    else if (upCmd.substring(3, 4) == "1") {
      verboseResults = 1;
      sendResult(R_OK);
    }
    else {
      sendResult(R_ERROR);
    }
  }

  /**** Set current baud rate ****/
  else if (upCmd.indexOf("AT$SB=") == 0) {
    setBaudRate(upCmd.substring(6).toInt());
  }

  /**** Display current baud rate ****/
  else if (upCmd.indexOf("AT$SB?") == 0) {
    sendString(String(bauds[serialspeed]));;
  }

  /**** Display Network settings ****/
  else if (upCmd == "ATI") {
    displayNetworkStatus();
    sendResult(R_OK);
  }

  /**** Display profile settings ****/
  else if (upCmd == "AT&V") {
    displayCurrentSettings();
    waitForSpace();
    displayStoredSettings();
    sendResult(R_OK);
  }

  /**** Save (write) current settings to EEPROM ****/
  else if (upCmd == "AT&W") {
    writeSettings();
    sendResult(R_OK);
  }

  /**** Set or display a speed dial number ****/
  else if (upCmd.indexOf("AT&Z") == 0) {
    byte speedNum = upCmd.substring(4, 5).toInt();
    if (speedNum >= 0 && speedNum <= 9) {
      if (upCmd.substring(5, 6) == "=") {
        String speedDial = cmd;
        storeSpeedDial(speedNum, speedDial.substring(6));
        sendResult(R_OK);
      }
      if (upCmd.substring(5, 6) == "?") {
        sendString(speedDials[speedNum]);
        sendResult(R_OK);
      }
    } else {
      sendResult(R_ERROR);
    }
  }

  /**** Set WiFi SSID ****/
  else if (upCmd.indexOf("AT$SSID=") == 0) {
    ssid = cmd.substring(8);
    sendResult(R_OK);
  }

  /**** Display WiFi SSID ****/
  else if (upCmd == "AT$SSID?") {
    sendString(ssid);
    sendResult(R_OK);
  }

  /**** Set WiFi Password ****/
  else if (upCmd.indexOf("AT$PASS=") == 0) {
    password = cmd.substring(8);
    sendResult(R_OK);
  }

  /**** Display WiFi Password ****/
  else if (upCmd == "AT$PASS?") {
    sendString(password);
    sendResult(R_OK);
  }

  /**** Reset EEPROM and current settings to factory defaults ****/
  else if (upCmd == "AT&F") {
    defaultEEPROM();
    readSettings();
    sendResult(R_OK);
  }

  /**** Set HEX Translate On ****/
  else if (upCmd == "ATHEX=1") {
    hex = true;
    sendResult(R_OK);
  }

  /**** Set HEX Translate Off ****/
  else if (upCmd == "ATHEX=0") {
    hex = false;
    sendResult(R_OK);
  }

  /**** Hang up a call ****/
  else if (upCmd.indexOf("ATH") == 0) {
    hangUp();
  }

  /**** Hang up a call ****/
  else if (upCmd.indexOf("AT$RB") == 0) {
    sendResult(R_OK);
    Serial.flush();
    delay(500);
    ESP.reset();
  } 

  /**** Exit modem command mode, go online ****/
  else if (upCmd == "ATO") {
    if (callConnected == 1) {
      sendResult(R_CONNECT);
      cmdMode = false;
    } else {
      sendResult(R_ERROR);
    }
  }

  /**** See my IP address ****/
  else if (upCmd == "ATIP?")
  {
    Serial.println(WiFi.localIP());
    sendResult(R_OK);
  }

  /**** HTTP GET request ****/
  else if (upCmd.indexOf("ATGET") == 0)
  {
    // From the URL, aquire required variables
    // (12 = "ATGEThttp://")
    int portIndex = cmd.indexOf(":", 12); // Index where port number might begin
    int pathIndex = cmd.indexOf("/", 12); // Index first host name and possible port ends and path begins
    int port;
    String path, host;
    if (pathIndex < 0)
    {
      pathIndex = cmd.length();
    }
    if (portIndex < 0)
    {
      port = 80;
      portIndex = pathIndex;
    }
    else
    {
      port = cmd.substring(portIndex + 1, pathIndex).toInt();
    }
    host = cmd.substring(12, portIndex);
    path = cmd.substring(pathIndex, cmd.length());
    if (path == "") path = "/";
    char *hostChr = new char[host.length() + 1];
    host.toCharArray(hostChr, host.length() + 1);

    // Establish connection
    if (!tcpClient.connect(hostChr, port))
    {
      sendResult(R_NOCARRIER);
      connectTime = 0;
      callConnected = false;
    }
    else
    {
      sendResult(R_CONNECT);
      connectTime = millis();
      cmdMode = false;
      callConnected = true;

      // Send a HTTP request before continuing the connection as usual
      String request = "GET ";
      request += path;
      request += " HTTP/1.1\r\nHost: ";
      request += host;
      request += "\r\nConnection: close\r\n\r\n";
      tcpClient.print(request);
    }
    delete hostChr;
  }

  /**** Unknown command ****/
  else sendResult(R_ERROR);

  cmd = "";
}

/**
   Arduino main loop function
*/
void loop()
{
  /**** AT command mode ****/
  if (cmdMode == true)
  {

    // In command mode - don't exchange with TCP but gather characters to a string
    if (Serial.available())
    {
      char chr = Serial.read();
      lastChar = chr;

      // Return, enter, new line, carriage return.. anything goes to end the command
      if ((chr == '\n') || (chr == '\r'))
      {
        command();
        cmd = "";
      }
      // Backspace or delete deletes previous character
      else if ((chr == 8) || (chr == 127) || (chr == 20))
      {
        cmd.remove(cmd.length() - 1);
        if (echo == true) {
          Serial.write(chr);
        }
      }
      else
      {
        if (cmd.length() < MAX_CMD_LENGTH) cmd.concat(chr);
        if (echo == true) {
          Serial.write(chr);
        }
        if (hex) {
          Serial.print(chr, HEX);
        }
      }
    }
  }
  /**** Connected mode ****/
  else
  {
    lastChar = 0;
    // Transmit from terminal to TCP
    if (Serial.available())
    {
      // In telnet in worst case we have to escape every byte
      // so leave half of the buffer always free
      int max_buf_size;
      if (telnet == true)
        max_buf_size = TX_BUF_SIZE / 2;
      else
        max_buf_size = TX_BUF_SIZE;

      // Read from serial, the amount available up to
      // maximum size of the buffer
      size_t len = std::min(Serial.available(), max_buf_size);
      Serial.readBytes(&txBuf[0], len);
      updateDataOut();

      // Enter command mode with "+++" sequence
      for (int i = 0; i < (int)len; i++)
      {
        if (txBuf[i] == '+') plusCount++; else plusCount = 0;
        if (plusCount >= 3)
        {
          plusTime = millis();
        }
        if (txBuf[i] != '+')
        {
          plusCount = 0;
        }
      }

      // Double (escape) every 0xff for telnet, shifting the following bytes
      // towards the end of the buffer from that point
      if (telnet == true)
      {
        for (int i = len - 1; i >= 0; i--)
        {
          if (txBuf[i] == 0xff)
          {
            for (int j = TX_BUF_SIZE - 1; j > i; j--)
            {
              txBuf[j] = txBuf[j - 1];
            }
            len++;
          }
        }
      }
      // Write the buffer to TCP finally
      tcpClient.write(&txBuf[0], len);
      yield();
    }

    // Transmit from TCP to terminal
    while (tcpClient.available())
    {
      uint8_t rxByte = tcpClient.read();
      updateDataIn();

      // Is a telnet control code starting?
      if ((telnet == true) && (rxByte == 0xff))
      {
#ifdef DEBUG
        Serial.print("<t>");
#endif
        rxByte = tcpClient.read();
        if (rxByte == 0xff)
        {
          // 2 times 0xff is just an escaped real 0xff
          Serial.write(0xff); Serial.flush();
        }
        else
        {
          // rxByte has now the first byte of the actual non-escaped control code
#ifdef DEBUG
          Serial.print(rxByte);
          Serial.print(",");
#endif
          uint8_t cmdByte1 = rxByte;
          rxByte = tcpClient.read();
          uint8_t cmdByte2 = rxByte;
          // rxByte has now the second byte of the actual non-escaped control code
#ifdef DEBUG
          Serial.print(rxByte); Serial.flush();
#endif
          // We are asked to do some option, respond we won't
          if (cmdByte1 == DO)
          {
            tcpClient.write((uint8_t)255); tcpClient.write((uint8_t)WONT); tcpClient.write(cmdByte2);
          }
          // Server wants to do any option, allow it
          else if (cmdByte1 == WILL)
          {
            tcpClient.write((uint8_t)255); tcpClient.write((uint8_t)DO); tcpClient.write(cmdByte2);
          }
        }
#ifdef DEBUG
        Serial.print("</t>");
#endif
      }
      else
      {
        // Non-control codes pass through freely
        Serial.write(rxByte); yield(); Serial.flush(); yield();
      }
    }
  }

  // If we have received "+++" as last bytes from serial port and there
  // has been over a second without any more bytes
  if (plusCount >= 3)
  {
    if (millis() - plusTime > 1000)
    {
      //tcpClient.stop();
      cmdMode = true;
      sendResult(R_OK);
      plusCount = 0;
    }
  }

  // Go to command mode if TCP disconnected and not in command mode
  if ((!tcpClient.connected()) && (cmdMode == false) && callConnected == true)
  {
    cmdMode = true;
    sendResult(R_NOCARRIER);
    connectTime = 0;
    callConnected = false;
  }
  updateState();
}

// include pictures
#include "small-inv-c64.h"
#include "small-wifi.h"

// include dialog-10 font
#include "myfont.h"

#define DOT_SIZE 4
#define FONT_HEIGHT 9

void displayInit() {
  display.init();
  display.flipScreenVertically();
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
}

void updateDataIn() {
  display.setColor(BLACK);
  display.fillCircle(DISPLAY_WIDTH-(2*(DOT_SIZE+1)), DOT_SIZE+1, DOT_SIZE); yield();

  ind_dIn++;
  if(ind_dIn > DOT_SIZE) {
    ind_dIn = 0;
  }

  display.setColor(WHITE);
  display.fillCircle(DISPLAY_WIDTH-(2*(DOT_SIZE+1)), DOT_SIZE+1, ind_dIn); yield();

  display.display();
}

void updateDataOut() {
  display.setColor(BLACK);
  display.fillCircle(DISPLAY_WIDTH-(1*(DOT_SIZE+1)), DOT_SIZE+1, DOT_SIZE); yield();

  ind_dOut++;
  if(ind_dOut > DOT_SIZE) {
    ind_dOut = 0;
  }

  display.setColor(WHITE);
  display.fillCircle(DISPLAY_WIDTH-(1*(DOT_SIZE+1)), DOT_SIZE+1, ind_dOut); yield();
  
  display.display();
}

void updateState() {
  int l = 0;
  display.clear(); yield();

  // draw status line
  String stat = String(bauds[serialspeed]/100, DEC);
  if(stat.length() < 2) {
    stat = " " + stat;
  }
  
  if(cmdMode) {
    stat += " CMD";
  } else {
    stat += " DTA";
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    stat += " CONNECTED";
  }
  if (WiFi.status() == WL_IDLE_STATUS) {
    stat += " OFFLINE";
  }
  if (WiFi.status() == WL_CONNECT_FAILED) {
    stat += " CONN FAILED";
  }
  if (WiFi.status() == WL_NO_SSID_AVAIL) {
    stat += " SSID UNAVAIL";
  }
  if (WiFi.status() == WL_CONNECTION_LOST) {
    stat += " CONN LOST";
  }
  if (WiFi.status() == WL_DISCONNECTED) {
    stat += " DISCONN";
  }
  if (WiFi.status() == WL_SCAN_COMPLETED) {
    stat += "SCAN DONE";
  }
  display.drawString(0, (l++)*FONT_HEIGHT, stat); yield();
  display.drawHorizontalLine(0, l*FONT_HEIGHT + 1, DISPLAY_WIDTH);

  display.drawString(0, (l++)*FONT_HEIGHT, WiFi.SSID()); yield();

  stat = "";
  byte mac[6];
  WiFi.macAddress(mac);
  stat += String(mac[0], HEX);
  stat += (":");
  stat += String(mac[1], HEX);
  stat += (":");
  stat += String(mac[2], HEX);
  stat += (":");
  stat += String(mac[3], HEX);
  stat += (":");
  stat += String(mac[4], HEX);
  stat += (":");
  stat += String(mac[5], HEX);
  yield();
  display.drawString(0, (l++)*FONT_HEIGHT, stat); yield();

  stat = "IP:";
  stat += WiFi.localIP().toString(); yield();
  display.drawString(0, (l++)*FONT_HEIGHT, stat); yield();
  display.drawHorizontalLine(0, l*FONT_HEIGHT + 1, DISPLAY_WIDTH);

  if (callConnected) {
    stat = "DST:";
    stat += ipToString(tcpClient.remoteIP()); yield();
    display.drawString(0, (l++)*FONT_HEIGHT, stat); yield();

    stat = "PORT:";
    stat += String(tcpClient.remotePort(), DEC); yield();
    display.drawString(0, (l++)*FONT_HEIGHT, stat); yield();

    stat = "DUR:";
    stat += connectTimeString(); yield();
    display.drawString(0, (l++)*FONT_HEIGHT, stat); yield();
  } else {
    display.drawString(0, (l++)*FONT_HEIGHT, "NOT CONNECTED"); yield();
  }

  // data out
  display.setColor(WHITE);
  display.drawCircle(DISPLAY_WIDTH-(1*(DOT_SIZE+1)), DOT_SIZE+1, ind_dOut); yield();

  // data in
  display.setColor(WHITE);
  display.fillCircle(DISPLAY_WIDTH-(2*(DOT_SIZE+1)), DOT_SIZE+1, ind_dIn); yield();

  if(verboseResults && lastChar!=0) {
    stat = "0x";
    stat += String(lastChar, HEX);
    display.drawString(DISPLAY_WIDTH-30, DISPLAY_HEIGHT-11, stat); yield();
  }
  
  display.display(); yield();
}

void drawC64Logo() {
  display.clear(); yield();
  display.drawXbm(
    (DISPLAY_WIDTH-c64_width)/2,
    (DISPLAY_HEIGHT-c64_height)/2,
    c64_width,
    c64_height,
    c64_bits); yield();
  display.display(); yield();
}

void drawWifiLogo() {
  display.clear(); yield();
  display.drawXbm(
    (DISPLAY_WIDTH-wifi_width)/2,
    (DISPLAY_HEIGHT-wifi_height)/2,
    wifi_width,
    wifi_height,
    wifi_bits); yield();
  display.display(); yield();
}

