/*===============Laser Keyence===================*/

#include <RF24.h>
#include <SPI.h>
#include <Sync.h>
#include <EEPROM.h>
#include <Wire.h>
#include "SdFat.h"
#include <ClickEncoder.h>
#include <TimerOne.h>
#include <LiquidCrystal_I2C.h>
#include <printf.h>

/*================================================================================================================================
  =================================================================================================================================*/
#define laser1 A5
#define laser2 A6
#define laser3 A7
#define pinShift 9
LiquidCrystal_I2C lcd(0x27, 16, 2);


RF24 myRadio(7, 8);                    // nRF24L01(+) radio attached using Getting Started board
const uint16_t this_node  = 01;       // Address of our node in Octal format
const uint16_t other_node = 00;      // Address of the other node in Octal format
unsigned long interval = 2000;       // interval refresh LCD
int saverate = 0;
int saverate_addr = 1;
unsigned long last_sent;             // When did we last send?
unsigned long packets_sent;          // How many have we sent already
byte addresses[][6] = {"00", "02"};


const uint8_t SD_CS_PIN = 6;
const uint8_t SOFT_MISO_PIN = 3;
const uint8_t SOFT_MOSI_PIN = 4;
const uint8_t SOFT_SCK_PIN  = 5;
SoftSpiDriver<SOFT_MISO_PIN, SOFT_MOSI_PIN, SOFT_SCK_PIN> softSpi;
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(0), &softSpi)
SdFat sd;
File file;
File root;
const int max_file = 21;
int file_counter = 0;
char temp_name[20];
String files[max_file];
//int file_selected = 0;
//String file_opt[2] = {"Hapus File? Btl", "Hapus File? Ya"};
//int file_opt_selected = 0;
//void select_file(bool selected_only = false);
void list_files();

ClickEncoder *encoder;
int16_t last, value;
bool up = false;
bool down = false;
bool klik = false;

const int max_menu = 5;
String menu[max_menu] = {"Start Akuisisi", "File", "Transmiter", "Interval", "RESET"};
int current_menu = 0;
int current_page = 0;

String transmiter[2] = {"NRF", "RS845"};
int current_transmiter = 0;
int transmiter_addr = 0;


struct paket {
  float sensor1 = 0;
  float sensor2 = 0;
  float sensor3 = 0;
};
paket kalibrasi;
//paket zero_shift;

const int RS485_CONTROL = 2;

/*===============================================================================================================================
  ===============================================================================================================================*/

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
  Timer1.initialize(5000);
  Timer1.attachInterrupt(timerIsr);
  encoder = new ClickEncoder(A3, A2, A4);
  last = encoder->getValue();
  lcd.begin();
  lcd.clear();
  SPI.begin();
  myRadio.begin();
  init_radio();
  current_transmiter = EEPROM.read(transmiter_addr);
  saverate = EEPROM.read(saverate_addr);
  if (saverate > 45 ) {
    EEPROM.write(saverate_addr, 5);
  }
  saverate = EEPROM.read(saverate_addr);
  interval = 500 + (saverate * 100);
  Serial.println("interval :" + interval);
  if (current_transmiter > 1) {
    EEPROM.write(transmiter_addr, 0);
  }
  current_transmiter = EEPROM.read(transmiter_addr);
  Serial.println("Transmiter :" + transmiter[current_transmiter]);

  int retrying_init_sdcard = 10;
  while (retrying_init_sdcard > 0) {
    if (!sd.begin(SD_CONFIG)) {
      Serial.println("SDCard Config Error");
      lcd.clear();
      lcd.print("SDCard error");
      file.close();
      root.close();
      retrying_init_sdcard--;
    } else {
      retrying_init_sdcard = 0;
      reset_root();
    }
  }
  delay(1000);
  pinMode(RS485_CONTROL, OUTPUT);
  digitalWrite(RS485_CONTROL, LOW); //set low to receive command
  printf_begin();
  zero_shifting();
}

void loop() {
  if (current_page == 0) {
    chg_menu();
  }
  else if (current_page == 1) {
    akuisisi();
  }
  else if (current_page == 2) {
    pageFiles();
  }
  else if (current_page == 3) {
    chg_transmiter();
  }
  else if (current_page == 4) {
    chg_interval();
  }
  else if (current_page == 5) {
    resetAll();
  }
}

void chg_menu() {
  showMenu();
  String command;
  while (!klik) {

    if (current_menu < (max_menu - 1) && up) {
      up = false;
      current_menu++;
      showMenu();
    }
    else if (current_menu > 0 && down) {
      down = false;
      current_menu--;
      showMenu();
    }

    check_communication_command(command);
    if (command.equals("START_REC")) {
      current_menu = 0;
      break;
    }
    getInput();
  }
  klik = false;
  current_page = current_menu + 1;
}

void chg_transmiter() {
  if (current_transmiter == 0) {
    lcd.clear();
    lcd.print(">" + transmiter[0]);
    lcd.setCursor(1, 1);
    lcd.print(transmiter[1]);
  }
  else {
    lcd.clear();
    lcd.setCursor(1, 0);
    lcd.print(transmiter[0]);
    lcd.setCursor(0, 1);
    lcd.print(">" + transmiter[1]);
  }
  while (!klik) {
    if (up) {
      up = false;
      current_transmiter = 1;
      lcd.clear();
      lcd.setCursor(1, 0);
      lcd.print(transmiter[0]);
      lcd.setCursor(0, 1);
      lcd.print(">" + transmiter[1]);
      EEPROM.update(transmiter_addr, current_transmiter);
    }
    else if (down) {
      down = false;
      current_transmiter = 0;
      lcd.clear();
      lcd.print(">" + transmiter[0]);
      lcd.setCursor(1, 1);
      lcd.print(transmiter[1]);
      EEPROM.update(transmiter_addr, current_transmiter);
    }
    getInput();
  }
  klik = false;
  current_page = 0;
  showMenu();
}
void chg_interval() {
  lcd.clear();
  lcd.print(interval);
  lcd.setCursor(5, 0);
  lcd.print("mS");
  while (!klik) {
    if (saverate < 45 && up) {
      up = false;
      saverate++;
      interval = 500 + (saverate * 100);
      EEPROM.update(saverate_addr, saverate);
      lcd.clear();
      lcd.print(interval);
      lcd.setCursor(5, 0);
      lcd.print("mS");
    }
    else if (saverate > 0 && down) {
      down = false;
      saverate--;
      interval = 500 + (saverate * 100);
      EEPROM.update(saverate_addr, saverate);
      lcd.clear();
      lcd.print(interval);
      lcd.setCursor(5, 0);
      lcd.print("mS");
    }
    getInput();
  }
  klik = false;
  current_page = 0;
  showMenu();
}

void showMenu() {
  lcd.clear();
  if (current_menu == 0) {
    lcd.print(">" + menu[current_menu]);
    lcd.setCursor(1, 1);
    lcd.print(menu[current_menu + 1]);
  } else {
    lcd.setCursor(1, 0);
    lcd.print(menu[current_menu - 1]);
    lcd.setCursor(0, 1);
    lcd.print(">" + menu[current_menu]);
  }
}
void getInput() {
  ClickEncoder::Button b = encoder->getButton();
  value += encoder->getValue();
  //Serial.println(value);
  if (b != ClickEncoder::Open) {
    switch (b) {
      case ClickEncoder::Clicked:
        klik = true;
        Serial.println("klik");
        break;
    }
  } else if (value / 3 > last) {
    last = value / 3;
    up = true;
    Serial.println("up");

  } else if (value / 3 < last) {
    last = value / 3;
    down = true;
    Serial.println("down");
  }
}

void akuisisi() {
  lcd.clear();
  zero_shifting();
  Make_File();
  init_radio();
  //myRadio.printPrettyDetails();
  String command;
  paket kirim;
  while (!klik) {
    for (int i = 0; i < 10; i++) {
      kirim.sensor1 += analogRead(laser1);
      kirim.sensor2 += analogRead(laser2);
      kirim.sensor3 += analogRead(laser3);
      delay(5);
    }
    kirim.sensor1 = (((int)kirim.sensor1 / 10) - kalibrasi.sensor1) / (1023 - kalibrasi.sensor1) * 400;
    kirim.sensor2 = (((int)kirim.sensor2 / 10) - kalibrasi.sensor2) / (1023 - kalibrasi.sensor2) * 400;
    kirim.sensor3 = (((int)kirim.sensor3 / 10) - kalibrasi.sensor3) / (1023 - kalibrasi.sensor3) * 400;
    

    check_communication_command(command);
    //Serial.println("command:" + command);
    if (current_transmiter == 0) {
      myRadio.stopListening();
      myRadio.openWritingPipe(addresses[0]);
      delay(100);
      if (command.indexOf("V") >= 0) {
        //myRadio.stopListening();
        //myRadio.openWritingPipe(addresses[0]);
        if (myRadio.write(&kirim, sizeof(kirim))) {
          Serial.println("ok.");
          lcd.setCursor(0, 0);
          lcd.print("Status:");
          lcd.setCursor(8, 0);
          lcd.print("Terkirim");
        } else {
          Serial.println("gagal");
          lcd.setCursor(0, 0);
          lcd.print("Status:");
          lcd.setCursor(8, 0);
          lcd.print("_Gagal__");
        }
        Serial.print("Sending tru radio ");

        //          bool ok = ;
        //        delay(10);
        int retry = 0;
        do {

          if (myRadio.write(&kirim, sizeof(kirim))) {
            Serial.println("ok.");
            lcd.setCursor(0, 0);
            lcd.print("Status:");
            lcd.setCursor(8, 0);
            lcd.print("Terkirim");
            break;
          } else {
            Serial.println("gagal");
            lcd.setCursor(0, 0);
            lcd.print("Status:");
            lcd.setCursor(8, 0);
            lcd.print("_Gagal__");
            retry++;
          }
        } while (retry < 10);
      }
    }
    if (current_transmiter == 1) {
      if (command.equals("V")) {
        Serial.println("Sending");
        digitalWrite(RS485_CONTROL, HIGH);
        Serial1.print("V1|");
        Serial1.print(kirim.sensor1, 4);
        Serial1.print("#V2|");
        Serial1.print(kirim.sensor2, 4);
        Serial1.print("#V3|");
        Serial1.print(kirim.sensor3, 4);
        Serial1.println('#');
        Serial1.flush();
        digitalWrite(RS485_CONTROL, LOW);

      }

    }
    unsigned long now = millis();              // If it's time to send a message, send it!
    if ( now - last_sent >= interval  ) {
      last_sent = now;
      lcd.clear();
      lcd.setCursor(0, 1);
      lcd.print(kirim.sensor1);
      lcd.setCursor(4, 1);
      lcd.print("|");
      lcd.setCursor(6, 1);
      lcd.print(kirim.sensor2);
      lcd.setCursor(10, 1);
      lcd.print("|");
      lcd.setCursor(12, 1);
      lcd.print(kirim.sensor3);
      lcd.setCursor(0, 0);
      lcd.print("Status:");
      lcd.setCursor(8, 0);
      //lcd.print("_Record_");
      Write_File(kirim);

    }
    if (command.equals("STOP_REC")) {
      break;
    }
    getInput();
  }
  file.close();
  klik = false;
  current_page = 0;
  showMenu();
}

void timerIsr() {
  encoder->service();
}

void init_radio() {
  myRadio.begin();
  myRadio.setChannel(1200);
  myRadio.setPALevel(RF24_PA_MAX);
  myRadio.setDataRate(RF24_2MBPS) ;
  myRadio.setPayloadSize(120);
  //myRadio.openWritingPipe(addresses[0]);
  myRadio.openReadingPipe(1, addresses[1]);
  //myRadio.startListening();
  // myRadio.disableAckPayload();
}

void check_communication_command(String &command) {
  //String command = "";
  if (current_transmiter == 0) { //using NRF
    myRadio.openReadingPipe(1, addresses[1]);
    myRadio.startListening();
    char a[9];
    char v[] = "V";
    char strt[] = "START_REC";
    char stp[] = "STOP_REC";
    char *ab;
    if (myRadio.available()) {
      myRadio.read(&a, sizeof(a));
      Serial.println("Data from radio " + String(a));
      ab = strstr(a, strt);
      if (ab) {
        command = String(strt);
      }
      ab = strstr(a, stp);
      if (ab) {
        command = String(stp);
      }
      ab = strstr(a, v);
      if (ab) {
        command = String(v);
      }
    }
  } else if (current_transmiter == 1)//Using RS485
  {
    get_data_serial(&Serial1, RS485_CONTROL , command);
  }
  command.trim();
  //return command;
}

bool get_data_serial(
  Stream* channel,
  int control_pin,
  String& data_received) {

  String data_payload;
  digitalWrite(control_pin, LOW);
  if (channel->available()) {
    data_payload = channel->readStringUntil('\n');
    data_payload.trim();
    data_received = String(data_payload);          // get data payload
    return true;
  }
  return false;
}

bool send_data_serial(    // Return true if ack is found or if confirm_ack is false, return false if ack is not found
  Stream* channel,          // Serial or Serial1 etc.
  int control_pin,           // RS485 control pin
  String command,            // The data to send
  bool confirm_ack,          // Is ack expected? If true it will check if command streamed back from stream channel
  int retry,                 // How many time(s) to trial (first try is not count), only use if confirm_ack is true
  String& data_received) {   // Data from stream wil be write in this variable, if confirm_ack then it will store any string streamed from stream channel

  if (confirm_ack) {
    do {
      digitalWrite(control_pin, HIGH);
      channel->println(command);
      channel->flush();
      if (get_data_serial(channel, control_pin, data_received)) {
        Serial.println("ack data " + data_received);
        if (data_received.equals(command)) {
          return true;
        }
      }
      retry--;
    } while (retry > 0);
    return false;                                       //ack not found
  } else {
    digitalWrite(control_pin, HIGH);
    channel->println(command);
    channel->flush();
    return true;                                         //always return true if confirm_ack is false
  }
}

void Make_File() {
  list_files();
  String filename = "Laser_Tes" + String(file_counter) + ".csv";
  char filename_char[15];
  filename.toCharArray(filename_char, 15);
  if (!file.open(filename_char, O_WRITE | O_CREAT | O_AT_END)) {
    lcd.clear();
    lcd.print("SDCard error");
    delay(2000);
  } else {
    file.print("LVDT_1");
    file.print(",");
    file.print("LVDT_2");
    file.print(",");
    file.println("LVDT_3");
  }
}

void Write_File(paket &kirim) {
  file.print(kirim.sensor1, 2);
  file.print(",");
  file.print(kirim.sensor2, 2);
  file.print(",");
  file.println(kirim.sensor3, 2);
}

void list_files() {
  file_counter = 0;
  while (file.openNext(&root, O_READ) && file_counter < max_file - 1) {
    if (!file.isDir() ) {
      file.getName(temp_name, 19);
      files[file_counter] = temp_name;
      Serial.println(files[file_counter]);
      file_counter++;
    }
    file.close();
  }
  files[file_counter] = "Kembali";

  if (root.getError()) {
    Serial.println("openNext failed");
  }
  /*
  else {
    file_selected = 0;
    if (show_on_lcd) {
      select_file();
    }
    for (int i = 0; i <= file_counter; i++) {
      Serial.println(files[i]);
    }
  }*/

  reset_root();

}

void reset_root() {
  root.close();
  if (!root.open("/")) {
    Serial.println("Error opening root");
  }
}
void showfile(int &counter) {
  if (file_counter==0){
    lcd.clear();
    lcd.setCursor(1, 0);
    lcd.print("File Kosong");
    lcd.setCursor(0, 1);
    lcd.print(">" + files[counter]);
  }
  else if (counter == 0) {
    lcd.clear();
    lcd.print(">" + files[counter]);
    lcd.setCursor(1, 1);
    lcd.print(files[counter + 1]);
  } 
  else {
    lcd.clear();
    lcd.setCursor(1, 0);
    lcd.print(files[counter - 1]);
    lcd.setCursor(0, 1);
    lcd.print(">" + files[counter]);
  }
}

void pageFiles() {
  list_files();
  int counter = 0;
  lcd.clear();
  showfile(counter);
  bool hold = true;

  while (hold) {
    if (counter < file_counter && up) {
      up = false;
      counter++;
      showfile(counter);
    }
    else if (counter > 0 && down) {
      down = false;
      counter--;
      showfile(counter);
    }
    if (klik) {
      klik = false;
      if (counter == file_counter) {
        break;
      }
      deleteFile(counter);
    }
    getInput();
  }
  klik = false;
  current_page = 0;
}

void deleteFile(int &counter) {
  const String del[] = {"Delete?", "Tidak"};
  bool hapus = true;
  lcd.clear();
  lcd.print(files[counter]);
  lcd.setCursor(0, 1);
  lcd.print(">");
  lcd.setCursor(1, 1);
  lcd.print(del[0]);
  lcd.setCursor(10, 1);
  lcd.print(del[1]);
  while (!klik) {
    if (down) {
      lcd.clear();
      lcd.print(files[counter]);
      lcd.setCursor(0, 1);
      lcd.print(">");
      lcd.setCursor(1, 1);
      lcd.print(del[0]);
      lcd.setCursor(10, 1);
      lcd.print(del[1]);
      hapus = true;
      down = false;
    }
    else if (up) {
      lcd.clear();
      lcd.print(files[counter]);
      lcd.setCursor(1, 1);
      lcd.print(del[0]);
      lcd.setCursor(9, 1);
      lcd.print(">");
      lcd.setCursor(10, 1);
      lcd.print(del[1]);
      hapus = false;
      up = false;
    }
    getInput();
  }
  
  if (hapus == true) {
    Serial.println("hapus");
    files[counter].toCharArray(temp_name, 19);
    delay(1000);
    if (!file.open(temp_name, O_WRITE)) {
      if (file.remove()) {
        lcd.clear();
        lcd.print("Berhasil.");
        delay(3000);
        file.close();
        reset_root();
        list_files();
        //delay(2000);
        //select_file();
      } else {
        lcd.clear();
        lcd.print("Gagal.");
        file.close();
        delay(2000);
        //select_file();
      }
    }
  }else{
    Serial.println("Tidak dihapus");
  }
  klik = false;
  showfile(counter);
}
void resetAll() {
  lcd.clear();
  lcd.print("RESET...........");
  SPI.begin();
  myRadio.begin();
  init_radio();
  int retrying_init_sdcard = 10;
  while (retrying_init_sdcard > 0) {
    if (!sd.begin(SD_CONFIG)) {
      Serial.println("SDCard Config Error");
      lcd.clear();
      lcd.print("SDCard error");
      file.close();
      root.close();
      retrying_init_sdcard--;
    } else {
      retrying_init_sdcard = 0;
      reset_root();
    }
  }
  delay(1000);
  pinMode(RS485_CONTROL, OUTPUT);
  digitalWrite(RS485_CONTROL, LOW); //set low to receive command
  printf_begin();
  zero_shifting();
  //calibrated();
}
void zero_shifting() {
  Serial.println("Zero Shift");
  digitalWrite(pinShift, HIGH);
  lcd.clear();
  lcd.print("Zero Shifting");
  for (int i = 0; i < 10; i++) {
    lcd.setCursor(i, 1);
    lcd.print(".");
    delay(100);
  }
  digitalWrite(pinShift, LOW);
  
  kalibrasi.sensor1 = 0;
  kalibrasi.sensor2 = 0;
  kalibrasi.sensor3 = 0;
  for (int i = 0; i < 15; i++) {
    kalibrasi.sensor1 += analogRead(laser1);
    kalibrasi.sensor2 += analogRead(laser2);
    kalibrasi.sensor3 += analogRead(laser3);
    lcd.setCursor(i, 1);
    lcd.print(".");
    delay(250);
  }
  kalibrasi.sensor1 = (int)kalibrasi.sensor1 / 15;
  kalibrasi.sensor2 = (int)kalibrasi.sensor2 / 15;
  kalibrasi.sensor3 = (int)kalibrasi.sensor3 / 15;
  current_page = 0;
  showMenu();
}


/*
void calibrated() {
  lcd.clear();
  lcd.print("Mengkalibrasi");
  kalibrasi.sensor1 = 0;
  kalibrasi.sensor2 = 0;
  kalibrasi.sensor3 = 0;
  for (int i = 0; i < 15; i++) {
    kalibrasi.sensor1 += analogRead(laser1);
    kalibrasi.sensor2 += analogRead(laser2);
    kalibrasi.sensor3 += analogRead(laser3);
    lcd.setCursor(i, 1);
    lcd.print(".");
    delay(250);
  }
  kalibrasi.sensor1 = (int)kalibrasi.sensor1 / 15;
  kalibrasi.sensor2 = (int)kalibrasi.sensor2 / 15;
  kalibrasi.sensor3 = (int)kalibrasi.sensor3 / 15;
  Serial.println(kalibrasi.sensor1);
  Serial.println(kalibrasi.sensor2);
  Serial.println(kalibrasi.sensor3);
  current_page = 0;
  showMenu();
}
*/
