/*
 *	"IC-9700 Sequencer" Version 2.0 - John M. Price (WA2FZW) 02/01/2025
 *
 *		This program was originally designed to run a sequencer for the Yaesu FT-891.
 *		Subsequent versions of the hardware and software were modified to also work with
 *		my Icom FT-9700 on multiple bands.
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
 *		When the preamp for one band is forced on and the radio is in receive mode, the
 *		transmit LED for that band blinks rapidly to warn the operator that transmitting
 *		on that band will probably destroy the preamp.
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
 *		K2 now switches the LNA and provides a PTT indication to an SDR switch.
 *
 *		Other than I changed the names of some of the symbols, there are no major changes
 *		in the code.
 *
 *
 *	Modified 12/01/2022 - Version 1.5
 *
 *		The hardware and software are both modified to support 2 band operation for the
 *		Icom IC-9700 and support for the Yaesu FT-891 is eliminated.
 *
 *		This version adds the capability to control 2 amplifiers, 2 preamps and 2 SDR
 *		switches for 2 different bands on the IC-9700, and drops the transmit inhibit
 *		capability that was for the FT-891 only.
 *
 *
 *	Modified 02/01/2025 - Version 2.0
 *
 *		The hardware was redesigned to add the capability to trigger the sequencer via
 *		a RTS signal provided over a USB to TTL converter. For digital modes using WSJT-X,
 *		this allows the sequencer to switch the LNA and SDR (if being used) followed by
 *		keying the amplifier and finally keying the radio itself. This allows us to have
 *		the same safety factor as the original design had using the transmit inhibit feature
 *		of the Yaesu FT-891.
 *
 *		For SSB operation where the radio is keyed via the microphone PTT, the original
 *		sequencing is retained. DO NOT try to use VOX keying or break-in CW. A foot switch
 *		can be used in parallel with the RTS signal for safe SSB or CW operation.
 *
 *		An Arduino Nano runs the unit.
 */

#include <Arduino.h>						// General Arduino definitions


/*
 *	Currently, there are 2 'DEBUG' levels. Setting 'DEBUG' to '1' enables status
 *	messages to be sent to the serial monitor. Setting it to '2' causes the
 *	sequence timings to be exaggerated to make the switching sequences easier to
 *	monitor with meters and/or scopes.
 */

#define	DEBUG	1


/*
 *	Define the GPIO pins used for various things.
 */

#define	PTT		    2					// PTT Indication (interrupt capable)
#define	RTS		    3					// RTS Indication (interrupt capable)
#define	TX1_LED		4					// Band 1 transmit indicator LED
#define	TX2_LED		5					// Band 2 transmit indicator LED
#define	AMP_RELAY	7					// Keys the amplifiers
#define	LNA1_RELAY	8					// Operates the band 1 preamp and SDR switch
#define	LNA2_RELAY	9					// Operates the band 2 preamp and SDR switch
#define	TX_KEY	   13					// Keys the radio when sequencer triggered via RTS
#define	LNA1_OFF   A0					// Preamp for band 1 always off (bypass) when LOW
#define LNA1_ON    A1					// Preamp for band 1 always on (RX mode) when LOW
#define	LNA2_OFF   A2					// Preamp for band 2 always off (bypass) when LOW
#define LNA2_ON    A3					// Preamp for band 2 always on (RX mode) when LOW


/*
 *	There are 2 TX LEDs and one might ask why. Whenever one of the LNAs is set to
 *	the always on condition, we blink the TX led associated with that LNA as a 
 *	warning to the operator that if they transmit on that band, they're going to
 *	turn the LNA into smoke!
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

#define	TX_OFF			HIGH				// When PTT is HIGH, transmitter is off
#define	TX_ON			 LOW				// When PTT is LOW, transmitter is on

#define	RTS_ACTIVE		 LOW				// The RTS and PTT pins are active
#define	PTT_ACTIVE		 LOW				// when they show a LOW indication


/*
 *	These symbols control the delay times between when we see the transmitter keyed
 *	and when we turn the amplifier and preamp on or off. I measured the on/off times for
 *	the preamp; it takes about 5mS to switch from receive to transmit and about 50mS
 *	to switch back to receive. We'll set the times considerably higher just to be sure.
 *
 *	If the DEBUG level is 2 or higher, the times are lengthened to facilitate checking
 *	the operation with a meter and/or a scope.
 */

#if ( DEBUG >= 2 )

#define	LNA_ON_DLY		3000				// Wait time after switching LNA to TX
#define	PTT_ON_DELAY	3000				// Wait time after switching amp to TX
#define	PTT_OFF_DELAY	  10				// Wait time between RTS inactive and PTT off
#define	AMP_OFF_DLY		3000				// Wait time after switching amp to RX

#else

#define	LNA_ON_DLY		25					// Wait time after switching LNA to TX
#define	PTT_ON_DELAY	50					// Wait time after switching amp to TX
#define	PTT_OFF_DELAY	10					// Wait time between RTS inactive and PTT off
#define	AMP_OFF_DLY		50					// Wait time after switching amp to RX

#endif


/*
 *	These are used to determine when the TX/RX state changes:
 */

volatile bool	txState     = TX_OFF;		// Current transmit/receive state
volatile bool	oldTxState  = TX_OFF;		// Previous state

volatile bool	rtsState   	= !RTS_ACTIVE;	// Assume RTS is not active (it could be)
volatile bool	pttState    = !PTT_ACTIVE;	// Assume PTR is not active (it could be)

		 bool	lna_1_ForcedOff;			// Preamp #1 always off if true
		 bool	lna_1_ForcedOn;				// Preamp #1 is always on if true
		 bool	lna_2_ForcedOff;			// Preamp #2 always off if true
		 bool	lna_2_ForcedOn;				// Preamp #2 is always on if true

		 bool	tx_1_LedState = false;		// Blinking TX1_LED is off
		 bool	tx_2_LedState = false;		// Blinking TX2_LED is off


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
 *	The order of things here is important.
 *
 *	Experimentation has shown that the preamp and amplifier relays will not operate
 *	until they are set as OUTPUT pins, thus the preamp will startup in transmit
 *	(bypass) mode and the amplifier will be off.
 *
 *	Make sure the radio isn't transmitting, unless the operator has it keyed manually.
 */

	pinMode ( TX_KEY, OUTPUT);
	digitalWrite ( TX_KEY, TX_OFF );

	txState = TX_OFF;								// Assume transmitter is off


/*
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

	delay ( AMP_OFF_DLY );							// Time for amp to switch to RX
	SwitchPreamp ( BOTH_BANDS, LNA_RX );			// Put LNAs into RX mode


/*
 *	Setup the LED, RTS and PTT pins, then get the current states of the RTS and PTT
 *	pins. If either is active it will be detected in the loop function and the
 *	appropriate action will be taken to start the appropriate transmit sequence.
 *
 *	Notice, we have NOT enabled the interrupt capability for either of these pins yet
 *	and that the old states are set to !ACTIVE.
 */

	pinMode      ( TX1_LED, OUTPUT );				// Band 1 transmit indicator LED pin
	pinMode      ( TX2_LED, OUTPUT );				// Band 2 transmit indicator LED pin

	pinMode      ( RTS, INPUT_PULLUP );				// USB to TTL converter RTS indicator pin
	pinMode      ( PTT, INPUT_PULLUP );				// Radio PTT indicator pin

	rtsState = digitalRead ( RTS );					// Get current RTS state
	pttState = digitalRead ( PTT );					// And PTT state


/*
 *	Whenever the radio changes state from transmit to receive or vice-versa an
 *	interrupt is generated. The radio can be put into the transmit mode by pressing
 *	the PTT switch on the microphone or via the transmit button on the radio. In
 *	either case an interrupt is generated from the PTT GPIO pin.
 *
 *	When operating on one of the digital modes using WSJT-X (or other programs),
 *	an interrupt is generated from the RTS GPIO pin. A footswitch can also be
 *	installed that mimics RTS keying for SSB or CW operation.
 *
 *	How we handle the sequencing is different depending how the sequencing was
 *	initiated. 
 */

	attachInterrupt	( digitalPinToInterrupt ( RTS ), RTS_ISR, CHANGE );
	attachInterrupt	( digitalPinToInterrupt ( PTT ), PTT_ISR, CHANGE );

	Serial.println ( "WA2FZW - Amplifier/Preamp Sequencer - Version 2.0\n" );

#if	DEBUG

	ShowPinStatus ();					// Display status of control inputs

#endif

//	Test_LNA ();						// Operates the relays in a never ending loop

}										// End of 'setup'


void loop ()
{
	CheckBlink ();								// Need to flash the TX LED?


/*
 *	SwitchPreamp is called every 50 milliseconds to detect whether or not the
 *	position of one of the LNA switches has changed.
 */

	if (( millis () % 50 ) == 0 )
		SwitchPreamp ( BOTH_BANDS, LNA_RX );	// See if either preamp needs to be in RX mode


/*
 *	If either the PTT or RTS line is active, the transmitter should be on if not so already.
 *	If both the PTT and RTS lines are not active, the transmitter should be in receive mode
 *	if not so already. Note the optional footswitch is operated, that is the same as the RTS
 *	being active.
 */

	if (( pttState == PTT_ACTIVE ) || ( rtsState == RTS_ACTIVE ))
		txState = TX_ON;

	if (( pttState != PTT_ACTIVE ) && ( rtsState != RTS_ACTIVE ))
		txState = TX_OFF;


/*
 *	Adding the following two lines of code cured a problem of the sequencer locking up
 *	in transmit mode when the foot switch was operated and released before the receive
 *	to transmit sequence had time to complete. I'm still not exactly sure why this fixed
 *	the problem!
 */

	if (( pttState == PTT_ACTIVE ) && ( rtsState != RTS_ACTIVE ))
		SetReceive ();


/*
 *	There are two ways the PTT can be active; either the radio was keyed manually, or
 *	the PTT was activated at the end of the transmit sequence that was initiated by
 *	the RTS going active.
 *
 *	So, whenever we see the PTT active, we turn the transmit LEDs on.
 */

	if ( digitalRead ( PTT ) == PTT_ACTIVE )
	{
		digitalWrite ( TX1_LED, LED_ON );
		digitalWrite ( TX2_LED, LED_ON );
	}

	else
	{
		digitalWrite ( TX1_LED, LED_OFF );
		digitalWrite ( TX2_LED, LED_OFF );
	}


/*
 *	If the transmitter state changed, we need to figure out how things changed and
 *	take the appropriate action. When going from receive to transmit, the sequence
 *	of switching depends on whether the transmitter was ordered on via the RTS
 *	line or via the PTT line. That will be handled in the SetTransmit function.
 */

	if ( txState != oldTxState )				// Did TX/RX state change?
	{
#if DEBUG

		Serial.print ( "\nTime: " );	Serial.println ( millis ());
		Serial.print ( "pttState = " );	Serial.println ( pttState );
		Serial.print ( "rtsState = " );	Serial.println ( rtsState );

#endif

		if ( txState == TX_ON )					// Now transmitting?
			SetTransmit ();						// Yes, run transmit setup sequence

		else									// If receiving
			SetReceive ();						// Run the receive setup sequence
	}
}												// End of loop function


/*
 *	SetTransmit puts the preamps into transmit mode then after LNA_ON_DLY milliseconds
 *	turns on the selected amplifier. The preamp/SDR switch LED operates from the associated
 *	preamp relay, so its state is handled automagically.
 *
 *	If the transmitter was ordered on via the RTS line, we also key the radio via the
 *	TX_KEY GPIO pin, which activates the radio's PTT line via K4 on the schematic. Note
 *	that this will cause an interrupt that the PTT_ISR function will see. It will be
 *	ignored it RTS is active.
 */

void SetTransmit ()
{
	SwitchPreamp ( BOTH_BANDS, LNA_TX );		// Switch the preamps and SDR switches to TX mode

	if ( DEBUG )
	{
		Serial.print ( "LNAs in TX: " );	Serial.println ( millis ());
	}

	delay ( LNA_ON_DLY );						// Wait for the preamp(s) to actually switch

	digitalWrite ( AMP_RELAY, AMP_ON );			// Then turn on the amplifier(s)

	if ( DEBUG )
	{
		Serial.print ( "Amp On " );			Serial.println ( millis ());
	}


/*
 *	If the RTS is active, we key the radio's PTT line, but note that we do not mark the PTT
 *	as being active. Doing so caused a lockup where the PTT was never being released when the
 *	RTS went inactive. Note also that PTT interrupts are ignored in the PTT_ISR function when
 *	RTS is active.
 */

	if ( rtsState == RTS_ACTIVE )				// Transmit commanded via the RTS?
	{
		delay ( PTT_ON_DELAY );					// Yes, wait for amplifier to switch to TX
		digitalWrite ( TX_KEY, TX_ON );			// Then key the radio

		if ( DEBUG )
		{
			Serial.print ( "PTT On " );		Serial.println ( millis ());
		}
	}

	oldTxState = txState = TX_ON;				// Transmitter is on
}												// End of SetTransmit


/*
 *	SetReceive also becomes a bit trickier in version 2.0 because that radio can
 *	now be transmitting because the RTS line was keyed by one of the digital programs
 *	or the optional footswitch or manually via the PTT line.
 *
 *	First thing we will do is to check if the RTS line is active. If it is active,
 *	we simply set the transmit state to on and return.
 *
 *	If RTS is inactive, we will try to put the radio into receive mode by dropping the
 *	TX_KEY line which will drop the PTT line unless the radio was also manually keyed.
 *	If that is the case, we don't want to switch the amplifier, LNA and SDR switch to
 *	receive mode.
 */

void SetReceive ()
{
	if ( digitalRead ( RTS ) == RTS_ACTIVE )	// If RTS is still on
	{
		txState = TX_ON;						// Then transmitter is assumed to be on
		return;									// Don't do anything
	}


/*
 *	RTS is not active, but may have been. If it was active, PTT might be active because
 *	we turned it on via K4, but it may have been on because the transmitter had been
 *	keyed manually.
 *
 *	We check to see if PTT is on and if so, we turn it off and wait up to PTT_OFF_DELAY
 *	milliseconds to see if it gors inactive. If PTT is still on after the delay, we
 *	assume that the transmitter is still manually keyed, so we need to show the 'txState'
 *	as ACTIVE and we return.
 */
 

	if ( digitalRead ( PTT ) == PTT_ACTIVE )			// Could be manual keying or K4
		for ( int i = 0; i < PTT_OFF_DELAY; i++ )
		{
			delay ( 1 );								// Wait a millisecond
			digitalWrite ( TX_KEY, TX_OFF );			// Try to put the radio back in receive mode
			if ( digitalRead ( PTT ) != PTT_ACTIVE )	// If PTT is inactive
				break;									// All is good!
		}

	if ( digitalRead ( PTT ) == PTT_ACTIVE )	// Check it again; if active
	{
		txState = TX_ON;						// the transmitter is assumed to be on
		return;									// manually, so don't do anything
	}

	if ( DEBUG )
	{
		Serial.print ( "PTT Off " );		Serial.println ( millis ());
	}

	digitalWrite ( TX1_LED, LED_OFF );
	digitalWrite ( TX2_LED, LED_OFF );

	digitalWrite ( AMP_RELAY, AMP_OFF );		// Then turn the amplifier off
	delay ( AMP_OFF_DLY );						// Wait for it to actually switch

	if ( DEBUG )
	{
		Serial.print ( "Amp Off " );		Serial.println ( millis ());
	}

	delay ( LNA_ON_DLY );						// Wait for it to actually switch

	SwitchPreamp ( BOTH_BANDS, LNA_RX );		// Switch the preamps to RX mode if appropriate

	if ( DEBUG )
	{
		Serial.print ( "LNAs in RX: " );	Serial.println ( millis ());
	}

	oldTxState = txState = TX_OFF;				// The transmitter is off
}												// End of SetRecive


/*
 *	CheckBlink looks to see if the 'txState' is off (receive mode) and one or both of
 *	the LNAs are in the forced on mode. If so, we flash the associated transmit LED every
 *	tenth of a second as a warning to the operator.
 *
 *	As noted in the documentation, one must be extremly cautious putting the preamp and
 *	into the permanent on mode. The capability was added do that when using my IC-9700 in
 *	satellite mode and transmitting on 70cm and receiving on 2 meters, the 2 meter preamp
 *	would still be on when transmitting on 70cm (or vice-versa).
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
}													// End of CheckBlink


/*
 *	PTT_ISR is called via an interrupt whenever the PTT pin changes state, and
 *	RTS_ISR is called via an interrupt whenever the RTS pin changes state.
 *
 *	All we do is set the current state of the pins. The loop logic will figure out
 *	what to do with the information.
 *
 *	Note that when the sequence is triggered by the RTS signal, we will get a PTT
 *	interrupt when we key the radio via the TX_KEY GPIO pin; it will be ignored.
 */

void PTT_ISR ()
{
	if ( rtsState == RTS_ACTIVE )
		return;

	pttState = digitalRead ( PTT );
}

void RTS_ISR ()
{
	rtsState = digitalRead ( RTS );
}


/*
 *	SwitchPreamp turns the preamp on or off based on the argument and the state of
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
 *	separately by turning its power off or operating the bypass switch on the unit.
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

		if ( !lna_1_ForcedOn && !lna_1_ForcedOff )		// If in sequenced mode
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
 
		if ( !lna_2_ForcedOn && !lna_2_ForcedOff )		// If in sequenced mode
		{
			if (( newState == LNA_RX ) && ( txState == TX_OFF ))
				digitalWrite ( LNA2_RELAY, LNA_RX );	// Switch the preamp to receive mode

			else
				digitalWrite ( LNA2_RELAY, LNA_TX );	// Switch the preamp to transmit mode
		}
	}
}														// End of SwitchPreamp


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
		digitalWrite ( LNA1_RELAY, LNA_RX );			// Put both in RX mode
		digitalWrite ( LNA2_RELAY, LNA_RX );

		if ( !digitalRead  ( LNA1_ON ))					// Is LNA 1 in forced on mode?
		{
			Serial.println ( "LNA1 Forced on" );		// Yes

			for ( int i = 0; i < 5; i++ )				// Cycle it 5 times
			{
				digitalWrite ( LNA1_RELAY, LNA_TX );
				delay ( 200 );
				digitalWrite ( LNA1_RELAY, LNA_RX );
				delay ( 200 );
			}
		}

		if ( !digitalRead ( LNA1_OFF ))					// LNA 1 in always off mode?
		{
			Serial.println ( "LNA1 Forced off" );		// Yes
			digitalWrite ( LNA1_RELAY, LNA_TX );		// Not sure about this???
		}

		if ( !digitalRead  ( LNA2_ON ))					// LNA 2 always on mode?
		{
			Serial.println ( "LNA2 Forced on" );		// Yes

			for ( int i = 0; i < 5; i++ )				// Cycle it 5 times
			{
				digitalWrite ( LNA2_RELAY, LNA_TX );
				delay ( 200 );
				digitalWrite ( LNA2_RELAY, LNA_RX );
				delay ( 200 );
			}
		}


		if ( !digitalRead ( LNA2_OFF ))					// LNA 2 in always off mode?
		{
			Serial.println ( "LNA2 Forced off" );
			digitalWrite ( LNA2_RELAY, LNA_TX );		// Not sure about this???
		}

		delay ( 2500 );									// 2 1/2 second delay
	}
}														// End of Test_LNA


/*
 *	ShowPinStatus displays the state of the LNA switch and the RTS and PTT inputs. It is
 *	called at the end of the setup function if the 'DEBUG' level is '1' or higher.
 */

void ShowPinStatus ()
{
	uint8_t	swStatus = 0;


/*
 *	LNA 1 Switch status:
 */

	Serial.print ( "LNA 1 Status = " );				// First part of the message

	if ( !digitalRead ( LNA1_OFF ))					// Now figure out the state of
		swStatus = 1;
	if ( !digitalRead ( LNA1_ON ))
		swStatus = 2;

	switch ( swStatus )
	{
		case 0:
			Serial.println ( "Sequenced" );
			break;

		case 1:
			Serial.println ( "Always Off" );
			break;

		case 2:
			Serial.println ( "Always On" );			// The switch
	}


/*
 *	LNA 2 Switch status:
 */

	swStatus = 0;
	Serial.print ( "LNA 2 Status = " );				// First part of the message

	if ( !digitalRead ( LNA2_OFF ))					// Now figure out the state of
		swStatus = 1;
	if ( !digitalRead ( LNA2_ON ))
		swStatus = 2;

	switch ( swStatus )
	{
		case 0:
			Serial.println ( "Sequenced" );
			break;

		case 1:
			Serial.println ( "Always Off" );
			break;

		case 2:
			Serial.println ( "Always On" );			// The switch
	}


/*
 *	RTS status:
 */

	Serial.print ( "RTS Status = " );

	if ( digitalRead ( RTS ) == RTS_ACTIVE )
		Serial.println ( "Active" );

	else
		Serial.println ( "Inactive" );


/*
 *	PTT status:
 */

	Serial.print ( "PTT Status = " );

	if ( digitalRead ( PTT ) == PTT_ACTIVE )
		Serial.println ( "Active" );

	else
		Serial.println ( "Inactive" );
}													// End of ShowPinStatus
