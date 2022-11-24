/*
 *	"FT-891 Sequencer" Version 1.4 - John M. Price (WA2FZW) 11/03/2022
 *
 *		This program runs the FT-891 sequencer module that I designed. The purpose
 *		of the module is to switch an antenna mounted meter preamp from receive mode
 *		to transmit mode, then key the linear amplifier before the FT-891 actually
 *		starts transmitting. When the radio goes from transmit to receive, the process is
 *		reversed.
 *
 *		The PCB uses relays to provide power (or remove it) from the preamp and to key the
 *		amplifier.
 *
 *		The key to the operation is the fact that the FT-891 has a "TX Inhibit" input.
 *		When +5V is applied to that input, it prevents the radio from transmitting
 *		regardless of the state of the PTT line.
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
 *		Hardware changes to allow keying of 2 amplifiers via K1 (2 meter and 432 on the
 *		IC-9700). Also changed the hardware so that in addition to controlling the LNA,
 *		K2 now switches the LNA and provides a PTT indication to an SDR switch for the
 *		FT-891.
 *
 *		Other than I changed the names of some of the symbols, there are no major changes
 *		in the code.
 */

#include <Arduino.h>						// General Arduino definitions


/*
 *	Define the GPIO pins used for various things.
 */

#define	TX_GROUND	    2					// PTT Indication (interrupt capable)
#define	TX_INH			4					// Transmit inhibit output to the radio
#define	TX_LED			5					// Transmit indicator LED
#define	AMP_RELAY		7					// Keys the amplifiers
#define	LNA_RELAY		8					// Operates the preamp and SDR switch
#define	LNA_OFF			9					// Preamp always off (bypass) when LOW
#define LNA_ON		   10					// Preamp always on (RX mode) when LOW


/*
 *	These define the HIGH/LOW conditions in more meaningful terms:
 */

#define	LED_ON			 LOW				// Turn the transmit (red) LED on
#define	LED_OFF			HIGH				// Turn the transmit (red) LED off

#define	PTT_ON			 LOW				// When TX_GROUND (aka PTT) is LOW we're transmitting
#define	PTT_OFF			HIGH				// Receiving

#define	AMP_ON			 LOW				// Go LOW to key the amplifiers
#define	AMP_OFF			HIGH

#define	LNA_RX			HIGH				// Go LOW to put the LNA in receive mode
#define	LNA_TX			 LOW				// LOW also triggers the SDR switch

#define	INH_ON			HIGH				// TX_INH goes high to stop transmitting
#define	INH_OFF			 LOW				// on the FT-891 only (N/A for the IC-9700)

#define	TX_OFF			HIGH				// When "TX_GROUND" is HIGH, transmitter is off
#define	TX_ON			 LOW				// When "TX_GROUND" is LOW, transmitter is on


/*
 *	These symbols control the delay times between when we see the transmitter keyed
 *	and when we turn the amplifier and preamp on or off. I measured the on/off times for
 *	the preamp; it takes about 5mS to switch from receive to transmit and about 50mS
 *	to switch back to receive. We'll set the times considerably higher just to be sure.
 *
 *	I haven't measured the switching time for the amp, but it really doesn't matter.
 */

#define	AMP_ON_DLY		 50					// 50 milliseconds
#define	AMP_OFF_DLY		 50					// 50 milliseconds

#define	LNA_ON_DLY		 25					// 25 milliseconds
#define	LNA_OFF_DLY		100					// 100 milliseconds (currently not used)


/*
 *	These are used to determine when the TX/RX state changes:
 */

volatile bool txState     = TX_OFF;			// Current transmit/receive state
volatile bool oldTxState  = TX_OFF;			// Previous state

		 bool lnaForcedOff;					// Preamp always off if true
		 bool lnaForcedOn;					// Preamp is always on if true

		 bool txLedState = false;			// Blinking TX LED is off


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
 */

	pinMode		 ( TX_INH, OUTPUT );			// Actual inhibit signal and associated LED
	digitalWrite ( TX_INH, INH_ON );			// are on


/*
 *	Next, make sure the amplifier is off:
 */

	pinMode      ( AMP_RELAY, OUTPUT );			// Amplifier relay
	digitalWrite ( AMP_RELAY, AMP_OFF );		// Amplifiers are off

	pinMode		 ( LNA_ON,  INPUT_PULLUP );		// Preamp always on switch contact
	pinMode		 ( LNA_OFF, INPUT_PULLUP );		// Preamp always off switch contact
	pinMode      ( LNA_RELAY, OUTPUT );			// Preamp/SDR switch relay


/*
 *	Now we can safely put the preamp into receive mode if it is enabled:
 */

	SwitchPreamp ( LNA_RX );


/*
 *	We don't really know the state of the PTT signal, so we will check it. Based on
 *	its current state, we set the values of "txState" and "oldTxState" and turn the
 *	TX LED on or off as appropriate. Remember, the TX inhibit is still on, so the
 *	radio cannot actually produce power (FT-891 only).
 */

	pinMode      ( TX_GROUND, INPUT_PULLUP );			// Radio PTT indicator pin
	oldTxState = txState = digitalRead ( TX_GROUND );	// Read it and set state variables

	pinMode      ( TX_LED, OUTPUT );					// Transmit indicator LED pin

	if ( txState == TX_ON )								// Is the PTT active?
		digitalWrite ( TX_LED, LED_ON );				// Yes, turn the LED on

	else												// Otherwise
		digitalWrite ( TX_LED, LED_OFF );				// Yes, turn the LED off


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

	Serial.println ( "WA2FZW - Amplifier/Preamp Sequencer - Version 1.4" );



/*
 *	If the following function call is un-commented, we go into a loop that just blinks
 *	the LEDs on and off for testing purposes. The sequencer must be connected to a
 *	13.8V source for the preamp LED, but the preamp and amplifier should not be connected
 *	as the function does operate the associated relays for those.
 */

//	TestLEDs ();							// Blinks the LEDS forever if enabled
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

	SwitchPreamp ( LNA_RX );					// See if the preamp needs to be in RX mode


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
 *	"SetTransmit" puts the preamp in transmit mode then after LNA_ON_DLY milliseconds
 *	turns on the amplifier then after another AMP_ON_DLY milliseconds releases the TX
 *	inhibit line (and turns the LED off). The preamp/SDR switch LED operates from the
 *	preamp relay, so its state is handled automagically.
 *
 *	Note that for the FT-891, the TX_INH is always on when in receive mode, so the radio
 *	cannot produce power until we release it; this is not applicable for the IC-9700.
 */

void SetTransmit ()
{
	digitalWrite ( TX_INH, INH_ON );			// The inhibit line should already be on,
	digitalWrite ( TX_LED, LED_ON );			// Indicate PTT is active

	SwitchPreamp ( LNA_TX );					// Switch the preamp and SDR switch to TX mode
	delay ( LNA_ON_DLY );						// Wait for it to actually switch

	digitalWrite ( AMP_RELAY, AMP_ON );			// Then turn on the amplifier(s)

	delay ( AMP_ON_DLY );						// Time for it to actually switch

	digitalWrite ( TX_INH, INH_OFF );			// Allow the transmitter to produce power
}


/*
 *	"SetReceive" turns the TX inhibit line (and LED) on and turns the amplifier(s) off. Then
 *	after an appropriate delay, switches the preamp and SDR switch back into receive mode.
 */

void SetReceive ()
{
	digitalWrite ( TX_INH, INH_ON );			// Disable the transmitter
	digitalWrite ( TX_LED, LED_OFF );			// Indicate PTT is not active
	digitalWrite ( AMP_RELAY, AMP_OFF );		// Then turn the amplifier off

	delay ( AMP_OFF_DLY );						// Wait for it to actually switch

	SwitchPreamp ( LNA_RX );					// Switch the preamp to RX mode if appropriate
}


/*
 *	"CheckBlink" looks to see if the "txState" is off (receive mode) and  "lnaForcedOn"
 *	is true. If so, we flash the transmit LED every quarter second as a warning to the
 *	operator.
 *
 *	As noted in the documentation, one must be extramely cautious putting the preamp and
 *	into the permanent on mode. The capability was added do that when using my IC-9700 in
 *	satellite mode and transmitting on 70cm and receiving on 2 meters, the 2 meter preamp
 *	would still be on when transmitting on 70cm.
 */

void CheckBlink ()
{
	if (( txState == TX_OFF ) && lnaForcedOn )
	{
		if (( millis() % 100 ) == 0 )					// Tenth of a second mark?
		{
			txLedState = !txLedState;
			digitalWrite ( TX_LED, txLedState );		// Blink the LED
			delay ( 2 );								// Force the clock ahead a few ticks
		}

	else if ( txState == TX_OFF )						// Not transmitting and preamp not
		digitalWrite ( TX_LED, LED_OFF );				// forced on - LED off
	}
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
 */

void SwitchPreamp ( bool newState )
{

/*
 *	First see if the preamp switch is in the always on mode. That is incicated by a LOW
 *	on the LNA_ON pin. If this is the case, we turn it on and return.
 */

	lnaForcedOn = !digitalRead ( LNA_ON );			// Forced on if LOW

	if ( lnaForcedOn )
	{
		digitalWrite ( LNA_RELAY, LNA_RX );			// Switch the preamp to receive mode
		return;
	}


/*
 *	Next see if the switch says the preamp should remain off. This is indicated by a
 *	LOW condition on the LNA_OFF pin.
 */

	lnaForcedOff = !digitalRead ( LNA_OFF );		// See if the preamp is disabled

	if ( lnaForcedOff )
	{
		digitalWrite ( LNA_RELAY, LNA_TX );			// Switch the preamp to transmit mode
		return;
	}


/*
 *	If we get here, the preamp status depends entirely on the TX/RX state of the radio.
 *	If the requested state is receive mode and we aren't transmitting, then we put the
 *	preamp in to receive (on) mode. If the radio is transmitting or the requested mode
 *	is transmit, then we put the preamp into transmit (bypass) mode.
 */
	
	if (( newState == LNA_RX ) && ( txState == TX_OFF ))
		digitalWrite ( LNA_RELAY, LNA_RX );		// Switch the preamp to receive mode

	else
		digitalWrite ( LNA_RELAY, LNA_TX );		// Switch the preamp to transmit mode
}


/*
 *	A blink function to test the LEDs and relays; when invoked, it runs forever.
 *
 *	Note that the preamp LED will not light unless the sequencer is connected to the
 *	radio which provides the 13.8V needed to activate the preamp and light the LED.
 *
 *	The relay for the amplifier is activated when the TX LED is turned on so one can test
 *	the contact closure for that relay; it is inactive for the other two states.
 */

void TestLEDs ()
{
	while ( true )
	{


/*
 *	Step 1: Turn the transmit LED and amplifier relay on and turn everything else off.
 */

		digitalWrite ( TX_LED,       LED_ON );			// Transmit LED is on
		Serial.println ( "Transmit LED is on" );		// If the serial monitor is active

		digitalWrite ( TX_INH,       INH_OFF );			// Inhibit line is off

		SwitchPreamp ( LNA_TX );						// Preamp in TX mode (LED off)

		digitalWrite ( AMP_RELAY,    AMP_ON );			// amplifier off

		delay ( 2000 );


/*
 *	Step 2: Turn the inhibit LED and inhibit line on and turn everything else off.
 */

		digitalWrite ( TX_INH,       INH_ON );			// Inhibit line is on

		SwitchPreamp ( LNA_TX );						// Preamp in TX mode (LED off)

		digitalWrite ( AMP_RELAY,    AMP_OFF );			// amplifier off

		digitalWrite ( TX_LED,       LED_OFF );			// Transmit LED is off

		Serial.println ( "Transmit Inhibit LED is on" );

		delay ( 2000 );


/*
 *	Step 3: Put the preamp in receive mode (and LED on) and turn everything else off
 *	unless the preamp is disabled.
 */

		lnaForcedOff = digitalRead  ( LNA_ON );	// See if the preamp is enabled

		Serial.print ( "lnaForcedOff = ");
		Serial.println ( lnaForcedOff );

		if ( lnaForcedOff )							// If the preamp is enabled
		{
			SwitchPreamp ( LNA_RX );					// Switch the preamp to receive mode
			Serial.println ( "Preamp LED is on" ); 
		}

		else											// Otherwise
		{
			SwitchPreamp ( LNA_TX );					// Switch the preamp to transmit mode
			Serial.println ( "Preamp is disabled - LED is off" );
		}

		digitalWrite ( AMP_RELAY,    AMP_OFF );			// amplifier off

		digitalWrite ( TX_INH,       INH_OFF );			// Inhibit line is off

		digitalWrite ( TX_LED,       LED_OFF );			// Transmit LED is off

		delay ( 2000 );
	}
}
