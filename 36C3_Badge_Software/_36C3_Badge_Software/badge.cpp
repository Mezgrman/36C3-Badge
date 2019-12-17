#include "badge.h"
#include "util.h"

Badge badge;

ISR(TIMER2_COMPA_vect) {
  // Timer 2 interrupt, called 8000 times per second

  // Handle PWM
  badge.pwmCounter++;
  if (badge.pwmCounter >= 64) badge.pwmCounter = 0;

  if (badge.pwmCounter >= badge.pwmValueDestruction1) {
    digitalWrite(badge.PIN_LED_D1, 0);
  } else  {
    digitalWrite(badge.PIN_LED_D1, 1);
  }
  if (badge.pwmCounter >= badge.pwmValueDestruction2) {
    digitalWrite(badge.PIN_LED_D2, 0);
  } else  {
    digitalWrite(badge.PIN_LED_D2, 1);
  }
  if (badge.pwmCounter >= badge.pwmValueDestruction3) {
    digitalWrite(badge.PIN_LED_D3, 0);
  } else {
    digitalWrite(badge.PIN_LED_D3, 1);
  }
  if (badge.pwmCounter >= badge.pwmValueHope1) {
    digitalWrite(badge.PIN_LED_H1, 0);
  } else  {
    digitalWrite(badge.PIN_LED_H1, 1);
  }
  if (badge.pwmCounter >= badge.pwmValueHope2) {
    digitalWrite(badge.PIN_LED_H2, 0);
  } else  {
    digitalWrite(badge.PIN_LED_H2, 1);
  }

  badge.timer2InterruptCounter++;
  if (badge.timer2InterruptCounter % 800 == 0) {
    // Called every 100ms
    badge.vfdDoScroll();
    badge.battUpdateAverage();
  }
  if (badge.timer2InterruptCounter % 8000 == 0) {
    // Called every second
    badge.timer2InterruptCounter = 0;
  }
}

Badge::Badge() {
  vfdClearBuffer();
  spiConfig = SPISettings(SPI_PARAMS);
}

void Badge::begin() {
  pinMode(PIN_VFD_RST, OUTPUT);
  pinMode(PIN_VFD_CS, OUTPUT);

  pinMode(PIN_VFD_SUP_CLK_A, OUTPUT);
  pinMode(PIN_VFD_SUP_CLK_B, OUTPUT);

  pinMode(PIN_LED_D1, OUTPUT);
  pinMode(PIN_LED_D2, OUTPUT);
  pinMode(PIN_LED_D3, OUTPUT);
  pinMode(PIN_LED_H1, OUTPUT);
  pinMode(PIN_LED_H2, OUTPUT);

  digitalWrite(PIN_VFD_RST, HIGH);
  digitalWrite(PIN_VFD_CS, HIGH);

  // Setup timer 2
  TCCR2A = 0b00000010;  // CTC mode
  TCCR2B = 0b00000010;  // F_CPU/8
  TIMSK2 = 0b00000010;  // OCIE2A (output compare interrupt A) enabled
  OCR2A  = 250;         // Gives 8000 interrupts per second

  SPI.begin();

  vfdSetSupply(1);

  vfdSPIBegin();
  vfdSendCmd(VFD_NUMDIGIT, VFD_NUM_CHARS);
  vfdSPIEnd();

  vfdSetBrightness(15);
  vfdSetTestMode(NONE);
}

void Badge::vfdSetBrightness(uint8_t level) {
  // Set the VFD brightness (0 to 15)

  if (level > 15) level = 15;
  vfdSendCmd(VFD_DUTY, level);
}

void Badge::vfdSetSupply(uint8_t state) {
  // Set the VFD anode & filament power on or off

  if (state) {
    TCCR1A = 1 << COM1A1 | 1 << COM1B1 | 1 << COM1B0; // OC2A:OC2B L:H
    TCCR1C = 1 << FOC1A | 1 << FOC1B;   // force output compare to fix phase

    TCCR1A = 1 << COM1A0 | 1 << COM1B0; // OC2A, OC2B toggle
    TCCR1B = 1 << WGM12;                // CTC

    OCR1A = SUPPLY_CLK - 1;
    OCR1B = SUPPLY_CLK - 1;

    TCCR1B |= 1 << CS11;  // F_CPU/8, start supply clock
    TCCR1C = 0x00;
  } else {
    TCCR1C = 0;
    TCCR1B = 0; // stop supply clock
    TCCR1A = 0; // OC2A:OC2B L:L, normal port operation
  }
  delay(1);
}

void Badge::vfdSetTestMode(t_VFDTestMode mode) {
  // Set the VFD test mode (all segments on, all off or normal operation)

  switch (mode) {
    case ALL_ON: {
        vfdSendCmd(VFD_LIGHTS, VFD_LI_ON);
        break;
      }
    case ALL_OFF: {
        vfdSendCmd(VFD_LIGHTS, VFD_LI_OFF);
        break;
      }
    case NONE: {
        vfdSendCmd(VFD_LIGHTS, VFD_LI_NORM);
        break;
      }
  }
}

void Badge::vfdWriteText(char* text) {
  // Output a text on the VFD

  vfdScrollLen = strlen(text);
  vfdScrollPos = VFD_NUM_CHARS - 1;

  if (vfdScrollLen > VFD_BUF_SIZE - 1) {
    vfdScrollLen = VFD_BUF_SIZE - 1;
    text[vfdScrollLen] = '\0';
  }

  vfdClearBuffer();
  strcpy(vfdBuffer, text);
  vfdUpdate();
}

void Badge::vfdSetCharacter(uint8_t addr, char* charData) {
  vfdSPIBegin();
  vfdSPISelect();
  vfdSendCmdSeq(VFD_CGRAM_WR, addr & 0x0f);
  vfdSendCmdSeq(charData[0], 0);
  vfdSendCmdSeq(charData[1], 0);
  vfdSPIDeselect();
  vfdSPIEnd();
}

void Badge::vfdSetProgressBar(uint8_t top, uint8_t bottom, uint8_t peak_top, uint8_t peak_bottom) {
  char charconf[VFD_NUM_CHARS * 2];
  int full;
  int last;
  char *ptr;
  int row;

  // Sanitize input
  if (top > VFD_NUM_CHARS * 5)
    top = VFD_NUM_CHARS * 5;
  if (bottom > VFD_NUM_CHARS * 5)
    bottom = VFD_NUM_CHARS * 5;
  if (peak_top > VFD_NUM_CHARS * 5)
    peak_top = VFD_NUM_CHARS * 5;
  if (peak_bottom > VFD_NUM_CHARS * 5)
    peak_bottom = VFD_NUM_CHARS * 5;

  // Clear memory
  for (ptr = charconf; ptr < charconf + VFD_NUM_CHARS * 2; ++ptr) {
    *ptr = 0;
  }

  // Calculate ticks
  if (peak_top != 0) {
    peak_top--;
    ptr = charconf + ((peak_top / 5) * 2);
    *(ptr++) |= vfdProgressTickChars[0][peak_top % 5][0];
    *ptr |= vfdProgressTickChars[0][peak_top % 5][1];
  }
  if (peak_bottom != 0) {
    peak_bottom--;
    ptr = charconf + ((peak_bottom / 5) * 2);
    *(ptr++) |= vfdProgressTickChars[1][peak_bottom % 5][0];
    *ptr |= vfdProgressTickChars[1][peak_bottom % 5][1];
  }

  // Calculate top memory
  for (row = 0; row < 2; ++row) {
    if (row == 0) {
      full = top / 5;
      last = top % 5;
    } else {
      full = bottom / 5;
      last = bottom % 5;
    }
    for (ptr = charconf; ptr < charconf + full * 2; ++ptr) {
      *(ptr++) |= vfdProgressBarChars[row][0][0];
      *ptr |= vfdProgressBarChars[row][0][1];
    }
    if (last != 0) {
      *(ptr++) |= vfdProgressBarChars[row][last][0];
      *ptr |= vfdProgressBarChars[row][last][1];
    }
  }

  for (uint8_t i = 0; i < VFD_NUM_CHARS; i++) {
    vfdSetCharacter(i, charconf + (i * 2));
  }
}

void Badge::vfdSetScrollSpeed(uint32_t speed) {
  // Enable scrolling on the VFD. Speed = number of 100ms intervals between movements

  vfdScrollSpeed = speed;
}

void Badge::vfdDoScroll() {
  // Advance the scroll position of the VFD (to be called by a timer interrupt)

  if (vfdScrollSpeed == 0) return;

  vfdSetScrollSpeedTickCount++;
  if (vfdSetScrollSpeedTickCount < badge.vfdScrollSpeed) return;

  vfdSetScrollSpeedTickCount = 0;

  badge.vfdUpdate();

  if (badge.vfdScrollSpeed > 0) {
    if (++badge.vfdScrollPos >= badge.vfdScrollLen)
      badge.vfdScrollPos = 0;
  }
  else {
    if (--badge.vfdScrollPos < 0)
      badge.vfdScrollPos = badge.vfdScrollLen - 1;
  }
}

char Badge::vfdGetCode(char c) {
  // Get the VFD character code for a given ASCII character

  if (c >= '@' && c <= '_')   // 64.. -> 16..
    c -= 48;
  else if (c >= ' ' && c <= '?') // 32.. -> 48..
    c += 16;
  else if (c >= 'a' && c <= 'z') // 97.. -> 17..
    c -= 80;
  else if (c > 15)  // invalid and not a custom char -> ?
    c = 79;

  return c;
}

void Badge::setCrack(t_Crack crack, uint8_t value) {
  // Set a PWM value for thegiven illuminated crack

  switch (crack) {
    case DESTRUCTION1: {
        pwmValueDestruction1 = value;
        break;
      }
    case DESTRUCTION2: {
        pwmValueDestruction2 = value;
        break;
      }
    case DESTRUCTION3: {
        pwmValueDestruction3 = value;
        break;
      }
    case HOPE1: {
        pwmValueHope1 = value;
        break;
      }
    case HOPE2: {
        pwmValueHope2 = value;
        break;
      }
  }
}

void Badge::battUpdateAverage() {
  battAverage = movingAvg(battAvgValues, &battAvgSum, battAvgPos, BATT_AVG_NUM_VALUES, analogRead(PIN_BATT_ADC));
  battAvgPos++;
  if (battAvgPos >= BATT_AVG_NUM_VALUES) battAvgPos = 0;
}

uint16_t Badge::battGetLevel() {
  // Get the battery level in mV

  return VCC_VOLTAGE * (uint32_t)battAverage / 1024.0;
}

void Badge::vfdReset() {
  // Perform a hardware reset of the VFD

  digitalWrite(PIN_VFD_RST, LOW);
  delayMicroseconds(1); // tWRES
  digitalWrite(PIN_VFD_RST, HIGH);
  delayMicroseconds(1); // tRSOFF
}

void Badge::vfdSendCmd(char cmd, char arg) {
  // Initialize an SPI transfer and send a command to the VFD

  vfdSPIBegin();
  vfdSPISelect();
  vfdSendCmdSeq(cmd, arg);
  vfdSPIDeselect();
  vfdSPIEnd();
}

void Badge::vfdSendCmdSeq(char cmd, char arg) {
  // Just send a command to the VFD (if a transfer has already been initialized)

  SPI.transfer(cmd | arg);
  delayMicroseconds(8); // tDOFF ; 1/2 tCSH
}

void Badge::vfdSendChar(char c) {
  // Send an ASCII character to the VFD

  SPI.transfer(vfdGetCode(c));
  delayMicroseconds(8); // tDOFF and 1/2 tCSH for last data
}

void Badge::vfdUpdate() {
  // Update the VFD contents

  vfdSPIBegin();
  vfdSPISelect();

  vfdSendCmdSeq(VFD_DCRAM_WR, 0);

  int16_t p = vfdScrollPos;

  for (int16_t i = 0; i < VFD_NUM_CHARS; i++) {
    if (vfdBuffer[p] == 0x00) {
      vfdSendChar(' ');
      p--;
    } else {
      vfdSendChar(vfdBuffer[p--]);
    }

    if (p < 0)
      p = vfdScrollLen - 1;
  }

  vfdSPIDeselect();
  vfdSPIEnd();
}

void Badge::vfdSPIBegin() {
  // Begin an SPI transfer to the VFD

  SPI.beginTransaction(spiConfig);
}

void Badge::vfdSPIEnd() {
  // End an SPI transfer to the VFD

  SPI.endTransaction();
}

void Badge::vfdSPISelect() {
  // Assert the VFD's chip select input

  digitalWrite(PIN_VFD_CS, LOW);
  delayMicroseconds(1); // tCSS
}

void Badge::vfdSPIDeselect() {
  // De-assert the VFD's chip select input

  delayMicroseconds(8); // 1/2 tCSH
  digitalWrite(PIN_VFD_CS, HIGH);
}

void Badge::vfdClearBuffer() {
  memset(vfdBuffer, 0x00, VFD_BUF_SIZE);
}
