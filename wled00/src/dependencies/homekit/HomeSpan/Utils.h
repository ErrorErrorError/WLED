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
 
#pragma once

#ifdef ESP32

#include <Arduino.h>
#include <driver/timer.h>

#ifdef ESPHOMEKIT_DEBUG
  #pragma message "Homekit 2.7.0 debug mode"
  #define EHK_DEBUG(x)  Serial.print (x)
  #define EHK_DEBUGLN(x) Serial.println (x)
  #define EHK_DEBUGF(x, args...) Serial.printf(x, args)
#else
  #define EHK_DEBUG(x)
  #define EHK_DEBUGLN(x)
  #define EHK_DEBUGF(x, args...)
#endif

namespace Utils {

char *readSerial(char *c, int max);   // read serial port into 'c' until <newline>, but storing only first 'max' characters (the rest are discarded)
String mask(char *c, int n);          // simply utility that creates a String from 'c' with all except the first and last 'n' characters replaced by '*'
  
}

/////////////////////////////////////////////////
// Creates a temporary buffer that is freed after
// going out of scope

template <class bufType>
struct TempBuffer {
  bufType *buf;
  int nBytes;
  
  TempBuffer(size_t len){
    nBytes=len*sizeof(bufType);
    buf=(bufType *)heap_caps_malloc(nBytes,MALLOC_CAP_8BIT);
    if(buf==NULL){
      EHK_DEBUG("\n\n*** FATAL ERROR: Requested allocation of ");
      EHK_DEBUG(nBytes);
      EHK_DEBUG(" bytes failed.  Program Halting.\n\n");
      while(1);
    }
   }

  ~TempBuffer(){
    heap_caps_free(buf);
  }

  int len(){
    return(nBytes);
  }
  
};

#endif