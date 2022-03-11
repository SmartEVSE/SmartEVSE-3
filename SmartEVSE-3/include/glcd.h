/*
;	 Project:       Smart EVSE
;
;
;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in
; all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
; THE SOFTWARE.
*/

#ifndef __GLCD_H
#define __GLCD_H

#define GLCD_MERGE 0b00001000
#define GLCD_HIRES_FONT
#define GLCD_FULL_CHARSET
#define GLCD_ALIGN_LEFT   0
#define GLCD_ALIGN_CENTER 1
#define GLCD_ALIGN_RIGHT  2

extern void GLCDHelp(void);
extern void GLCD(void);
extern void GLCDMenu(unsigned char Buttons);
extern void GLCD_init(void);
extern void GLCD_version(void);


#endif // #ifndef __GLCD_H 