# Embedded Systems I: Final Project Report

This project reads a users's pulse and breathing rate and alerts them if either is out of an acceptable range. There is also an interface used to both display the readings and set appropriate pulse and breathing limits.

## Pulse reading
The pulse is read by a phototransistor and LED combination. The phototransistor readings are normalized by a set of op amps. The output of this mimics the pulse detected by the user's finger and is displayed on an inboard LED and is read in by a pin on the tm4c123gh6pm (Red Board). 

The Red Board controls the flat head LED placed closest to the phototransistor. Because of a periodic timer, every second the Red Board sends out a 100ms pulse to the LED. The phototransistor values are read in by the AIN3 input before and after the pulse. These values are compared with each other. If the values show that there is some light in the room and that the before and after values are significantly different, it is decided that a finger is present, which sets a global variable, `pulse_active` to true. This makes the LED turn on until there are 3 readings that have a low difference in the reading, which indicate no finger is over the sensor. The LED turns off in that case and `pulse_active` is set to false.

The wide timer interrupt service routine was chosen to read the signal in pin because it returns the time interval between pulses and triggers an interrupt service routine. This wide timer fires each time a positive edge is detected, which in this case, means that a single pulse has been detected. Once the Red Board starts reading pulse values, it has to convert them from microseconds per pulse to beats (pulses) per minute. This is accomplished through the `calc_bpm()` function. The `calc_bpm()` function takes the time in clocks and converts it into microseconds, then seconds. Then the number of pulses per second is multiplied by 60 to extrapolate the number of pulses per minute. 

It is important to note that the Red Board makes no readings while `pulse_active` is false, meaning that while there is no finger on the sensor, no readings are taken.

After the individual readings are converted to beats per minute, they are stored in an array with 5 elements. If the values it received are within the parameters, they are inserted in the array in a FIFO style, meaning that the oldest values are replaced. The values of this array are averaged with each other (excluding zero values). This helps provide a more accurate reading of the pulse. 

## Respirator
The second main component of this project is the respirator. Breaths are measured with a strain gauge which is attached to an analog to digital converter for weigh scales (HX711). The analog to digital converter interfaces with the Red Board through the SPI protocol. 

The HX711 notifies the Red Board that it's ready to share data when it sets the data pin to high. The Red board is configured with a GPIO interrupt that is triggered whenever the data pin is high. 

Once the interrupt is triggered, the Red Board sets the clock pin to high, waits 3 microseconds, reads the data pin, adds the reading to a `value` variable, logical shifts it to the left once, sets the clock pin to low, and waits 10 clocks. This is repeated 24 times to gather all the data from the HX711. After that, another clock pulse is sent to indicate that the A channel should be sampled with 128 gain.

After a full reading is collected, the Red Board checks the current reading with the previous one to see whether the reading is increasing (user is breathing in) or decreasing (user is breathing out). The Red Board waits for the first increasing reading after three increasing and three decreasing inputs. This is one breath. The time for one breath is calculated by taking the number of samples per breath and multiplying it by ten, since the HX711 returns ten values every second. 60 is divided by the time for one breath in order to calculate the breaths per minute. 

If the number of breaths per minute is not within the acceptable range, the user is notified by the inboard blue LED. Once the number of breaths per minute is back within the acceptable range, the blue LED turns off. 

## Shell
The shell is the only thing run in `main()`. It prompts the user to enter a command and reacts accordingly. The shell interfaces with the Red Board using UART and ran at a Baud rate of 115200. 

The user can set maximum and minimum acceptable parameters for both the pulse reader and respirator with the commands `alarm pulse <min> <max>` and `alarm respirator <min> <max>` respectively. 

The commands `pulse` and `respirator` show the current values for both the pulse reader and respirator. 

The shell takes in a string as an input and parses the string using the function `parseFields()`. This function breaks down the string into an initial command and its following arguments. Indices of the different arguments, the input string, and the number of fields are all stored in a special data struct. The command is verified with `isCommand()` which also allows a specified number of minimum arguments.

If a command takes in arguments, `getFieldString()` and `getFieldInteger()` are used to read the arguments in from the initial data struct. 

## Pins used
For indicating whether or not a reading was within the acceptable range, the inbuilt red and blue LEDs were used. These are pins PF1 and PF2 respectively.

Pin PC6 was used to read the output of the op amps and trigger the wide timer (Timer 1 subtimer A).

Pin PC7 was used to control the flat top LED in the pulse reader.

Pins PE2 and PE6 were used for the data and clock pins (that interfaced with the HX711), respectively.

Timer 4A was configured as a periodic timer and checked whether or not a finger was present every second (by activating `pulse_active()`).

A GPIO timer was set on Port E to check when pin PE6 (data) was high. This was really helpful in getting the fastest possible readings from the HX711. 

PA1 and PA0 were used for the UART.
