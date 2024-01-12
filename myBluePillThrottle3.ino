/*
  myBluePillThrottle3.ino
  
  Simple DCC-EX tethered throttle using Blue Pill, 5x4 keypad, 
  and OLED display.  

  The action of the "Brake" mode in this version emulates the
  Pause button in Engine Driver.

  Modified to invert direction keys in Run mode using Esc key.

  Andrew Palm, 1/12/2024

  Only activates decoder functions 0 to 9, and toggled
  between on and off.

  There are three modes, Run, Fn, and Addr, as follows:

  Run mode:  Primary mode of operation with keypad assignments:

    F1 - Enter Fn mode
    # - Toggle track power
    * - Enter Addr mode
    UP - Increase speed (inactive while braked)
    DOWN - Decrease speed (inactive while braked)
    Esc - Invert direction keys (toggle function)
    Ent - Stop, set speed to zero
    LEFT - Go Forward direction
    0 - Toggle brake
    RIGHT - Go Reverse direction

  Fn mode:  Toggle decoder functions 0 to 9

    0 to 9 - Toggle function
    Esc - Exit Fn mode without entering digit
    
  Addr mode:  Enter loco address (short or long)

    0 to 9 - Used to enter address into display
    Ent - Finished entering address, set address
    Esc - Exit Addr mode without setting address  

*/
#include <Adafruit_SSD1306.h>
#include <Keypad.h>

//--------------------------------------------------------------------
// Hardware connections
//
//   PA9  - UART TX  - Serial connection to MEGA command station RX2
//   PA10 - UART RX  - Serial connection to MEGA command station TX2
//
//   PB6  - I2C SCL  - I2C clock for 128x32 OLED display 
//   PB7  - I2c SDA  - I2C data  for 128x32 OLED display
//
// Note:  The above pins are defaults with STM32duino Core generic STM32F1xx
//
// 5 row 4 column keypad
#define KP_COL1 PA0
#define KP_COL2 PA1
#define KP_COL3 PA2
#define KP_COL4 PA3
#define KP_ROW5 PA4
#define KP_ROW4 PA5
#define KP_ROW3 PA6
#define KP_ROW2 PA7
#define KP_ROW1 PA8

//--------------------------------------------------------------------
// Global variables for throttle operation
#define SPEED_MAX 127

int loco;         // Decoder address
int oldLoco;
int speed;
int oldSpeed;
int speedStep = 1;
enum dirState  {
  Reverse = 0, 
  Forward = 1
};
enum dirState dir;

enum dirKey {
  Normal = 0,
  Inverted = 1
};
enum dirKey dirKeyMode;   // Indicates LEFT is Fwd (Normal) 
                          // or LEFT key is Rev (Inverted)

bool powerOnFlag;

bool brakedFlag;
bool decrToStop;
bool incrToOldSpeed;
int brakeTimeIvl = 300;   // Time (milliseconds) between speed 
                          // steps entering and leaving braked state
int brakeTime;            // Time for next speed change for brake
                          // set or release


enum opMode {
  Run = 0,  // Normal throttle control
  Fn = 1,  // Decoder function
  Addr = 2  // Set loco address
};
enum opMode mode;

char key;

int fnOnFlags[10];  // On/off = 1/0 states of functions F0 to F9

//--------------------------------------------------------------------
// Definitions for display
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
// Contrary to specs, I had to use 0x3C screen address below for both
// 128x64 and 128x32 displays.
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

//--------------------------------------------------------------------
// Definitions for keypad
const byte ROWS = 5;  // 5 rows
const byte COLS = 4;  // 4 columns
/*
// Names on keys not needed, but here for reference
char* keyName[] = {
            "F1",  "F2", "#", "*",
            "1",  "2", "3", "UP",
            "4",  "5", "6", "DOWN",
            "7",  "8", "9", "ESC",
            "LEFT",  "0", "RIGHT", "ENTER"  
      };
*/
char keyID[] = {
        'A',  'B', '#', '*',
        '1',  '2', '3', 'C',
        '4',  '5', '6', 'D',
        '7',  '8', '9', 'E',
        'F',  '0', 'G', 'H'
      };                    

char keys[ROWS][COLS] = {
  {keyID[0], keyID[1], keyID[2], keyID[3]},
  {keyID[4], keyID[5], keyID[6], keyID[7]},
  {keyID[8], keyID[9], keyID[10], keyID[11]},
  {keyID[12], keyID[13], keyID[14], keyID[15]},
  {keyID[16], keyID[17], keyID[18], keyID[19]}
};

byte rowPins[ROWS] = {KP_ROW1, KP_ROW2, KP_ROW3, KP_ROW4, KP_ROW5};
byte colPins[COLS] = {KP_COL1, KP_COL2, KP_COL3, KP_COL4};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

//--------------------------------------------------------------------
// Declare functions
void SetUpDisplay(void);
void UpdateDisplay(void);
void SendLocoCommand(void);
void SendFnCommand(char k);
void ToggleFnOnFlag(int i);

//--------------------------------------------------------------------
void setup() {

  Serial.begin(115200);   // Initialize UART connection to DCC-EX CW
  Wire.begin();   // Initialize I2C for display

  // Set initial mode to Run
  mode = Run;
  // Default global variables for throttle operation
  speed = 0;
  dir = Forward;
  dirKeyMode = Normal;
  loco = 3;
  brakedFlag = false;
  decrToStop = false;
  incrToOldSpeed = false;
  powerOnFlag = false;
  // Default states of decoder functions F0 to F9
  for(int j=0; j<10; j++) {
    fnOnFlags[j] = 0;
  }
  
  // Set startup display with defaults
  SetUpDisplay();
  delay(500);
  UpdateDisplay();

}

void loop() {

  char key;   // Character returned from keypad
  int keyNum; // Number of function requested

  // Poll keypad

  switch (mode) {

    case Run:

      // Slow to a stop if Brake state was toggled on
      if (decrToStop) {
        if (millis() >= brakeTime) {
          // Decrement speed
          if (speed > speedStep) {
            speed = speed - speedStep;
          } else {
            speed = 0;
          }
          if (speed == 0) {
            decrToStop = false;
          } else {
            brakeTime = millis() + brakeTimeIvl;
          }      
          SendLocoCommand();
          UpdateDisplay();
        }
      }

      // Increase speed to old speed if Brake state was
      // toggled off
      if (incrToOldSpeed) {
        if (millis() >= brakeTime) {
          // Increment speed
          if (speed <= oldSpeed - speedStep) {
            speed = speed + speedStep;
          } else {
            speed = oldSpeed;
          }
          if (speed == oldSpeed) {
            incrToOldSpeed = false;
            brakedFlag = false;
          } else {
            brakeTime = millis() + brakeTimeIvl;
          }
          SendLocoCommand();
          UpdateDisplay();
        }
      }

      // Poll keypad and respond to key press
      key = keypad.getKey();

      switch (key) {
        case 'A':   // F1 key
          mode = Fn;
          UpdateDisplay();
          break;
        case '*':
          mode = Addr;
          oldLoco = loco;   // Save old address in case of error
          loco = 0;         // Prepare for new address
          UpdateDisplay();
          break;
        case 'C':   // Increment speed, UP key
          if (!brakedFlag) {
            // Stop and speed change due to brake release
            incrToOldSpeed = false;
            // Increase speed if not braked
            if (speed < SPEED_MAX - speedStep) {
              speed = speed + speedStep;
            } else {
              speed = SPEED_MAX;
            }
            SendLocoCommand();
            UpdateDisplay();
          }
          break;
        case 'D':   // Decrement speed, DOWN key
          if (!brakedFlag) {
            // Stop any speed change due to brake release
            incrToOldSpeed = false;
            // Decrease speed if not braked
            if (speed > speedStep) {
              speed = speed - speedStep;
            } else {
              speed = 0;
            }
            SendLocoCommand();  
            UpdateDisplay();
          }
          break;
        case 'F':   // LEFT direction key
          if (dirKeyMode == Normal) dir = Forward;
          else dir = Reverse;
          SendLocoCommand();
          UpdateDisplay();
          break;
        case '0':   // Brake key
          if (brakedFlag) {   // Exit braked state (toggle off)
            incrToOldSpeed = true;
            brakedFlag = false;
          } else {            // Enter braked state (toggle on)
            brakedFlag = true;
            oldSpeed = speed; // Save current speed
            decrToStop = true;
          }
          brakeTime = millis() + brakeTimeIvl;
          break;
        case 'G':   // RIGHT direction key
          if (dirKeyMode == Normal) dir = Reverse;
          else dir = Forward;
          SendLocoCommand();
          UpdateDisplay();
          break;
        case 'H':   // Stop, stop brake speed changes, ENTER key
          speed = 0;
          brakedFlag = false;
          incrToOldSpeed = false;
          decrToStop = false;
          SendLocoCommand();
          UpdateDisplay();
          break;
        case '#':   // Toggle track power
          if(!powerOnFlag) {
            Serial.println("<1 MAIN>");
            powerOnFlag = true;
            UpdateDisplay();
          } else {
            Serial.println("<0 MAIN>");
            powerOnFlag = false;  
            UpdateDisplay();
          }
          break;
        case 'E':   // Toggle direction keys mode, ESC key
          if (dirKeyMode == Normal) dirKeyMode = Inverted;
          else dirKeyMode = Normal;
          break;
        default:
          break;
      }

      break;    // End Run

    case Fn:
      key = keypad.getKey();  // Get function number (single digit)
      if (key >= '0' && key <= '9') {
          keyNum = key - '0';
          ToggleFnOnFlag(keyNum);
          SendFnCommand(keyNum);
          mode = Run;
          UpdateDisplay();
      } else {    // Abort function call with ESC key
        if (key == 'E') {
          mode = Run;
          UpdateDisplay();
        }
      }

      break;    // End Fn

    case Addr:
      key = keypad.getKey();  // Get digit for address
      if (key >= '0' && key <= '9') {
          loco = 10*loco + (key - '0');   // Build up address
          UpdateDisplay();
      } else {  
        switch (key) {
          case 'E':   // Escape out of Addr mode, ESC key
            loco = oldLoco;   // Restore original address
            mode = Run;
            UpdateDisplay();
            break;
          case 'H':   // Enter new address if within address range, ENTER key
            if (loco < 1 || loco > 10293) loco = oldLoco;
            mode = Run;
            UpdateDisplay();
            break;
          default:
            break;
        }
      }
      break;    // End Addr

    default:
      break;
    
  }
  
}

//--------------------------------------------------------------------
void ToggleFnOnFlag(int i) {
  if (fnOnFlags[i] == 0) fnOnFlags[i] = 1;
  else fnOnFlags[i] = 0;
}
//--------------------------------------------------------------------
void SendLocoCommand(void) {
  Serial.print("<t ");
  Serial.print(loco);
  Serial.print(" ");
  Serial.print(speed);
  Serial.print(" ");
  Serial.print(dir);
  Serial.println(">");
}
      
void SendFnCommand(int k) {
  // Send function command
  Serial.print("<F ");
  Serial.print(loco);
  Serial.print(" ");
  Serial.print(k);
  Serial.print(" ");
  Serial.print(fnOnFlags[k]);
  Serial.println(">");
}

//--------------------------------------------------------------------
void SetUpDisplay(void) {
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
}

void UpdateDisplay(void){
  // Version for
  display.clearDisplay();
  display.setTextSize(2);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font

  // Line 1
  display.print("Pwr: ");
  if (powerOnFlag) display.println("On");
  else display.println("Off");
  // Line 2
  display.print("Mode: ");
  switch (mode) {
    case Run:
      display.println("Run");
      break;
    case Fn:
      display.println("Fn");
      break;
    case Addr:
      display.println("Addr");
      break;
    default:
      display.println("?");
      break;
  }

  switch(mode) {
  
    case Run:
      // Normal throttle operations
      // Line 3
      display.print("Loco: ");
      display.println(loco);
      // Line 4
      display.print("S/D: ");
      display.print(speed);
      display.print("/");
      if (brakedFlag) {
        display.println("B");
      } else if (dir == 1) {
        display.println("F");
      } else {
        display.println("R");
      }
      break;

    case Fn:
      // Enter digit 0-9 for decoder function
      // Line 3
      display.println("Enter 0-9");
      // Line 4
      display.println("Esc=quit");
      break;

    case Addr:
      // Enter decoder address
      // Line 3
      display.print("Addr:");
      display.println(loco);
      // Line 4
      display.println("Esc=quit");
      break;

    default:
      display.println("?");
      break;
  }
  
  display.display();

}
/*
void UpdateDisplay(void){
  // Version for 128x32
  display.clearDisplay();
  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font

  // Line 1
  display.println("Michiana Traction Co.");
  // Line 2
  display.print("Mode: ");
  switch (mode) {
    case Run:
      display.print("Run");
      break;
    case Fn:
      display.print("Fn");
      break;
    case Addr:
      display.print("Addr");
      break;
    default:
      display.print("????");
      break;
  }
  display.print("  Pwr: ");
  if (powerOnFlag) display.println("On");
  else display.println("Off");

  switch(mode) {
  
    case Run:
      // Normal throttle operations
      // Line 3
      display.print("Loco: ");
      display.println(loco);
      // Line 4
      display.print("Speed: ");
      display.print(speed);
      display.print("  Dir: ");
      if (brakedFlag) {
        display.println("Brk");
      } else if (dir == 1) {
        display.println("Fwd");
      } else {
        display.println("Rev");
      }
      break;

    case Fn:
      display.println("Enter digit (0-9)");
      display.println("or Esc to quit");
      break;

    case Addr:
      display.print("Enter addr: ");
      display.println(loco);
      display.println("(Esc to quit)");
      break;

    default:
      display.println("?");
      break;
  }
  
  display.display();

}
*/
