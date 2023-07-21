#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <uri/UriBraces.h>
#include "CytronMotorDriver.h"
#include "main.h"

#define DEBUG


#if __has_include("secrets.h")
#include "secrets.h"
#endif

/* WiFi configuration */
#ifndef STASSID
#define STASSID "SSID of the WLAN router" // PUT YOUR "WIFI NAME" HERE
#define STAPSK  "passphrase" // PUT YOUR WIFI PASSWORD HERE
#define OPT_HOSTNAME  "table" // Optional hostname
#endif
const char *SSID = STASSID;
const char *PASSWORD = STAPSK;
const char *HOSTNAME = OPT_HOSTNAME;

/* Pin configuration */
#define TABLE_END_STOP D5 // pin for physical end-stop table switch
#define OPTO_SENSOR D6 // pin for opto sensor
#define MOTOR_DRIVER_PWM D4 // pwm pin for the motor driver board
#define MOTOR_DRIVER_DIR D3 // direction pin for the motor driver board
#define MOTOR_SPEED 255 // speed of the motor from 0-250

/* Configure ESP8266 web server */
ESP8266WebServer server(80); // use port 80

/* Configure the motor driver */
CytronMD motor(PWM_DIR, MOTOR_DRIVER_PWM, MOTOR_DRIVER_DIR);

/* States of the system */
typedef enum {
  CALIBRATING, // table is lowering to reach table END_STOP
  UP, // table is supposed to go up
  DOWN, // table is supposed to go down
  HOLD, // table is supposed to do nothing -> hold still
} state_t;

/* Global state of the system. In CALIBRATING initially. Then in HOLD state until command is received */
state_t g_system_state = CALIBRATING;

int g_opto_state = -1;
unsigned long g_last_on_hold_time = 0;
unsigned long g_last_opto_change_time = 0;

void motor_stuck_protection();

int get_current_height();

void stop_table();

void stop_motor_reset_position();

bool opto_sensor_not_changing_when_motor_on();

/* Maximum and minimum height of the table in cm */
const unsigned int MAX_HEIGHT = 120;/* 120 cm is the offical maximum height from the IKEA manual */
const unsigned int MIN_HEIGHT = 70; /* 70 cm is the offical minimum height from the IKEA manual */

/* Height tolerance (in cm) which is needed because the ultrasonic sensor is not really accurate */
const unsigned int HEIGHT_TOLERANCE = 2; /* This used to be 5 cm but was reduced to 2 because of how slow the table motor is */

const float ROTATION_TO_HEIGHT_RATIO = 50 / 23.0;

/* Global variable containing current position */
int g_opto_position;

/* Global variable which shall hold the wanted custom height when requested */
int g_custom_height = -1;

int g_endstop_value_trig_times = 0;

/*
 * Displays the index/main page in the browser of the client
 */
void display_index() {
  String s = MAIN_page; // read HTML contents from the MAIN_page variable which was stored in the flash (program) memory instead of SRAM, see main.h
  server.send(200, "text/html", s); // send web page
}

/*
 * The server sends a redirection response to the client so it will go back to the homepage after requesting a state change,
 * e.g. when motor up was clicked it shall go back to the homepage
 */
void send_homepage_redirection() {
  server.sendHeader("Location", "/"); // Client shall redirect to "/"
  server.send(303);
}

/*
 * Handles calls to the URI /motor/<string: action>/ and does state transitions:
 * if for example /motor/up is called, then the system will transition from the previous state
 * to the state UP.
 */
void handle_motor_requests() {
  String action = server.pathArg(0); // retrieve the given argument/action

  if (action == "up") {
    g_system_state = UP;
  } else if (action == "stop") {
    g_system_state = HOLD;
  } else if (action == "down") {
    g_system_state = DOWN;
  } else {
    Serial.println("Error: Action is unknown"); // system will stay in its previous state
  }

  g_custom_height = -1;

  // send response
  send_homepage_redirection();
}

/*
 * Handles calls to the URI /height/<string: height_in_cm>/
 * If a height is given, then the system shall transition into the CUSTOM_HEIGHT state.
 */
void handle_height_requests() {
  int height = atoi((server.pathArg(0)).c_str()); // convert string parameter to integer

  // only change the state if the given height is in the height boundaries
  if (height >= MIN_HEIGHT and height <= MAX_HEIGHT) {
    int current_height = get_current_height();
    if (current_height < height) {
      g_custom_height = height;
      g_system_state = UP;
    } else if (current_height > height) {
      g_custom_height = height;
      g_system_state = DOWN;
    }
  }

  // send response
  send_homepage_redirection();
}

/*
 * Handles calls to the URI /height/
 * Responds with the current height of the ultrasonic sensor
 */
void handle_read_height_requests() {
  int height = get_current_height();
  server.send(200, "text/plain", String(height));
}

/**
 * Setup the output pins
 */
void setup_pins() {
  // Pin setup for motor controller
  pinMode(MOTOR_DRIVER_PWM, OUTPUT);
  pinMode(MOTOR_DRIVER_DIR, OUTPUT);
  pinMode(TABLE_END_STOP, INPUT);
  pinMode(OPTO_SENSOR, INPUT);
}

/**
 * Takes care of the wifi configuration
 */
void setup_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(OPT_HOSTNAME); // set HOSTNAME
  WiFi.begin(SSID, PASSWORD);

  // Wait for wifi connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // Start the mDNS responder for esp8266.local
  if (MDNS.begin("esp8266")) {
    Serial.println("MDNS responder started");
  }
}

/**
 * Print information about the wifi connection:
 * SSID, IP, HOSTNAME
 */
void print_connection_info() {
  // Print connection info
  Serial.print("Connected to ");
  Serial.println(SSID);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("HOSTNAME: ");
  Serial.println(WiFi.hostname().c_str());
}

/**
 * Register the routes of the server
 */
void register_server_routes() {
  server.on(F("/"), display_index); // route: /
  server.on(UriBraces("/motor/{}"), handle_motor_requests); // route: /motor/<string: action>/
  server.on(UriBraces("/height/{}"), handle_height_requests); // route: /height/<string: height_in_cm>/
  server.on(UriBraces("/height"),
            handle_read_height_requests); // route: /height/ - is being called from client javascript
}

void init_timestamps() {
  unsigned long now = millis();
  g_last_on_hold_time = now;
  g_last_opto_change_time = now;
}

/*
 * Login to the network, setup the server and register URI call handlers.
 */
void setup(void) {
  motor.setSpeed(0);

  Serial.begin(115200);

  setup_wifi();

#ifdef DEBUG
  print_connection_info();
#endif
  register_server_routes();

  // Start the server
  server.begin();
  Serial.println("HTTP server started");

  init_timestamps();
}

/*
 * Retrieves the current height of the table by getting the distance of the
 * ultrasonic sensor.
 */
int get_current_height() {
  return (int) g_opto_position / 4.0 / ROTATION_TO_HEIGHT_RATIO + MIN_HEIGHT; // return height
}

/*
 * Raise the table until the max height is reached
 */
void raise_table() {
  if (get_current_height() < MAX_HEIGHT) {
    motor.setSpeed(MOTOR_SPEED);
  } else {
    stop_table();
  }
}

/*
 * Lower the table until the min height is reached
 */
void lower_table() {
  if (get_current_height() >= MIN_HEIGHT) {
    motor.setSpeed(-MOTOR_SPEED); // two's complement for negating the integer
  } else {
    stop_table();
  }
}

/*
 * Stop the table at the current height
 */
void stop_table() {
  motor.setSpeed(0);
  g_custom_height = -1;
  g_system_state = HOLD;
}

bool custom_height_reached() {
  return g_custom_height != -1 && g_custom_height == get_current_height();
}

/*
 * Controls the motor based on the system state g_system_state. This is pretty much the core FSM implementation for the state transistions.
 */
void handle_output() {
  if (custom_height_reached()) {
    Serial.println("Custom height reached ... switching to HOLD state");
    g_system_state = HOLD;
  }

  switch (g_system_state) {
    case UP:
      raise_table();
      break;
    case DOWN:
      lower_table(); // two's complement for negating the integer
      break;
    case HOLD:
      stop_table();
      break;
  }
}


void handle_endstop() {
  int endstop_value = digitalRead(TABLE_END_STOP);

  if (endstop_value == HIGH) {
    g_endstop_value_trig_times += 1;
  } else {
    g_endstop_value_trig_times = 0;
  }

  if (g_system_state == CALIBRATING) {
    if (endstop_value != HIGH) {
      motor.setSpeed(-MOTOR_SPEED);
    }
  }

  if ((endstop_value == HIGH && g_endstop_value_trig_times > 10) &&
      (g_system_state == DOWN || g_system_state == CALIBRATING)) {
    stop_motor_reset_position();
  }
}

void stop_motor_reset_position() {
  Serial.println("Endstop reached ... stopping motor, resetting position");
  stop_table();
  g_opto_position = 0;
}

void track_position() {
  int cur_opto_state = digitalRead(OPTO_SENSOR);

  unsigned long now = millis();

  if (g_opto_state != cur_opto_state) {
    Serial.println("Current position :" + String(g_opto_position) +
                   " state: " + String(cur_opto_state) + " / " + String(g_opto_state));

    g_opto_state = cur_opto_state;
    g_last_opto_change_time = now;

    if (g_system_state == UP) {
      g_opto_position += 1;
    } else {
      g_opto_position -= 1;
    }
  }
}

/*
 * Main loop. Gets the current height, retrieves inputs and do state
 * transitions and finally control the motor based on the state.
 */
void loop(void) {
  // request motor to go down until end-stop is triggered
  handle_endstop();

  if (g_system_state != CALIBRATING) {
    server.handleClient(); // gets input from a client
    handle_output(); // controls the height of the table based on the input
  }

  track_position();
  motor_stuck_protection();
}

void motor_stuck_protection() {
  if (g_system_state == HOLD) {
    g_last_on_hold_time = millis();
  } else if (opto_sensor_not_changing_when_motor_on()) {
    Serial.println("Motor stuck condition detected ... switching to HOLD state.");
    g_system_state = HOLD;
  }
}

bool opto_sensor_not_changing_when_motor_on() {
  unsigned long now = millis();

  return now - g_last_opto_change_time > 500 &&
         now - g_last_on_hold_time > 500;
}
