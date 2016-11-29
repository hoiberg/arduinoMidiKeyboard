  //
  //
  //     .oooooo..o     .              o8o                  oooooo   oooooo     oooo                       
  //    d8P'    `Y8   .o8              `"'                   `888.    `888.     .8'                        
  //    Y88bo.      .o888oo  .ooooo.  oooo  ooo. .oo.         `888.   .8888.   .8'    .oooo.   oooo    ooo 
  //     `"Y8888o.    888   d88' `88b `888  `888P"Y88b         `888  .8'`888. .8'    `P  )88b   `88.  .8'  
  //         `"Y88b   888   888ooo888  888   888   888  o8o     `888.8'  `888.8'      .oP"888    `88..8'   
  //    oo     .d8P   888 . 888    .o  888   888   888  `"'      `888'    `888'      d8(  888     `888'    
  //    8""88888P'    "888" `Y8bod8P' o888o o888o o888o o8o       `8'      `8'       `Y888""8o     .8'     
  //                                                    `]P                                    .o..P'      
  //                                                    '                                     `Y8P'       
  //
  //  Stein;Way v1 by Hoiberg42
  //  Created on 03-11-16
  //  Last updated on 29-11-16
  //  www.hangar42.nl
  //
  //  Note: I wired up the outputs of the shift registers incorrectly.
  //  This is compensated for by the _btn_notes lookup array.
  //  
  //  Note 2: The black keys on my keyboard give when pressed smaller
  //  time intervals (between the two buttons). So we use _btn_types
  //  as a lookup to determine whether a key is black or white.
  //
  //  Note 3: After setting latch high, wait at least one millisecond 
  //  before reading input pin states.
  //
  //  Note 4: MIDI_IN_MIDI_OUT not tested. Though I have tested parse_incoming()
  //  in another software project.
  //
  //  Bug: When 3 or more keys are pressed/released at exactly the same time, one
  //  or more of 'em will probably be ignored (resulting in a missing noteOn/noteOff).
  //  Only when USB is used as output (probably because of incorrect diode value -
  //  3V3 instead of 3V6).
  //                                                                                                 
  
  /********** Includes **********/

  #include <SoftwareSerial.h>
  #include <EEPROM.h>
  
  
  /********** Macros **********/

  #define SET(x,y) (x |= (1 << y))
  #define CLR(x,y) (x&=(~(1<<y)))
  #define ON(x,y)  (x & (1 << y))
  
  
  /********** Defines **********/
  
  #define D_LOW  CLR(PORTD, 5) // data pin
  #define D_HIGH SET(PORTD, 5)
  #define L_LOW  CLR(PORTD, 6) // latch pin
  #define L_HIGH SET(PORTD, 6)
  #define C_LOW  CLR(PORTD, 7) // clock pin
  #define C_HIGH SET(PORTD, 7)

  #define LED_LOW  CLR(PORTD, 4) // mode indicator LED
  #define LED_HIGH SET(PORTD, 4)

  #define BUZZER A3 // buzzer pin for debug

  #define MIDI_IN_BLE_OUT  0 // in/output modes
  #define MIDI_IN_USB_OUT  1
  #define MIDI_IN_MIDI_OUT 2

  #define MI_BO_PATTERN B10111111 // led blink patters
  #define MI_UO_PATTERN B10101111
  #define MI_MO_PATTERN B10101011
  
  
  /********** Prefs **********/

  //#define PRINT_DEBUG // for debug
  //#define PRINT_ON_TIMES
  //#define PRINT_OFF_TIMES
  //#define BUZ_DEBUG
  //#define BUZ_KEY_EVENTS
  
  #define LED_BLINK_LENGTH 100 // milliseconds
  #define BTN_DEBOUNCE_MIN 50

  #define ON_SLOW_MIN 3000 // microseconds
  #define ON_SLOW_MAX 120000
  #define ON_FAST_MIN 1800
  #define ON_FAST_MAX 120000

  #define OFF_SLOW_MIN 13000 // microseconds
  #define OFF_SLOW_MAX 250000
  #define OFF_FAST_MIN 8000
  #define OFF_FAST_MAX 180000

  /********** Enums **********/

  enum MessageNumber: byte {
    NoteOff = 0x8,
    NoteOn = 0x9,
    PolyphonicAftertouch = 0xA,
    ControlChange = 0xB,
    ProgramChange = 0xC,
    ChannelPressureAftertouch = 0xD,
    PitchWheelChange = 0xE,
    System = 0xF
  };

  enum SystemMessageNumber: byte {
    SystemExclusive = 0xF0,
    TimeCode = 0xF1,
    SongPosition = 0xF2,
    SongSelect = 0xF3,
    Undefined1 = 0xF4,
    Undefined2 = 0xF5,
    TuneRequest = 0xF6,
    EndOfExclusive = 0xF7,
    TimingClock = 0xF8,
    Undefined3 = 0xF9,
    Start = 0xFA,
    Continue = 0xFB,
    Stop = 0xFC,
    Undefined4 = 0xFD,
    ActiveSensing = 0xFE,
    Reset = 0xFF
  };

  enum KeyType: byte {
    Black = 0,
    White = 1
  };
  
  
  /********** Globals **********/

  byte _mode = MIDI_IN_BLE_OUT; // 0-2
  byte _channel = 0; // 0-15
  byte _octave = 36;  // 0-72 in steps of 12 (=offset)

  SoftwareSerial _serial_midi(2, 3);
  SoftwareSerial _serial_ble(A5, A4);
  
  Stream * _serial_out; // current active serial output

  byte _led_pattern = MI_BO_PATTERN;
  byte _led_index = 0; // current postition in pattern
  unsigned long _led_timing = 0; // time of last led toggle

  byte _btn_notes[22][6]; // lookup for midi notes
  byte _btn_types[22][6]; // lookup for fast/slow button
  byte _btn_states[22][6]; //TODO: Will be zero, right?
  unsigned long _btn_timestamps[22][6];

  byte _mode_btn_state = 0;
  byte _oct_btn_state = 0;
  byte _last_mode_btn_state = 0;
  byte _last_oct_btn_state = 0;
  unsigned long _mode_btn_debounce = 0;
  unsigned long _oct_btn_debounce = 0;
  

  /********** Functions **********/
  
  void setup() {

    //
    // set pin modes

    DDRD = B11111010;
    DDRB = B00000000;
    DDRC = B00011000;


    //
    // notify we've started

    #ifdef BUZ_DEBUG
      tone(BUZZER, 700, 200);
      delay(300);
    #endif


    //
    // load globals (or set defaults)

    if (EEPROM.read(0) > 10) {
      EEPROM.write(0, _mode);
      EEPROM.write(1, _octave);

      #ifdef BUZ_DEBUG
        tone(BUZZER, 300, 200); 
      #endif
    } else {
      _mode = EEPROM.read(0);
      _octave = EEPROM.read(1);

      #ifdef BUZ_DEBUG
        tone(BUZZER, 700, 200); 
      #endif
    }

    #ifdef BUZ_DEBUG
      delay(300);
    #endif


    //
    // fill in notes lookup

    const byte real_order[22] = {7, 0, 1, 2, 3, 4, 5, 6, 15, 8, 9, 10, 11, 12, 13, 14, 21, 16, 17, 18, 19, 20};

    for (byte r = 0; r < 22; r++) {
      byte i = real_order[r];
      for (byte c = 0; c < 3; c++) {
        byte v = (2-c)*21 + i;
        _btn_notes[r][c] = v;
        _btn_notes[r][c+3] = v;
      }
    }


    //
    // fill in btn types lookup

    for (byte r = 0; r < 22; r++) {
      for (byte c = 0; c < 6; c++) {
        switch (_btn_notes[r][c] % 12) {
          case 1:
          case 3:
          case 6:
          case 8:
          case 10:
            _btn_types[r][c] = Black;
            break;
          default:
            _btn_types[r][c] = White;
            break;
        }
      }
    }


    //
    // check channel jumpers connected to PINB 0-3

    L_LOW;
    shiftOut(5, 7, MSBFIRST, B01000000);
    shiftOut(5, 7, MSBFIRST, 0);
    shiftOut(5, 7, MSBFIRST, 0);
    L_HIGH;

    delay(1); // wait a little
    _channel = PINB & 0xF; // load reg

    
    //
    // load serial
    
    Serial.begin(31250); // USB out
    _serial_ble.begin(38400); // Bluetooth out
    _serial_midi.begin(31250); // MIDI in/out
    _serial_midi.listen(); // only MIDI has to listen
        
    if (_mode == MIDI_IN_BLE_OUT) {
      _serial_out = &_serial_ble; 
      _led_pattern = MI_BO_PATTERN;
    } else if (_mode == MIDI_IN_USB_OUT) {
      _serial_out = &Serial; 
      _led_pattern = MI_UO_PATTERN;
    } else if (_mode == MIDI_IN_MIDI_OUT) {
      _serial_out = &_serial_midi; 
      _led_pattern = MI_MO_PATTERN;
    }


    //
    // tell user

    #ifdef PRINT_DEBUG
      _serial_ble.print("Channel: ");
      _serial_ble.println(_channel, BIN);
  
      _serial_ble.print("Mode: ");
      _serial_ble.println(_mode);
      
      _serial_ble.println("Ready");
      _serial_ble.println();
    #endif

    
    //
    // prepare output states

    C_LOW;
    L_LOW;


    //
    // notify we're ready

    #ifdef BUZ_DEBUG
      tone(BUZZER, 1000, 1000);
      delay(1000);
    #endif
  }
  
  void loop() {

    //
    // led manipulation

    if (millis() >= _led_timing) {
      if (ON(_led_pattern, _led_index)) LED_HIGH;
      else LED_LOW;
      _led_timing = millis() + LED_BLINK_LENGTH;
      if (++_led_index > 7) {
        _led_index = 0;
        _led_timing += LED_BLINK_LENGTH * 8; // some extra delay inbetween cycles
      }
    }
 
 
    //
    // check for incoming messages & parse them

    while (_serial_midi.available())
      parse_incoming(_serial_midi.read());


    //
    // insert '1' in shift out

    D_HIGH;
    C_HIGH;
    C_LOW;
    D_LOW;
    L_HIGH;
    L_LOW;


    //
    // loop 21 times, for the shift outputs to keybuttons
    
    for (byte i = 0; i < 22; i++) {

        byte c = 0;

        if (i == 16) goto skip; // cuz of wiring mistake

        //
        // check inputs of front row.
        // if one went from off to on, send midi message.
        // if one went from on to off, store timestamp for use to calc noteOff velocity
        
        while (c < 3) {
           bool state = ON(PINB, c);
           
           if (state != _btn_states[i][c]) {
              _btn_states[i][c] = state;

              if (!state) {
                _btn_timestamps[i][c] = micros();
                
                #ifdef BUZ_KEY_EVENTS
                  tone(BUZZER, 2000, 50);
                #endif
                
              } else {
                byte other_c = c+3;
                unsigned long t = _btn_timestamps[i][other_c];
                if (t > 0) {
                  t = micros() - t;
                  
                  #ifdef PRINT_ON_TIMES
                    _serial_ble.println(t);
                  #endif
                  
                  if (_btn_types[i][c] == White) {
                    if (t < ON_SLOW_MIN) t = ON_SLOW_MIN;
                    t -= ON_SLOW_MIN;
                    t /= ((ON_SLOW_MAX-ON_SLOW_MIN)/0x7F);
                    if (t > 0x7E) t = 0x7E;
                    t = 0x7F - t;
                  } else {
                    if (t < ON_FAST_MIN) t = ON_FAST_MIN;
                    t -= ON_FAST_MIN;
                    t /= ((ON_FAST_MAX-ON_FAST_MIN)/0x7F);
                    if (t > 0x7E) t = 0x7E;
                    t = 0x7F - t;
                  }

                  byte buf[3];
                  buf[0] = NoteOn << 4;
                  buf[0] |= _channel;
                  buf[1] = _btn_notes[i][c] + _octave;
                  buf[2] = t & 0x7F;
                  _serial_out->write(buf, 3);
                  _btn_timestamps[i][other_c] = 0; // reset

                  #ifdef BUZ_KEY_EVENTS
                   tone(BUZZER, 1000, 50);
                  #endif
                }
              }
           }
           c++;
        }

                
        //
        // check inputs of the back row of btns.
        // if state went from low to high: store timestamp (for NoteOn msg).
        // if state went form high to low: if there is a timestamp
        // from the corresponding front row btn, send noteOff msg.

        while (c < 6) {
          bool state = ON(PINB, c);
          
          if (state != _btn_states[i][c]) {
            _btn_states[i][c] = state;

            if (state) {
              _btn_timestamps[i][c] = micros();
              
              #ifdef BUZ_KEY_EVENTS
                tone(BUZZER, 600, 50);
              #endif
              
            } else {
              byte other_c = c-3;
              unsigned long t = _btn_timestamps[i][other_c];
                if (t > 0) {
                  t = micros() - t;
                  
                  #ifdef PRINT_OFF_TIMES
                    _serial_ble.println(t);
                  #endif

                  if (_btn_types[i][c] == White) {
                    if (t < OFF_SLOW_MIN) t = OFF_SLOW_MIN; //TODO: HIER VERDER
                    t -= OFF_SLOW_MIN;
                    t /= ((OFF_SLOW_MAX-OFF_SLOW_MIN)/0x7F);
                    if (t > 0x7E) t = 0x7E;
                    t = 0x7F - t;
                  } else {
                    if (t < OFF_FAST_MIN) t = OFF_FAST_MIN;
                    t -= OFF_FAST_MIN;
                    t /= ((OFF_FAST_MAX-OFF_FAST_MIN)/0x7F);
                    if (t > 0x7E) t = 0x7E;
                    t = 0x7F - t;
                  }                  
                
                  byte buf[3];
                  buf[0] = NoteOff << 4;
                  buf[0] |= _channel;
                  buf[1] = _btn_notes[i][c] + _octave;
                  buf[2] = t;
                  _serial_out->write(buf, 3);
                  _btn_timestamps[i][other_c] = 0; // reset
  
                  #ifdef BUZ_KEY_EVENTS
                    tone(BUZZER, 200, 50);
                  #endif
              }
            }
          }
          c++;
        }


        skip:
        

        //
        // advance shift out

        C_HIGH;
        C_LOW;
        L_HIGH;
        L_LOW;
        
    }
    

    //
    // check mode button (shift is already in correct position
    // if it's been high for more that DEBOUNCE_MIN, change mode.

    byte mode_reading = ON(PINB, 5);
    if (mode_reading != _last_mode_btn_state) _mode_btn_debounce = millis();
    else if (mode_reading != _mode_btn_state && ((millis() - _mode_btn_debounce) > BTN_DEBOUNCE_MIN)) {
      _mode_btn_state = mode_reading;
      if (_mode_btn_state) {
        if (++_mode > 2) _mode = 0;
        EEPROM.write(0, _mode);
        if (_mode == MIDI_IN_BLE_OUT) {
          _serial_out = &_serial_ble; 
          _led_pattern = MI_BO_PATTERN;
        } else if (_mode == MIDI_IN_USB_OUT) {
          _serial_out = &Serial; 
          _led_pattern = MI_UO_PATTERN;
        } else if (_mode == MIDI_IN_MIDI_OUT) {
          _serial_out = &_serial_midi; 
          _led_pattern = MI_MO_PATTERN;
        }

        #ifdef PRINT_DEBUG
          _serial_ble.print("mode: ");
          _serial_ble.println(_mode);
        #endif

        #ifdef BUZ_KEY_EVENTS
          tone(BUZZER, 100, 100);  
        #endif
      }
    }

    _last_mode_btn_state = mode_reading;


    //
    // check ocatve button
    // if it's been high for more that DEBOUNCE_MIN, change octave offset.
    
    byte oct_reading = ON(PINB, 4);
    if (oct_reading != _last_oct_btn_state) _oct_btn_debounce = millis();
    else if (oct_reading != _oct_btn_state && ((millis() - _oct_btn_debounce) > BTN_DEBOUNCE_MIN)) {
      _oct_btn_state = oct_reading;
      if (_oct_btn_state) { 
        _octave += 12;
        if (_octave > 72) _octave = 0;
        EEPROM.write(1, _octave);

        #ifdef PRINT_DEBUG
          _serial_ble.print("oct: ");
          _serial_ble.println(_octave);
        #endif

        #ifdef BUZ_KEY_EVENTS
          tone(BUZZER, 100, 500); 
        #endif
      }
    }

    _last_oct_btn_state = oct_reading;
  }

  inline void parse_incoming(byte b) {

     #ifdef PRINT_DEBUG
      _serial_ble.print("In: ");
      _serial_ble.println(b, HEX);
    #endif

    //
    // this implementation covers pretty much all scenarios.
    // we know the expected length of all MIDI messages
    // except .SystemExclusive. In that case, the last byte
    // will be .EndOfExclusive
    
    static byte expected_length = 0;
    static byte received_length = 0;
    static byte data[64];


    //
    // if first, determine expected length
 
    if (received_length == 0) {
      switch (b >> 4) {
        case ProgramChange:
        case ChannelPressureAftertouch:
          expected_length = 2;
          break;
        case NoteOff:
        case NoteOn:
        case PolyphonicAftertouch:
        case ControlChange:
        case PitchWheelChange:
          expected_length = 3;
          break;
        case System:
          switch (b) {
              case SystemExclusive:
                expected_length = 64; // 64 prevents buffer overflow
                break;
              case Undefined1:
              case Undefined2:
              case TuneRequest:
              case TimingClock:
              case Undefined3:
              case Start:
              case Continue:
              case Stop:
              case Undefined4:
              case ActiveSensing:
              case Reset:
                expected_length = 1;
                break;
              case TimeCode:
              case SongSelect:
                expected_length = 2;
                break;
              case SongPosition:
                expected_length = 3;
                break;
          }
          break;
        default:
          return; // is not a status byte
          break;
      }
    }


    //
    // add to buffer if needed

    if (received_length < expected_length) {
      data[received_length++] = b;
    } 
    

    //
    // if sys excl: detect end
    
    if ((expected_length == 64) && (b == EndOfExclusive)) {
        expected_length = received_length;
    } 
    
    
    //
    // send when message is complete

    if (received_length == expected_length) {
      _serial_out->write(data, received_length);
      expected_length = 0;
      received_length = 0;
    }
  }
