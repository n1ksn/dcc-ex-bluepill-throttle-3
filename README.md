
# myBluePillThrottle3.ino
  
  Simple DCC-EX tethered throttle using Blue Pill, 5x4 keypad, 
  and OLED display.  

  The action of the "Brake" mode in this version emulates the
  Pause button in Engine Driver.

  Modified to invert direction keys in Run mode using Esc key.

  Andrew Palm, 1/11/2024

  Only activates decoder functions 0 to 9, and toggled
  between on and off.

  There are three modes, Run, Fn, and Addr, as follows:

  Run mode:  Primary mode of operation with keypad assignments:

    F1 - Enter Fn mode
    # - Toggle track power
    * - Enter Addr mode
    UP - Increase speed
    DOWN - Decrease speed
    Esc - Invert direction keys (toggle function)
    Ent - Stop, set speed to zero
    LEFT - Go Forward direction
    0 - Brake, stop but speed unchanged (use between successive
        forward and reverse moves)
    RIGHT - Go Reverse direction

  Fn mode:  Toggle decoder functions 0 to 9

    0 to 9 - Toggle function
    Esc - Exit Fn mode without entering digit
    
  Addr mode:  Enter loco address (short or long)

    0 to 9 - Used to enter address into display
    Ent - Finished entering address, set address
    Esc - Exit Addr mode without setting address  

