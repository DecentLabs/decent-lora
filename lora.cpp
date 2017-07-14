#include <bcm2835.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include <RH_RF69.h>
#include <RH_RF95.h>

#define DEBUG false

#define BOARD_DRAGINO_PIHAT
#include "../RasPiBoards.h"

#define MAX_NODES 2

// Our RFM95 Configuration
#define RF_FREQUENCY  868.00
#define RF_NODE_ID    0

// Create an instance of a driver
RH_RF95 rf95(RF_CS_PIN, RF_IRQ_PIN);

//Flag for Ctrl-C
volatile sig_atomic_t force_exit = false;

void sig_handler(int sig)
{
  printf("\n%s Break received, exiting!\n", __BASEFILE__);
  force_exit=true;
}

void recv() {
#ifdef RF_IRQ_PIN
      // We have a IRQ pin ,pool it instead reading
      // Modules IRQ registers from SPI in each loop

      // Rising edge fired ?
      if (bcm2835_gpio_eds(RF_IRQ_PIN)) {
        // Now clear the eds flag by setting it to 1
        bcm2835_gpio_set_eds(RF_IRQ_PIN);
        //printf("Packet Received, Rising event detect for pin GPIO%d\n", RF_IRQ_PIN);
#endif

        if (rf95.available()) {
          // Should be a message for us now
          uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
          uint8_t len  = sizeof(buf);
          uint8_t from = rf95.headerFrom();
          uint8_t to   = rf95.headerTo();
          uint8_t id   = rf95.headerId();
          uint8_t flags= rf95.headerFlags();;
          int8_t rssi  = rf95.lastRssi();

          if (rf95.recv(buf, &len)) {
            printf("<== %d\t%d\t%ddB\t", from, to, rssi);
            printbuffer(buf, len);
          } else {
            Serial.print("receive failed");
          }
          printf("\n");
        }

#ifdef RF_IRQ_PIN
      }
#endif

}

void send(uint8_t *data, uint8_t len) {
  printf("==> ");
  printbuffer(data, len);
  printf("\n");
  if (rf95.send(data, len)) {
    printf("QUEUED\n");
  } else {
    printf("ERR\n");
  }
  if (rf95.waitPacketSent()) {
    printf("SENT\n");
  } else {
    printf("ERR\n");
  }
}

//Main Function
int main (int argc, const char* argv[] )
{

  signal(SIGINT, sig_handler);
  if (DEBUG) printf( "%s\n", __BASEFILE__);

  if (!bcm2835_init()) {
    if (DEBUG) fprintf( stderr, "%s bcm2835_init() Failed\n\n", __BASEFILE__ );
    return 1;
  }

  if (DEBUG) printf( "RF95 CS=GPIO%d", RF_CS_PIN);

#ifdef RF_IRQ_PIN
  if (DEBUG) printf( ", IRQ=GPIO%d", RF_IRQ_PIN );
  // IRQ Pin input/pull down
  pinMode(RF_IRQ_PIN, INPUT);
  bcm2835_gpio_set_pud(RF_IRQ_PIN, BCM2835_GPIO_PUD_DOWN);
  // Now we can enable Rising edge detection
  bcm2835_gpio_ren(RF_IRQ_PIN);
#endif

#ifdef RF_RST_PIN
  if (DEBUG) printf( ", RST=GPIO%d", RF_RST_PIN );
  // Pulse a reset on module
  pinMode(RF_RST_PIN, OUTPUT);
  digitalWrite(RF_RST_PIN, LOW );
  bcm2835_delay(150);
  digitalWrite(RF_RST_PIN, HIGH );
  bcm2835_delay(100);
#endif

  if (!rf95.init()) {
    if (DEBUG) fprintf( stderr, "\nRF95 module init failed, Please verify wiring/module\n" );
  } else {
    // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on

    // RF95 Modules don't have RFO pin connected, so just use PA_BOOST
    // check your country max power useable, in EU it's +14dB
    rf95.setTxPower(14, false);

    // You can optionally require this module to wait until Channel Activity
    // Detection shows no activity on the channel before transmitting by setting
    // the CAD timeout to non-zero:
    //rf95.setCADTimeout(10000);

    // Adjust Frequency
    rf95.setFrequency(RF_FREQUENCY);

    // If we need to send something
    rf95.setThisAddress(RF_NODE_ID);
    rf95.setHeaderFrom(RF_NODE_ID);

    // Be sure to grab all node packet
    // we're sniffing to display, it's a demo
    rf95.setPromiscuous(true);

    if (DEBUG) printf( " OK NodeID=%d @ %3.2fMHz\n", RF_NODE_ID, RF_FREQUENCY );

    // non-blocking reads on stdin
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

    //Begin the main body of code
    bool sent = false;
    while (!force_exit) {

      int cycle = (millis() / 1000) % MAX_NODES;
      if (cycle == RF_NODE_ID) {
        if (!sent) {
          rf95.setModeTx();
          char line[RH_RF95_MAX_MESSAGE_LEN];
          int len = read(0, line, RH_RF95_MAX_MESSAGE_LEN);
          if (len > 0) {
            printf("LEN %d", len);
            //uint8_t data[] = "Hello world!";
            uint8_t data[RH_RF95_MAX_MESSAGE_LEN] = {0};
            memcpy(data, line, len);
            send(data, len + 1);
            sent = true;
          }
          rf95.setModeRx();
        }
      } else {
        sent = false;
        recv();
      }

      // 
      bcm2835_delay(5);
    }
  }

  if (DEBUG) printf( "\n%s Ending\n", __BASEFILE__ );
  bcm2835_close();
  return 0;
}


