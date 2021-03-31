/*===============import library===================*/

#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>
#include <Sync.h>
#include <EEPROM.h>
#include <Wire.h> 
#include "SdFat.h"
#include <ClickEncoder.h>
#include <TimerOne.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>  

/*================================================================================================================================
=================================================================================================================================*/
#define lvdt1 A5
#define lvdt2 A6
#define lvdt3 A7
#define pinShift 46
LiquidCrystal_I2C lcd(0x27, 16, 2);

RF24 radio(7,8);                     // nRF24L01(+) radio attached using Getting Started board 
RF24Network network(radio);          // Network uses that radio
const uint16_t this_node = 01;       // Address of our node in Octal format
const uint16_t other_node = 00;      // Address of the other node in Octal format
unsigned long interval = 1000;       // ms  // How often to send 'hello world to the other unit
unsigned long last_sent;             // When did we last send?
unsigned long packets_sent;          // How many have we sent already

const uint8_t SD_CS_PIN = 6;
const uint8_t SOFT_MISO_PIN = 3;
const uint8_t SOFT_MOSI_PIN = 4;
const uint8_t SOFT_SCK_PIN  = 5;
SoftSpiDriver<SOFT_MISO_PIN, SOFT_MOSI_PIN, SOFT_SCK_PIN> softSpi;
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(0), &softSpi)
SdFat sd;
File file;
File root;

ClickEncoder *encoder;
int16_t last, value;
bool up = false;
bool down = false;
bool klik =false;

const int max_menu=5;
String menu[max_menu]={"Start Akuisisi","Zero_Shifting","Transmiter","Interval","Reset Default"};
int current_menu=0;
int current_page=0;

String transmiter[2]={"NRF","RS845"};
int current_transmiter = 0;
int transmiter_addr = 0;

const int max_file = 20;
int file_counter = 0;
char temp_name[20];
String files[max_file];
int file_selected = 0;
String file_opt[2] = {"Hapus File? Btl", "Hapus File? Ya"};
int file_opt_selected = 0;
void select_file(bool selected_only = false);

struct paket {
  float sensor1 = 0;
  float sensor2 = 0;
  float sensor3 = 0;
};
paket kalibrasi;

/*===============================================================================================================================
===============================================================================================================================*/

void setup() {
    Serial.begin(9600);
    pinMode(pinShift,OUTPUT);
    Timer1.initialize(5000);
    Timer1.attachInterrupt(timerIsr);
    encoder = new ClickEncoder(A2, A3, A4);
    last = encoder->getValue();
    lcd.begin();
    lcd.clear();
    SPI.begin();
    radio.begin();
    network.begin(/*channel*/ 90, /*node address*/ this_node);
    current_transmiter = EEPROM.read(transmiter_addr);
    if(current_transmiter > 1){
      EEPROM.write(transmiter_addr, 0);
    }
    Serial.println(transmiter[current_transmiter]);
    if (!sd.begin(SD_CONFIG)) {
      Serial.println("SDCard Config Error");
      lcd.clear();
      lcd.print("SDCard error");
    }else{  
      reset_root();  
    }
    zero_shift();
    
}

void loop() {
  if(current_page==0){
      chg_menu();
  }
  else if(current_page==1){
      akuisisi();
  }
  else if(current_page==2){
      zero_shift();
  }
  else if(current_page==3){
      chg_transmiter();
  }
  else if(current_page==4){
      chg_interval();
  }
  else if(current_page==5){
      reset();
  }
}
void chg_menu(){
  showMenu();
  while(!klik){
    getInput();
    if(current_menu < (max_menu-1) && up){
        up=false;
        current_menu++;
        showMenu();
      }
      else if(current_menu > 0 && down){
        down=false;
        current_menu--;
        showMenu();
      }
  }
  klik=false;
  current_page=current_menu+1; 
}

void chg_transmiter(){
  if(current_transmiter==0){
    lcd.clear();
    lcd.print(">"+transmiter[0]);
    lcd.setCursor(1,1);
    lcd.print(transmiter[1]);
  }
  else{
    lcd.clear();
    lcd.setCursor(1,0);
    lcd.print(transmiter[0]);
    lcd.setCursor(0,1);
    lcd.print(">"+transmiter[1]);
  }
  while(!klik){
    if(up){
        up=false;
        current_transmiter=1;
        lcd.clear();
        lcd.setCursor(1,0);
        lcd.print(transmiter[0]);
        lcd.setCursor(0,1);
        lcd.print(">"+transmiter[1]);
        EEPROM.update(transmiter_addr, current_transmiter);
      }
      else if(down){
        down=false;
        current_transmiter=0;
        lcd.clear();
        lcd.print(">"+transmiter[0]);
        lcd.setCursor(1,1);
        lcd.print(transmiter[1]);
        EEPROM.update(transmiter_addr, current_transmiter);
      }
    getInput();
  }
  klik=false;
  current_page=0;
  showMenu();    
}
void chg_interval(){
  lcd.clear();
  lcd.print(interval);
  lcd.setCursor(5,0);
  lcd.print("ms");
  while(!klik){
    if(interval < 10000 && up){
        up=false;
        interval=interval/100;
        interval++;
        interval=interval*100;
        lcd.clear();
        lcd.print(interval);
        lcd.setCursor(5,0);
        lcd.print("ms");
      }
      else if(interval > 100 && down){
        down = false;
        interval=interval/100;
        interval--;
        interval=interval*100;
        lcd.clear();
        lcd.print(interval);
        lcd.setCursor(5,0);
        lcd.print("ms");
      }
    getInput();  
  }
  klik = false;
  current_page=0;
  showMenu();
}

void reset_root(){
  root.close();  
  if (!root.open("/")){
    Serial.println("Error opening root");
  }
}
/*
void reset(){
  lcd.clear();
  lcd.print("Reset...");
  reset_root();
  file_selected = 0;
  file_counter = 0;
  delay(1000);
  showMenu();
}

void delete_file(int counter){  
  if(counter == 0){
    lcd.clear();
    lcd.print("Menghapus file ");
    lcd.setCursor(0,1);
    lcd.print(files[file_selected]);    
  }
  files[file_selected].toCharArray(temp_name, 19);
  if(!file.open(temp_name, O_WRITE)){
    delay(1000);
    if(file.remove()){
      lcd.clear();
      lcd.print("Berhasil."); 
      delay(3000);
      reset_root();
      list_files(); 
      delay(3000);
      select_file();      
    }else{      
      lcd.clear();
      lcd.print("Gagal.");
      file.close();
      delay(3000);
      select_file();
    }
  }else if(counter <=5){
    counter++;
    lcd.clear();
    lcd.print("Gagal menghapus.");
    lcd.setCursor(0,1);
    lcd.print("Retrying..."+String(counter));
    delete_file(counter);
  }else{   
    lcd.clear();
    lcd.print("Gagal menghapus.");
    delay(3000);
    select_file();
  }
}
*/
void showMenu(){
  lcd.clear();
  if(current_menu==0){
    lcd.print(">"+menu[current_menu]);
    lcd.setCursor(1,1);
    lcd.print(menu[current_menu+1]);
  }else{
    lcd.setCursor(1,0);
    lcd.print(menu[current_menu-1]);
    lcd.setCursor(0,1);
    lcd.print(">"+menu[current_menu]);
  }
}
void getInput(){
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
  }else if (value/3 > last) {
    last = value/3;
    up = true;
    Serial.println("up");
    
  }else if (value/3 < last) {
    last = value/3;
    down = true;
    Serial.println("down");  
  }
}

void reset(){
  Serial.println("reset");
  interval = 2000;
  current_transmiter = 0;
  lcd.clear();
  lcd.print("Reset Default");
  for(int i=0;i<15;i++){
    lcd.setCursor(i,1);
    lcd.print(".");
    delay(100);
  }
  current_page=0;
  showMenu();
}

void akuisisi(){

  while(!klik){
    paket kirim;
    if(current_transmiter ==0){
      network.update();
      unsigned long now = millis();              // If it's time to send a message, send it!
      if ( now - last_sent >= interval  ){
        last_sent = now;
        Serial.print("Sending...");
        for(int i=0; i<10; i++){
          kirim.sensor1+=analogRead(lvdt1);
          kirim.sensor2+=analogRead(lvdt2);
          kirim.sensor3+=analogRead(lvdt3);
          delay(5);
        }
        
        kirim.sensor1=(((int)kirim.sensor1/10)-kalibrasi.sensor1)/(1023-kalibrasi.sensor1)*400;
        kirim.sensor2=(((int)kirim.sensor2/10)-kalibrasi.sensor2)/(1023-kalibrasi.sensor2)*400;
        kirim.sensor3=(((int)kirim.sensor3/10)-kalibrasi.sensor3)/(1023-kalibrasi.sensor3)*400;
        if(kirim.sensor1<0){
          kirim.sensor1=0;
        }
        if(kirim.sensor2<0){
          kirim.sensor2=0;
        }
        if(kirim.sensor3<0){
          kirim.sensor3=0;
        }
        lcd.clear();
        lcd.print(kirim.sensor1);
        lcd.setCursor(5,0);
        lcd.print("mm");
        lcd.setCursor(10,0);
        lcd.print(kirim.sensor2);
        lcd.setCursor(14,0);
        lcd.print("mm");
        lcd.setCursor(0,1);
        lcd.print(kirim.sensor3);
        lcd.setCursor(5,1);
        lcd.print("mm");
        RF24NetworkHeader header(/*to node*/ other_node);
        bool ok = network.write(header,&kirim,sizeof(kirim));
        if (ok){
          Serial.println("ok.");
          lcd.setCursor(10,1);
          lcd.print("Terkirim");
        }
        else{
          Serial.println("failed.");
          lcd.setCursor(10,1);
          lcd.print("Gagal");
        }
      }
    }
    else if(current_transmiter ==1){
      Serial.println("RS485");
      lcd.print("RS485");
    }
   getInput(); 
  }
  klik=false;
  current_page=0;
  showMenu();
}

void calibrated(){
  lcd.clear();
  lcd.print("Kalibrasi");
  kalibrasi.sensor1=0;
  kalibrasi.sensor2=0;
  kalibrasi.sensor3=0;
  for(int i=0;i<15;i++){
    kalibrasi.sensor1+=analogRead(lvdt1);
    kalibrasi.sensor2+=analogRead(lvdt2);
    kalibrasi.sensor3+=analogRead(lvdt3);
    lcd.setCursor(i,1);
    lcd.print(".");
    delay(250);
  }
  kalibrasi.sensor1=(int)kalibrasi.sensor1/15;
  kalibrasi.sensor2=(int)kalibrasi.sensor2/15;
  kalibrasi.sensor3=(int)kalibrasi.sensor3/15;
  
  Serial.println(kalibrasi.sensor1);
  Serial.println(kalibrasi.sensor2);
  Serial.println(kalibrasi.sensor3);
  current_page=0;
  showMenu();
}

void timerIsr() {
  encoder->service();
}

void zero_shift(){
  Serial.println("Zero Shift");
  digitalWrite(pinShift,HIGH);
  lcd.clear();
  lcd.print("Zero Shift");
  for(int i=0;i<10;i++){
    lcd.setCursor(i,1);
    lcd.print(".");
    delay(100);
  }
  digitalWrite(pinShift,LOW);
  calibrated();
  current_page=0;
  showMenu();
}
