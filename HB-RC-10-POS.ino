//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2016-10-31 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// 2021-02-27 jp112sdl Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//- -----------------------------------------------------------------------------------------------------------------------
// ci-test=yes board=328p aes=no

#define SENSOR_ONLY

#define EI_NOTEXTERNAL
#include <EnableInterrupt.h>

#include <AskSinPP.h>
#include <LowPower.h>
#include <Register.h>
#include <MultiChannelDevice.h>

#define CONFIG_BUTTON_PIN    8
#define SEND_BUTTON_PIN      9
#define LED_PIN_1            4
#define LED_PIN_2            5
#define ANALOG_PIN           14
#define ACTIVATION_PIN       6

#define PEERS_PER_CHANNEL    8

using namespace as;

const struct DeviceInfo PROGMEM devinfo = {
  {0xf3, 0x2c, 0x01},       // Device ID
  "JPRC10POS1",             // Device Serial
  {0xf3, 0x2c},             // Device Model
  0x10,                     // Firmware Version
  as::DeviceType::Remote,   // Device Type
  {0x00, 0x00}              // Info Bytes
};

typedef AvrSPI<10,11,12,13> SPIType;
typedef Radio<SPIType, 2> RadioType;
typedef DualStatusLed<LED_PIN_1, LED_PIN_2> LedType;
typedef AskSin<LedType, IrqInternalBatt, RadioType> Hal;
Hal hal;

DEFREGISTER(RemoteReg1,CREG_AES_ACTIVE)
class RemoteList1 : public RegList1<RemoteReg1> {
public:
  RemoteList1 (uint16_t addr) : RegList1<RemoteReg1>(addr) {}
  void defaults () {
    clear();
  }
};

class RemoteChannel : public Channel<Hal,RemoteList1,EmptyList,DefList4,PEERS_PER_CHANNEL,List0> {
private:
  uint8_t       repeatcnt;
public:

  typedef Channel<Hal,RemoteList1,EmptyList,DefList4,PEERS_PER_CHANNEL,List0> BaseChannel;

  RemoteChannel () : BaseChannel(), repeatcnt(0) {}
  virtual ~RemoteChannel () {}

  void sendShortPress() {
    DHEX(BaseChannel::number());
    RemoteEventMsg& msg = (RemoteEventMsg&)this->device().message();
    msg.init(this->device().nextcount(),this->number(),repeatcnt,false,this->device().battery().low());
    this->device().sendPeerEvent(msg,*this);
    repeatcnt++;
  }

  uint8_t status () const { return 0; }
  uint8_t flags () const {  return 0; }
};

typedef MultiChannelDevice<Hal,RemoteChannel,10> RemoteType;
RemoteType sdev(devinfo,0x20);

class RC10Btn : public Button {
public:
  typedef Button ButtonType;

  RC10Btn (uint8_t longpresstime=3) { this->setLongPressTime(seconds2ticks(longpresstime)); }
  virtual ~RC10Btn () {}
  virtual void state (uint8_t s) {
    uint8_t old = ButtonType::state();
    ButtonType::state(s);
    if( s == ButtonType::released ) {
      sdev.battery().setIdle();

      digitalWrite(ACTIVATION_PIN, HIGH);
      _delay_ms(10);
      uint16_t aVal = analogRead(ANALOG_PIN);
      digitalWrite(ACTIVATION_PIN, LOW);

      uint8_t chNum = (aVal > 999) ? 10 : (aVal / 100) + 1;
      DPRINT("aVal =");DDEC(aVal);DPRINT(" chNum =");DDECLN(chNum);
      sdev.channel(chNum).sendShortPress();

      sdev.battery().unsetIdle();
    }
    else if( s == ButtonType::longpressed ) {
      if( old == ButtonType::longpressed ) {
        if( sdev.getList0().localResetDisable() == false ) {
          sdev.reset(); // long pressed again - reset
        }
      }
      else {
        sdev.led().set(LedStates::key_long);
      }
    }
    else if ( s == ButtonType::longreleased ) {
      sdev.startPairing();
    }
  }
} cfgBtn;


void setup () {
  DINIT(57600,ASKSIN_PLUS_PLUS_IDENTIFIER);
  pinMode(ACTIVATION_PIN, OUTPUT);

  sdev.init(hal);

  hal.battery.init();
  hal.battery.low(22);
  hal.battery.critical(20);

  buttonISR(cfgBtn,CONFIG_BUTTON_PIN);

  sdev.initDone();

  while (hal.battery.current() == 0);
}

void loop() {
  bool worked = hal.runready();
  bool poll = sdev.pollRadio();
  if( worked == false && poll == false ) {
    if( hal.battery.critical() ) {
      DPRINTLN("SLEEP FOREVER");
      hal.activity.sleepForever(hal);
    }
    hal.activity.savePower<Sleep<>>(hal);
  }
}
