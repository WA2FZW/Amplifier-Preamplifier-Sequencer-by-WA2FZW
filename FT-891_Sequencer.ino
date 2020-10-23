/*
 *	"FT-891 Sequencer" - John M. Price (WA2FZW) 10/10/2020
 *
 *		This program runs the FT-891 sequencer module that I designed. The purpose
 *		of the module is to switch an antenna mounted 6 meter preamp from receive mode
 *		to transmit mode, then key the SB-200 linear amplifier before the FT-891 actually
 *		starts transmitting. When the radio goes from transmit to receive, the process is
 *		reversed.
 *
 *		The PCB uses relays to provide power (or remove it) from the preamp and to key the
 *		SB-200.
 *
 *		The key to the operation is the fact that the FT-891 has a "TX Inhibit" input.
 *		When +5V is applied to that input, it prevents the radio from transmitting
 *		regardless of the state of the PTT line.
 *
 *		An Arduino Nano runs the unit.
 */

#include <Arduino.h>						// General Arduino definitions


/*
 *	Define the GPIO pins used for various things.
 */

#define	TX_GROUND	    2					// PTT Indication (interrupt capable)
#define	TX_INH			4					// Transmit inhibit output to the radio
#define	TX_LED			5					// Transmit indicator LED
#define	TX_INH_LED		6					// Transmit inhibit LED
#define	AMP_RELAY		7					// Keys the SB-200
#define	PREAMP_RELAY	8					// Operates the preamp mode
#define	PREAMP_SWITCH	9					// Preamp enable/disable switch


/*
 *	These define the HIGH/LOW conditions in more meaningful terms:
 */

#define	LED_ON			 LOW				// Turn either LED on or off
#define	LED_OFF			HIGH

#define	PTT_ON			LOW					// When TX_GROUND (aka PTT) is LOW we're transmitting
#define	PTT_OFF			HIGH

#define	AMP_ON			 LOW				// Go LOW to key the SB-200				
#define	AMP_OFF			HIGH

#define	PREAMP_RX		 LOW				// Go low to put the preamp in receive mode
#define	PREAMP_TX		HIGH

#define	PREAMP_ENABLE	HIGH				// Preamp is enabled (RX mode) when switch is HIGH
#define	PREAMP_DISABLE	 LOW				// disables (TX mode) when switch is LOW

#define	INH_ON			HIGH				// TX_INH goes high to stop transmitting
#define	INH_OFF			 LOW

#define	TX_OFF			HIGH				// When "TX_GROUND" is HIGH, transmitter is off
#define	TX_ON			 LOW				// When "TX_GROUND" is LOW, transmitter is on


/*
 *	These symbols control the delay times between when we see the transmitter keyed
 *	and when we turn the SB-200 and preamp on or off. I measured the on/off times for
 *	the preamp; it takes about 5mS to switch from receive to transmit and about 50mS
 *	to switch back to receive. We'll set the times considerably higher just to be sure.
 *
 *	I haven't measured the switching time for the amp, but it really doesn't matter.
 */

#define	AMP_ON_DLY		 50					// 50 milliseconds
#define	AMP_OFF_DLY		 50					// 50 milliseconds

#define	PREAMP_ON_DLY	 25					// 25 milliseconds
#define	PREAMP_OFF_DLY	100					// 100 milliseconds (currently not used)


/*
 *	These are used to determine when the TX/RX state changes:
 */

volatile bool txState     = TX_OFF;			// Current transmit/receive state
volatile bool oldTxState  = TX_OFF;			// Previous state

		 bool preampEnabled;				// Preamp enabled (true) or disabled

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
 *	inhibit line on. Should the transmitter happen to be keyed when the program
 *	starts, this will prevent the transmitter from producing any output power until
 *	everything else is set up.
 *
 *	Experimentation has shown that the preamp and SB-200 relays will not operate
 *	until they are set as "OUTPUT" pins, thus the preamp will startup in transmit
 *	(bypass) mode and the amplifier will be off.
 */

	pinMode		 ( TX_INH, OUTPUT );				// Actual inhibit signal
	digitalWrite ( TX_INH, INH_ON );				// is on

	pinMode      ( TX_INH_LED, OUTPUT );			// Transmit inhibit indicator LED
	digitalWrite ( TX_INH_LED, LED_ON );			// is on


/*
 *	Next, make sure the amplifier is off:
 */

	pinMode      ( AMP_RELAY, OUTPUT );				// SB-200 relay
	digitalWrite ( AMP_RELAY, AMP_OFF );			// Amplifier is off

	pinMode		 ( PREAMP_SWITCH, INPUT_PULLUP );	// Enable/disable switch
	pinMode      ( PREAMP_RELAY, OUTPUT );			// Preamp relay


/*
 *	Now we can safely put the preamp into receive mode if it is enabled:
 */

	SwitchPreamp ( PREAMP_RX );


/*
 *	We don't really know the state of the PTT signal, so we will check it. Based on
 *	its current state, we set the values of "txState" and "oldTxState" and turn the
 *	TX LED on or off as appropriate. Remember, the TX inhibit is on, so the radio
 *	cannot actually produce power.
 */

	pinMode      ( TX_GROUND, INPUT_PULLUP );			// Radio PTT indicator pin
	oldTxState = txState = digitalRead ( TX_GROUND );	// Read it and set state variables

	pinMode      ( TX_LED, OUTPUT );					// Transmit indicator LED pin

	if ( txState == TX_ON )								// Is the PTT active?
		digitalWrite ( TX_LED, LED_ON );				// Yes, turn the LED on

	else												// Otherwise
		digitalWrite ( TX_LED, LED_ON );				// Yes, turn the LED off


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

	Serial.println ( "WA2FZW - FT-891 Transmit Sequencer - Version 1.1" );



/*
 *	If the following function call is un-commented, we go into a loop that just blinks
 *	the LEDs on and off for testing purposes. The sequencer must be connected to the
 *	radio to provide the 13.8V source for the preamp LED, but the preamp and amplifier
 *	should not be connected as the function does operate the associated relays for
 *	those.
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

void loop()
{
	SwitchPreamp ( PREAMP_RX );					// See if the preamp needs to be in RX mode

	if ( txState != oldTxState )				// Did state change?
	{
		oldTxState = txState;					// Yes, save current state

		if ( txState == TX_ON )					// Now transmitting?
			SetTransmit ();						// Yes, run transmit setup sequence

		else									// If receiving
			SetReceive ();						// Run the receive setup sequence
	}
}


/*
 *	"SetTransmit" puts the preamp in transmit mode then after PREAMP_ON_DLY milliseconds
 *	turns on the SB-200 then after another AMP_ON_DLY milliseconds releases the TX
 *	inhibit line (and turns the LED off). The preamp LED operates from the preamp
 *	relay, so its state is handled automagically.
 */

void SetTransmit ()
{
	digitalWrite ( TX_INH, INH_ON );				// The inhibit line should already be on,
	digitalWrite ( TX_INH_LED, LED_ON );			// But just to be certain

	digitalWrite ( TX_LED, LED_ON );				// Indicate PTT is active

	SwitchPreamp ( PREAMP_TX );						// Switch the preamp to transmit mode
	
	delay ( PREAMP_ON_DLY );						// Wait for it to actually switch

	digitalWrite ( AMP_RELAY, AMP_ON );				// Then turn on the SB-200

	delay ( AMP_ON_DLY );							// Time for it to actually switch

	digitalWrite ( TX_INH,     INH_OFF );			// Allow the transmitter to produce power
	digitalWrite ( TX_INH_LED, LED_OFF );			// and turn the LED off
}


/*
 *	"SetReceive" turns the TX inhibit line (and LED) on and turns the SB-200 off. Then
 *	after an appropriate delay, switches the preamp back into receive mode.
 */

void SetReceive ()
{
	digitalWrite ( TX_INH, INH_ON );				// Disable the transmitter
	digitalWrite ( TX_INH_LED, LED_ON );			// and turn the inhibit LED on

	digitalWrite ( TX_LED, LED_OFF );				// Indicate PTT is not active

	digitalWrite ( AMP_RELAY, AMP_OFF );			// Then turn the SB-200 off

	delay ( AMP_OFF_DLY );							// Wait for it to actually switch

	SwitchPreamp ( PREAMP_RX );						// Switch the preamp to RX mode (if enabled)
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
 *	"SwitchPreamp" turns the preamp on or off based on the argument.
 *
 *	BUT, we only will switch it into receive mode if three conditions are true:
 *
 *		1.	The requested "newState" is receive mode
 *		2.	The preamp is enabled (via the switch)
 *		3.	We are not currently transmitting
 *
 *	If any of those conditions are false, we switch it into transmit (bypass) mode.
 */

void SwitchPreamp ( bool newState )
{
	preampEnabled = digitalRead  ( PREAMP_SWITCH );			// See if the preamp is enabled
	
	if (( newState == PREAMP_RX ) && preampEnabled && (txState == TX_OFF ))
		digitalWrite ( PREAMP_RELAY, PREAMP_RX );			// Switch the preamp to receive mode

	else
		digitalWrite ( PREAMP_RELAY, PREAMP_TX );			// Switch the preamp to transmit mode
}


/*
 *	A blink function to test the LEDs and relays; when invoked, it runs forever.
 *
 *	Note that the preamp LED will not light unless the sequencer is connected to the
 *	radio which provides the 13.8V needed to activate the preamp and light the LED.
 *
 *	The relay for the SB-200 is activated when the TX LED is turned on so one can test
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

		digitalWrite ( TX_INH_LED,   LED_OFF );			// Inhibit LED is off
		digitalWrite ( TX_INH,       INH_OFF );			// Inhibit line is off

		SwitchPreamp ( PREAMP_TX );						// Preamp in TX mode (LED off)

		digitalWrite ( AMP_RELAY,    AMP_ON );			// SB-200 off

		delay ( 2000 );


/*
 *	Step 2: Turn the inhibit LED and inhibit line on and turn everything else off.
 */

		digitalWrite ( TX_INH_LED,   LED_ON );			// Inhibit LED is on
		digitalWrite ( TX_INH,       INH_ON );			// Inhibit line is on

		SwitchPreamp ( PREAMP_TX );						// Preamp in TX mode (LED off)

		digitalWrite ( AMP_RELAY,    AMP_OFF );			// SB-200 off

		digitalWrite ( TX_LED,       LED_OFF );			// Transmit LED is off

		Serial.println ( "Transmit Inhibit LED is on" );

		delay ( 2000 );


/*
 *	Step 3: Put the preamp in receive mode (and LED on) and turn everything else off
 *	unless the preamp is disabled.
 */

		preampEnabled = digitalRead  ( PREAMP_SWITCH );	// See if the preamp is enabled

		Serial.print ( "preampEnabled = ");
		Serial.println ( preampEnabled );

		if ( preampEnabled )							// If the preamp is enabled
		{
			SwitchPreamp ( PREAMP_RX );					// Switch the preamp to receive mode
			Serial.println ( "Preamp LED is on" ); 
		}

		else											// Otherwise
		{
			SwitchPreamp ( PREAMP_TX );					// Switch the preamp to transmit mode
			Serial.println ( "Preamp is disabled - LED is off" );
		}

		digitalWrite ( AMP_RELAY,    AMP_OFF );			// SB-200 off

		digitalWrite ( TX_INH_LED,   LED_OFF );			// Inhibit LED is off
		digitalWrite ( TX_INH,       INH_OFF );			// Inhibit line is off

		digitalWrite ( TX_LED,       LED_OFF );			// Transmit LED is off

		delay ( 2000 );
	}
}
