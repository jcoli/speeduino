/*
Speeduino - Simple engine management for the Arduino Mega 2560 platform
Copyright (C) Josh Stewart
A full copy of the license may be found in the projects root directory
*/

void initialiseADC()
{
#if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega1281__) || defined(__AVR_ATmega2560__) || defined(__AVR_ATmega2561__) //AVR chips use the ISR for this

  #if defined(ANALOG_ISR)
    //This sets the ADC (Analog to Digitial Converter) to run at 250KHz, greatly reducing analog read times (MAP/TPS)
    //the code on ISR run each conversion every 25 ADC clock, conversion run about 100KHz effectively
    //making a 6250 conversions/s on 16 channels and 12500 on 8 channels devices.
    noInterrupts(); //Interrupts should be turned off when playing with any of these registers
    
    ADCSRB = 0x00; //ADC Auto Trigger Source is in Free Running mode
    //ADCSRB &= B11111000;
    ADMUX = 0x40;  //Select AREF as reference, ADC Left Adjust Result, Starting at channel 0

    //All of the below is the longhand version of: ADCSRA = 0xEE;
    #define ADFR 5 //Why the HELL isn't this defined in the same place as everything else (wiring.h)?!?!
    BIT_SET(ADCSRA,ADFR); //Set free running mode
    BIT_SET(ADCSRA,ADIE); //Set ADC interrupt enabled
    BIT_CLEAR(ADCSRA,ADIF); //Clear interrupt flag

    // Set ADC clock to 250KHz (Prescaler = 64)
    BIT_SET(ADCSRA,ADPS2);
    BIT_SET(ADCSRA,ADPS1);
    BIT_SET(ADCSRA,ADPS0);
    
    BIT_SET(ADCSRA,ADEN); //Enable ADC
    
    interrupts();
    BIT_SET(ADCSRA,ADSC); //Start conversion
    
  #else
    //This sets the ADC (Analog to Digitial Converter) to run at 1Mhz, greatly reducing analog read times (MAP/TPS)
    //1Mhz is the fastest speed permitted by the CPU without affecting accuracy
    //Please see chapter 11 of 'Practical Arduino' (http://books.google.com.au/books?id=HsTxON1L6D4C&printsec=frontcover#v=onepage&q&f=false) for more details
    //Can be disabled by removing the #include "fastAnalog.h" above
     BIT_SET(ADCSRA,ADPS2);
     BIT_CLEAR(ADCSRA,ADPS1);
     BIT_CLEAR(ADCSRA,ADPS0);
  #endif
#endif
}

void instanteneousMAPReading()
{
  //Instantaneous MAP readings
  #if defined(ANALOG_ISR) 
    tempReading = AnChannel[pinMAP-A0];
  #else
    tempReading = analogRead(pinMAP);
    tempReading = analogRead(pinMAP);
  #endif  
  //Error checking
  if(tempReading >= VALID_MAP_MAX || tempReading <= VALID_MAP_MIN) { mapErrorCount += 1; }
  else { currentStatus.mapADC = tempReading; mapErrorCount = 0; }
        
  currentStatus.MAP = fastMap1023toX(currentStatus.mapADC, configPage1.mapMax); //Get the current MAP value
}

void readMAP()
{
  //MAP Sampling system
  switch(configPage1.mapSample)
  {
    case 0:
      //Instantaneous MAP readings
      instanteneousMAPReading();
      break;
      
    case 1:
      //Average of a cycle
      
      if (currentStatus.RPM < 1) {  instanteneousMAPReading(); return; } //If the engine isn't running, fall back to instantaneous reads
       
      if( (MAPcurRev == startRevolutions) || (MAPcurRev == startRevolutions+1) ) //2 revolutions are looked at for 4 stroke. 2 stroke not currently catered for. 
      {
        #if defined(ANALOG_ISR) 
          tempReading = AnChannel[pinMAP-A0];
        #else
          tempReading = analogRead(pinMAP);
          tempReading = analogRead(pinMAP);
        #endif
        
        //Error check
        if(tempReading < VALID_MAP_MAX && tempReading > VALID_MAP_MIN)
        {
          MAPrunningValue = MAPrunningValue + tempReading; //Add the current reading onto the total
          MAPcount++;
        }
        else { mapErrorCount += 1; }
      }
      else
      {
        //Reaching here means that the last cylce has completed and the MAP value should be calculated
        currentStatus.mapADC = ldiv(MAPrunningValue, MAPcount).quot;
        currentStatus.MAP = fastMap1023toX(currentStatus.mapADC, configPage1.mapMax); //Get the current MAP value
        MAPcurRev = startRevolutions; //Reset the current rev count
        MAPrunningValue = 0;
        MAPcount = 0;
      }
      break;
    
    case 2:
      //Minimum reading in a cycle
      if (currentStatus.RPM < 1) {  instanteneousMAPReading(); return; } //If the engine isn't running, fall back to instantaneous reads
        
      if( (MAPcurRev == startRevolutions) || (MAPcurRev == startRevolutions+1) ) //2 revolutions are looked at for 4 stroke. 2 stroke not currently catered for. 
      {
        #if defined(ANALOG_ISR) 
          tempReading = AnChannel[pinMAP-A0];
        #else
          tempReading = analogRead(pinMAP);
          tempReading = analogRead(pinMAP);
        #endif
        //Error check
        if(tempReading < VALID_MAP_MAX && tempReading > VALID_MAP_MIN)
        {
          if( tempReading < MAPrunningValue) { MAPrunningValue = tempReading; } //Check whether the current reading is lower than the running minimum
        }
        else { mapErrorCount += 1; }
      }
      else
      {
        //Reaching here means that the last cylce has completed and the MAP value should be calculated
        currentStatus.mapADC = MAPrunningValue;
        currentStatus.MAP = fastMap1023toX(currentStatus.mapADC, configPage1.mapMax); //Get the current MAP value
        MAPcurRev = startRevolutions; //Reset the current rev count
        MAPrunningValue = 1023; //Reset the latest value so the next reading will always be lower
      }
      break;
  }
}

void readTPS()
{
  currentStatus.TPSlast = currentStatus.TPS;
  currentStatus.TPSlast_time = currentStatus.TPS_time;
  #if defined(ANALOG_ISR) 
    byte tempTPS = fastMap1023toX(AnChannel[pinTPS-A0], 255); //Get the current raw TPS ADC value and map it into a byte
  #else
    analogRead(pinTPS);
    byte tempTPS = fastMap1023toX(analogRead(pinTPS), 255); //Get the current raw TPS ADC value and map it into a byte
  #endif
  currentStatus.tpsADC = ADC_FILTER(tempTPS, ADCFILTER_TPS, currentStatus.tpsADC);
  //Check that the ADC values fall within the min and max ranges (Should always be the case, but noise can cause these to fluctuate outside the defined range). 
  byte tempADC = currentStatus.tpsADC; //The tempADC value is used in order to allow TunerStudio to recover and redo the TPS calibration if this somehow gets corrupted
  if (currentStatus.tpsADC < configPage1.tpsMin) { tempADC = configPage1.tpsMin; }
  else if(currentStatus.tpsADC > configPage1.tpsMax) { tempADC = configPage1.tpsMax; }
  currentStatus.TPS = map(tempADC, configPage1.tpsMin, configPage1.tpsMax, 0, 100); //Take the raw TPS ADC value and convert it into a TPS% based on the calibrated values
  currentStatus.TPS_time = currentLoopTime;  
}

void readCLT()
{
  #if defined(ANALOG_ISR) 
    tempReading = fastMap1023toX(AnChannel[pinCLT-A0], 511); //Get the current raw CLT value
  #else
    tempReading = analogRead(pinCLT);
    tempReading = fastMap1023toX(analogRead(pinCLT), 511); //Get the current raw CLT value
  #endif
  currentStatus.cltADC = ADC_FILTER(tempReading, ADCFILTER_CLT, currentStatus.cltADC);
  currentStatus.coolant = cltCalibrationTable[currentStatus.cltADC] - CALIBRATION_TEMPERATURE_OFFSET; //Temperature calibration values are stored as positive bytes. We subtract 40 from them to allow for negative temperatures
}

void readIAT()
{
  #if defined(ANALOG_ISR) 
    tempReading = fastMap1023toX(AnChannel[pinIAT-A0], 511); //Get the current raw IAT value
  #else
    tempReading = analogRead(pinIAT);
    tempReading = fastMap1023toX(analogRead(pinIAT), 511); //Get the current raw IAT value
  #endif
  currentStatus.iatADC = ADC_FILTER(tempReading, ADCFILTER_IAT, currentStatus.iatADC);
  currentStatus.IAT = iatCalibrationTable[currentStatus.iatADC] - CALIBRATION_TEMPERATURE_OFFSET;
}

void readO2()
{
  #if defined(ANALOG_ISR) 
    tempReading = fastMap1023toX(AnChannel[pinO2-A0], 511); //Get the current O2 value. 
  #else
    tempReading = analogRead(pinO2);
    tempReading = fastMap1023toX(analogRead(pinO2), 511); //Get the current O2 value. 
  #endif
  currentStatus.O2ADC = ADC_FILTER(tempReading, ADCFILTER_O2, currentStatus.O2ADC);
  currentStatus.O2 = o2CalibrationTable[currentStatus.O2ADC];
}
       
/* Second O2 currently disabled as its not being used
  currentStatus.O2_2ADC = map(analogRead(pinO2_2), 0, 1023, 0, 511); //Get the current O2 value.
  currentStatus.O2_2ADC = ADC_FILTER(tempReading, ADCFILTER_O2, currentStatus.O2_2ADC);
  currentStatus.O2_2 = o2CalibrationTable[currentStatus.O2_2ADC];
*/

void readBat()
{
  #if defined(ANALOG_ISR) 
    tempReading = fastMap1023toX(AnChannel[pinBat-A0], 245); //Get the current raw Battery value. Permissible values are from 0v to 24.5v (245)
  #else
    tempReading = analogRead(pinBat);
    tempReading = fastMap1023toX(analogRead(pinBat), 245); //Get the current raw Battery value. Permissible values are from 0v to 24.5v (245)
  #endif
  currentStatus.battery10 = ADC_FILTER(tempReading, ADCFILTER_BAT, currentStatus.battery10);
}

/*
 * The interrupt function for reading the flex sensor frequency
 * This value is incremented with every pulse and reset back to 0 once per second
 */
void flexPulse()
 {
   ++flexCounter;
 }

