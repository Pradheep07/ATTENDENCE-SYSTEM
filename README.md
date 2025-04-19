# ATTENDENCE-SYSTEM
# PIC16F877A Access Control & Attendance System

## Overview
A microcontroller-based access control and attendance tracking system using the PIC16F877A, DS1302 real-time clock, a 4x4 matrix keypad, and a 16x2 LCD display. The system allows users to enter a 4‑digit ID to mark entry or exit, tracks time spent inside, lists present users, displays current time, and supports a secure system reset via a PIN.

## Features
- **User Identification**: 10 predefined users (Roll numbers 2301–2310).
- **Entry/Exit Tracking**: Records entry and exit times, calculates duration.
- **Present Users List**: Shows up to 30 present users (configurable).
- **Time Display**: Current time and inside count on demand.
- **Secure System Reset**: Protected by a 4‑digit PIN (default `9988`).
- **LCD Feedback**: Clear prompts and status messages on 16×2 LCD.
- **Memory-Efficient**: Bit-array for user presence and dynamic entry-time storage.

## Hardware Requirements
- **Microcontroller**: PIC16F877A
- **RTC Module**: DS1302
- **Keypad**: 4×4 matrix
- **Display**: 16×2 character LCD (HD44780-compatible)
- **Power Supply**: 5V regulated
- **Programmer**: PICkit3 or compatible
- Connecting wires, breadboard or PCB

## Pin Mapping
| PIC Pin | Signal           | Description                     |
|---------|------------------|---------------------------------|
| RA0     | DS1302_RST       | RTC chip select                 |
| RA1     | DS1302_IO        | RTC data I/O                    |
| RA2     | DS1302_CLK       | RTC clock                       |
| RC1     | LCD_RS           | LCD register select             |
| RC0     | LCD_RW           | LCD read/write (tied low)       |
| RC2     | LCD_E            | LCD enable                      |
| RD0–RD7 | LCD_DATA (D0–D7) | LCD data bus                    |
| RB0–RB3 | KEYPAD_ROWS      | Keypad row outputs              |
| RB4–RB7 | KEYPAD_COLS      | Keypad column inputs (pull-ups) |

## Software Requirements
- MPLAB X IDE
- XC8 Compiler (v2.0 or later)

## Installation & Build
1. **Clone Repository**
   ```bash
   git clone https://github.com/<your-username>/pic-access-control.git
   cd pic-access-control
   ```
2. **Open Project**
   - Launch MPLAB X, select *Open Project*, and choose the `.X` project file.
3. **Configure**
   - Ensure the oscillator is set to HS (20 MHz crystal).
   - Verify configuration bits in `main.c`:
     ```c
     #pragma config FOSC = HS      // 20 MHz High-Speed Crystal
     #pragma config WDTE = OFF     // Watchdog Timer disabled
     #pragma config PWRTE = ON     // Power-up Timer enabled
     #pragma config BOREN = OFF    // Brown-out Reset disabled
     #pragma config LVP = OFF      // Low-Voltage Programming disabled
     #pragma config CP = OFF       // Code protection off
     #pragma config WRT = OFF      // Flash write protection off
     ```
4. **Build & Program**
   - Click *Build* (hammer icon) to compile.
   - Connect PICkit3, select *Make and Program Device*.

## Usage
1. **Idle Screen**: Shows `ACCESS SYSTEM` prompt with `ID: _`.
2. **Enter ID**: Type 4‑digit roll number (e.g., `2301`). A cursor `_` will show progress.
3. **Submit (#)**
   - **Entry**: Marks entry if user not present.
   - **Exit**: Calculates and displays duration if user was inside.
   - **Errors**: Invalid ID or incomplete entry prompts an error.
4. **Clear (*)**: Cancels current input and returns to idle.
5. **Info (A)**: Shows current time and number of people inside.
6. **List (B)**: Scrolls through present users (ID, name, and time inside).
7. **Time (C)**: Displays current time full-screen.
8. **Reset (D)**: Enters secure reset PIN mode (`ENTER RESET PIN:`).
   - Type PIN (`9988`), submit with `#` to perform a full system reset.
   - Cancel with `*` to return.

## Customization
- **Users**: Edit the `users[]` array in `main.c`. Maximum 10 entries by default.
- **Reset PIN**: Change `RESET_PIN` macro.
- **Max Capacity**: Adjust `MAX_USERS` and `MAX_PRESENT_USERS` constants.

## License
This project is released under the [MIT License](LICENSE).

---
Made with ♥ using XC8 & MPLAB X.

