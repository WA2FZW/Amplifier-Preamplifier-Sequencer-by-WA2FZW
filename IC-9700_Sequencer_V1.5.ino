/*
 *	"IC-9700 Sequencer" Version 1.5 - John M. Price (WA2FZW) 12/01/2022
 *
 *		This program was originally designed to run a sequencer for the Yaesu FT-891.
 *		Subsequent versions of the hardware and software were modified to also work with
 *		my Icom FT-9700 on a single band.
 *
 *		This version adds the capability to control 2 amplifiers, 2 preamps and 2 SDR
 *		switches for 2 different bands on the IC-9700, and drops the transmit inhibit
 *		capability that was for the FT-891 only.
 *
 *		An Arduino Nano runs the unit.
 *
 *
 *	Modified 11/30/2021 - Version 1.2
 *
 *		The original design didn't work correctly with my Icom IC-9700 when operating
 *		in satellite mode. In satellite mode the radio operates in a full duplex cross-band
 *		mode on the 70cm and 2 meter bands. When transmitting on 70cm, the 2 meter preamp
 *		was being disabled. Since it is important to be able to hear one's self on the
 *		satellites, this was undesirable.
 *
 *		The solution was to modify the preamp enable switch to add a position to force it
 *		to remain on regardless of the TX/RX status of the radio. This also required some
 *		minor changes to the software.
 *
 *		When the preamp is forced on and the radio is in receive mode, the transmit LED
 *		blink rapidly to warn the operator that transmitting in that mode might damage
 *		the preamp if transmitting on the same antenna connection.
 *
 *
 *	Modified 12/28/2021 - Version 1.3
 *
 *		Made some slight hardware changes to further simplify the operation with radios other
 *		than the FT-891. One change was to eliminate using separate GPIO pins to turn the
 *		FT-891 transmit inhibit on and to light the associated LED. Both are now operated
 *		from the same GPIO pin.
 *
 *
 *	Modified 11/03/2022 - Version 1.4
 *
 *		Hardware changes to allow keying of 2 amplifiers via K1 (2 meter and 432 on my
 *		IC-9700). Also changed the hardware so that in addition to controlling the LNA,
 *		K2 now switches the LNA and provides a PTT indication to an SDR switch for the
 *		FT-891.
 *
 *		Other than I changed the names of some of the symbols, there are no major changes
 *		in the code.
 *
 *
 *	Modified 12/01/2022 - Version 1.5
 *
 *		The hardware and software are both modified to support 2 band operation for the
 *		Icom IC-9700 and support for the Yaesu FT-891 is eliminated.
 */

#include <Arduino.h>						// General Arduino definitions


/*
 *	Define the GPIO pins used for various things.
 */

#define	TX_GROUND	    2					// PTT Indication (interrupt capable)
#define	TX1_LED			4					// Band 1 transmit indicator LED
#define	TX2_LED			5					// Band 2 transmit indicator LED
#define	AMP_RELAY		7					// Keys the amplifiers
#define	LNA1_RELAY		8					// Operates the preamp and SDR switch
#define	LNA2_RELAY		9					// Operates the preamp and SDR switch
#define	LNA1_OFF	   A0					// Preamp for band 1 always off (bypass) when LOW
#define LNA1_ON		   A1					// Preamp for band 1 always on (RX mode) when LOW
#define	LNA2_OFF	   A2					// Preamp for band 2 always off (bypass) when LOW
#define LNA2_ON		   A3					// Preamp for band 2 always on (RX mode) when LOW


/*
 *	A note about the TX LEDs; When the radio is transmitting, they will both be turned
 *	on. We implemented separate ones so either one can be flashed when the associated
 *	preamp is in the forced on condition.
 */

/*
 *	These symbols are used for the various functions that turn things on and off
 *	to indicate which band(s) the operation is for.
 */

 #define NO_BAND	0						// Neither band
 #define BAND_1		1						// Operation applies to band 1
 #define BAND_2		2						// Operation applies to band 2
 #define BOTH_BANDS 3						// Operation applies to both bands

/*
 *	These define the HIGH/LOW conditions in more meaningful terms:
 */

#define	LED_ON			 LOW				// Turn the transmit (red) LEDs on
#define	LED_OFF			HIGH				// Turn the transmit (red) LEDs off

#define	AMP_ON			 LOW				// Go LOW to key the amplifiers
#define	AMP_OFF			HIGH

#define	LNA_RX			HIGH				// Go LOW to put the LNAs in receive mode
#define	LNA_TX			 LOW				// LOW also triggers the SDR switchs

#define	TX_OFF			HIGH				// When "TX_GROUND" is HIGH, transmitter is off
#define	TX_ON			 LOW				// When "TX_GROUND" is LOW, transmitter is on


/*
 *	These symbols control the delay times between when we see the transmitter keyed
 *	and when we turn the amplifier and preamp on or off. I measured the on/off times for
 *	the preamp; it takes about 5mS to switch from receive to transmit and about 50mS
 *	to switch back to receive. We'll set the times considerably higher just to be sure.
 */

#define	AMP_OFF_DLY		 50					// 50 milliseconds
#define	LNA_ON_DLY		 25					// 25 milliseconds


/*
 *	These are used to determine when the TX/RX state changes:
 */

volatile bool txState     = TX_OFF;			// Current transmit/receive state
volatile bool oldTxState  = TX_OFF;			// Previous state

		 bool lna_1_ForcedOff;				// Preamp #1 always off if true
		 bool lna_1_ForcedOn;				// Preamp #1 is always on if true
		 bool lna_2_ForcedOff;				// Preamp #2 always off if true
		 bool lna_2_ForcedOn;				// Preamp #2 is always on if true

		 bool tx_1_LedState = false;		// Blinking TX1_LED is off
		 bool tx_2_LedState = false;		// Blinking TX2_LED is off


/*
 *	"setup" initializes all the GPIO pins and the serial port. The serial port isn't
 *	really used except for debugging.
 */

void setup()
{
	Serial.begin ( 115200 );						// Start the serial monitor port


/*
 *	Set up the GPIO pins:
 *
 *	The order of things here is important. The first thing we do is to turn the TX
 *	inhibit line on (applies to the FT-891 only). Should the transmitter happen to
 *	be keyed when the program starts, this will prevent the transmitter from producing
 *	any output power until everything else is set up.
 *
 *	Experimentation has shown that the preamp and amplifier relays will not operate
 *	until they are set as "OUTPUT" pins, thus the preamp will startup in transmit
 *	(bypass) mode and the amplifier will be off.
 *
 *	Make sure the amplifier is off:
 */

	pinMode      ( AMP_RELAY, OUTPUT );				// Amplifier relay
	digitalWrite ( AMP_RELAY, AMP_OFF );			// Amplifiers are off

	pinMode		 ( LNA1_ON,  INPUT_PULLUP );		// Preamp #1 always on switch contact
	pinMode		 ( LNA1_OFF, INPUT_PULLUP );		// Preamp #1 always off switch contact
	pinMode      ( LNA1_RELAY, OUTPUT );			// Preamp/SDR switch #1 relay

	pinMode		 ( LNA2_ON,  INPUT_PULLUP );		// Preamp #2 always on switch contact
	pinMode		 ( LNA2_OFF, INPUT_PULLUP );		// Preamp #2 always off switch contact
	pinMode      ( LNA2_RELAY, OUTPUT );			// Preamp/SDR switch #2 relay


/*
 *	Now we can safely put the preamps into receive mode if they are enabled:
 */

	SwitchPreamp ( BOTH_BANDS, LNA_RX );


/*
 *	We don't really know the state of the PTT signal, so we will check it. Based on
 *	its current state, we set the values of "txState" and "oldTxState" and turn the
 *	TX LEDs on or off as appropriate.
 */

	pinMode      ( TX_GROUND, INPUT_PULLUP );			// Radio PTT indicator pin
	oldTxState = txState = digitalRead ( TX_GROUND );	// Read it and set state variables

	pinMode      ( TX1_LED, OUTPUT );					// Band 1 transmit indicator LED pin
	pinMode      ( TX2_LED, OUTPUT );					// Band 2 transmit indicator LED pin

	if ( txState == TX_ON )								// Is the PTT active?
	{
		digitalWrite ( TX1_LED, LED_ON );				// Yes, turn both LEDs on
		digitalWrite ( TX2_LED, LED_ON );
	}

	else												// Otherwise
	{
		digitalWrite ( TX1_LED, LED_OFF );				// Yes, turn both LEDs off
		digitalWrite ( TX2_LED, LED_OFF );
	}


/*
 *	Whenever the radio changes state from transmit to receive or vice-versa an
 *	interrupt is generated. The "TxInterrupt" ISR detects the change in state and
 *	sets the "txState" variable appropriately. We will detect and handle the change
 *	in the "loop" function.
 *
 *	Notice we set the "oldTxState" to "TX_ON" even though it is probably off when
 *	the program starts. For some reason, the program was starting up with the
 *	transmit LED on until I added this!
 */

	attachInterrupt	( digitalPinToInterrupt ( TX_GROUND ), TxInterrupt, CHANGE );

	oldTxState = TX_ON;

	Serial.println ( "WA2FZW - Amplifier/Preamp Sequencer - Version 1.5" );

//	Test_LNA ();

}


/*
 *	The "loop" function looks for changes in the "txState", which is set in the
 *	"TxInterrupt" ISR function.
 *
 *	If the state changes from receive to transmit, we run the sequence to set
 *	everything up for transmitting; if it goes from transmit to receive, we
 *	reverse things.
 */

void loop ()
{
	CheckBlink ();								// Need to flash the TX LED?

	SwitchPreamp ( BOTH_BANDS, LNA_RX );		// See if either preamp needs to be in RX mode


/*
 *	If the state of the transmitter changed (manually or via CAT control) we either
 *	start the transmit sequence or receive sequence as appropriate:
 */

	if ( txState != oldTxState )				// Did TX/RX state change?
	{
		oldTxState = txState;					// Yes, save current state

		if ( txState == TX_ON )					// Now transmitting?
			SetTransmit ();						// Yes, run transmit setup sequence

		else									// If receiving
			SetReceive ();						// Run the receive setup sequence
	}
}


/*
 *	"SetTransmit" puts the preamps into transmit mode then after LNA_ON_DLY milliseconds
 *	turns on the amplifiers The preamp/SDR switch LED operates from the associated preamp
 *	relay, so its state is handled automagically.
 */

void SetTransmit ()
{
	SwitchPreamp ( BOTH_BANDS, LNA_TX );		// Switch the preamps and SDR switchs to TX mode
	delay ( LNA_ON_DLY );						// Wait for them to actually switch

	digitalWrite ( TX1_LED, LED_ON );			// Yes, turn both LEDs on
	digitalWrite ( TX2_LED, LED_ON );

	digitalWrite ( AMP_RELAY, AMP_ON );			// Then turn on the amplifier(s)
}


/*
 *	"SetReceive" turns the amplifier(s) off. Then after an appropriate delay, switches
 *	the preamps and SDR switchs back into receive mode.
 */

void SetReceive ()
{
	digitalWrite ( AMP_RELAY, AMP_OFF );		// Then turn the amplifier off

	delay ( AMP_OFF_DLY );						// Wait for it to actually switch

	SwitchPreamp ( BOTH_BANDS, LNA_RX );		// Switch the preamps to RX mode if appropriate

	digitalWrite ( TX1_LED, LED_OFF );				// Yes, turn both LEDs on
	digitalWrite ( TX2_LED, LED_OFF );

}


/*
 *	"CheckBlink" looks to see if the "txState" is off (receive mode) and one or both of
 *	the LNAs are in the forced on mode. If so, we flash the associated transmit LED every
 *	quarter second as a warning to the operator.
 *
 *	As noted in the documentation, one must be extramely cautious putting the preamp and
 *	into the permanent on mode. The capability was added do that when using my IC-9700 in
 *	satellite mode and transmitting on 70cm and receiving on 2 meters, the 2 meter preamp
 *	would still be on when transmitting on 70cm.
 */

void CheckBlink ()
{
	if (( millis() % 100 ) != 0 )						// Tenth of a second mark?
		return;											// Nothing to do yet


/*
 *	Check preamp #1:
 */

	if (( txState == TX_OFF ) && lna_1_ForcedOn )
	{
		tx_1_LedState = !tx_1_LedState;
		digitalWrite ( TX1_LED, tx_1_LedState );	// Blink the LED
	}
	else if ( txState == TX_OFF )					// Not transmitting and preamp not
		digitalWrite ( TX1_LED, LED_OFF );			// forced on - LED off


/*
 *	Check preamp #2:
 */

	if (( txState == TX_OFF ) && lna_2_ForcedOn )
	{
		tx_2_LedState = !tx_2_LedState;
		digitalWrite ( TX2_LED, tx_2_LedState );	// Blink the LED
	}

	else if ( txState == TX_OFF )					// Not transmitting and preamp not
		digitalWrite ( TX2_LED, LED_OFF );			// forced on - LED off

	delay ( 2 );									// Force clock ahead a couple of ticks						
}


/*
 *	"TxInterrupt" is called via an interrupt whenever the "TX_Ground" pin changes
 *	state. All we do is record the new state for now.
 */

void TxInterrupt ()
{
	txState = digitalRead ( TX_GROUND );
}


/*
 *	"SwitchPreamp" turns the preamp on or off based on the argument and the state of
 *	the preamp switch.
 *
 *	Modified in Version 1.2 to allow the preamp to be forced into the always off mode
 *	or always on regardless of the TX/RX state of the radio.
 *
 *	If both the LNA_ON and LNA_OFF pins are in a high state, the preamp is turned
 *	on or off based on the TX/RX status of the radio.
 *
 *	If the LNA_ON pin is low, the preamp is turned on regardless of the TX/RX state
 *	of the radio.
 *
 *	If the LNA_OFF pin is low, the preamp is turned off regardless of the TX/RX state of
 *	the radio.
 *
 *	In Version 1.4, the LNA relay also operates an SDR switch. It can be disabled
 *	separately by turning its power off.
 *
 *	Modified in Version 1.5 to operate either one of 2 LNAs or both. For now, the code
 *	is essentially repeated twice; once for each preamp/SDR.
 */

void SwitchPreamp ( uint8_t whichBand, bool newState )
{

	if ( whichBand && BAND_1 )					// Band 1 request (or maybe both)?
	{
		

/*
 *	First see if the preamp switch is in the always on mode. That is incicated by a LOW
 *	on the LNA_ON pin. If this is the case, we turn it on and return.
 */

		lna_1_ForcedOn = !digitalRead ( LNA1_ON );		// Forced on if LOW

		if ( lna_1_ForcedOn )
			digitalWrite ( LNA1_RELAY, LNA_RX );		// Switch the preamp to receive mode


/*
 *	Next see if the switch says the preamp should remain off. This is indicated by a
 *	LOW condition on the LNA_OFF pin.
 */

		lna_1_ForcedOff = !digitalRead ( LNA1_OFF );	// See if the preamp is disabled

		if ( lna_1_ForcedOff )
			digitalWrite ( LNA1_RELAY, LNA_TX );		// Switch the preamp to transmit mode


/*
 *	If we get here, the preamp status depends entirely on the TX/RX state of the radio.
 *	If the requested state is receive mode and we aren't transmitting, then we put the
 *	preamp in to receive (on) mode. If the radio is transmitting or the requested mode
 *	is transmit, then we put the preamp into transmit (bypass) mode.
 */

		if ( !lna_1_ForcedOn && !lna_1_ForcedOff )
		{
			if (( newState == LNA_RX ) && ( txState == TX_OFF ))
				digitalWrite ( LNA1_RELAY, LNA_RX );	// Switch the preamp to receive mode

			else
				digitalWrite ( LNA1_RELAY, LNA_TX );	// Switch the preamp to transmit mode
		}
	}


/*
 * 	Now we repeat the process for band #2 (or maybe it was for both)
 * 
 *	First see if the preamp switch is in the always on mode. That is incicated by a LOW
 *	on the LNA_ON pin. If this is the case, we turn it on and return.
 */

	if ( whichBand && BAND_2 )					// Band 2 request (or maybe both)?
	{
		

/*
 *	First see if the preamp switch is in the always on mode. That is incicated by a LOW
 *	on the LNA_ON pin. If this is the case, we turn it on and return.
 */

		lna_2_ForcedOn = !digitalRead ( LNA2_ON );		// Forced on if LOW

		if ( lna_2_ForcedOn )
			digitalWrite ( LNA2_RELAY, LNA_RX );		// Switch the preamp to receive mode


/*
 *	Next see if the switch says the preamp should remain off. This is indicated by a
 *	LOW condition on the LNA_OFF pin.
 */

		lna_2_ForcedOff = !digitalRead ( LNA2_OFF );	// See if the preamp is disabled

		if ( lna_2_ForcedOff )
			digitalWrite ( LNA2_RELAY, LNA_TX );		// Switch the preamp to transmit mode


/*
 *	If we get here, the preamp status depends entirely on the TX/RX state of the radio.
 *	If the requested state is receive mode and we aren't transmitting, then we put the
 *	preamp in to receive (on) mode. If the radio is transmitting or the requested mode
 *	is transmit, then we put the preamp into transmit (bypass) mode.
 */
		if ( !lna_2_ForcedOn && !lna_2_ForcedOff )
		{
			if (( newState == LNA_RX ) && ( txState == TX_OFF ))
				digitalWrite ( LNA2_RELAY, LNA_RX );	// Switch the preamp to receive mode

			else
				digitalWrite ( LNA2_RELAY, LNA_TX );	// Switch the preamp to transmit mode
		}
	}
}


/*
 *	Test_LNA runs a never-ending loop to facilitate testing the LNA enable/disable/sequenced
 *	switches. It is only invoked when the function call at the end of the setup function is
 *	un-commented. When it is invoked, none of the other sequencer functions are operational.
 *
 *	The documentation describes how to use it.
 */

void Test_LNA ()
{
	while ( true )
	{
		digitalWrite ( LNA1_RELAY, HIGH );
		digitalWrite ( LNA2_RELAY, HIGH );

		if ( !digitalRead  (LNA1_ON ))
		{
			Serial.println ( "LNA1 Forced on" );
			for ( int i = 0; i < 5; i++ )
			{
				digitalWrite ( LNA1_RELAY, LOW );
				delay ( 200 );
				digitalWrite ( LNA1_RELAY, HIGH );
				delay ( 200 );
			}
		}

		if ( !digitalRead ( LNA1_OFF ))
		{
			Serial.println ( "LNA1 Forced off" );
			digitalWrite ( LNA1_RELAY, LOW );
		}

		if ( !digitalRead  (LNA2_ON ))
		{
			Serial.println ( "LNA2 Forced on" );
			for ( int i = 0; i < 5; i++ )
			{
				digitalWrite ( LNA2_RELAY, LOW );
				delay ( 200 );
				digitalWrite ( LNA2_RELAY, HIGH );
				delay ( 200 );
			}
		}


		if ( !digitalRead ( LNA2_OFF ))
		{
			Serial.println ( "LNA2 Forced off" );
			digitalWrite ( LNA2_RELAY, LOW );
		}

		delay ( 2500 );
	}
}
