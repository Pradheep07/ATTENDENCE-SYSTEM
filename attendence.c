#include <xc.h>
#include <string.h> // Required for strcmp

#pragma config FOSC = HS      // Oscillator Selection bits (HS oscillator)
#pragma config WDTE = OFF     // Watchdog Timer Enable bit (WDT disabled)
#pragma config PWRTE = ON     // Power-up Timer Enable bit (PWRT enabled)
#pragma config CP = OFF       // FLASH Program Memory Code Protection bit (Code protection off)
#pragma config BOREN = OFF    // Brown-out Reset Enable bit (BOR disabled)
#pragma config LVP = OFF      // Low-Voltage (Single-Supply) In-Circuit Serial Programming Enable bit (RB3 is digital I/O, HV on MCLR must be used for programming)
#pragma config CPD = OFF      // Data EEPROM Memory Code Protection bit (Data EEPROM code protection off)
#pragma config WRT = OFF      // Flash Program Memory Write Enable bits (Write protection off; all program memory may be written to by EECON control)

#define _XTAL_FREQ 20000000

// DS1302 pin mapping
#define DS1302_RST  RA0
#define DS1302_IO   RA1
#define DS1302_CLK  RA2

// LCD pin mapping
#define LCD_RS RC1
#define LCD_RW RC0
#define LCD_E  RC2
#define LCD_PORT PORTD

// --- Constants ---
#define MAX_USERS 10
#define MAX_PRESENT_USERS 10
const char RESET_PIN[5] = "9988"; // Security PIN for reset

// Function prototypes
void delay_ms(unsigned int ms);
void delay_us(unsigned int us);
void LCD_Data(unsigned char data);
void LCD_Cmd(unsigned char cmd);
void LCD_Init();
void LCD_SetCursor(char row, char col);
void Send2Lcd(const char Adr, const char *Lcd);
void clearSecondLine(); // Now essentially sends 16 spaces to 0xC0
void clearLine1();      // Sends 16 spaces to 0x80
void padLine(char startAddress, unsigned char writtenChars); // Helper for padding
void processKey(char key);
char* findUserName(char* rollNo);
int getUserIndex(char* rollNo);
void resetDisplay();
void formatTimeFromSeconds(unsigned int totalSeconds, char* timeStr);
void performSystemReset(); // Moved actual reset logic here

// DS1302 RTC Functions
void DS1302_Init();
void DS1302_WriteByte(unsigned char data);
unsigned char DS1302_ReadByte();
void DS1302_Write(unsigned char cmd, unsigned char data);
unsigned char DS1302_Read(unsigned char cmd);
unsigned char BCD_to_Dec(unsigned char bcd);
unsigned char Dec_to_BCD(unsigned char dec);
void getTimeString(char* timeStr); // HH:MM:SS (8 chars + null)
unsigned int getCurrentTimeInSeconds();

// Global variables
unsigned int peoplePresent = 0; // Count of people currently inside
char currentID[5] = ""; // To store user ID (4 digits + null)
unsigned int idPos = 0; // Position in ID entry

// --- PIN Entry State ---
unsigned char pinEntryMode = 0; // 0 = Normal, 1 = Waiting for Reset PIN
char currentPin[5] = ""; // Buffer for entered PIN
unsigned char pinPos = 0; // Position in PIN entry

// Predefined users (Roll Number to Name mapping)
typedef struct {
    char rollNo[5];
    char name[17];  // Max 16 chars for LCD display + null terminator
} User;

// Store user data in program memory instead of RAM
const User users[MAX_USERS] = {
    {"2301", "Aarav"},    {"2302", "Diya"},     {"2303", "Arjun"},
    {"2304", "Ananya"},   {"2305", "Ishaan"},   {"2306", "Siya"},
    {"2307", "Vihaan"},   {"2308", "Aanya"},    {"2309", "Advait"},
    {"2310", "Avni"}
};

// MEMORY OPTIMIZATION: Use bit array for status (1 bit per user)
// and separate tracking for entry times only for present users
typedef struct {
    unsigned char statusBits[4];  // 32 bits (4 bytes) for up to 32 users' status
    unsigned int entryTimes[MAX_PRESENT_USERS];  // Entry times for present users (Size increased)
    unsigned char entryUserIndex[MAX_PRESENT_USERS];  // Which user each entry time belongs to (index + 1, 0=empty) (Size increased)
} StatusTracking;

StatusTracking presence = {0}; // Initialize all to zero

// Helper functions for bit manipulation
void setUserPresent(unsigned char userIndex, unsigned char present) {
    if (userIndex >= MAX_USERS) return; // Bounds check
    unsigned char byteIndex = userIndex / 8;
    unsigned char bitPos = userIndex % 8;
    if(present) { presence.statusBits[byteIndex] |= (1 << bitPos); }
    else { presence.statusBits[byteIndex] &= ~(1 << bitPos); }
}

unsigned char isUserPresent(unsigned char userIndex) {
    if (userIndex >= MAX_USERS) return 0; // Bounds check
    unsigned char byteIndex = userIndex / 8;
    unsigned char bitPos = userIndex % 8;
    return (presence.statusBits[byteIndex] & (1 << bitPos)) ? 1 : 0;
}

unsigned char addEntryTime(unsigned char userIndex, unsigned int entryTime) {
    if(peoplePresent >= MAX_PRESENT_USERS) return 0; // Check against the max PRESENT users limit
    for (unsigned char slot = 0; slot < MAX_PRESENT_USERS; slot++) {
        if (presence.entryUserIndex[slot] == 0) {
            presence.entryUserIndex[slot] = userIndex + 1; // Store index+1
            presence.entryTimes[slot] = entryTime;
            return 1; // Success
        }
    }
    return 0; // No empty slot found (shouldn't happen if peoplePresent is accurate)
}

unsigned int getEntryTime(unsigned char userIndex) {
    for(unsigned char i = 0; i < MAX_PRESENT_USERS; i++) {
        if(presence.entryUserIndex[i] == userIndex + 1) { // Compare with index+1
            return presence.entryTimes[i];
        }
    }
    return 0; // Not found (user isn't currently marked as present with a time)
}

void removeEntryTime(unsigned char userIndex) {
    for(unsigned char i = 0; i < MAX_PRESENT_USERS; i++) {
        if(presence.entryUserIndex[i] == userIndex + 1) { // Compare with index+1
            presence.entryUserIndex[i] = 0; // Mark slot as empty
            presence.entryTimes[i] = 0;     // Clear time
            return; // Found and removed
        }
    }
}

// --- Interrupt Service Routine (Placeholder) ---
void __interrupt() isr() {
    GIE = 0; // Disable interrupts during ISR (Good practice)
    // Add interrupt flag checks and handlers here if needed
    // Example: if(PIR1bits.TMR1IF) { /* handle timer1 */ PIR1bits.TMR1IF = 0; }
    GIE = 1; // Re-enable interrupts before exiting if required by application logic
}

void main()
{
    char keyValues[4][4] = {
        {'1', '2', '3', 'A'}, {'4', '5', '6', 'B'},
        {'7', '8', '9', 'C'}, {'*', '0', '#', 'D'}
    };

    // --- Port Initialization ---
    TRISA = 0x02;  // RA1 (DS1302_IO) needs input capability. Others output.
    TRISC = 0x00;  // PORTC (LCD Control) -> Output
    TRISD = 0x00;  // PORTD (LCD Data) -> Output
    TRISB = 0xF0;  // RB7-RB4 (Keypad Cols) -> Input, RB3-RB0 (Keypad Rows) -> Output

    // --- Peripheral Setup ---
    ADCON1 = 0x06; // Configure PORTA pins as digital I/O on PIC16F877A
    OPTION_REGbits.nRBPU = 0; // Enable PORTB pull-ups for keypad columns

    // --- Module Initialization ---
    LCD_Init();
    DS1302_Init();

    resetDisplay(); // Show the initial welcome screen
    // --- Main Loop ---
    while(1)
    {
        char key = '\0'; // Store detected key press

        // Keypad Scanning Logic (Row by Row) - Standard polling
        PORTB = 0b11111110; // Activate Row 0 (RB0=0)
        if (RB4 == 0) { key = keyValues[0][0]; while(RB4==0); }
        else if (RB5 == 0) { key = keyValues[0][1]; while(RB5==0); }
        else if (RB6 == 0) { key = keyValues[0][2]; while(RB6==0); }
        else if (RB7 == 0) { key = keyValues[0][3]; while(RB7==0); }

        if(key == '\0') {
            PORTB = 0b11111101; // Activate Row 1 (RB1=0)
            if (RB4 == 0) { key = keyValues[1][0]; while(RB4==0); }
            else if (RB5 == 0) { key = keyValues[1][1]; while(RB5==0); }
            else if (RB6 == 0) { key = keyValues[1][2]; while(RB6==0); }
            else if (RB7 == 0) { key = keyValues[1][3]; while(RB7==0); }
        }

        if(key == '\0') {
            PORTB = 0b11111011; // Activate Row 2 (RB2=0)
            if (RB4 == 0) { key = keyValues[2][0]; while(RB4==0); }
            else if (RB5 == 0) { key = keyValues[2][1]; while(RB5==0); }
            else if (RB6 == 0) { key = keyValues[2][2]; while(RB6==0); }
            else if (RB7 == 0) { key = keyValues[2][3]; while(RB7==0); }
        }

        if(key == '\0') {
            PORTB = 0b11110111; // Activate Row 3 (RB3=0)
            if (RB4 == 0) { key = keyValues[3][0]; while(RB4==0); }
            else if (RB5 == 0) { key = keyValues[3][1]; while(RB5==0); }
            else if (RB6 == 0) { key = keyValues[3][2]; while(RB6==0); }
            else if (RB7 == 0) { key = keyValues[3][3]; while(RB7==0); }
        }

        // Process detected key press
        if(key != '\0') {
            processKey(key);
            delay_ms(150); // Debounce/delay after processing a key
        }
         delay_ms(20); // Small delay between scans to reduce resource usage
    }
}

// Restore default display, ensuring lines are padded/cleared
void resetDisplay() {
    Send2Lcd(0x80, " ACCESS SYSTEM  "); // 16 Chars Centered
    Send2Lcd(0xC0, "ID: _           "); // 16 Chars, cursor at pos 4
    idPos = 0;
    currentID[0] = '\0'; // Clear internal ID buffer
    pinEntryMode = 0; // Exit PIN entry mode if active
    pinPos = 0;
    currentPin[0] = '\0'; // Clear internal PIN buffer
}

// Helper to pad the rest of a line with spaces
void padLine(char startAddress, unsigned char writtenChars) {
    char currentPos = startAddress + writtenChars;
    // Calculate end position based on line (0x80-0x8F for line 1, 0xC0-0xCF for line 2)
    char endPos = (startAddress < 0xC0) ? (0x80 + 16) : (0xC0 + 16);
    while (currentPos < endPos) {
        LCD_SetCursor(0, currentPos); // Use SetCursor for simplicity here
        LCD_Data(' ');
        currentPos++;
    }
}

// Process keypad input with enhanced visuals, padding, and PIN mode
void processKey(char key) {
    // --- Digit Entry (0-9) ---
    if(key >= '0' && key <= '9') {
        if (pinEntryMode) { // --- PIN Entry Mode ---
            if (pinPos < 4) {
                currentPin[pinPos++] = key;
                currentPin[pinPos] = '\0';

                // Display: "PIN: 123_"
                LCD_Cmd(0xC0); // Go to start of line 2
                LCD_Data('P'); LCD_Data('I'); LCD_Data('N'); LCD_Data(':'); LCD_Data(' '); // "PIN: "
                unsigned char charsWritten = 5;
                for(unsigned char i=0; i<pinPos; i++) { LCD_Data(currentPin[i]); charsWritten++; } // Display digits

                if(pinPos < 4) {
                    LCD_Data('_'); // Show cursor
                    charsWritten++;
                }
                padLine(0xC0, charsWritten); // Pad rest of line 2
            }
        } else { // --- Normal ID Entry Mode ---
            if(idPos < 4) {
                currentID[idPos++] = key;
                currentID[idPos] = '\0'; // Null terminate

                // Display: "ID: 123_"
                LCD_Cmd(0xC0); // Go to start of line 2
                LCD_Data('I'); LCD_Data('D'); LCD_Data(':'); LCD_Data(' '); // "ID: "
                unsigned char charsWritten = 4;
                for(unsigned char i=0; i<idPos; i++) { LCD_Data(currentID[i]); charsWritten++; } // Display digits

                if(idPos < 4) {
                    LCD_Data('_'); // Show cursor
                    charsWritten++;
                }
                padLine(0xC0, charsWritten); // Pad rest of line 2
            }
            // Ignore digits if 4 already entered
        }
    }
    // --- Submit Key (#) ---
    else if(key == '#') {
        if (pinEntryMode) { // --- Submit PIN ---
            if (pinPos == 4) {
                // Compare entered PIN with RESET_PIN
                int match = 1;
                for(int i=0; i<4; i++){
                    if(currentPin[i] != RESET_PIN[i]) {
                        match = 0;
                        break;
                    }
                }

                if (match) {
                    // PIN Correct - Perform Reset
                    performSystemReset(); // Calls resetDisplay at the end
                } else {
                    // PIN Incorrect
                    Send2Lcd(0x80, "   RESET DENIED "); // 16 Chars
                    Send2Lcd(0xC0, "  INVALID PIN!  "); // 16 Chars
                    delay_ms(1500);
                    resetDisplay(); // Go back to initial state
                }
            } else { // Incomplete PIN
                Send2Lcd(0x80, "    ERROR!      ");
                Send2Lcd(0xC0, " ENTER 4 DIGITS ");
                delay_ms(1000);
                 // Restore PIN entry prompt
                Send2Lcd(0x80, "ENTER RESET PIN:");
                LCD_Cmd(0xC0); LCD_Data('P'); LCD_Data('I'); LCD_Data('N'); LCD_Data(':'); LCD_Data(' ');
                unsigned char charsWritten = 5;
                for(unsigned char i=0; i<pinPos; i++) { LCD_Data(currentPin[i]); charsWritten++; }
                LCD_Data('_'); charsWritten++;
                padLine(0xC0, charsWritten);
            }

        } else { // --- Submit ID ---
            if(idPos == 4) { // Process only if 4 digits entered
                char* userName = findUserName(currentID);

                if(userName[0] != 'U' || userName[1] != 'n') { // Check if user is known (basic check)
                    // Indicate processing
                    Send2Lcd(0x80, "  PROCESSING... "); // 16 Chars
                    Send2Lcd(0xC0, "                "); // Clear line 2
                    delay_ms(300); // Short delay

                    // Line 1: Display "ID: XXXX NamePart"
                    LCD_Cmd(0x80);
                    LCD_Data('I'); LCD_Data('D'); LCD_Data(':'); LCD_Data(' '); // "ID: "
                    for(int i=0; i<4; i++) { LCD_Data(currentID[i]); } // "ID: 1234"
                    LCD_Data(' '); // Space after ID
                    unsigned char line1Chars = 10; // Chars written so far: "ID: 1234 "

                    // Display first part of name (up to 6 chars to fit)
                    for(int i = 0; i < 6 && userName[i] != '\0'; i++) {
                        LCD_Data(userName[i]);
                        line1Chars++;
                    }
                    padLine(0x80, line1Chars); // Pad rest of line 1


                    // Line 2: Process Entry/Exit
                    int userIndex = getUserIndex(currentID); // Should be >= 0 here
                    char timeStr[9];
                    getTimeString(timeStr); // Get HH:MM:SS
                    unsigned int currentTime = getCurrentTimeInSeconds();

                    if (userIndex != -1) { // Double check index validity
                        if(!isUserPresent(userIndex)) { // --- Process Entry ---
                            if (peoplePresent < MAX_PRESENT_USERS) { // Check against new limit
                                setUserPresent(userIndex, 1);
                                addEntryTime(userIndex, currentTime);
                                peoplePresent++;
                                // Display: "ENTRY: HH:MM:SS"
                                Send2Lcd(0xC0, "ENTRY: ");       // 7 Chars
                                Send2Lcd(0xC7, timeStr);       // 8 Chars (HH:MM:SS)
                                padLine(0xC0, 7 + 8);          // Pad rest (1 char)
                            } else {
                                Send2Lcd(0xC0, " ACCESS DENIED! "); // 16 Chars
                                Send2Lcd(0x80, " MAXIMUM INSIDE "); // Update line 1 too
                            }
                        } else { // --- Process Exit ---
                            setUserPresent(userIndex, 0);
                            unsigned int entryTime = getEntryTime(userIndex);
                            removeEntryTime(userIndex); // Remove before decrementing count
                            if (peoplePresent > 0) peoplePresent--;

                            // Calculate time spent
                            unsigned int timeSpent;
                            // Handle midnight rollover
                            if (currentTime < entryTime) { timeSpent = (86400u - entryTime) + currentTime; }
                            else { timeSpent = currentTime - entryTime; }

                            // Display: "EXIT: HH:MM:SS "
                            Send2Lcd(0xC0, "EXIT: ");         // 6 Chars
                            Send2Lcd(0xC6, timeStr);         // 8 Chars (HH:MM:SS)
                            padLine(0xC0, 6 + 8);            // Pad rest (2 chars)
                            delay_ms(1000); // Show exit time

                            // Display: "DUR: HH:MM:SS   "
                            char durationStr[9];
                            formatTimeFromSeconds(timeSpent, durationStr);
                            Send2Lcd(0xC0, "DUR: ");          // 5 Chars
                            Send2Lcd(0xC5, durationStr);     // 8 Chars (HH:MM:SS)
                            padLine(0xC0, 5 + 8);            // Pad rest (3 chars)
                        }
                        delay_ms(1500); // Display result (Entry/Exit/Duration) longer
                    } else {
                         // This case should technically not happen if findUserName worked, but good practice
                        Send2Lcd(0x80, "   SYSTEM ERROR ");
                        Send2Lcd(0xC0, "  USER IDX FAIL ");
                        delay_ms(1500);
                    }
                } else { // --- Invalid ID Entered ---
                    Send2Lcd(0x80, "    ERROR!      "); // 16 Chars Centered
                    Send2Lcd(0xC0, "  INVALID ID    "); // 16 Chars Centered
                    delay_ms(1000);
                }
                resetDisplay(); // Reset for next input after processing or error

            } else { // --- Incomplete ID Entered ---
                Send2Lcd(0x80, "    ERROR!      "); // 16 Chars Centered
                Send2Lcd(0xC0, " ENTER 4 DIGITS "); // 16 Chars Centered
                delay_ms(1000);

                // Restore previous partial entry screen
                Send2Lcd(0x80, " ACCESS SYSTEM  "); // Restore line 1
                LCD_Cmd(0xC0); LCD_Data('I'); LCD_Data('D'); LCD_Data(':'); LCD_Data(' ');
                unsigned char charsWritten = 4;
                for(unsigned char i=0; i<idPos; i++) { LCD_Data(currentID[i]); charsWritten++; }
                LCD_Data('_'); charsWritten++;
                padLine(0xC0, charsWritten);
            }
        } // End ID submit
    }
    // --- Clear Key (*) ---
    else if(key == '*') {
         if (pinEntryMode) {
             // Cancel PIN entry
             resetDisplay();
         } else {
            // Clear ID entry
             resetDisplay();
         }
    }
    // --- Info Key (A) ---
    else if(key == 'A') {
        if (pinEntryMode) return; // Ignore during PIN entry

        char timeStr[9];
        getTimeString(timeStr);
        Send2Lcd(0x80, "TIME: ");         // 6 Chars
        Send2Lcd(0x86, timeStr);         // 8 Chars (HH:MM:SS)
        padLine(0x80, 6 + 8);            // Pad rest (2 chars)

        // Convert count to string (Handles up to 99, as MAX_PRESENT_USERS is 30)
        char countStr[3]; // NN + null
        unsigned char countLen = 0;
        if (peoplePresent == 0) { countStr[0] = '0'; countLen = 1; }
        else if (peoplePresent < 10) { countStr[0] = peoplePresent + '0'; countLen = 1; }
        else { // 10-30
            countStr[0] = (peoplePresent / 10) + '0';
            countStr[1] = (peoplePresent % 10) + '0';
            countLen = 2;
        }
        countStr[countLen] = '\0';

        Send2Lcd(0xC0, "INSIDE: ");       // 8 Chars
        Send2Lcd(0xC8, countStr);        // 1 or 2 Chars
        padLine(0xC0, 8 + countLen);     // Pad rest
        delay_ms(1500); // Display info longer
        resetDisplay();
    }
    // --- List Key (B) ---
    else if(key == 'B') {
        if (pinEntryMode) return; // Ignore during PIN entry

        unsigned char shownCount = 0;
        unsigned char firstFound = 0;
        unsigned char displayIndex = 0; // Index for numbering on screen (1, 2, 3...)

        for(int i = 0; i < MAX_USERS; i++) {
            if(isUserPresent(i)) {
                 if (!firstFound) { // Display header only once
                     Send2Lcd(0x80, "PRESENT USERS:  "); // 16 Chars
                     Send2Lcd(0xC0, "                "); // Clear line 2
                     firstFound = 1;
                     delay_ms(500); // Brief pause on header
                 }
                 displayIndex++;

                // --- Get User Data ---
                char rollNo[5];
                for(int j = 0; j < 4; j++) { rollNo[j] = users[i].rollNo[j]; }
                rollNo[4] = '\0';

                char name[17];
                for(int j = 0; j < 16; j++) { name[j] = users[i].name[j]; if(name[j] == '\0') break; }
                name[16] = '\0';

                // --- Display Part 1: "N: 2301 NamePart" ---
                LCD_Cmd(0x80);
                // Handle index display (up to 2 digits needed now)
                char indexStr[3]; unsigned char indexLen = 0;
                if (displayIndex < 10) { indexStr[0] = displayIndex + '0'; indexLen=1; }
                else { indexStr[0] = (displayIndex / 10) + '0'; indexStr[1] = (displayIndex % 10) + '0'; indexLen=2;}
                indexStr[indexLen] = '\0';
                for(int k=0; k<indexLen; k++) { LCD_Data(indexStr[k]); } // N or NN
                LCD_Data(':'); LCD_Data(' '); // ": "
                unsigned char line1Chars = indexLen + 2;

                for(int j=0; j<4; j++) { LCD_Data(rollNo[j]); } // "N: 1234"
                line1Chars += 4;
                LCD_Data(' '); // Space
                line1Chars++;

                // Display first part of name (adjust chars based on index len)
                unsigned char nameMaxChars = 16 - line1Chars;
                for(int j = 0; j < nameMaxChars && name[j] != '\0'; j++) {
                    LCD_Data(name[j]);
                    line1Chars++;
                }
                padLine(0x80, line1Chars); // Pad rest of line 1

                 // --- Display Part 2: "TIME: HH:MM:SS " ---
                 unsigned int currentTime = getCurrentTimeInSeconds();
                 unsigned int entryTime = getEntryTime(i);
                 unsigned int timeElapsed;
                 if (entryTime == 0 && !isUserPresent(i)) { // Should not happen, but safety
                    timeElapsed = 0;
                 } else if (currentTime < entryTime) { // Handle midnight rollover
                    timeElapsed = (86400u - entryTime) + currentTime;
                 } else {
                     timeElapsed = currentTime - entryTime;
                 }
                 char durationStr[9];
                 formatTimeFromSeconds(timeElapsed, durationStr);

                 Send2Lcd(0xC0, "TIME: ");         // 6 Chars
                 Send2Lcd(0xC6, durationStr);     // 8 Chars
                 padLine(0xC0, 6 + 8);            // Pad rest (2 chars)

                 delay_ms(2000); // Pause to show current user's info (ID/Name + Time)

                 shownCount++; // Increment count of users actually displayed in this pass

                 // --- Check if need to prompt for more ---
                 // Display up to ~5 at a time before prompting (adjust as needed)
                 if(shownCount >= 5 && displayIndex < peoplePresent) {
                     Send2Lcd(0x80, "PRESS B FOR MORE"); // 16 Chars

                     // Show total count on second line: "INSIDE: N    "
                      char countStr[3]; unsigned char countLen = 0;
                      if (peoplePresent < 10) { countStr[0] = peoplePresent + '0'; countLen = 1; }
                      else { countStr[0] = (peoplePresent / 10) + '0'; countStr[1] = (peoplePresent % 10) + '0'; countLen = 2; }
                      countStr[countLen] = '\0';
                      Send2Lcd(0xC0, "INSIDE: ");
                      Send2Lcd(0xC8, countStr);
                      padLine(0xC0, 8 + countLen);

                     delay_ms(1500);
                     goto endListDisplay_B; // Exit loop cleanly after prompt
                 }
             } // End if(isUserPresent)
         } // End for loop

         // --- After Loop: Handle cases ---
         if (displayIndex == 0) { // No users found inside
             Send2Lcd(0x80, "STATUS:         "); // 16 Chars
             Send2Lcd(0xC0, "NO USERS INSIDE "); // 16 Chars
             delay_ms(1500);
         } else if (shownCount < 5) { // Only needed if we showed all users and it was less than 5
             delay_ms(500); // Brief pause after showing the last user if list is short
         }

     endListDisplay_B: // Label to jump to after prompt or finishing list
         resetDisplay();

    }
    // --- Time Key (C) ---
    else if(key == 'C') {
        if (pinEntryMode) return; // Ignore during PIN entry

        char timeStr[9];
        getTimeString(timeStr);
        Send2Lcd(0x80, " CURRENT TIME:  "); // 16 Chars
        Send2Lcd(0xC0, "   "); // Pad left (3 spaces)
        Send2Lcd(0xC3, timeStr); // HH:MM:SS (8 chars) at col 3
        padLine(0xC0, 3 + 8);    // Pad rest (5 chars)
        delay_ms(2000); // Show time longer
        resetDisplay();
    }
    // --- Reset Key (D) ---
    else if(key == 'D') {
        if (pinEntryMode) { // Already in PIN mode, maybe user pressed D again?
             // Reset PIN entry state without full system reset
             pinPos = 0;
             currentPin[0] = '\0';
             // Re-display prompt
             Send2Lcd(0x80, "ENTER RESET PIN:"); // 16 Chars
             Send2Lcd(0xC0, "PIN: _          "); // 16 Chars
        } else {
            // --- Initiate PIN Entry ---
            pinEntryMode = 1;
            pinPos = 0;
            currentPin[0] = '\0';
            Send2Lcd(0x80, "ENTER RESET PIN:"); // 16 Chars
            Send2Lcd(0xC0, "PIN: _          "); // 16 Chars
        }
    }
}

// --- Actual System Reset Logic ---
void performSystemReset() {
    Send2Lcd(0x80, " SYSTEM RESET   "); // 16 Chars
    Send2Lcd(0xC0, "PLEASE WAIT...  "); // 16 Chars

    // Visual progress (optional but nice)
    delay_ms(500);
    LCD_Cmd(0xC0); // Go to start of line 2
    for(int i = 0; i < 16; i++) {
        LCD_Data('*');
        delay_ms(80); // Slow progress bar
    }

    // Perform actual reset of state
    peoplePresent = 0;
    // Clear status bits
    for(unsigned char i = 0; i < sizeof(presence.statusBits); i++) { presence.statusBits[i] = 0; }
    // Clear entry time tracking
    for(unsigned char i = 0; i < MAX_PRESENT_USERS; i++) {
        presence.entryUserIndex[i] = 0;
        presence.entryTimes[i] = 0;
    }
    delay_ms(500); // Pause after reset visual
    resetDisplay(); // Reset LCD and state variables (including pinEntryMode)
}


// ------------------ DS1302 Functions (Keep as before) ------------------
void DS1302_Init() {
    DS1302_RST = 0; DS1302_CLK = 0;
    TRISA &= ~((1 << 0) | (1 << 2)); // Ensure RST, CLK are output
    TRISA &= ~(1 << 1); // Ensure IO is initially output
}
void DS1302_WriteByte(unsigned char data) {
     TRISA &= ~(1 << 1); // IO as output
    for (char i = 0; i < 8; i++) {
        DS1302_IO = (data >> i) & 1;
        delay_us(1); DS1302_CLK = 1; delay_us(1); DS1302_CLK = 0; delay_us(1);
    }
}
unsigned char DS1302_ReadByte() {
    unsigned char value = 0;
    TRISA |= (1 << 1); delay_us(1); // IO as input
    for (char i = 0; i < 8; i++) {
        if (DS1302_IO) value |= (1 << i);
        DS1302_CLK = 1; delay_us(1); DS1302_CLK = 0; delay_us(1);
    }
    TRISA &= ~(1 << 1); DS1302_IO = 0; // IO back to output low
    return value;
}
void DS1302_Write(unsigned char cmd, unsigned char data) {
    DS1302_RST = 1; delay_us(4);
    DS1302_WriteByte(cmd); DS1302_WriteByte(data);
    DS1302_RST = 0; delay_us(4);
}
unsigned char DS1302_Read(unsigned char cmd) {
    unsigned char data;
    DS1302_RST = 1; delay_us(4);
    DS1302_WriteByte(cmd | 0x01); // Read command
    data = DS1302_ReadByte();
    DS1302_RST = 0; delay_us(4);
    return data;
}
// Convert BCD to Decimal
unsigned char BCD_to_Dec(unsigned char bcd) { return ((bcd >> 4) * 10) + (bcd & 0x0F); }
// Convert Decimal to BCD (Clamps input > 99)
unsigned char Dec_to_BCD(unsigned char dec) { if (dec > 99) dec = 99; return (unsigned char)(((dec / 10) << 4) | (dec % 10)); }

// Get time string HH:MM:SS (8 chars + null)
void getTimeString(char* timeStr) {
    unsigned char sec = BCD_to_Dec(DS1302_Read(0x81) & 0x7F); // Mask CH bit
    unsigned char min = BCD_to_Dec(DS1302_Read(0x83));
    unsigned char hr  = BCD_to_Dec(DS1302_Read(0x85) & 0x3F); // Assuming 24hr mode
    timeStr[0] = (hr / 10) + '0'; timeStr[1] = (hr % 10) + '0'; timeStr[2] = ':';
    timeStr[3] = (min / 10) + '0'; timeStr[4] = (min % 10) + '0'; timeStr[5] = ':';
    timeStr[6] = (sec / 10) + '0'; timeStr[7] = (sec % 10) + '0'; timeStr[8] = '\0';
}
// Get current time in seconds since midnight
unsigned int getCurrentTimeInSeconds() {
    unsigned char sec = BCD_to_Dec(DS1302_Read(0x81) & 0x7F);
    unsigned char min = BCD_to_Dec(DS1302_Read(0x83));
    unsigned char hr  = BCD_to_Dec(DS1302_Read(0x85) & 0x3F);
    return (unsigned int)hr * 3600u + (unsigned int)min * 60u + (unsigned int)sec;
}
// Format seconds to HH:MM:SS string (8 chars + null)
void formatTimeFromSeconds(unsigned int totalSeconds, char* timeStr) {
    totalSeconds %= 86400u; // Ensure wrap around 24 hours for display
    unsigned int hours = totalSeconds / 3600u;
    unsigned int minutes = (totalSeconds % 3600u) / 60u;
    unsigned int seconds = totalSeconds % 60u;
    timeStr[0] = (hours / 10) + '0'; timeStr[1] = (hours % 10) + '0'; timeStr[2] = ':';
    timeStr[3] = (minutes / 10) + '0'; timeStr[4] = (minutes % 10) + '0'; timeStr[5] = ':';
    timeStr[6] = (seconds / 10) + '0'; timeStr[7] = (seconds % 10) + '0'; timeStr[8] = '\0';
}

// Find user name by roll number (using static buffer for return)
// Returns pointer to static buffer with name, or "Unknown User"
char* findUserName(char* rollNo) {
    static char nameBuffer[17]; // Static buffer to return name from const memory
    for(int i = 0; i < MAX_USERS; i++) {
        int match = 1;
        for(int j = 0; j < 4; j++) {
             // Check input against const user data
            if(users[i].rollNo[j] != rollNo[j]) {
                match = 0;
                break;
            }
        }
        // Ensure the stored roll number is also exactly 4 characters long before null terminator
        if(match && users[i].rollNo[4] == '\0') {
            // Found: Copy name from const memory to static buffer
            for(int k = 0; k < 16; k++) {
                nameBuffer[k] = users[i].name[k];
                if(nameBuffer[k] == '\0') break; // Stop copying at null terminator
            }
            nameBuffer[16] = '\0'; // Ensure null termination
            return nameBuffer;
        }
    }
    // Not found: Copy "Unknown User" to the static buffer
    const char* unknown = "Unknown User";
    for(int k=0; k<16; k++){ // Copy up to 16 chars or null terminator
        nameBuffer[k]=unknown[k];
        if(!unknown[k]) break;
    }
    nameBuffer[16] = '\0'; // Ensure null termination
    return nameBuffer;
}

// Get user index by roll number
int getUserIndex(char* rollNo) {
    for(int i = 0; i < MAX_USERS; i++) {
        int match = 1;
        for(int j = 0; j < 4; j++) {
            if(users[i].rollNo[j] != rollNo[j]) {
                match = 0;
                break;
            }
        }
         if(match && users[i].rollNo[4] == '\0') {
             return i; // Match found, return index
         }
    }
    return -1; // Not found
}

// Clear the second line of the LCD by writing 16 spaces
void clearSecondLine() { Send2Lcd(0xC0, "                "); }
// Clear the first line of the LCD by writing 16 spaces
void clearLine1() { Send2Lcd(0x80, "                "); }

// ------------------ LCD Functions (Keep as before) ------------------
void LCD_Cmd(unsigned char cmd) {
    LCD_RS = 0; LCD_RW = 0; LCD_PORT = cmd;
    LCD_E = 1; delay_us(1); LCD_E = 0;
    // Use longer delay only for specific commands like clear/home
    if (cmd == 0x01 || cmd == 0x02) delay_ms(2); else delay_us(50);
}
void LCD_Data(unsigned char data) {
    LCD_RS = 1; LCD_RW = 0; LCD_PORT = data;
    LCD_E = 1; delay_us(1); LCD_E = 0;
    delay_us(50); // Delay for data write processing
}
void LCD_Init() {
    LCD_E = 0; LCD_RS = 0; LCD_RW = 0; delay_ms(20); // Power on delay
    LCD_Cmd(0x38); delay_ms(5);   // Function Set: 8-bit, 2 Line, 5x7 dots
    LCD_Cmd(0x38); delay_us(150); // Repeat Function Set
    LCD_Cmd(0x38); delay_us(150); // Repeat Function Set
    LCD_Cmd(0x0C); delay_us(150); // Display ON, Cursor OFF, Blink OFF
    LCD_Cmd(0x01); delay_ms(2);   // Clear Display Screen
    LCD_Cmd(0x06); delay_us(150); // Entry Mode Set: Increment cursor, No shift
}
// Set cursor position (row 0 or 1, col 0-15)
void LCD_SetCursor(char row, char col) {
    if (col >= 16) col = 15; // Limit column
    if (col < 0) col = 0;
    if (row < 0) row = 0;
    if (row > 1) row = 1;
    LCD_Cmd(((row == 0) ? 0x80 : 0xC0) + col);
}
// Send string to LCD starting at specified address (0x80 or 0xC0 based)
void Send2Lcd(const char Adr, const char *Lcd) {
    LCD_Cmd(Adr); // Set cursor position
    while(*Lcd) { // While character is not null terminator
        LCD_Data(*Lcd++); // Send character and increment pointer
    }
}

// ------------------ Delay Functions (Optimized slightly for 20MHz) ------------------

void delay_ms(unsigned int ms) {
    while(ms--) {

        volatile unsigned int i;
        // The exact count (1660) depends on compiler, adjust if needed
        for(i = 0; i < 1660; i++);
     }
}

void delay_us(unsigned int us) {

     while(us--) {
        _nop(); _nop(); _nop(); // ~3 cycles

     }
}
