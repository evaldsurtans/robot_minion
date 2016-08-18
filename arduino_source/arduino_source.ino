#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <Wtv020sd16p.h>

// #define DEBUG

const int MOISTURE_OUTSIDE_POT = 800;
const int THERESHOLD_DRY = 30;
const int THERESHOLD_DELAY_LOOPS = 2;
const int THERESHOLD_BATTERY_LEVEL_LOW = 2000;
const int THERESHOLD_BATTERY_LEVEL_HIGH = 3000;

const int SOUND_PLEASE_WATER = 0;
const int SOUND_THANK_YOU = 1;
const int SOUND_THANK_NJAM = 2;
const int SOUND_PUT_ME_IN_POT = 3;
const int SOUND_CHARGE_ME = 4;
const int SOUND_CHARGING = 5;
const int SOUND_CHARGED_THANK_YOU = 6;
const int SOUND_I_CAN_SEE_YOU = 7;

const int PIN_AUDIO_RESET = A0;  // The pin number of the reset pin.
const int PIN_AUDIO_CLOCK = A1;  // The pin number of the clock pin.
const int PIN_AUDIO_PLAY = A2;  // The pin number of the data pin.
const int PIN_AUDIO_BUSY = 13;  // The pin number of the busy pin. State that something is playing
const int PIN_MOISTURE = 3;
const int PIN_LED = 13;
const int PIN_PIR = 10;
const int PIN_BATTERY = A6;

Wtv020sd16p wtv020sd16p(PIN_AUDIO_RESET, PIN_AUDIO_CLOCK, PIN_AUDIO_PLAY, PIN_AUDIO_BUSY);

int moisture_dry = 0;
int moisture_wet = 0;

byte is_battery_charge_needed = 0;
byte is_in_pot = 0;
byte is_needed_to_water = 0;

byte is_battery_charging = 0;

int counter_delay_loops = 0;
int counter = 0;
int counter_battery_warning = 0;
int counter_movement = 0;

volatile int f_wdt=1;

/*
Sleep mode to save a battery using WDT watchdog
*/
void enterSleep()
{
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); //SLEEP_MODE_PWR_SAVE
  sleep_enable();
  
  /* Now enter sleep mode. */
  sleep_mode();
  
  /* The program will continue from here after the WDT timeout*/
  sleep_disable(); /* First thing to do is disable sleep. */
  
  /* Re-enable the peripherals. */
  power_all_enable();
}

/*
Read battery level from internal CPU voltage at 1.1V
*/
long readVcc() {
  // Read 1.1V reference against AVcc
  // set the reference to Vcc and the measurement to the internal 1.1V reference
  #if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
    ADMUX = _BV(MUX5) | _BV(MUX0);
  #elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
    ADMUX = _BV(MUX3) | _BV(MUX2);
  #else
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #endif  

  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA,ADSC)); // measuring

  uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH 
  uint8_t high = ADCH; // unlocks both

  long result = (high<<8) | low;

  result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
  return result; // Vcc in millivolts
}

void playSound(int sound)
{
  wtv020sd16p.asyncPlayVoice(sound);
  
  digitalWrite(PIN_LED, LOW);
  delay(1000);
  digitalWrite(PIN_LED, HIGH);
  delay(1000);
  digitalWrite(PIN_LED, LOW);
  delay(2000);

}

/*
Main loop of program called only when controller is awake
*/
void loop()
{
    if(f_wdt == 1)
    {
      
      int movement = digitalRead(PIN_PIR);
      int moisture = analogRead(PIN_MOISTURE);
      int battery_charging = analogRead(PIN_BATTERY);
      long battery_level = readVcc();
      counter++;
      
#ifdef DEBUG
      Serial.begin(4800);
      delay(200);
      char str[200];
      
      sprintf(str, "%d\t%d\0", battery_level, battery_charging);  
      Serial.println(str);
      sprintf(str, "%d\t%d\t%d\t%d\t%d\0", counter, moisture, moisture_dry, moisture_wet, movement);  
      Serial.println(str);

      //Heart-beat LED
      digitalWrite(PIN_LED, LOW);
      delay(1000);
      digitalWrite(PIN_LED, HIGH);
      
      Serial.end();
      delay(1000);
      digitalWrite(PIN_LED, LOW);

      
      /*
      //Debug battery level
      int test = (double)battery_level / 500.0;
      for(int i = 0; i < test; i++)
      {
        digitalWrite(PIN_LED, LOW);
        delay(500);
        digitalWrite(PIN_LED, HIGH);
        delay(500);
        digitalWrite(PIN_LED, LOW);
      }
      delay(1000);
      */
      
#endif

      
      counter_delay_loops++;
      if(counter_delay_loops > THERESHOLD_DELAY_LOOPS)
      {
        counter_delay_loops = 0;

        //Battery charging detected
        if(battery_charging > 500)
        {
          if(!is_battery_charging)
          {
            is_battery_charging = 1;
            playSound(SOUND_CHARGING);
            delay(2000);          
          }
        }
        else
        {
          is_battery_charging = 0;
        }

        //Taken out of pot
        if(is_in_pot)
        {
          if(moisture >= MOISTURE_OUTSIDE_POT)
          {
            is_in_pot = 0;
          }
        }
        
        //put inside pot
        if(!is_in_pot && !is_battery_charge_needed && !is_battery_charging)
        {
          if(moisture >= MOISTURE_OUTSIDE_POT)
          {
            playSound(SOUND_PUT_ME_IN_POT);
          }
          else
          {
            is_in_pot = 1;
            playSound(SOUND_THANK_YOU);
            
            //register dry thereshold
            delay(5000);
            if(moisture_dry == 0)
            {
              moisture = moisture_dry = analogRead(PIN_MOISTURE);  
            }
          }
        }
        
        // Low battery
        if(battery_level < THERESHOLD_BATTERY_LEVEL_LOW && movement)
        {
          is_battery_charge_needed = 1;
          counter_battery_warning++;

          // Ask to charge once in 5 cycles
          if(counter_battery_warning == 1)
          {
             playSound(SOUND_CHARGE_ME);
             delay(5000);
          }
          else if(counter_battery_warning >= 5)
          {
            counter_battery_warning = 0;
          }       
        }
       
        // Battery charged
        if(battery_level > THERESHOLD_BATTERY_LEVEL_HIGH && movement)
        {
          if(is_battery_charging)
          {
            playSound(SOUND_CHARGED_THANK_YOU);
            is_battery_charge_needed = 0;
            delay(5000);
          }         
        }

        if(is_in_pot && !is_battery_charge_needed && (movement || moisture_wet == 0))
        {
          //inside pot
          if(moisture >= moisture_dry)
          {
            is_needed_to_water = true;
            
            playSound(SOUND_PLEASE_WATER);
          }
          else
          {
            if(is_needed_to_water)
            {
              int sound = (random(1000) < 500) ? SOUND_THANK_NJAM : SOUND_THANK_YOU;
              if(moisture_wet == 0 && moisture < moisture_dry - THERESHOLD_DRY)
              {
                moisture_wet = moisture;
                sound = SOUND_THANK_NJAM;
              }
              
              if(moisture <= moisture_wet)
              {
                is_needed_to_water = 0;
                playSound(sound);
              }
              else
              {
                playSound(SOUND_PLEASE_WATER);
              }
              
            }
            else
            {
              //When no water needed - easter egg
              counter_movement++;

              if(counter_movement > 3)
              {
                if(random(1000) < 500)
                {
                  playSound(SOUND_I_CAN_SEE_YOU);
                }
                counter_movement = 0;
              }
            }
          }
        }

        delay(200);
      }

      //Enable this line to debug speaker if sound is working
      //playSound(SOUND_PLEASE_WATER);
      
      f_wdt = 0;
      enterSleep();
    }
    else
    {
      //multiple loops before put to sleep
    }
}

/*
Entry point of program
*/
void setup()
{
  wtv020sd16p.reset();
  //wtv020sd16p.unmute();
  
  pinMode(PIN_PIR, INPUT); 
  pinMode(PIN_LED, OUTPUT); 
  pinMode(PIN_BATTERY, INPUT); 

  randomSeed(readVcc());

  //Signal that it is working
  digitalWrite(PIN_LED, LOW);
  delay(1000);
  digitalWrite(PIN_LED, HIGH);
  delay(1000);
  digitalWrite(PIN_LED, LOW);

  //Signal that it is working
  playSound(SOUND_I_CAN_SEE_YOU);
 
  /* Clear the reset flag. */
  MCUSR &= ~(1<<WDRF);
  
  /* In order to change WDE or the prescaler, we need to
   * set WDCE (This will allow updates for 4 clock cycles).
   */
  WDTCSR |= (1<<WDCE) | (1<<WDE);

  /* set new watchdog timeout prescaler value */
  WDTCSR = 1<<WDP0 | 1<<WDP3; /* 8.0 seconds max for AT328 */
  
  //WDTCSR = 1<<WDP1 | 1<<WDP2; //1 sek
  
  //WDTCSR = 1<<WDP3; //4 sek
  
  /* Enable the WD interrupt (note no reset). */
  WDTCSR |= _BV(WDIE);
  
}


// Watchdog WDT vector
ISR(WDT_vect) {
  if(f_wdt == 0)
  {
    f_wdt=1;
  }
}
