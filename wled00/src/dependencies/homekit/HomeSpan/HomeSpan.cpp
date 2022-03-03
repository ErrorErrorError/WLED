/*********************************************************************************
 *  MIT License
 *  
 *  Copyright (c) 2020-2022 Gregg E. Berman
 *  
 *  https://github.com/HomeSpan/HomeSpan
 *  
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *  
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *  
 ********************************************************************************/

#ifdef ESP32

#include <ESPmDNS.h>
#include <nvs_flash.h>
#include <sodium.h>
#include <WiFi.h>
#include <driver/ledc.h>
#include <mbedtls/version.h>
#include <esp_task_wdt.h>

#include "HomeSpan.h"
#include "HAP.h"

using namespace Utils;

HAPClient **hap;                    // HAP Client structure containing HTTP client connections, parsing routines, and state variables (global-scoped variable)
Span homeSpan;                      // HAP Attributes database and all related control functions for this Accessory (global-scoped variable)
HapCharacteristics hapChars;        // Instantiation of all HAP Characteristics (used to create SpanCharacteristics)

///////////////////////////////
//         Span              //
///////////////////////////////

void Span::begin(Category catID, const char *displayName, const char *hostNameBase, const char *modelName){
  this->displayName=displayName;
  this->hostNameBase=hostNameBase;
  this->modelName=modelName;
  sprintf(this->category,"%d",(int)catID);

  esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0));       // required to avoid watchdog timeout messages from ESP32-C3

  if(requestedMaxCon<maxConnections)                          // if specific request for max connections is less than computed max connections
    maxConnections=requestedMaxCon;                           // over-ride max connections with requested value

  hap=(HAPClient **)calloc(maxConnections,sizeof(HAPClient *));
  for(int i=0;i<maxConnections;i++)
    hap[i]=new HAPClient;

  hapServer=new WiFiServer(tcpPortNum);

  nvs_flash_init();                             // initialize non-volatile-storage partition in flash  

  delay(2000);

  EHK_DEBUG("\n************************************************************\n"
                 "Welcome to HomeSpan!\n"
                 "Apple HomeKit for the Espressif ESP-32 WROOM and Arduino IDE\n"
                 "************************************************************\n\n"
                 "** Please ensure serial monitor is set to transmit <newlines>\n\n");

  EHK_DEBUG("Message Logs:     Level ");
  EHK_DEBUG(logLevel);  
  EHK_DEBUG("\nStatus LED:       Pin ");
  if(statusPin>=0){
    EHK_DEBUG(statusPin);
    if(autoOffLED>0)
      EHK_DEBUGF("  (Auto Off=%d sec)",autoOffLED);
  }
  else
    EHK_DEBUG("-  *** WARNING: Status LED Pin is UNDEFINED");
  EHK_DEBUG("\nDevice Control:   Pin ");
  if(controlPin>=0)
    EHK_DEBUG(controlPin);
  else
    EHK_DEBUG("-  *** WARNING: Device Control Pin is UNDEFINED");
  EHK_DEBUG("\nHomeSpan Version: ");
  EHK_DEBUG(HOMESPAN_VERSION);
  EHK_DEBUG("\nArduino-ESP Ver.: ");
  EHK_DEBUG(ARDUINO_ESP_VERSION);
  EHK_DEBUGF("\nESP-IDF Version:  %d.%d.%d",ESP_IDF_VERSION_MAJOR,ESP_IDF_VERSION_MINOR,ESP_IDF_VERSION_PATCH);
  EHK_DEBUGF("\nESP32 Chip:       %s Rev %d %s-core %dMB Flash", ESP.getChipModel(),ESP.getChipRevision(),
                ESP.getChipCores()==1?"single":"dual",ESP.getFlashChipSize()/1024/1024);
  
  #ifdef ARDUINO_VARIANT
    EHK_DEBUG("\nESP32 Board:      ");
    EHK_DEBUG(ARDUINO_VARIANT);
  #endif
  
  EHK_DEBUGF("\nPWM Resources:    %d channels, %d timers, max %d-bit duty resolution",
                LEDC_SPEED_MODE_MAX*LEDC_CHANNEL_MAX,LEDC_SPEED_MODE_MAX*LEDC_TIMER_MAX,LEDC_TIMER_BIT_MAX-1);

  EHK_DEBUGF("\nSodium Version:   %s  Lib %d.%d",sodium_version_string(),sodium_library_version_major(),sodium_library_version_minor());
  char mbtlsv[64];
  mbedtls_version_get_string_full(mbtlsv);
  EHK_DEBUGF("\nMbedTLS Version:  %s",mbtlsv);

  EHK_DEBUG("\nSketch Compiled:  ");
  EHK_DEBUG(__DATE__);
  EHK_DEBUG(" ");
  EHK_DEBUG(__TIME__);

  EHK_DEBUG("\n\nDevice Name:      ");
  EHK_DEBUG(displayName);  
  EHK_DEBUG("\n\n");
}  // begin

///////////////////////////////

void Span::poll() {

  if(!strlen(category)){
    EHK_DEBUG("\n** FATAL ERROR: Cannot run homeSpan.poll() without an initial call to homeSpan.begin()!\n** PROGRAM HALTED **\n\n");
    while(1);    
  }

  if(!isInitialized){
  
    if(!homeSpan.Accessories.empty()){

      if(!homeSpan.Accessories.back()->Services.empty())
        homeSpan.Accessories.back()->Services.back()->validate();    
        
      homeSpan.Accessories.back()->validate();    
    }

    checkRanges();

    if(nWarnings>0){
      configLog+="\n*** CAUTION: There " + String((nWarnings>1?"are ":"is ")) + String(nWarnings) + " WARNING" + (nWarnings>1?"S":"") + " associated with this configuration that may lead to the device becoming non-responsive, or operating in an unexpected manner. ***\n";
    }

    processSerialCommand("i");        // print homeSpan configuration info
   
    if(nFatalErrors>0){
      EHK_DEBUG("\n*** PROGRAM HALTED DUE TO ");
      EHK_DEBUG(nFatalErrors);
      EHK_DEBUG(" FATAL ERROR");
      if(nFatalErrors>1)
        EHK_DEBUG("S");
      EHK_DEBUG(" IN CONFIGURATION! ***\n\n");
      while(1);
    }    

    EHK_DEBUG("\n");
        
    HAPClient::init();        // load HAP settings  

    if(!strlen(network.wifiData.ssid)){
      EHK_DEBUG("*** WIFI CREDENTIALS DATA NOT FOUND.  ");
      EHK_DEBUG("YOU MAY CONFIGURE BY TYPING 'W <RETURN>'.\n\n");
    }
  
    EHK_DEBUG(displayName);
    EHK_DEBUG(" is READY!\n\n");
    isInitialized=true;
  } // isInitialized

  if(strlen(network.wifiData.ssid)>0){
      checkConnect();
  }

  char cBuf[17]="?";
  
  if(Serial.available()){
    readSerial(cBuf,16);
    processSerialCommand(cBuf);
  }

  WiFiClient newClient;

  if(newClient=hapServer->available()){                        // found a new HTTP client
    int freeSlot=getFreeSlot();                                // get next free slot

    if(freeSlot==-1){                                          // no available free slots
      freeSlot=randombytes_uniform(maxConnections);
      LOG2("=======================================\n");
      LOG1("** Freeing Client #");
      LOG1(freeSlot);
      LOG1(" (");
      LOG1(millis()/1000);
      LOG1(" sec) ");
      LOG1(hap[freeSlot]->client.remoteIP());
      LOG1("\n");
      hap[freeSlot]->client.stop();                     // disconnect client from first slot and re-use
    }

    hap[freeSlot]->client=newClient;             // copy new client handle into free slot

    LOG2("=======================================\n");
    LOG1("** Client #");
    LOG1(freeSlot);
    LOG1(" Connected: (");
    LOG1(millis()/1000);
    LOG1(" sec) ");
    LOG1(hap[freeSlot]->client.remoteIP());
    LOG1(" on Socket ");
    LOG1(hap[freeSlot]->client.fd()-LWIP_SOCKET_OFFSET+1);
    LOG1("/");
    LOG1(CONFIG_LWIP_MAX_SOCKETS);
    LOG1("\n");
    LOG2("\n");

    hap[freeSlot]->cPair=NULL;                   // reset pointer to verified ID
    homeSpan.clearNotify(freeSlot);             // clear all notification requests for this connection
    HAPClient::pairStatus=pairState_M1;         // reset starting PAIR STATE (which may be needed if Accessory failed in middle of pair-setup)
  }

  for(int i=0;i<maxConnections;i++){                     // loop over all HAP Connection slots
    
    if(hap[i]->client && hap[i]->client.available()){       // if connection exists and data is available

      HAPClient::conNum=i;                                // set connection number
      hap[i]->processRequest();                           // process HAP request
      
      if(!hap[i]->client){                                 // client disconnected by server
        LOG1("** Disconnecting Client #");
        LOG1(i);
        LOG1("  (");
        LOG1(millis()/1000);
        LOG1(" sec)\n");
      }

      LOG2("\n");

    } // process HAP Client 
  } // for-loop over connection slots

  HAPClient::callServiceLoops();
  HAPClient::checkPushButtons();
  HAPClient::checkNotifications();  
  HAPClient::checkTimedWrites();    
} // poll

///////////////////////////////

int Span::getFreeSlot(){
  
  for(int i=0;i<maxConnections;i++){
    if(!hap[i]->client)
      return(i);
  }

  return(-1);          
}

//////////////////////////////////////

void Span::checkConnect(){

  if(connected){
    if(WiFi.status()==WL_CONNECTED)
      return;
      
    EHK_DEBUG("\n\n*** WiFi Connection Lost!\n");      // losing and re-establishing connection has not been tested
    connected=false;
    waitTime=60000;
    alarmConnect=0;
  }

  if(WiFi.status()!=WL_CONNECTED){
    if(millis()<alarmConnect)         // not yet time to try to try connecting
      return;

    if(waitTime==60000)
      waitTime=1000;
    else
      waitTime*=2;
      
    if(waitTime==32000){
      EHK_DEBUG("\n*** Can't connect to ");
      EHK_DEBUG(network.wifiData.ssid);
      EHK_DEBUG(".  You may type 'W <return>' to re-configure WiFi, or 'X <return>' to erase WiFi credentials.  Will try connecting again in 60 seconds.\n\n");
      waitTime=60000;
    } else {    
      EHK_DEBUG("Trying to connect to ");
      EHK_DEBUG(network.wifiData.ssid);
      EHK_DEBUG(".  Waiting ");
      EHK_DEBUG(waitTime/1000);
      EHK_DEBUG(" second(s) for response...\n");
      WiFi.begin(network.wifiData.ssid,network.wifiData.pwd);
    }

    alarmConnect=millis()+waitTime;

    return;
  }

  connected=true;

  EHK_DEBUG("Successfully connected to ");
  EHK_DEBUG(network.wifiData.ssid);
  EHK_DEBUG("! IP Address: ");
  EHK_DEBUG(WiFi.localIP());
  EHK_DEBUG("\n");

  char id[18];                              // create string version of Accessory ID for MDNS broadcast
  memcpy(id,HAPClient::accessory.ID,17);    // copy ID bytes
  id[17]='\0';                              // add terminating null

  // create broadcaset name from server base name plus accessory ID (without ':')

  int nChars;

  if(!hostNameSuffix)
    nChars=snprintf(NULL,0,"%s-%.2s%.2s%.2s%.2s%.2s%.2s",hostNameBase,id,id+3,id+6,id+9,id+12,id+15);
  else
    nChars=snprintf(NULL,0,"%s%s",hostNameBase,hostNameSuffix);
    
  char hostName[nChars+1];
  
  if(!hostNameSuffix)
    sprintf(hostName,"%s-%.2s%.2s%.2s%.2s%.2s%.2s",hostNameBase,id,id+3,id+6,id+9,id+12,id+15);
  else
    sprintf(hostName,"%s%s",hostNameBase,hostNameSuffix);

  char d[strlen(hostName)+1];  
  sscanf(hostName,"%[A-Za-z0-9-]",d);
  
  if(strlen(hostName)>255|| hostName[0]=='-' || hostName[strlen(hostName)-1]=='-' || strlen(hostName)!=strlen(d)){
    EHK_DEBUGF("\n*** Error:  Can't start MDNS due to invalid hostname '%s'.\n",hostName);
    EHK_DEBUG("*** Hostname must consist of 255 or less alphanumeric characters or a hyphen, except that the hyphen cannot be the first or last character.\n");
    EHK_DEBUG("*** PROGRAM HALTED!\n\n");
    while(1);
  }
    
  EHK_DEBUG("\nStarting MDNS...\n\n");
  EHK_DEBUG("HostName:      ");
  EHK_DEBUG(hostName);
  EHK_DEBUG(".local:");
  EHK_DEBUG(tcpPortNum);
  EHK_DEBUG("\nDisplay Name:  ");
  EHK_DEBUG(displayName);
  EHK_DEBUG("\nModel Name:    ");
  EHK_DEBUG(modelName);
  EHK_DEBUG("\nSetup ID:      ");
  EHK_DEBUG(qrID);
  EHK_DEBUG("\n\n");

  MDNS.begin(hostName);                         // set server host name (.local implied)
  MDNS.setInstanceName(displayName);            // set server display name
  MDNS.addService("_hap","_tcp",tcpPortNum);    // advertise HAP service on specified port

  // add MDNS (Bonjour) TXT records for configurable as well as fixed values (HAP Table 6-7)

  char cNum[16];
  sprintf(cNum,"%d",hapConfig.configNumber);
  
  mdns_service_txt_item_set("_hap","_tcp","c#",cNum);            // Accessory Current Configuration Number (updated whenever config of HAP Accessory Attribute Database is updated)
  mdns_service_txt_item_set("_hap","_tcp","md",modelName);       // Accessory Model Name
  mdns_service_txt_item_set("_hap","_tcp","ci",category);        // Accessory Category (HAP Section 13.1)
  mdns_service_txt_item_set("_hap","_tcp","id",id);              // string version of Accessory ID in form XX:XX:XX:XX:XX:XX (HAP Section 5.4)

  mdns_service_txt_item_set("_hap","_tcp","ff","0");             // HAP Pairing Feature flags.  MUST be "0" to specify Pair Setup method (HAP Table 5-3) without MiFi Authentification
  mdns_service_txt_item_set("_hap","_tcp","pv","1.1");           // HAP version - MUST be set to "1.1" (HAP Section 6.6.3)
  mdns_service_txt_item_set("_hap","_tcp","s#","1");             // HAP current state - MUST be set to "1"

  if(!HAPClient::nAdminControllers())                            // Accessory is not yet paired
    mdns_service_txt_item_set("_hap","_tcp","sf","1");           // set Status Flag = 1 (Table 6-8)
  else
    mdns_service_txt_item_set("_hap","_tcp","sf","0");           // set Status Flag = 0

  mdns_service_txt_item_set("_hap","_tcp","hspn",HOMESPAN_VERSION);           // HomeSpan Version Number (info only - NOT used by HAP)
  mdns_service_txt_item_set("_hap","_tcp","ard-esp32",ARDUINO_ESP_VERSION);   // Arduino-ESP32 Version Number (info only - NOT used by HAP)
  mdns_service_txt_item_set("_hap","_tcp","board",ARDUINO_VARIANT);           // Board Name (info only - NOT used by HAP)

  uint8_t hashInput[22];
  uint8_t hashOutput[64];
  char setupHash[9];
  size_t len;
  
  memcpy(hashInput,qrID,4);                                           // Create the Seup ID for use with optional QR Codes.  This is an undocumented feature of HAP R2!
  memcpy(hashInput+4,id,17);                                          // Step 1: Concatenate 4-character Setup ID and 17-character Accessory ID into hashInput
  mbedtls_sha512_ret(hashInput,21,hashOutput,0);                      // Step 2: Perform SHA-512 hash on combined 21-byte hashInput to create 64-byte hashOutput
  mbedtls_base64_encode((uint8_t *)setupHash,9,&len,hashOutput,4);    // Step 3: Encode the first 4 bytes of hashOutput in base64, which results in an 8-character, null-terminated, setupHash
  mdns_service_txt_item_set("_hap","_tcp","sh",setupHash);            // Step 4: broadcast the resulting Setup Hash

  EHK_DEBUGF("Starting HAP Server on port %d supporting %d simultaneous HomeKit Controller Connections...\n",tcpPortNum,maxConnections);

  hapServer->begin();

  EHK_DEBUG("\n");

  if(!HAPClient::nAdminControllers()){
    EHK_DEBUG("DEVICE NOT YET PAIRED -- PLEASE PAIR WITH HOMEKIT APP\n\n");
  }

  if(wifiCallback)
    wifiCallback();
  
} // initWiFi

///////////////////////////////

void Span::setQRID(const char *id){
  
  char tBuf[5];
  sscanf(id,"%4[0-9A-Za-z]",tBuf);
  
  if(strlen(id)==4 && strlen(tBuf)==4){
    sprintf(qrID,"%s",id);
  }
    
} // setQRID

///////////////////////////////

void Span::processSerialCommand(const char *c){

  switch(c[0]){

    case 's': {    
      
      EHK_DEBUG("\n*** HomeSpan Status ***\n\n");

      EHK_DEBUG("IP Address:        ");
      EHK_DEBUG(WiFi.localIP());
      EHK_DEBUG("\n\n");
      EHK_DEBUG("Accessory ID:      ");
      HAPClient::charPrintRow(HAPClient::accessory.ID,17);
      EHK_DEBUG("                               LTPK: ");
      HAPClient::hexPrintRow(HAPClient::accessory.LTPK,32);
      EHK_DEBUG("\n");

      HAPClient::printControllers();
      EHK_DEBUG("\n");

      for(int i=0;i<maxConnections;i++){
        EHK_DEBUG("Connection #");
        EHK_DEBUG(i);
        EHK_DEBUG(" ");
        if(hap[i]->client){
      
          EHK_DEBUG(hap[i]->client.remoteIP());
          EHK_DEBUG(" on Socket ");
          EHK_DEBUG(hap[i]->client.fd()-LWIP_SOCKET_OFFSET+1);
          EHK_DEBUG("/");
          EHK_DEBUG(CONFIG_LWIP_MAX_SOCKETS);
          
          if(hap[i]->cPair){
            EHK_DEBUG("  ID=");
            HAPClient::charPrintRow(hap[i]->cPair->ID,36);
            EHK_DEBUG(hap[i]->cPair->admin?"   (admin)":" (regular)");
          } else {
            EHK_DEBUG("  (unverified)");
          }
      
        } else {
          EHK_DEBUG("(unconnected)");
        }

        EHK_DEBUG("\n");
      }

      EHK_DEBUG("\n*** End Status ***\n\n");
    } 
    break;

    case 'd': {      
      TempBuffer <char> qBuf(sprintfAttributes(NULL)+1);
      sprintfAttributes(qBuf.buf);  

      EHK_DEBUG("\n*** Attributes Database: size=");
      EHK_DEBUG(qBuf.len()-1);
      EHK_DEBUG("  configuration=");
      EHK_DEBUG(hapConfig.configNumber);
      EHK_DEBUG(" ***\n\n");
      prettyPrint(qBuf.buf);
      EHK_DEBUG("\n*** End Database ***\n\n");
    }
    break;

    case 'Q': {
      char tBuf[5];
      const char *s=c+1+strspn(c+1," ");
      sscanf(s," %4[0-9A-Za-z]",tBuf);
  
      if(strlen(s)==4 && strlen(tBuf)==4){
        sprintf(qrID,"%s",tBuf);
        EHK_DEBUG("\nChanging default Setup ID for QR Code to: '");
        EHK_DEBUG(qrID);
        EHK_DEBUG("'.  Will take effect after next restart.\n\n");
        nvs_set_str(HAPClient::hapNVS,"SETUPID",qrID);                           // update data
        nvs_commit(HAPClient::hapNVS);          
      } else {
        EHK_DEBUG("\n*** Invalid request to change Setup ID for QR Code to: '");
        EHK_DEBUG(s);
        EHK_DEBUG("'.  Setup ID must be exactly 4 alphanumeric characters (0-9, A-Z, and a-z).\n\n");  
      }        
    }
    break;

    case 'S': {
      char buf[128];
      char setupCode[10];

      struct {                                      // temporary structure to hold SRP verification code and salt stored in NVS
        uint8_t salt[16];
        uint8_t verifyCode[384];
      } verifyData;      

      sscanf(c+1," %9[0-9]",setupCode);

      if(strlen(setupCode)!=8){
        EHK_DEBUG("\n*** Invalid request to change Setup Code.  Code must be exactly 8 digits.\n\n");
      } else

      if(!network.allowedCode(setupCode)){
        EHK_DEBUG("\n*** Invalid request to change Setup Code.  Code too simple.\n\n");
      } else {
        sprintf(buf,"\n\nGenerating SRP verification data for new Setup Code: %.3s-%.2s-%.3s ... ",setupCode,setupCode+3,setupCode+5);
        EHK_DEBUG(buf);
        HAPClient::srp.createVerifyCode(setupCode,verifyData.verifyCode,verifyData.salt);                         // create verification code from default Setup Code and random salt
        nvs_set_blob(HAPClient::srpNVS,"VERIFYDATA",&verifyData,sizeof(verifyData));                              // update data
        nvs_commit(HAPClient::srpNVS);                                                                            // commit to NVS
        EHK_DEBUG("New Code Saved!\n");

        EHK_DEBUG("Setup Payload for Optional QR Code: ");
        EHK_DEBUG(qrCode.get(atoi(setupCode),qrID,atoi(category)));
        EHK_DEBUG("\n\n");        
      }            
    }
    break;

    case 'U': {
      HAPClient::removeControllers();                                                                           // clear all Controller data  
      nvs_set_blob(HAPClient::hapNVS,"CONTROLLERS",HAPClient::controllers,sizeof(HAPClient::controllers));      // update data
      nvs_commit(HAPClient::hapNVS);                                                                            // commit to NVS
      EHK_DEBUG("\n*** HomeSpan Pairing Data DELETED ***\n\n");

      for(int i=0;i<maxConnections;i++){     // loop over all connection slots
        if(hap[i]->client){                    // if slot is connected
          LOG1("*** Terminating Client #");
          LOG1(i);
          LOG1("\n");
          hap[i]->client.stop();
        }
      }
      
      EHK_DEBUG("\nDEVICE NOT YET PAIRED -- PLEASE PAIR WITH HOMEKIT APP\n\n");
      mdns_service_txt_item_set("_hap","_tcp","sf","1");                                                        // set Status Flag = 1 (Table 6-8)
      
      if(strlen(network.wifiData.ssid)==0)
        EHK_DEBUG("\nNetwork wifi not set up.");
      else
        EHK_DEBUG("\nNetwork wifi is set up.");
    }
    break;

    case 'A': {
      if(strlen(network.wifiData.ssid)>0){
        EHK_DEBUG("*** Stopping all current WiFi services...\n\n");
        hapServer->end();
        MDNS.end();
        WiFi.disconnect();
      }

      if(strlen(network.setupCode)){
        char s[10];
        sprintf(s,"S%s",network.setupCode);
        processSerialCommand(s);
      } else {
        EHK_DEBUG("*** Setup Code Unchanged\n");
      }

      EHK_DEBUG("\n*** Re-starting ***\n\n");
      delay(1000);
      ESP.restart();                                                                             // re-start device   
    }
    break;

    case 'H': {
      nvs_erase_all(HAPClient::hapNVS);
      nvs_commit(HAPClient::hapNVS);      
      EHK_DEBUG("\n*** HomeSpan Device ID and Pairing Data DELETED!  Restarting...\n\n");
      delay(1000);
      ESP.restart();
    }
    break;

    case 'F': {
      nvs_erase_all(HAPClient::hapNVS);
      nvs_commit(HAPClient::hapNVS);   
      EHK_DEBUG("\n*** FACTORY RESET!  Restarting...\n\n");
      delay(1000);
      ESP.restart();
    }
    break;

    case 'L': {
      int level=0;
      sscanf(c+1,"%d",&level);
      
      if(level<0)
        level=0;
      if(level>2)
        level=2;

      EHK_DEBUG("\n*** Log Level set to ");
      EHK_DEBUG(level);
      EHK_DEBUG("\n\n");
      delay(1000);
      setLogLevel(level);     
    }
    break;

    case 'i':{

      EHK_DEBUG("\n*** HomeSpan Info ***\n\n");

      EHK_DEBUG(configLog);
      EHK_DEBUG("\nConfigured as Bridge: ");
      EHK_DEBUG(homeSpan.isBridge?"YES":"NO");
      EHK_DEBUG("\n\n");

      char d[]="------------------------------";
      EHK_DEBUGF("%-30s  %s  %10s  %s  %s  %s  %s  %s\n","Service","UUID","AID","IID","Update","Loop","Button","Linked Services");
      EHK_DEBUGF("%.30s  %.4s  %.10s  %.3s  %.6s  %.4s  %.6s  %.15s\n",d,d,d,d,d,d,d,d);
      for(int i=0;i<Accessories.size();i++){                             // identify all services with over-ridden loop() methods
        for(int j=0;j<Accessories[i]->Services.size();j++){
          SpanService *s=Accessories[i]->Services[j];
          EHK_DEBUGF("%-30s  %4s  %10u  %3d  %6s  %4s  %6s  ",s->hapName,s->type,Accessories[i]->aid,s->iid, 
                 (void(*)())(s->*(&SpanService::update))!=(void(*)())(&SpanService::update)?"YES":"NO",
                 (void(*)())(s->*(&SpanService::loop))!=(void(*)())(&SpanService::loop)?"YES":"NO",
                 (void(*)(int,boolean))(s->*(&SpanService::button))!=(void(*)(int,boolean))(&SpanService::button)?"YES":"NO"
                 );
          if(s->linkedServices.empty())
            EHK_DEBUG("-");
          for(int k=0;k<s->linkedServices.size();k++){
            EHK_DEBUG(s->linkedServices[k]->iid);
            if(k<s->linkedServices.size()-1)
              EHK_DEBUG(",");
          }
          EHK_DEBUG("\n");
        }
      }
      EHK_DEBUG("\n*** End Info ***\n");
    }
    break;

    case '?': {    
      
      EHK_DEBUG("\n*** HomeSpan Commands ***\n\n");
      EHK_DEBUG("  s - print connection status\n");
      EHK_DEBUG("  i - print summary information about the HAP Database\n");
      EHK_DEBUG("  d - print the full HAP Accessory Attributes Database in JSON format\n");
      EHK_DEBUG("\n");      
      EHK_DEBUG("  W - configure WiFi Credentials and restart\n");      
      EHK_DEBUG("  X - delete WiFi Credentials and restart\n");      
      EHK_DEBUG("  S <code> - change the HomeKit Pairing Setup Code to <code>\n");
      EHK_DEBUG("  Q <id> - change the HomeKit Setup ID for QR Codes to <id>\n");
      EHK_DEBUG("  A - start the HomeSpan Setup Access Point\n");      
      EHK_DEBUG("\n");      
      EHK_DEBUG("  V - delete value settings for all saved Characteristics\n");
      EHK_DEBUG("  U - unpair device by deleting all Controller data\n");
      EHK_DEBUG("  H - delete HomeKit Device ID as well as all Controller data and restart\n");      
      EHK_DEBUG("\n");      
      EHK_DEBUG("  F - factory reset and restart\n");      
      EHK_DEBUG("\n");          
      EHK_DEBUG("  L <level> - change the Log Level setting to <level>\n");
      EHK_DEBUG("\n");

      for(auto uCom=homeSpan.UserCommands.begin(); uCom!=homeSpan.UserCommands.end(); uCom++)      // loop over all UserCommands using an iterator
        EHK_DEBUGF("  @%c %s\n",uCom->first,uCom->second->s);

      if(!homeSpan.UserCommands.empty())
        EHK_DEBUG("\n");
        
      EHK_DEBUG("  ? - print this list of commands\n\n");     
      EHK_DEBUG("*** End Commands ***\n\n");
    }
    break;

    case '@':{

      auto uCom=UserCommands.find(c[1]);

      if(uCom!=UserCommands.end()){
        uCom->second->userFunction(c+1);
        break;
      }
    }

    default:
      EHK_DEBUG("*** Unknown command: '");
      EHK_DEBUG(c);
      EHK_DEBUG("'.  Type '?' for list of commands.\n");
    break;
    
  } // switch
}

///////////////////////////////

void Span::setWifiCredentials(const char *ssid, const char *pwd){
  sprintf(network.wifiData.ssid,"%.*s",MAX_SSID,ssid);
  sprintf(network.wifiData.pwd,"%.*s",MAX_PWD,pwd);
}

///////////////////////////////

int Span::sprintfAttributes(char *cBuf){

  int nBytes=0;

  nBytes+=snprintf(cBuf,cBuf?64:0,"{\"accessories\":[");

  for(int i=0;i<Accessories.size();i++){
    nBytes+=Accessories[i]->sprintfAttributes(cBuf?(cBuf+nBytes):NULL);    
    if(i+1<Accessories.size())
      nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,",");
    }
    
  nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,"]}");
  return(nBytes);
}

///////////////////////////////

void Span::prettyPrint(char *buf, int nsp){
  int s=strlen(buf);
  int indent=0;
  
  for(int i=0;i<s;i++){
    switch(buf[i]){
      
      case '{':
      case '[':
        EHK_DEBUG(buf[i]);
        EHK_DEBUG("\n");
        indent+=nsp;
        for(int j=0;j<indent;j++)
          EHK_DEBUG(" ");
        break;

      case '}':
      case ']':
        EHK_DEBUG("\n");
        indent-=nsp;
        for(int j=0;j<indent;j++)
          EHK_DEBUG(" ");
        EHK_DEBUG(buf[i]);
        break;

      case ',':
        EHK_DEBUG(buf[i]);
        EHK_DEBUG("\n");
        for(int j=0;j<indent;j++)
          EHK_DEBUG(" ");
        break;

      default:
        EHK_DEBUG(buf[i]);
           
    } // switch
  } // loop over all characters

  EHK_DEBUG("\n");
} // prettyPrint

///////////////////////////////

SpanCharacteristic *Span::find(uint32_t aid, int iid){

  int index=-1;
  for(int i=0;i<Accessories.size();i++){   // loop over all Accessories to find aid
    if(Accessories[i]->aid==aid){          // if match, save index into Accessories array
      index=i;
      break;
    }
  }

  if(index<0)                  // fail if no match on aid
    return(NULL);
    
  for(int i=0;i<Accessories[index]->Services.size();i++){                           // loop over all Services in this Accessory
    for(int j=0;j<Accessories[index]->Services[i]->Characteristics.size();j++){     // loop over all Characteristics in this Service
      
      if(iid == Accessories[index]->Services[i]->Characteristics[j]->iid)           // if matching iid
        return(Accessories[index]->Services[i]->Characteristics[j]);                // return pointer to Characteristic
    }
  }

  return(NULL);                // fail if no match on iid
}

///////////////////////////////

int Span::countCharacteristics(char *buf){

  int nObj=0;
  
  const char tag[]="\"aid\"";
  while((buf=strstr(buf,tag))){         // count number of characteristic objects in PUT JSON request
    nObj++;
    buf+=strlen(tag);
  }

  return(nObj);
}

///////////////////////////////

int Span::updateCharacteristics(char *buf, SpanBuf *pObj){

  int nObj=0;
  char *p1;
  int cFound=0;
  boolean twFail=false;
  
  while(char *t1=strtok_r(buf,"{",&p1)){           // parse 'buf' and extract objects into 'pObj' unless NULL
   buf=NULL;
    char *p2;
    int okay=0;
    
    while(char *t2=strtok_r(t1,"}[]:, \"\t\n\r",&p2)){

      if(!cFound){                                 // first token found
        if(strcmp(t2,"characteristics")){
          EHK_DEBUG("\n*** ERROR:  Problems parsing JSON - initial \"characteristics\" tag not found\n\n");
          return(0);
        }
        cFound=1;
        break;
      }
      
      t1=NULL;
      char *t3;
      if(!strcmp(t2,"aid") && (t3=strtok_r(t1,"}[]:, \"\t\n\r",&p2))){
        sscanf(t3,"%u",&pObj[nObj].aid);
        okay|=1;
      } else 
      if(!strcmp(t2,"iid") && (t3=strtok_r(t1,"}[]:, \"\t\n\r",&p2))){
        pObj[nObj].iid=atoi(t3);
        okay|=2;
      } else 
      if(!strcmp(t2,"value") && (t3=strtok_r(t1,"}[]:,\"",&p2))){
        pObj[nObj].val=t3;
        okay|=4;
      } else 
      if(!strcmp(t2,"ev") && (t3=strtok_r(t1,"}[]:, \"\t\n\r",&p2))){
        pObj[nObj].ev=t3;
        okay|=8;
      } else 
      if(!strcmp(t2,"pid") && (t3=strtok_r(t1,"}[]:, \"\t\n\r",&p2))){        
        uint64_t pid=strtoull(t3,NULL,0);        
        if(!TimedWrites.count(pid)){
          EHK_DEBUG("\n*** ERROR:  Timed Write PID not found\n\n");
          twFail=true;
        } else        
        if(millis()>TimedWrites[pid]){
          EHK_DEBUG("\n*** ERROR:  Timed Write Expired\n\n");
          twFail=true;
        }        
      } else {
        EHK_DEBUG("\n*** ERROR:  Problems parsing JSON characteristics object - unexpected property \"");
        EHK_DEBUG(t2);
        EHK_DEBUG("\"\n\n");
        return(0);
      }
    } // parse property tokens

    if(!t1){                                                                  // at least one token was found that was not initial "characteristics"
      if(okay==7 || okay==11  || okay==15){                                   // all required properties found                           
        nObj++;                                                               // increment number of characteristic objects found        
      } else {
        EHK_DEBUG("\n*** ERROR:  Problems parsing JSON characteristics object - missing required properties\n\n");
        return(0);
      }
    }
      
  } // parse objects

  snapTime=millis();                                           // timestamp for this series of updates, assigned to each characteristic in loadUpdate()

  for(int i=0;i<nObj;i++){                                     // PASS 1: loop over all objects, identify characteristics, and initialize update for those found

    if(twFail){                                                // this is a timed-write request that has either expired or for which there was no PID
      pObj[i].status=StatusCode::InvalidValue;                 // set error for all characteristics      
      
    } else {
      pObj[i].characteristic = find(pObj[i].aid,pObj[i].iid);  // find characteristic with matching aid/iid and store pointer          

      if(pObj[i].characteristic)                                                      // if found, initialize characterstic update with new val/ev
        pObj[i].status=pObj[i].characteristic->loadUpdate(pObj[i].val,pObj[i].ev);    // save status code, which is either an error, or TBD (in which case isUpdated for the characteristic has been set to true) 
      else
        pObj[i].status=StatusCode::UnknownResource;                                   // if not found, set HAP error            
    }
      
  } // first pass
      
  for(int i=0;i<nObj;i++){                                     // PASS 2: loop again over all objects       
    if(pObj[i].status==StatusCode::TBD){                       // if object status still TBD

      StatusCode status=pObj[i].characteristic->service->update()?StatusCode::OK:StatusCode::Unable;                  // update service and save statusCode as OK or Unable depending on whether return is true or false

      for(int j=i;j<nObj;j++){                                                      // loop over this object plus any remaining objects to update values and save status for any other characteristics in this service
        
        if(pObj[j].characteristic->service==pObj[i].characteristic->service){       // if service of this characteristic matches service that was updated
          pObj[j].status=status;                                                    // save statusCode for this object
          LOG1("Updating aid=");
          LOG1(pObj[j].characteristic->aid);
          LOG1(" iid=");  
          LOG1(pObj[j].characteristic->iid);
          if(status==StatusCode::OK){                                                     // if status is okay
            pObj[j].characteristic->uvSet(pObj[j].characteristic->value,pObj[j].characteristic->newValue);               // update characteristic value with new value
            LOG1(" (okay)\n");
          } else {                                                                        // if status not okay
            pObj[j].characteristic->uvSet(pObj[j].characteristic->newValue,pObj[j].characteristic->value);                // replace characteristic new value with original value
            LOG1(" (failed)\n");
          }
          pObj[j].characteristic->isUpdated=false;             // reset isUpdated flag for characteristic
        }
      }
    } // object had TBD status
  } // loop over all objects
      
  return(1);
}

///////////////////////////////

void Span::clearNotify(int slotNum) {
  
  for(int i=0;i<Accessories.size();i++){
    for(int j=0;j<Accessories[i]->Services.size();j++){
      for(int k=0;k<Accessories[i]->Services[j]->Characteristics.size();k++){
        Accessories[i]->Services[j]->Characteristics[k]->ev[slotNum]=false;
      }
    }
  }
}

///////////////////////////////

int Span::sprintfNotify(SpanBuf *pObj, int nObj, char *cBuf, int conNum){

  int nChars=0;
  boolean notifyFlag=false;
  
  nChars+=snprintf(cBuf,cBuf?64:0,"{\"characteristics\":[");

  for(int i=0;i<nObj;i++){                              // loop over all objects
    
    if(pObj[i].status==StatusCode::OK && pObj[i].val){           // characteristic was successfully updated with a new value (i.e. not just an EV request)
      
      if(pObj[i].characteristic->ev[conNum]){           // if notifications requested for this characteristic by specified connection number
      
        if(notifyFlag)                                                           // already printed at least one other characteristic
          nChars+=snprintf(cBuf?(cBuf+nChars):NULL,cBuf?64:0,",");               // add preceeding comma before printing next characteristic
        
        nChars+=pObj[i].characteristic->sprintfAttributes(cBuf?(cBuf+nChars):NULL,GET_AID+GET_NV);    // get JSON attributes for characteristic
        notifyFlag=true;
        
      } // notification requested
    } // characteristic updated
  } // loop over all objects

  nChars+=snprintf(cBuf?(cBuf+nChars):NULL,cBuf?64:0,"]}");

  return(notifyFlag?nChars:0);                          // if notifyFlag is not set, return 0, else return number of characters printed to cBuf
}

///////////////////////////////

int Span::sprintfAttributes(SpanBuf *pObj, int nObj, char *cBuf){

  int nChars=0;

  nChars+=snprintf(cBuf,cBuf?64:0,"{\"characteristics\":[");

  for(int i=0;i<nObj;i++){
      nChars+=snprintf(cBuf?(cBuf+nChars):NULL,cBuf?128:0,"{\"aid\":%u,\"iid\":%d,\"status\":%d}",pObj[i].aid,pObj[i].iid,(int)pObj[i].status);
      if(i+1<nObj)
        nChars+=snprintf(cBuf?(cBuf+nChars):NULL,cBuf?64:0,",");
  }

  nChars+=snprintf(cBuf?(cBuf+nChars):NULL,cBuf?64:0,"]}");

  return(nChars);    
}

///////////////////////////////

int Span::sprintfAttributes(char **ids, int numIDs, int flags, char *cBuf){

  int nChars=0;
  uint32_t aid;
  int iid;
  
  SpanCharacteristic *Characteristics[numIDs];
  StatusCode status[numIDs];
  boolean sFlag=false;

  for(int i=0;i<numIDs;i++){              // PASS 1: loop over all ids requested to check status codes - only errors are if characteristic not found, or not readable
    sscanf(ids[i],"%u.%d",&aid,&iid);     // parse aid and iid
    Characteristics[i]=find(aid,iid);     // find matching chararacteristic
    
    if(Characteristics[i]){                                          // if found
      if(Characteristics[i]->perms&PERMS::PR){                       // if permissions allow reading
        status[i]=StatusCode::OK;                                    // always set status to OK (since no actual reading of device is needed)
      } else {
        Characteristics[i]=NULL;                                     
        status[i]=StatusCode::WriteOnly;
        sFlag=true;                                                  // set flag indicating there was an error
      }
    } else {
      status[i]=StatusCode::UnknownResource;
      sFlag=true;                                                    // set flag indicating there was an error
    }
  }

  nChars+=snprintf(cBuf,cBuf?64:0,"{\"characteristics\":[");  

  for(int i=0;i<numIDs;i++){              // PASS 2: loop over all ids requested and create JSON for each (with or without status code base on sFlag set above)
    
    if(Characteristics[i])                                                                         // if found
      nChars+=Characteristics[i]->sprintfAttributes(cBuf?(cBuf+nChars):NULL,flags);                // get JSON attributes for characteristic
    else{
      sscanf(ids[i],"%u.%d",&aid,&iid);     // parse aid and iid                        
      nChars+=snprintf(cBuf?(cBuf+nChars):NULL,cBuf?64:0,"{\"iid\":%d,\"aid\":%u}",iid,aid);      // else create JSON attributes based on requested aid/iid
    }
    
    if(sFlag){                                                                                    // status flag is needed - overlay at end
      nChars--;
      nChars+=snprintf(cBuf?(cBuf+nChars):NULL,cBuf?64:0,",\"status\":%d}",(int)status[i]);
    }
  
    if(i+1<numIDs)
      nChars+=snprintf(cBuf?(cBuf+nChars):NULL,cBuf?64:0,",");
    
  }

  nChars+=snprintf(cBuf?(cBuf+nChars):NULL,cBuf?64:0,"]}");

  return(nChars);    
}

///////////////////////////////

void Span::checkRanges(){

  boolean okay=true;
  homeSpan.configLog+="\nRange Check:";
  
  for(int i=0;i<Accessories.size();i++){
    for(int j=0;j<Accessories[i]->Services.size();j++){
      for(int k=0;k<Accessories[i]->Services[j]->Characteristics.size();k++){
        SpanCharacteristic *chr=Accessories[i]->Services[j]->Characteristics[k];

        if(chr->format!=STRING && (chr->uvGet<double>(chr->value) < chr->uvGet<double>(chr->minValue) || chr->uvGet<double>(chr->value) > chr->uvGet<double>(chr->maxValue))){
          char c[256];
          sprintf(c,"\n  \u2718 Characteristic %s with AID=%d, IID=%d: Initial value of %.4f is out of range [%.4f,%.4f]",
                chr->hapName,chr->aid,chr->iid,chr->uvGet<double>(chr->value),chr->uvGet<double>(chr->minValue),chr->uvGet<double>(chr->maxValue));
          if(okay)
            homeSpan.configLog+="\n";
          homeSpan.configLog+=c;
          homeSpan.nWarnings++;
          okay=false;
        }       
      }
    }
  }

  if(okay)
    homeSpan.configLog+=" No Warnings";
  homeSpan.configLog+="\n\n";
}

///////////////////////////////
//      SpanAccessory        //
///////////////////////////////

SpanAccessory::SpanAccessory(uint32_t aid){
  if(!homeSpan.Accessories.empty()){

    if(homeSpan.Accessories.size()==HAPClient::MAX_ACCESSORIES){
      EHK_DEBUG("\n\n*** FATAL ERROR: Can't create more than ");
      EHK_DEBUG(HAPClient::MAX_ACCESSORIES);
      EHK_DEBUG(" Accessories.  Program Halting.\n\n");
      while(1);      
    }
    
    this->aid=homeSpan.Accessories.back()->aid+1;
    
    if(!homeSpan.Accessories.back()->Services.empty())
      homeSpan.Accessories.back()->Services.back()->validate();    
      
    homeSpan.Accessories.back()->validate();    
  } else {
    this->aid=1;
  }
  
  homeSpan.Accessories.push_back(this);

  if(aid>0){                 // override with user-specified aid
    this->aid=aid;
  }

  homeSpan.configLog+="\u27a4 Accessory:  AID=" + String(this->aid);

  for(int i=0;i<homeSpan.Accessories.size()-1;i++){
    if(this->aid==homeSpan.Accessories[i]->aid){
      homeSpan.configLog+=" *** ERROR!  ID already in use for another Accessory. ***";
      homeSpan.nFatalErrors++;
      break;
    }
  }

  if(homeSpan.Accessories.size()==1 && this->aid!=1){
    homeSpan.configLog+=" *** ERROR!  ID of first Accessory must always be 1. ***";
    homeSpan.nFatalErrors++;    
  }

  homeSpan.configLog+="\n";

}

///////////////////////////////

void SpanAccessory::validate(){

  boolean foundInfo=false;
  boolean foundProtocol=false;
  
  for(int i=0;i<Services.size();i++){
    if(!strcmp(Services[i]->type,"3E"))
      foundInfo=true;
    else if(!strcmp(Services[i]->type,"A2"))
      foundProtocol=true;
    else if(aid==1)                             // this is an Accessory with aid=1, but it has more than just AccessoryInfo and HAPProtocolInformation.  So...
      homeSpan.isBridge=false;                  // ...this is not a bridge device
  }

  if(!foundInfo){
    homeSpan.configLog+="   \u2718 Service AccessoryInformation";
    homeSpan.configLog+=" *** ERROR!  Required Service for this Accessory not found. ***\n";
    homeSpan.nFatalErrors++;
  }    

  if(!foundProtocol && (aid==1 || !homeSpan.isBridge)){           // HAPProtocolInformation must always be present in Accessory if aid=1, and any other Accessory if the device is not a bridge)
    homeSpan.configLog+="   \u2718 Service HAPProtocolInformation";
    homeSpan.configLog+=" *** ERROR!  Required Service for this Accessory not found. ***\n";
    homeSpan.nFatalErrors++;
  }    
}

///////////////////////////////

int SpanAccessory::sprintfAttributes(char *cBuf){
  int nBytes=0;

  nBytes+=snprintf(cBuf,cBuf?64:0,"{\"aid\":%u,\"services\":[",aid);

  for(int i=0;i<Services.size();i++){
    nBytes+=Services[i]->sprintfAttributes(cBuf?(cBuf+nBytes):NULL);    
    if(i+1<Services.size())
      nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,",");
    }
    
  nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,"]}");

  return(nBytes);
}

///////////////////////////////
//       SpanService         //
///////////////////////////////

SpanService::SpanService(const char *type, const char *hapName){

  if(!homeSpan.Accessories.empty() && !homeSpan.Accessories.back()->Services.empty())      // this is not the first Service to be defined for this Accessory
    homeSpan.Accessories.back()->Services.back()->validate();    

  this->type=type;
  this->hapName=hapName;

  homeSpan.configLog+="   \u279f Service " + String(hapName);
  
  if(homeSpan.Accessories.empty()){
    homeSpan.configLog+=" *** ERROR!  Can't create new Service without a defined Accessory! ***\n";
    homeSpan.nFatalErrors++;
    return;
  }

  homeSpan.Accessories.back()->Services.push_back(this);  
  iid=++(homeSpan.Accessories.back()->iidCount);  

  homeSpan.configLog+=":  IID=" + String(iid) + ", UUID=\"" + String(type) + "\"";

  if(!strcmp(this->type,"3E") && iid!=1){
    homeSpan.configLog+=" *** ERROR!  The AccessoryInformation Service must be defined before any other Services in an Accessory. ***";
    homeSpan.nFatalErrors++;
  }

  homeSpan.configLog+="\n";

}

///////////////////////////////

SpanService *SpanService::setPrimary(){
  primary=true;
  return(this);
}

///////////////////////////////

SpanService *SpanService::setHidden(){
  hidden=true;
  return(this);
}

///////////////////////////////

SpanService *SpanService::addLink(SpanService *svc){
  linkedServices.push_back(svc);
  return(this);
}

///////////////////////////////

int SpanService::sprintfAttributes(char *cBuf){
  int nBytes=0;

  nBytes+=snprintf(cBuf,cBuf?64:0,"{\"iid\":%d,\"type\":\"%s\",",iid,type);
  
  if(hidden)
    nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,"\"hidden\":true,");
    
  if(primary)
    nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,"\"primary\":true,");

  if(!linkedServices.empty()){
    nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,"\"linked\":[");
    for(int i=0;i<linkedServices.size();i++){
      nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,"%d",linkedServices[i]->iid);
      if(i+1<linkedServices.size())
        nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,",");
    }
     nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,"],");
  }
    
  nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,"\"characteristics\":[");
  
  for(int i=0;i<Characteristics.size();i++){
    nBytes+=Characteristics[i]->sprintfAttributes(cBuf?(cBuf+nBytes):NULL,GET_META|GET_PERMS|GET_TYPE|GET_DESC);    
    if(i+1<Characteristics.size())
      nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,",");
  }
    
  nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,"]}");

  return(nBytes);
}

///////////////////////////////

void SpanService::validate(){

  for(int i=0;i<req.size();i++){
    boolean valid=false;
    for(int j=0;!valid && j<Characteristics.size();j++)
      valid=!strcmp(req[i]->type,Characteristics[j]->type);
      
    if(!valid){
      homeSpan.configLog+="      \u2718 Characteristic " + String(req[i]->hapName);
      homeSpan.configLog+=" *** WARNING!  Required Characteristic for this Service not found. ***\n";
      homeSpan.nWarnings++;
    }
  }

  vector<HapChar *>().swap(opt);
  vector<HapChar *>().swap(req);
}

///////////////////////////////
//    SpanCharacteristic     //
///////////////////////////////

SpanCharacteristic::SpanCharacteristic(HapChar *hapChar){
  type=hapChar->type;
  perms=hapChar->perms;
  hapName=hapChar->hapName;
  format=hapChar->format;
  staticRange=hapChar->staticRange;

  homeSpan.configLog+="      \u21e8 Characteristic " + String(hapName);

  if(homeSpan.Accessories.empty() || homeSpan.Accessories.back()->Services.empty()){
    homeSpan.configLog+=" *** ERROR!  Can't create new Characteristic without a defined Service! ***\n";
    homeSpan.nFatalErrors++;
    return;
  }

  iid=++(homeSpan.Accessories.back()->iidCount);
  service=homeSpan.Accessories.back()->Services.back();
  aid=homeSpan.Accessories.back()->aid;

  ev=(boolean *)calloc(homeSpan.maxConnections,sizeof(boolean));
}

///////////////////////////////

int SpanCharacteristic::sprintfAttributes(char *cBuf, int flags){
  int nBytes=0;

  const char permCodes[][7]={"pr","pw","ev","aa","tw","hd","wr"};

  const char formatCodes[][8]={"bool","uint8","uint16","uint32","uint64","int","float","string"};

  nBytes+=snprintf(cBuf,cBuf?64:0,"{\"iid\":%d",iid);

  if(flags&GET_TYPE)  
    nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,",\"type\":\"%s\"",type);

  if(perms&PR){    
    if(perms&NV && !(flags&GET_NV))
      nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,",\"value\":null");
    else
      nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,",\"value\":%s",uvPrint(value).c_str());      
  }

  if(flags&GET_META){
    nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,",\"format\":\"%s\"",formatCodes[format]);
    
    if(customRange && (flags&GET_META)){
      nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?128:0,",\"minValue\":%s,\"maxValue\":%s",uvPrint(minValue).c_str(),uvPrint(maxValue).c_str());
        
      if(uvGet<float>(stepValue)>0)
        nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?128:0,",\"minStep\":%s",uvPrint(stepValue).c_str());
    }

    if(unit){
      if(strlen(unit)>0)
        nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?128:0,",\"unit\":\"%s\"",unit);
     else
        nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?128:0,",\"unit\":null");
    }

    if(validValues){
      nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?128:0,",\"valid-values\":%s",validValues);      
    }
  }
    
  if(desc && (flags&GET_DESC)){
    nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?128:0,",\"description\":\"%s\"",desc);    
  }

  if(flags&GET_PERMS){
    nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,",\"perms\":[");
    for(int i=0;i<7;i++){
      if(perms&(1<<i)){
        nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,"\"%s\"",permCodes[i]);
        if(perms>=(1<<(i+1)))
          nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,",");
      }
    }
    nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,"]");
  }

  if(flags&GET_AID)
    nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,",\"aid\":%u",aid);
  
  if(flags&GET_EV)
    nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,",\"ev\":%s",ev[HAPClient::conNum]?"true":"false");

  nBytes+=snprintf(cBuf?(cBuf+nBytes):NULL,cBuf?64:0,"}");

  return(nBytes);
}

///////////////////////////////

StatusCode SpanCharacteristic::loadUpdate(char *val, char *ev){

  if(ev){                // request for notification
    boolean evFlag;
    
    if(!strcmp(ev,"0") || !strcmp(ev,"false"))
      evFlag=false;
    else if(!strcmp(ev,"1") || !strcmp(ev,"true"))
      evFlag=true;
    else
      return(StatusCode::InvalidValue);
    
    if(evFlag && !(perms&EV))         // notification is not supported for characteristic
      return(StatusCode::NotifyNotAllowed);
      
    LOG1("Notification Request for aid=");
    LOG1(aid);
    LOG1(" iid=");
    LOG1(iid);
    LOG1(": ");
    LOG1(evFlag?"true":"false");
    LOG1("\n");
    this->ev[HAPClient::conNum]=evFlag;
  }

  if(!val)                // no request to update value
    return(StatusCode::OK);
  
  if(!(perms&PW))         // cannot write to read only characteristic
    return(StatusCode::ReadOnly);

  switch(format){
    
    case BOOL:
      if(!strcmp(val,"0") || !strcmp(val,"false"))
        newValue.BOOL=false;
      else if(!strcmp(val,"1") || !strcmp(val,"true"))
        newValue.BOOL=true;
      else
        return(StatusCode::InvalidValue);
      break;

    case INT:
      if(!strcmp(val,"false"))
        newValue.INT=0;
      else if(!strcmp(val,"true"))
        newValue.INT=1;
      else if(!sscanf(val,"%d",&newValue.INT))
        return(StatusCode::InvalidValue);
      break;

    case UINT8:
      if(!strcmp(val,"false"))
        newValue.UINT8=0;
      else if(!strcmp(val,"true"))
        newValue.UINT8=1;
      else if(!sscanf(val,"%hhu",&newValue.UINT8))
        return(StatusCode::InvalidValue);
      break;
            
    case UINT16:
      if(!strcmp(val,"false"))
        newValue.UINT16=0;
      else if(!strcmp(val,"true"))
        newValue.UINT16=1;
      else if(!sscanf(val,"%hu",&newValue.UINT16))
        return(StatusCode::InvalidValue);
      break;
      
    case UINT32:
      if(!strcmp(val,"false"))
        newValue.UINT32=0;
      else if(!strcmp(val,"true"))
        newValue.UINT32=1;
      else if(!sscanf(val,"%u",&newValue.UINT32))
        return(StatusCode::InvalidValue);
      break;
      
    case UINT64:
      if(!strcmp(val,"false"))
        newValue.UINT64=0;
      else if(!strcmp(val,"true"))
        newValue.UINT64=1;
      else if(!sscanf(val,"%llu",&newValue.UINT64))
        return(StatusCode::InvalidValue);
      break;

    case FLOAT:
      if(!sscanf(val,"%lg",&newValue.FLOAT))
        return(StatusCode::InvalidValue);
      break;

    case STRING:
      uvSet(newValue,(const char *)val);
      break;

    default:
    break;

  } // switch

  isUpdated=true;
  updateTime=homeSpan.snapTime;
  return(StatusCode::TBD);
}

///////////////////////////////

unsigned long SpanCharacteristic::timeVal(){
  
  return(homeSpan.snapTime-updateTime);
}

///////////////////////////////

SpanCharacteristic *SpanCharacteristic::setValidValues(int n, ...){
  char c[256];
  String *s = new String("[");
  va_list vl;
  va_start(vl,n);
  for(int i=0;i<n;i++){
    *s+=va_arg(vl,int);
    if(i!=n-1)
      *s+=",";
  }
  va_end(vl);
  *s+="]";

  homeSpan.configLog+=String("         \u2b0c Set Valid Values for ") + String(hapName) + " with IID=" + String(iid);

  if(validValues){
    sprintf(c,"  *** ERROR!  Valid Values already set for this Characteristic! ***\n");
    homeSpan.nFatalErrors++;
  } else 

  if(format!=UINT8){
    sprintf(c,"  *** ERROR!  Can't set Valid Values for this Characteristic! ***\n");
    homeSpan.nFatalErrors++;      
  } else {
    
    validValues=s->c_str();
    sprintf(c,":  ValidValues=%s\n",validValues);
  }

  homeSpan.configLog+=c;
  return(this);
}

///////////////////////////////
//        SpanRange          //
///////////////////////////////

SpanRange::SpanRange(int min, int max, int step){

  if(homeSpan.Accessories.empty() || homeSpan.Accessories.back()->Services.empty() || homeSpan.Accessories.back()->Services.back()->Characteristics.empty() ){
    homeSpan.configLog+="    \u2718 SpanRange: *** ERROR!  Can't create new Range without a defined Characteristic! ***\n";
    homeSpan.nFatalErrors++;
  } else {
    homeSpan.Accessories.back()->Services.back()->Characteristics.back()->setRange(min,max,step);
  }
}

///////////////////////////////
//        SpanButton         //
///////////////////////////////

SpanButton::SpanButton(int pin, uint16_t longTime, uint16_t singleTime, uint16_t doubleTime){

  homeSpan.configLog+="      \u25bc SpanButton: Pin=" + String(pin) + ", Single=" + String(singleTime) + "ms, Double=" + String(doubleTime) + "ms, Long=" + String(longTime) + "ms";

  if(homeSpan.Accessories.empty() || homeSpan.Accessories.back()->Services.empty()){
    homeSpan.configLog+=" *** ERROR!  Can't create new PushButton without a defined Service! ***\n";
    homeSpan.nFatalErrors++;
    return;
  }

  EHK_DEBUG("Configuring PushButton: Pin=");     // initialization message
  EHK_DEBUG(pin);
  EHK_DEBUG("\n");

  this->pin=pin;
  this->longTime=longTime;
  this->singleTime=singleTime;
  this->doubleTime=doubleTime;
  service=homeSpan.Accessories.back()->Services.back();

  if((void(*)(int,int))(service->*(&SpanService::button))==(void(*)(int,int))(&SpanService::button)){
    homeSpan.configLog+=" *** WARNING:  No button() method defined for this PushButton! ***";
    homeSpan.nWarnings++;
  }

  pushButton=new PushButton(pin);         // create underlying PushButton
  
  homeSpan.configLog+="\n";  
  homeSpan.PushButtons.push_back(this);
}


///////////////////////////////
//     SpanUserCommand       //
///////////////////////////////

SpanUserCommand::SpanUserCommand(char c, const char *s, void (*f)(const char *v)){
  this->s=s;
  userFunction=f;
   
  homeSpan.UserCommands[c]=this;
}

#endif