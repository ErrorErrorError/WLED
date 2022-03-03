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

///////////////////////////////////
// SPAN SERVICES (HAP Chapter 8) //
///////////////////////////////////

// Macros to define vectors of required and optional characteristics for each Span Service structure

#define REQ(HAPCHAR) req.push_back(&hapChars.HAPCHAR)
#define OPT(HAPCHAR) opt.push_back(&hapChars.HAPCHAR)

namespace Service {

  struct AccessoryInformation : SpanService { AccessoryInformation() : SpanService{"3E","AccessoryInformation"}{
    REQ(FirmwareRevision);
    REQ(Identify);
    REQ(Manufacturer);
    REQ(Model);
    REQ(Name);
    REQ(SerialNumber);
    OPT(HardwareRevision);      
  }};

  struct HAPProtocolInformation : SpanService { HAPProtocolInformation() : SpanService{"A2","HAPProtocolInformation"}{
    REQ(Version);
  }};

  struct LightBulb : SpanService { LightBulb() : SpanService{"43","LightBulb"}{
    REQ(On);
    OPT(Brightness);
    OPT(Hue);
    OPT(Name);
    OPT(Saturation);
    OPT(ColorTemperature);
  }};
}

//////////////////////////////////////////
// SPAN CHARACTERISTICS (HAP Chapter 9) //
//////////////////////////////////////////

// Macro to define Span Characteristic structures based on name of HAP Characteristic, default value, and min/max value (not applicable for STRING or BOOL which default to min=0, max=1)

#define CREATE_CHAR(TYPE,HAPCHAR,DEFVAL,MINVAL,MAXVAL) \
  struct HAPCHAR : SpanCharacteristic { HAPCHAR(TYPE val=DEFVAL) : SpanCharacteristic {&hapChars.HAPCHAR} { init(val,(TYPE)MINVAL,(TYPE)MAXVAL); } };

namespace Characteristic {
  CREATE_CHAR(int,Brightness,0,0,100);
  CREATE_CHAR(const char *,FirmwareRevision,"1.0.0",0,1);
  CREATE_CHAR(const char *,HardwareRevision,"1.0.0",0,1);
  CREATE_CHAR(double,Hue,0,0,360);
  CREATE_CHAR(boolean,Identify,false,0,1);
  CREATE_CHAR(uint32_t,Identifier,0,0,255);
  CREATE_CHAR(const char *,Manufacturer,"HomeSpan",0,1);
  CREATE_CHAR(const char *,Model,"HomeSpan-ESP32",0,1);
  CREATE_CHAR(const char *,Name,"unnamed",0,1);
  CREATE_CHAR(boolean,On,false,0,1);
  CREATE_CHAR(double,Saturation,0,0,100);
  CREATE_CHAR(const char *,SerialNumber,"HS-12345",0,1);
  CREATE_CHAR(const char *,Version,"1.0.0",0,1);
}

//////////////////////////////////////////
// MACRO TO ADD CUSTOM CHARACTERISTICS  //
//////////////////////////////////////////

#define CUSTOM_CHAR(NAME,UUID,PERMISISONS,FORMAT,DEFVAL,MINVAL,MAXVAL,STATIC_RANGE) \
  HapChar _CUSTOM_##NAME {#UUID,#NAME,(PERMS)(PERMISISONS),FORMAT,STATIC_RANGE}; \
  namespace Characteristic { struct NAME : SpanCharacteristic { NAME(FORMAT##_t val=DEFVAL) : SpanCharacteristic {&_CUSTOM_##NAME} { init(val,(FORMAT##_t)MINVAL,(FORMAT##_t)MAXVAL); } }; }

#define CUSTOM_CHAR_STRING(NAME,UUID,PERMISISONS,DEFVAL) \
  HapChar _CUSTOM_##NAME {#UUID,#NAME,(PERMS)(PERMISISONS),STRING,true}; \
  namespace Characteristic { struct NAME : SpanCharacteristic { NAME(const char * val=DEFVAL) : SpanCharacteristic {&_CUSTOM_##NAME} { init(val); } }; }
