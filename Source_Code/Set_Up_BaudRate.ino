#include <SoftwareSerial.h>

SoftwareSerial myESP(A2, A3); // RX, TX

void setup() {
  Serial.begin(9600);
  
  Serial.println("=== BAT DAU SUA LOI BAUD RATE ===");
  Serial.println("1. Thu ket noi o toc do 115200...");
  
  // Mở kết nối ở 115200 để bắt được ESP
  myESP.begin(115200); 
  delay(1000);
  
  // Gửi lệnh test
  myESP.println("AT");
  delay(500);
  
  if(myESP.available()){
     Serial.println("--> Tim thay ESP! Dang cai dat lai toc do ve 9600...");
     
     // LỆNH QUAN TRỌNG: Ép về 9600 và lưu vào bộ nhớ
     myESP.println("AT+UART_DEF=9600,8,1,0,0");
     // Nếu lệnh trên không ăn, thử thêm lệnh này: myESP.println("AT+CIOBAUD=9600");
     
     delay(2000); // Chờ ESP lưu cài đặt
     
     Serial.println("--> Da gui lenh. Bay gio test lai o toc do 9600...");
     
     // Đóng kết nối cũ, mở lại ở 9600 để kiểm tra
     myESP.end();
     myESP.begin(9600);
     delay(1000);
     
     myESP.println("AT");
     delay(1000);
     
     if(myESP.available()) {
       while(myESP.available()) Serial.write(myESP.read());
       Serial.println("\n=== THANH CONG! ESP DA CHAY O 9600 ===");
       Serial.println("Bay gio ban hay nap lai code CUA THONG MINH.");
     } else {
       Serial.println("Van chua duoc. Hay thu reset Arduino.");
     }
     
  } else {
    Serial.println("--> Khong tim thay ESP o 115200.");
    Serial.println("Kiem tra lai day noi TX-RX (Nho noi cheo day!).");
  }
}

void loop() {
  // Cầu nối để test thủ công
  if (myESP.available()) Serial.write(myESP.read());
  if (Serial.available()) myESP.write(Serial.read());
}