#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <Wire.h>
#include <I2CKeyPad.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>

// ======= ESP-01 WiFi =======
SoftwareSerial myESP(A2, A3); // RX, TX (A2 nối TX của ESP, A3 nối RX của ESP)
String wifiSSID = "NhatQuang";
String wifiPass = "123456789";
String devId = "v2158880920ACB6C"; 
// ======= RFID =======
#define RST_PIN 9
#define SS_PIN 10
MFRC522 mfrc522(SS_PIN, RST_PIN);
String MasterTag = "97 AE 6C 05"; // Thẻ admin
String UIDCard = "";

// Danh sách thẻ được phép (tối đa 10 thẻ)
#define MAX_CARDS 10
String allowedCards[MAX_CARDS];
int cardCount = 0;

// ======= EEPROM Addresses =======
#define EEPROM_INIT_FLAG 0      // 1 byte - cờ khởi tạo
#define EEPROM_CARD_COUNT 1     // 1 byte - số lượng thẻ
#define EEPROM_CARDS_START 2    // Bắt đầu lưu thẻ (10 thẻ × 12 bytes = 120 bytes)
#define CARD_UID_LENGTH 12      // Độ dài UID tối đa
#define EEPROM_PASSWORD 122     // 10 bytes cho password
#define EEPROM_MASTER_TAG 132   // 12 bytes cho master tag
#define PASSWORD_MAX_LENGTH 10

// ======= LCD & Servo =======
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo servo;

// ======= LED & Buzzer =======
#define BlueLED  2
#define GreenLED 3
#define RedLED   4
#define Buzzer   5
#define Open_Door_Button 7
#define Door_Sensor 8  // MC-38A Door Sensor (NO - Normally Open)

// ======= Keypad I2C =======
#define KEYPAD_ADDR 0x20
I2CKeyPad keypad(KEYPAD_ADDR);

// Keymap layout (4x4)
const char keymap[16] = {
  '1', '2', '3', 'A',
  '4', '5', '6', 'B',
  '7', '8', '9', 'C',
  '*', '0', '#', 'D'
};

String password = "1234";
String inputPass = "";
char mode = 'X'; // X: menu chính, A: password, B: RFID, C: Admin verify, M: Admin menu, W: Waiting for door close

// Trạng thái cửa
boolean doorLocked = true;
boolean doorOpening = false;

int wrongAttempts = 0;
const int MAX_WRONG = 5;
// ================================================================
void setup() {
  Serial.begin(9600);
  myESP.begin(9600);
  connectWifi();
  SPI.begin();
  mfrc522.PCD_Init();

  // Khởi tạo I2C và keypad
  Wire.begin();
  if (keypad.begin() == true) {
    Serial.println(F("Keypad found"));
    keypad.loadKeyMap(keymap);
  } else {
    Serial.println(F("ERROR: Keypad not found"));
  }

  // Khởi tạo LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();

  // Khởi tạo Servo
  servo.attach(6);
  servo.write(90); // Cửa khóa ban đầu (180 độ)

  // Khởi tạo các chân GPIO
  pinMode(GreenLED, OUTPUT);
  pinMode(BlueLED, OUTPUT);
  pinMode(RedLED, OUTPUT);
  pinMode(Buzzer, OUTPUT);
  pinMode(Open_Door_Button, INPUT_PULLUP);
  pinMode(Door_Sensor, INPUT_PULLUP); // MC-38A với pull-up

  // Trạng thái ban đầu
  digitalWrite(BlueLED, HIGH);
  digitalWrite(GreenLED, LOW);
  digitalWrite(RedLED, LOW);
  noTone(Buzzer);

  // Màn hình khởi động
  lcd.setCursor(0, 0);
  lcd.print(F("Welcome Home!"));
  lcd.setCursor(0, 1);
  lcd.print(F("Initializing..."));
  delay(1000);

  // Đọc dữ liệu từ EEPROM
  loadCardsFromEEPROM();

  lcd.setCursor(0, 1);
  lcd.print(F("Loaded "));
  lcd.print(cardCount);
  lcd.print(F(" cards  "));
  delay(1000);
 
  showMainMenu();
}

void sendToExcel(String uid, String action) {
  lcd.setCursor(0, 1);
  lcd.print(F("Syncing Proxy..."));
  action.replace(" ", "");
  // Xóa bộ đệm
  while(myESP.available()) myESP.read();

  // Kết nối tới PushingBox (Cổng 80 - Rất nhẹ, ít tốn điện)
  myESP.println("AT+CIPSTART=\"TCP\",\"api.pushingbox.com\",80");
  delay(3000); 
  
  if (myESP.find("CONNECT")) {
    Serial.println(F("Connected to PushingBox"));
    
    // Tạo URL gửi cho PushingBox
    // Cấu trúc: /pushingbox?devid=...&uid=...&action=...
    String url = "/pushingbox?devid=" + devId + "&uid=" + uid + "&action=" + action;
    
    String cmd = "GET " + url + " HTTP/1.1\r\n" +
                 "Host: api.pushingbox.com\r\n" +
                 "Connection: close\r\n\r\n";
                 
    myESP.print(F("AT+CIPSEND="));
    myESP.println(cmd.length());
    delay(500); // Đợi ngắn thôi
    
    if(myESP.find(">")) {
      myESP.print(cmd);
      Serial.println(F("Data Sent via HTTP! Success."));
      long timeout = millis();
      while(millis() - timeout < 5000) {
        while(myESP.available()) {
          Serial.write(myESP.read());
        }
      }
    } else {
      Serial.println(F("Send Fail"));
    }
  } else {
    Serial.println(F("Connect Fail"));
    // Nếu kết nối lỗi, thử reset lại ESP
    myESP.println("AT+RST"); 
  }
  
  // Trả lại màn hình LCD
  lcd.setCursor(0, 1);
  lcd.print(F("                "));
}

void connectWifi() {
  myESP.println(F("AT+RST"));
  delay(2000);
  myESP.println(F("AT+CWMODE=1"));
  delay(1000);
  
  String cmd = "AT+CWJAP=\"" + wifiSSID + "\",\"" + wifiPass + "\"";
  myESP.println(cmd);
  
  lcd.clear();
  lcd.print(F("Connecting Wifi"));

  delay(5000);
  lcd.clear();
  myESP.println(F("AT+CIPMUX=1")); // Cho phép nhiều kết nối
  delay(1000);
  myESP.println(F("AT+CIPSERVER=1,80")); // Mở cổng 80
  delay(1000);
  
  // Lấy IP để hiển thị cho bạn biết
  myESP.println(F("AT+CIFSR"));
  
  Serial.println(F("Dang lay IP..."));

  // 2. Lắng nghe ESP trả lời và in ra màn hình
  long timeout = millis();
  while (millis() - timeout < 5000) { // Đợi 2 giây
    while (myESP.available()) {
      // Đọc từng chữ từ ESP và in sang Serial Monitor
      Serial.write(myESP.read()); 
    }
  }
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(F("Web Ready!"));
  delay(2000);
  lcd.clear();
}

void checkWebServer() {
  if (myESP.available()) {
    if (myESP.find("+IPD,")) {
      delay(300); // Đợi dữ liệu về hết
      
      String request = "";
      // Đọc tối đa 100 ký tự để tìm lệnh (đỡ tràn bộ nhớ)
      for (int i = 0; i < 100; i++) {
        if (myESP.available()) request += (char)myESP.read();
      }
      
      // Kiểm tra xem có lệnh "OPEN" không
      if (request.indexOf("OPEN") != -1) {
        Serial.println(F("WEB REQUEST: OPEN DOOR"));
        openDoorSuccess();
        sendToExcel("Web_User", "Remote_Open"); // Ghi log
      }
      
      // Đóng kết nối ngay để giải phóng tài nguyên
      // Thử đóng ID 0 và 1 (thường là 0)
      myESP.println(F("AT+CIPCLOSE=0"));
      myESP.println(F("AT+CIPCLOSE=1"));
    }
  }
}
// ================================================================
void loop() {
  // ======= KIỂM TRA CẢM BIẾN CỬA =======
  // MC-38A NO: LOW khi gần nhau, HIGH khi xa nhau
  checkWebServer();
  if (mode == 'W') {
    // Đang chờ cửa đóng
    if (digitalRead(Door_Sensor) == HIGH) {
      // Cảm biến chạm nhau -> đóng cửa
      closeDoor();
      return;
    }
    
    // Hiển thị trạng thái
    lcd.setCursor(0, 0);
    lcd.print(F("Door Opening    "));
    lcd.setCursor(0, 1);
    lcd.print(F("Push to close..."));
    delay(100);
    return;
  }

  // ======= NÚT MỞ THỦ CÔNG =======
  if (digitalRead(Open_Door_Button) == LOW) {
    manualOpen();
    return;
  }

  // ======= MENU CHÍNH =======
  if (mode == 'X') {
    char key = getKey();
    if (key == 'A') {
      mode = 'A';
      lcd.clear();
      lcd.print(F("Enter Password:"));
      lcd.setCursor(0, 1);
      inputPass = "";
    } else if (key == 'B') {
      mode = 'B';
      lcd.clear();
      lcd.print(F("Scan Your Card"));
      lcd.setCursor(0, 1);
      lcd.print(F("To Open..."));
    } else if (key == 'C') {
      mode = 'C';
      lcd.clear();
      lcd.print(F("Admin Mode"));
      lcd.setCursor(0, 1);
      lcd.print(F("Scan Admin Card"));
    }
  }

  // ======= CHẾ ĐỘ NHẬP MẬT KHẨU =======
  else if (mode == 'A') {
    char key = getKey();
    if (key) {
      if (key == '#') {
        if (inputPass.length() > 0) inputPass.remove(inputPass.length() - 1);
        updatePassDisplay();
      }
      else if (key == '*') {
        checkPassword();
      }
      else if (key == 'D') {
        mode = 'X';
        inputPass = "";
        showMainMenu();
      }
      else if (isDigit(key)) {
        inputPass += key;
        updatePassDisplay();
      }
    }
  }

  // ======= CHẾ ĐỘ RFID =======
  else if (mode == 'B') {
    char key = getKey();
    if (key == 'D') {
      mode = 'X';
      showMainMenu();
      return;
    }

    if (getUID()) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("UID: "));
      lcd.setCursor(0, 1);
      lcd.print(UIDCard);
      delay(1000);

      lcd.clear();
      lcd.setCursor(2, 0);
      lcd.print(F("Permission"));
      lcd.setCursor(0, 1);

      if (isCardAllowed(UIDCard)) {
        lcd.print(F("Access Granted!"));
        sendToExcel("Card_Entry", UIDCard);
        openDoorSuccess();
      } else {
        lcd.print(F("Access Denied!"));
        sendToExcel("Card_Entry", "Denied");
        accessDenied();
        delay(2000);
        mode = 'X';
        showMainMenu();
      }
    }
  }

  // ======= ADMIN MODE - XÁC THỰC =======
  else if (mode == 'C') {
    char key = getKey();
    if (key == 'D') {
      mode = 'X';
      showMainMenu();
      return;
    }

    if (getUID()) {
      lcd.clear();
      lcd.print(F("Checking..."));
      delay(500);

      if (UIDCard == MasterTag) {
        lcd.clear();
        lcd.print(F("Admin Verified!"));
        sendToExcel("Admin_Entry", MasterTag);
        delay(1000);
        mode = 'M';
        showAdminMenu();
      } else {
        lcd.clear();
        lcd.print(F("Access Denied!"));
        sendToExcel("Admin_Entry", "Denied");
        accessDenied();
        delay(2000);
        mode = 'X';
        showMainMenu();
      }
    }
  }

  // ======= ADMIN MENU =======
  else if (mode == 'M') {
    char key = getKey();
   
    if (key == '1') {
      addCard();
    }
    else if (key == '2') {
      removeCard();
    }
    else if (key == '3') {
      changePassword();
    }
    else if (key == '4') {
      changeMasterCard();
    }
    else if (key == 'D') {
      mode = 'X';
      showMainMenu();
    }
  }
}

// ================================================================
// ======= HÀM MỞ VÀ ĐÓNG CỬA =======

void openDoorSuccess() {
  digitalWrite(GreenLED, HIGH);
  digitalWrite(BlueLED, LOW);
  digitalWrite(RedLED, LOW);
  
  // Mở khóa: servo quay về 90 độ
  servo.write(180);
  doorLocked = false;
  doorOpening = true;
 
  for (int i = 0; i < 2; i++) {
    tone(Buzzer, 2000);
    delay(200);
    noTone(Buzzer);
    delay(200);
  }
 
  // Chuyển sang chế độ chờ đóng cửa
  mode = 'W';
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Door Unlocked!"));
  delay(1000);
 
  digitalWrite(GreenLED, LOW);
  digitalWrite(BlueLED, HIGH);
}

void closeDoor() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Door Closing..."));
  // Khóa cửa: servo quay về 180 độ
  servo.write(90);
  doorLocked = true;
  doorOpening = false;
  
  tone(Buzzer, 1800);
  delay(300);
  noTone(Buzzer);
  
  delay(1000);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Door Closed!"));
  delay(1500);
  
  // Trở về menu chính
  mode = 'X';
  inputPass = "";
  showMainMenu();
}

void manualOpen() {
  lcd.clear();
  lcd.print(F("Manual Override"));
  sendToExcel("Manual", "Open");
  lcd.setCursor(0, 1);
  lcd.print(F("Unlocking..."));

  digitalWrite(GreenLED, HIGH);
  digitalWrite(BlueLED, LOW);
  digitalWrite(RedLED, LOW);

  // Mở khóa: servo quay về 90 độ
  servo.write(180);
  doorLocked = false;
  doorOpening = true;
 
  for (int i = 0; i < 2; i++) {
    tone(Buzzer, 2000);
    delay(200);
    noTone(Buzzer);
    delay(200);
  }
 
  // Chuyển sang chế độ chờ đóng cửa
  mode = 'W';
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Door Unlocked!"));
  delay(1000);
 
  digitalWrite(GreenLED, LOW);
  digitalWrite(BlueLED, HIGH);
}

// ================================================================
// ======= EEPROM Functions =======

void loadCardsFromEEPROM() {
  // Kiểm tra cờ khởi tạo
  byte initFlag = EEPROM.read(EEPROM_INIT_FLAG);
 
  if (initFlag != 0xAA) {
    // Lần đầu tiên sử dụng, khởi tạo EEPROM
    Serial.println(F("First time init EEPROM"));
   
    // Lưu password và master tag mặc định VÀO EEPROM
    writeStringToEEPROM(EEPROM_PASSWORD, password);
    writeStringToEEPROM(EEPROM_MASTER_TAG, MasterTag);
   
    // Thêm master tag vào danh sách thẻ
    allowedCards[0] = MasterTag;
    cardCount = 1;
    saveCardsToEEPROM();
   
    // Đánh dấu đã khởi tạo
    EEPROM.write(EEPROM_INIT_FLAG, 0xAA);
   
    Serial.print(F("Saved default password: "));
    Serial.println(password);
    Serial.print(F("Saved default master tag: "));
    Serial.println(MasterTag);
  } else {
    // Đọc password từ EEPROM
    String loadedPassword = readStringFromEEPROM(EEPROM_PASSWORD);
    if (loadedPassword.length() >= 4) {
      password = loadedPassword;
      Serial.print(F("Loaded password: "));
      Serial.println(password);
    } else {
      Serial.println(F("Invalid password in EEPROM, using default: 1234"));
      password = "1234";
      writeStringToEEPROM(EEPROM_PASSWORD, password);
    }
   
    // Đọc master tag từ EEPROM
    String loadedMasterTag = readStringFromEEPROM(EEPROM_MASTER_TAG);
    if (loadedMasterTag.length() >= 8) {
      MasterTag = loadedMasterTag;
      Serial.print(F("Loaded master tag: "));
      Serial.println(MasterTag);
    } else {
      Serial.println(F("Invalid master tag in EEPROM, using default: 97 AE 6C 05"));
      MasterTag = "97 AE 6C 05";
      writeStringToEEPROM(EEPROM_MASTER_TAG, MasterTag);
    }
   
    // Đọc số lượng thẻ
    cardCount = EEPROM.read(EEPROM_CARD_COUNT);
   
    // Giới hạn số lượng hợp lệ
    if (cardCount > MAX_CARDS) cardCount = MAX_CARDS;
    if (cardCount < 1) cardCount = 1;
   
    // Đọc từng thẻ
    for (int i = 0; i < cardCount; i++) {
      allowedCards[i] = readStringFromEEPROM(EEPROM_CARDS_START + (i * CARD_UID_LENGTH));
      Serial.print(F("Loaded card "));
      Serial.print(i);
      Serial.print(F(": "));
      Serial.println(allowedCards[i]);
    }
   
    Serial.print(F("Total cards loaded: "));
    Serial.println(cardCount);
  }
}

void saveCardsToEEPROM() {
  // Lưu số lượng thẻ
  EEPROM.write(EEPROM_CARD_COUNT, cardCount);
 
  // Lưu từng thẻ
  for (int i = 0; i < cardCount; i++) {
    writeStringToEEPROM(EEPROM_CARDS_START + (i * CARD_UID_LENGTH), allowedCards[i]);
    Serial.print(F("Saved card "));
    Serial.print(i);
    Serial.print(F(": "));
    Serial.println(allowedCards[i]);
  }
 
  Serial.println(F("All cards saved to EEPROM"));
}

// Ghi chuỗi vào EEPROM
void writeStringToEEPROM(int address, String data) {
  int len = data.length();
  if (len > CARD_UID_LENGTH - 1) len = CARD_UID_LENGTH - 1;
 
  for (int i = 0; i < len; i++) {
    EEPROM.write(address + i, data[i]);
  }
  EEPROM.write(address + len, '\0'); // Null terminator
}

// Đọc chuỗi từ EEPROM
String readStringFromEEPROM(int address) {
  String data = "";
  char c;
 
  for (int i = 0; i < CARD_UID_LENGTH; i++) {
    c = EEPROM.read(address + i);
    if (c == '\0') break;
    data += c;
  }
 
  return data;
}

// ================================================================
// ======= HÀM PHỤ =======

void showMainMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("A:Pass B:RFID"));
  lcd.setCursor(0, 1);
  lcd.print(F("C:Admin Mode"));
}

void showAdminMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("1:Add 2:Remove"));
  lcd.setCursor(0, 1);
  lcd.print(F("3:PW 4:Master"));
}

void updatePassDisplay() {
  lcd.setCursor(0, 1);
  lcd.print(F("                "));
  lcd.setCursor(0, 1);
  for (unsigned int i = 0; i < inputPass.length(); i++) lcd.print("*");
}

void checkPassword() {
  lcd.clear();
  lcd.print(F("Checking..."));
  delay(500);
  lcd.clear();
  lcd.setCursor(2, 0);
  lcd.print(F("Permission"));
  lcd.setCursor(0, 1);
  if (inputPass == password) {
    lcd.print(F("Access Granted!"));
    sendToExcel("Pass_Entry", password);
    openDoorSuccess();
  } else {
    lcd.print(F("Access Denied!"));
    sendToExcel("Pass_Entry", "Denied");
    accessDenied();
    delay(2000);
    inputPass = "";
    mode = 'X';
    showMainMenu();
  }
}

void accessDenied() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(RedLED, HIGH);
    tone(Buzzer, 1500);
    delay(300);
    digitalWrite(RedLED, LOW);
    noTone(Buzzer);
    delay(200);
  }
  digitalWrite(BlueLED, HIGH);
}

// ================================================================
// ======= QUẢN LÝ THẺ =======

boolean isCardAllowed(String uid) {
  for (int i = 0; i < cardCount; i++) {
    if (allowedCards[i] == uid) {
      return true;
    }
  }
  return false;
}

void addCard() {
  if (cardCount >= MAX_CARDS) {
    lcd.clear();
    lcd.print(F("Full! Cannot"));
    lcd.setCursor(0, 1);
    lcd.print(F("add more cards"));
    delay(2000);
    showAdminMenu();
    return;
  }

  lcd.clear();
  lcd.print(F("Scan new card"));
  lcd.setCursor(0, 1);
  lcd.print(F("to add..."));

  unsigned long startTime = millis();
  while (millis() - startTime < 10000) {
    if (getUID()) {
      if (isCardAllowed(UIDCard)) {
        lcd.clear();
        lcd.print(F("Card exists!"));
        tone(Buzzer, 1000);
        delay(500);
        noTone(Buzzer);
        delay(1500);
        showAdminMenu();
        return;
      }

      // Thêm thẻ mới
      allowedCards[cardCount] = UIDCard;
      cardCount++;
     
      // Lưu vào EEPROM
      saveCardsToEEPROM();
      sendToExcel("Card_Added", UIDCard);
      lcd.clear();
      lcd.print(F("Completed!"));
      lcd.setCursor(0, 1);
      lcd.print(F("Card added"));
     
      tone(Buzzer, 2000);
      delay(200);
      noTone(Buzzer);
      delay(200);
      tone(Buzzer, 2000);
      delay(200);
      noTone(Buzzer);
     
      delay(2000);
      showAdminMenu();
      return;
    }
   
    char key = getKey();
    if (key == 'D') {
      showAdminMenu();
      return;
    }
  }

  lcd.clear();
  lcd.print(F("Timeout!"));
  delay(1500);
  showAdminMenu();
}

void removeCard() {
  if (cardCount <= 1) {
    lcd.clear();
    lcd.print(F("Cannot delete!"));
    lcd.setCursor(0, 1);
    lcd.print(F("Need 1+ card"));
    delay(2000);
    showAdminMenu();
    return;
  }

  lcd.clear();
  lcd.print(F("Scan card to"));
  lcd.setCursor(0, 1);
  lcd.print(F("remove..."));

  unsigned long startTime = millis();
  while (millis() - startTime < 10000) {
    if (getUID()) {
      if (UIDCard == MasterTag) {
        lcd.clear();
        lcd.print(F("Cannot delete"));
        lcd.setCursor(0, 1);
        lcd.print(F("Admin card!"));
        tone(Buzzer, 1000);
        delay(500);
        noTone(Buzzer);
        delay(1500);
        showAdminMenu();
        return;
      }

      boolean found = false;
      for (int i = 0; i < cardCount; i++) {
        if (allowedCards[i] == UIDCard) {
          for (int j = i; j < cardCount - 1; j++) {
            allowedCards[j] = allowedCards[j + 1];
          }
          cardCount--;
          found = true;
          break;
        }
      }

      if (found) {
        // Lưu vào EEPROM
        saveCardsToEEPROM();
        sendToExcel("Card_Removed", UIDCard);
        lcd.clear();
        lcd.print(F("Completed!"));
        lcd.setCursor(0, 1);
        lcd.print(F("Card removed"));
       
        tone(Buzzer, 2000);
        delay(200);
        noTone(Buzzer);
        delay(200);
        tone(Buzzer, 2000);
        delay(200);
        noTone(Buzzer);
       
        delay(2000);
      } else {
        lcd.clear();
        lcd.print(F("Card not found"));
        lcd.setCursor(0, 1);
        lcd.print(F("in list"));
        tone(Buzzer, 1000);
        delay(500);
        noTone(Buzzer);
        delay(1500);
      }
     
      showAdminMenu();
      return;
    }
   
    char key = getKey();
    if (key == 'D') {
      showAdminMenu();
      return;
    }
  }

  lcd.clear();
  lcd.print(F("Timeout!"));
  delay(1500);
  showAdminMenu();
}

// Đổi mật khẩu
void changePassword() {
  lcd.clear();
  lcd.print(F("New Password:"));
  lcd.setCursor(0, 1);
 
  String newPassword = "";
  unsigned long startTime = millis();
 
  while (millis() - startTime < 30000) {
    char key = getKey();
   
    if (key) {
      startTime = millis();
     
      if (key == '#') {
        if (newPassword.length() > 0) {
          newPassword.remove(newPassword.length() - 1);
          lcd.setCursor(0, 1);
          lcd.print("                ");
          lcd.setCursor(0, 1);
          for (unsigned int i = 0; i < newPassword.length(); i++) lcd.print("*");
        }
      }
      else if (key == '*') {
        if (newPassword.length() >= 4) {
          password = newPassword;
          writeStringToEEPROM(EEPROM_PASSWORD, password);
          sendToExcel("Pass_Changed", password);
          lcd.clear();
          lcd.print(F("Completed!"));
          lcd.setCursor(0, 1);
          lcd.print(F("Pass changed"));
         
          tone(Buzzer, 2000);
          delay(200);
          noTone(Buzzer);
          delay(200);
          tone(Buzzer, 2000);
          delay(200);
          noTone(Buzzer);
         
          delay(2000);
          showAdminMenu();
          return;
        } else {
          lcd.clear();
          lcd.print(F("Too short!"));
          lcd.setCursor(0, 1);
          lcd.print(F("Min 4 digits"));
          delay(1500);
          lcd.clear();
          lcd.print(F("New Password:"));
          lcd.setCursor(0, 1);
          for (unsigned int i = 0; i < newPassword.length(); i++) lcd.print("*");
        }
      }
      else if (key == 'D') {
        showAdminMenu();
        return;
      }
      else if (isDigit(key) && newPassword.length() < PASSWORD_MAX_LENGTH) {
        newPassword += key;
        lcd.setCursor(0, 1);
        for (unsigned int i = 0; i < newPassword.length(); i++) lcd.print("*");
      }
    }
  }
 
  lcd.clear();
  lcd.print(F("Timeout!"));
  delay(1500);
  showAdminMenu();
}

// Đổi thẻ Master
void changeMasterCard() {
  lcd.clear();
  lcd.print(F("Scan new"));
  lcd.setCursor(0, 1);
  lcd.print(F("Master card..."));
 
  unsigned long startTime = millis();
  while (millis() - startTime < 10000) {
    if (getUID()) {
      boolean isDuplicate = false;
      for (int i = 0; i < cardCount; i++) {
        if (allowedCards[i] == UIDCard && allowedCards[i] != MasterTag) {
          isDuplicate = true;
          break;
        }
      }
     
      if (isDuplicate) {
        lcd.clear();
        lcd.print(F("Card is already"));
        lcd.setCursor(0, 1);
        lcd.print(F("in user list!"));
        tone(Buzzer, 1000);
        delay(500);
        noTone(Buzzer);
        delay(1500);
        showAdminMenu();
        return;
      }
     
      // Cập nhật master tag trong danh sách
      for (int i = 0; i < cardCount; i++) {
        if (allowedCards[i] == MasterTag) {
          allowedCards[i] = UIDCard;
          break;
        }
      }
     
      // Lưu master tag mới
      MasterTag = UIDCard;
      writeStringToEEPROM(EEPROM_MASTER_TAG, MasterTag);
      saveCardsToEEPROM();
      sendToExcel("New_Master", MasterTag);
      lcd.clear();
      lcd.print(F("Completed!"));
      lcd.setCursor(0, 1);
      lcd.print(F("Master changed"));
     
      tone(Buzzer, 2000);
      delay(200);
      noTone(Buzzer);
      delay(200);
      tone(Buzzer, 2000);
      delay(200);
      noTone(Buzzer);
     
      delay(2000);
      showAdminMenu();
      return;
    }
   
    char key = getKey();
    if (key == 'D') {
      showAdminMenu();
      return;
    }
  }
 
  lcd.clear();
  lcd.print(F("Timeout!"));
  delay(1500);
  showAdminMenu();
}

// ================================================================
// ======= ĐỌC RFID =======

boolean getUID() {
  if (!mfrc522.PICC_IsNewCardPresent()) return false;
  if (!mfrc522.PICC_ReadCardSerial()) return false;

  UIDCard = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    UIDCard.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
    UIDCard.concat(String(mfrc522.uid.uidByte[i], HEX));
  }
  UIDCard.toUpperCase();
  UIDCard = UIDCard.substring(1);
  mfrc522.PICC_HaltA();
  return true;
}

// ================================================================
// ======= ĐỌC PHÍM TỪ KEYPAD I2C =======

char getKey() {
  if (keypad.isPressed()) {
    char key = keypad.getChar();
    delay(200);
    return key;
  }
  return 0;
}