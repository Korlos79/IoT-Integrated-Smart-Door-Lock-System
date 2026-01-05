#include <SoftwareSerial.h>

// --- CẤU HÌNH WIFI ---
String WIFI_SSID = "..."; // <--- Thay tên Wifi
String WIFI_PASS = "...";    // <--- Thay mật khẩu

// Chân nối: TX của ESP -> A2, RX của ESP -> A3
SoftwareSerial myESP(A2, A3); 

const int LED_PIN = 13; // Đèn báo trạng thái

void setup() {
  Serial.begin(9600);
  myESP.begin(9600);
  pinMode(LED_PIN, OUTPUT);

  Serial.println("--------------------------------");
  Serial.println("BAT DAU KET NOI WIFI...");
  
  // 1. GỌI HÀM KẾT NỐI NGAY KHI KHỞI ĐỘNG
  connectWifi();
}

void loop() {
  // 2. KIỂM TRA ĐỊNH KỲ (Mỗi 3 giây)
  if (checkWifiSimple()) {
    Serial.println("KET QUA: Wifi OK (Den sang)");
    digitalWrite(LED_PIN, HIGH); // Sáng đèn nếu có mạng
  } else {
    Serial.println("KET QUA: Mat ket noi! (Den tat)");
    digitalWrite(LED_PIN, LOW);  // Tắt đèn nếu mất mạng
    
    // Tùy chọn: Nếu mất mạng thì thử kết nối lại
    // connectWifi(); 
  }
  
  delay(3000);
}

// --- HÀM 1: THỰC HIỆN KẾT NỐI ---
void connectWifi() {
  // Reset module cho sạch
  myESP.println("AT+RST"); 
  delay(2000);
  
  // Chuyển chế độ Station
  myESP.println("AT+CWMODE=1");
  delay(500);
  
  // Lệnh kết nối: AT+CWJAP="TenWifi","MatKhau"
  Serial.print("Dang ket noi vao: ");
  Serial.println(WIFI_SSID);
  
  String cmd = "AT+CWJAP=\"" + WIFI_SSID + "\",\"" + WIFI_PASS + "\"";
  myESP.println(cmd);
  
  // Đợi 8 giây cho nó kết nối (Wifi cần thời gian để cấp IP)
  delay(8000); 
}

// --- HÀM 2: KIỂM TRA TRẠNG THÁI ---
bool checkWifiSimple() {
  // Xóa bộ đệm cũ
  while(myESP.available()) myESP.read();
  
  // Hỏi trạng thái
  myESP.println("AT+CIPSTATUS");
  delay(500); 
  
  if (myESP.available()) {
    String res = myESP.readString();
    
    // Nếu trả về số 2 (Got IP) hoặc 3 (Connected) là OK
    if (res.indexOf("STATUS:2") >= 0 || res.indexOf("STATUS:3") >= 0) {
      return true;
    }
  }
  return false;
}