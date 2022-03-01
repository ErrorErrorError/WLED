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

#include <WiFi.h>

#include <unordered_set>
#include "Settings.h"

using std::unordered_set;

const int MAX_SSID=32;                              // max number of characters in WiFi SSID
const int MAX_PWD=64;                               // max number of characters in WiFi Password

///////////////////////////////

struct HKNetwork {
  struct {
    char ssid[MAX_SSID+1]="";
    char pwd[MAX_PWD+1]="";
  } wifiData;
  
  char setupCode[8+1];  

  boolean allowedCode(char *s);                                             // checks if Setup Code is allowed (HAP defines a list of disallowed codes)
};