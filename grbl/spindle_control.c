/*
  spindle_control.c - spindle control methods
  Part of Grbl

  Copyright (c) 2012-2015 Sungeun K. Jeon
  Copyright (c) 2009-2011 Simen Svale Skogsrud

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "grbl.h"

//LUT to find corresponding PWM->RPM register value
//Substitutes requested 8bit PWM counter value with empirical RPM data
	static const __flash uint8_t lookup[256] = {
		0,	0,	59,	67,	73,	78,	82,	86,
		89,	93,	96,	98,	101,	103,	106,	108,
		110,	112,	114,	116,	118,	119,	121,	123,
		124,	126,	127,	129,	130,	132,	133,	134,
		136,	137,	138,	139,	141,	142,	143,	144,
		145,	146,	147,	148,	149,	150,	151,	152,
		153,	154,	155,	156,	157,	158,	159,	160,
		161,	162,	162,	163,	164,	165,	166,	166,
		167,	168,	169,	170,	170,	171,	172,	173,
		173,	174,	175,	176,	176,	177,	178,	178,
		179,	180,	180,	181,	182,	182,	183,	184,
		184,	185,	186,	186,	187,	187,	188,	189,
		189,	190,	190,	191,	192,	192,	193,	193,
		194,	194,	195,	196,	196,	197,	197,	198,
		198,	199,	199,	200,	200,	201,	201,	202,
		203,	203,	204,	204,	205,	205,	206,	206,
		207,	207,	208,	208,	208,	209,	209,	210,
		210,	211,	211,	212,	212,	213,	213,	214,
		214,	215,	215,	215,	216,	216,	217,	217,
		218,	218,	219,	219,	219,	220,	220,	221,
		221,	222,	222,	222,	223,	223,	224,	224,
		224,	225,	225,	226,	226,	226,	227,	227,
		228,	228,	228,	229,	229,	230,	230,	230,
		231,	231,	231,	232,	232,	233,	233,	233,
		234,	234,	234,	235,	235,	236,	236,	236,
		237,	237,	237,	238,	238,	238,	239,	239,
		240,	240,	240,	241,	241,	241,	242,	242,
		242,	243,	243,	243,	244,	244,	244,	245,
		245,	245,	246,	246,	246,	247,	247,	247,
		248,	248,	248,	249,	249,	249,	250,	250,
		250,	251,	251,	251,	252,	252,	252,	252,
		253,	253,	253,	254,	254,	254,	255,	255
		};

void spindle_init()
{    
  #ifdef VARIABLE_SPINDLE

    // Configure variable spindle PWM and enable pin, if requried. On the Uno, PWM and enable are
    // combined unless configured otherwise.
    SPINDLE_PWM_DDR |= (1<<SPINDLE_PWM_BIT); // Configure as PWM output pin.
    SPINDLE_TCCRA_REGISTER = SPINDLE_TCCRA_INIT_MASK; // Configure PWM output compare timer
    SPINDLE_TCCRB_REGISTER = SPINDLE_TCCRB_INIT_MASK;
    #ifdef USE_SPINDLE_DIR_AS_ENABLE_PIN
      SPINDLE_ENABLE_DDR |= (1<<SPINDLE_ENABLE_BIT); // Configure as output pin.
    #else 
      SPINDLE_DIRECTION_DDR |= (1<<SPINDLE_DIRECTION_BIT); // Configure as output pin.
    #endif
  
  #else  

    // Configure no variable spindle and only enable pin.
    SPINDLE_ENABLE_DDR |= (1<<SPINDLE_ENABLE_BIT); // Configure as output pin.
    SPINDLE_DIRECTION_DDR |= (1<<SPINDLE_DIRECTION_BIT); // Configure as output pin.
  
  #endif

  spindle_stop();
}


void spindle_stop()
{
  // On the Uno, spindle enable and PWM are shared. Other CPUs have seperate enable pin.
  #ifdef VARIABLE_SPINDLE
    SPINDLE_TCCRA_REGISTER &= ~(1<<SPINDLE_COMB_BIT); // Disable PWM. Output voltage is zero.
    #if defined(CPU_MAP_ATMEGA2560) || defined(USE_SPINDLE_DIR_AS_ENABLE_PIN)
      #ifdef INVERT_SPINDLE_ENABLE_PIN
        SPINDLE_ENABLE_PORT |= (1<<SPINDLE_ENABLE_BIT);  // Set pin to high
      #else
        SPINDLE_ENABLE_PORT &= ~(1<<SPINDLE_ENABLE_BIT); // Set pin to low
      #endif
    #endif
  #else
    #ifdef INVERT_SPINDLE_ENABLE_PIN
      SPINDLE_ENABLE_PORT |= (1<<SPINDLE_ENABLE_BIT);  // Set pin to high
    #else
      SPINDLE_ENABLE_PORT &= ~(1<<SPINDLE_ENABLE_BIT); // Set pin to low
    #endif
  #endif  
}


void spindle_set_state(uint8_t state, float rpm)
{
  if (sys.abort) { return; } // Block during abort.
  
  // Halt or set spindle direction and rpm. 
  if (state == SPINDLE_DISABLE) {

    spindle_stop();

  } else {

    #ifndef USE_SPINDLE_DIR_AS_ENABLE_PIN
      if (state == SPINDLE_ENABLE_CW) {
        SPINDLE_DIRECTION_PORT &= ~(1<<SPINDLE_DIRECTION_BIT);
      } else {
        SPINDLE_DIRECTION_PORT |= (1<<SPINDLE_DIRECTION_BIT);
      }
    #endif

    #ifdef VARIABLE_SPINDLE

      // TODO: Install the optional capability for frequency-based output for servos.
      uint8_t setpoint_pwm;  // 328p PWM register is 8-bit.

      // Calculate PWM register value based on rpm max/min settings and programmed rpm.
      if (rpm < 0.0) { spindle_stop(); } // RPM should never be negative, but check anyway.
      else {
        if (settings.rpm_max <= settings.rpm_min) {
          // No PWM range possible. Set simple on/off spindle control pin state.
          setpoint_pwm = SPINDLE_PWM_MAX_VALUE;
        } else {
          if (rpm > settings.rpm_max) { rpm = settings.rpm_max; }
          if (rpm < settings.rpm_min) { rpm = settings.rpm_min; }
          #ifdef SPINDLE_MINIMUM_PWM
            float pwm_gradient = (SPINDLE_PWM_MAX_VALUE-SPINDLE_MINIMUM_PWM)/(settings.rpm_max-settings.rpm_min);
            setpoint_pwm = floor( (rpm-settings.rpm_min)*pwm_gradient + (SPINDLE_MINIMUM_PWM+0.5));
          #else
            float pwm_gradient = (SPINDLE_PWM_MAX_VALUE)/(settings.rpm_max-settings.rpm_min);
            setpoint_pwm = floor( (rpm-settings.rpm_min)*pwm_gradient + 0.5);
          #endif
        }
		//JTS changes
		SPINDLE_TCCRA_REGISTER |= (1<<SPINDLE_COMB_BIT); // Ensure PWM output is enabled.
		
		setpoint_pwm = lookup[setpoint_pwm]; //LUT to find corresponding PWM->RPM register value
		
		uint8_t temp_pwm = SPINDLE_OCR_REGISTER; //get previous PWM register value
		while(temp_pwm != setpoint_pwm) {
			if(temp_pwm > setpoint_pwm) {
				temp_pwm -= 1;
			} else {
				temp_pwm += 1;
			}
			SPINDLE_OCR_REGISTER = temp_pwm; // Set PWM output level.
			delay_ms(2);
		}
		//end JTS changes
    
        // On the Uno, spindle enable and PWM are shared, unless otherwise specified.
        #if defined(USE_SPINDLE_DIR_AS_ENABLE_PIN) 
          #ifdef INVERT_SPINDLE_ENABLE_PIN
            SPINDLE_ENABLE_PORT &= ~(1<<SPINDLE_ENABLE_BIT);
          #else
            SPINDLE_ENABLE_PORT |= (1<<SPINDLE_ENABLE_BIT);
          #endif
        #endif
      }
      
    #else
      
     // NOTE: Without variable spindle, the enable bit should just turn on or off, regardless
     // if the spindle speed value is zero, as its ignored anyhow.
     #ifdef INVERT_SPINDLE_ENABLE_PIN
       SPINDLE_ENABLE_PORT &= ~(1<<SPINDLE_ENABLE_BIT);
     #else
       SPINDLE_ENABLE_PORT |= (1<<SPINDLE_ENABLE_BIT);
     #endif
      
    #endif

  }
}


void spindle_run(uint8_t state, float rpm)
{
  if (sys.state == STATE_CHECK_MODE) { return; }
  protocol_buffer_synchronize(); // Empty planner buffer to ensure spindle is set when programmed.  
  spindle_set_state(state, rpm);
}
