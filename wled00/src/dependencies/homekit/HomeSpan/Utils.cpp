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
 
#include "Utils.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Contains various generic utility functions and classes:
//
//  Utils::readSerial       - reads all characters from Serial port and saves only up to max specified
//  Utils::mask             - masks a string with asterisks (good for displaying passwords)
//
//  class PushButton        - tracks Single, Double, and Long Presses of a pushbutton that connects a specified pin to ground
//  class Blinker           - creates customized blinking patterns on an LED connected to a specified pin
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef ESP32

char *Utils::readSerial(char *c, int max){
  int i=0;
  char buf;

  while(1){

    while(!Serial.available());       // wait until there is a new character
    
    buf=Serial.read();
    
    if(buf=='\n'){         // exit upon newline
      if(i>0)              // characters have been typed
        c[i]='\0';            // replace newline with string terminator
      return(c);           // return updated string
    }

    if(buf!='\r'){         // save any character except carriage return
      c[i]=buf;               // store new character    
      if(i<max)               // do not store more than max characters (excluding string terminator)
        i++;
    }
  
  } // while(1)
  
} // readSerial

//////////////////////////////////////

String Utils::mask(char *c, int n){
  String s="";
  int len=strlen(c);
  
  for(int i=0;i<len;i++){
    if(i<n || i>=len-n)
      s+=c[i];
    else
      s+='*';
  }
  
  return(s);  
} // mask

#endif