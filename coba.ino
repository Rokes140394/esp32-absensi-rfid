#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include "time.h"
#include <SD.h>
#include <WebServer.h>

WebServer server(80);

// ================= WIFI =================
const char *ssid = "DRNET";
const char *password = "drnet2021";

// ================= ADMIN LOGIN =================
const char *adminUser = "admin";
const char *adminPass = "1234";

// ================= TIME =================
const long gmtOffset_sec = 25200;
const int daylightOffset_sec = 0;

// ================= OLED =================
Adafruit_SH1106G display(128, 64, &Wire, -1);

// ================= RFID =================
#define SS_PIN 5
#define RST_PIN 4
MFRC522 mfrc522(SS_PIN, RST_PIN);

// ================= SD CARD =================
SPIClass spiSD(HSPI);
#define SD_CS 13
#define SD_SCK 14
#define SD_MISO 26
#define SD_MOSI 27

// ================= TOMBOL MODE =================
#define PIN_TOMBOL 15
bool modeMasuk = true;
bool lastButtonState = HIGH;
bool wifiOK = false;
bool sdOK = false;
bool sistemSiap = false;
bool modeDaftar = false;



unsigned long lastDebounceTime = 0;

// ================= PROTOTYPE =================
void tampilOLED(String a, String b, String c);
void tampilJamStandby();
void simpanCSV(String nama, String divisi, String waktu);
void bacaTombolMode();
void handleRoot();
void initTime();
bool cariKartu(String uid, String &nama, String &divisi);
void handleTambah();
void tampilAbsensi(String status, String nama, String divisi, String waktu);
bool uidSudahTerdaftar(String uid);
bool isAuthenticated();

// ================= UTIL =================
String uidToString(MFRC522::Uid *uid) {
  String hasil = "";
  for (byte i = 0; i < uid->size; i++) {
    if (uid->uidByte[i] < 0x10) hasil += "0";
    hasil += String(uid->uidByte[i], HEX);
    if (i != uid->size - 1) hasil += " ";
  }
  hasil.toUpperCase();
  return hasil;
}

String getTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "NoTime";
  char buffer[30];
  strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", &timeinfo);
  return String(buffer);
}

String urlEncode(String s) {
  s.replace(" ", "%20");
  return s;
}

// ================= OLED =================
void tampilOLED(String a, String b, String c) {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  // ===== Baris 1 (Judul) =====
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(a);

  // ===== Baris 2 (UID / Pesan utama) =====
  display.setCursor(0, 16);

  if (b.length() > 10) {        // kalau teks panjang seperti UID RFID
    display.setTextSize(1);     // kecilkan supaya muat
  } else {
    display.setTextSize(2);     // teks pendek tetap besar
  }

  display.println(b);

  // ===== Baris 3 (Info bawah) =====
  display.setTextSize(1);
  display.setCursor(0, 40);
  display.println(c);

  display.display();
}

void tampilStatusSistem() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 10);
  display.print("WIFI : ");
  display.println(wifiOK ? "OK" : "FAIL");

  display.setCursor(0, 25);
  display.print("SD   : ");
  display.println(sdOK ? "OK" : "FAIL");

  display.display();
}



void tampilJamStandby() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  String hari[7] = { "Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jumat", "Sabtu" };
  String bulan[12] = { "Januari", "Februari", "Maret", "April", "Mei", "Juni",
                       "Juli", "Agustus", "September", "Oktober", "November", "Desember" };

  char jam[10];
  strftime(jam, sizeof(jam), "%H:%M:%S", &timeinfo);

  String tanggal = String(timeinfo.tm_mday) + " " + bulan[timeinfo.tm_mon] + " " + String(1900 + timeinfo.tm_year);

  display.clearDisplay();

  int16_t x1, y1;
  uint16_t w, h;

  // ===== MODE MASUK / PULANG (TENGAH ATAS) =====
  display.setTextSize(1);
  String modeText = modeMasuk ? "MASUK" : "PULANG";
  display.getTextBounds(modeText, 0, 0, &x1, &y1, &w, &h);
  int xMode = (128 - w) / 2;
  display.setCursor(xMode, 0);
  display.println(modeText);

  // ===== HARI =====
  display.setTextSize(2);
  display.setCursor(0, 13);
  display.println(hari[timeinfo.tm_wday]);

  // ===== TANGGAL =====
  display.setTextSize(1);
  display.setCursor(0, 34);
  display.println(tanggal);

  // ===== JAM =====
  display.setTextSize(2);
  display.setCursor(0, 48);
  display.println(jam);

  display.display();
}

// ================= SD =================
void simpanCSV(String nama, String divisi, String waktu) {
  File file = SD.open("/absensi.csv", FILE_APPEND);
  if (file) {
    file.print(nama);
    file.print(",");
    file.print(divisi);
    file.print(",");
    file.print(modeMasuk ? "MASUK" : "PULANG");
    file.print(",");
    file.println(waktu);
    file.close();
  }
}

// ================= CARI KARTU DI SD =================
bool cariKartu(String uid, String &nama, String &divisi) {
  File file = SD.open("/kartu.csv");
  if (!file) return false;  // cukup return false

  file.readStringUntil('\n');  // skip header

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();

    int p1 = line.indexOf(',');
    int p2 = line.indexOf(',', p1 + 1);

    if (p1 > 0 && p2 > 0) {
      String uidFile = line.substring(0, p1);
      if (uidFile == uid) {
        nama = line.substring(p1 + 1, p2);
        divisi = line.substring(p2 + 1);
        file.close();
        return true;
      }
    }
  }

  file.close();
  return false;
}

bool isAuthenticated() {
  if (!server.authenticate(adminUser, adminPass)) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

bool uidSudahTerdaftar(String uid) {
  File file = SD.open("/kartu.csv");
  if (!file) return false;

  file.readStringUntil('\n');  // lewati header

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();

    int koma = line.indexOf(',');
    if (koma > 0) {
      String uidFile = line.substring(0, koma);
      if (uidFile == uid) {
        file.close();
        return true;  // UID sudah ada
      }
    }
  }

  file.close();
  return false;  // belum ada
}


// ================= TOMBOL =================
void bacaTombolMode() {
  static bool lastStableState = HIGH;
  static bool lastReading = HIGH;
  static unsigned long lastDebounce = 0;
  static unsigned long tekanMulai = 0;
  static bool longPressDone = false;

  bool reading = digitalRead(PIN_TOMBOL);

  if (reading != lastReading) {
    lastDebounce = millis();
    lastReading = reading;
  }

  if ((millis() - lastDebounce) > 40) {

    if (reading == LOW && lastStableState == HIGH) {
      tekanMulai = millis();
      longPressDone = false;
    }

    // 🔥 LONG PRESS 3 DETIK → MODE DAFTAR
    if (reading == LOW && !longPressDone && (millis() - tekanMulai > 3000)) {
      modeDaftar = !modeDaftar;
      tampilOLED("MODE", modeDaftar ? "DAFTAR" : "ABSENSI", "");
      delay(1000);
      longPressDone = true;
    }

    // 🔹 SHORT PRESS → GANTI MASUK/PULANG
    if (lastStableState == HIGH && reading == LOW && !modeDaftar && (millis() - tekanMulai < 3000)) {
      modeMasuk = !modeMasuk;
    }

    lastStableState = reading;
  }
}


// ================= WEB =================
void handleRoot() {
  if (!isAuthenticated()) return;

  File file = SD.open("/kartu.csv");
  if (!file) {
    server.send(200, "text/plain", "File kartu.csv tidak ditemukan");
    return;
  }
  file.readStringUntil('\n');

  // ===== DAFTAR DIVISI =====
  String daftarDivisi[] = {
  "IT","RESTO","HK","FB","HRD","ACCOUNTING",
  "FO","KITCHEN","ENG","PURCAS","GM","ASS GM",
  "SALES"
};
  int jumlahDivisi = sizeof(daftarDivisi) / sizeof(daftarDivisi[0]);

  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
              "<style>"
              "body{font-family:Arial;background:#f2f2f2;text-align:center;}"
              "table{border-collapse:collapse;margin:auto;background:#fff;}"
              "th,td{border:1px solid #ccc;padding:6px 10px;}"
              "input,select{padding:8px;margin:6px;width:85%;max-width:320px;font-size:16px;border-radius:6px;border:1px solid #ccc;}"
              "button{padding:8px 16px;margin:4px;border:none;border-radius:6px;font-size:16px;}"
              ".edit{background:#FFC107;} .hapus{background:#F44336;color:white;} .tambah{background:#2196F3;color:white;}"
              "hr{border:none;border-top:2px solid #ddd;margin:25px 10px;}"
              ".section{background:#ffffff;padding:15px;margin:15px auto;border-radius:10px;max-width:420px;box-shadow:0 2px 6px rgba(0,0,0,0.08);}"
              "h3{margin-top:5px;color:#333;}"
              "</style>"
              "<script>"
              "function cariNama(){"
              "var input=document.getElementById('search');"
              "var filter=input.value.toUpperCase();"
              "var table=document.getElementById('tabelUser');"
              "var tr=table.getElementsByTagName('tr');"
              "for(var i=1;i<tr.length;i++){"
              "var td=tr[i].getElementsByTagName('td')[1];"
              "if(td){"
              "var txt=td.textContent||td.innerText;"
              "tr[i].style.display=txt.toUpperCase().indexOf(filter)>-1?'':'none';"
              "}"
              "}"
              "}"
              "</script>"
              "</head><body>";

  html += "<div class='section'>";
  html += "<h3>Dashboard Absensi</h3>";
  html += "<a href='/laporan'><button style='background:#4CAF50;color:white;'>Lihat Laporan Absensi</button></a><br>";
  html += "<a href='/logout'><button style='background:#555;color:white;'>Logout</button></a>";
  html += "</div><hr>";



  // ===== FORM TAMBAH USER =====
  html += "<div class='section'>";
  html += "<h3>Tambah User Baru</h3>";
  html += "<form action='/tambah' method='GET'>";
  html += "<input name='nama' placeholder='Nama'><br>";
  html += "<select name='divisi'>";

  for (int i = 0; i < jumlahDivisi; i++) {
    html += "<option value='" + daftarDivisi[i] + "'>" + daftarDivisi[i] + "</option>";
  }

  html += "</select><br>";
  html += "<button class='tambah' type='submit'>Tambah User</button>";
  html += "</form>";

  html += "<p>Tempel kartu saat MODE DAFTAR sebelum menambahkan user</p>";
  html += "</div><hr>";

  // ===== SEARCH =====
  html += "<div class='section'>";
  html += "<h3>Cari Nama</h3>";
  html += "<input type='text' id='search' onkeyup='cariNama()' placeholder='Ketik nama...'>";
  html += "</div><hr>";
 
  // ===== TABEL USER =====
  html += "<table id='tabelUser'><tr><th>UID</th><th>Nama</th><th>Divisi</th><th>Aksi</th></tr>";

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();

    int p1 = line.indexOf(',');
    int p2 = line.indexOf(',', p1 + 1);

    if (p1 > 0 && p2 > 0) {
      String uid = line.substring(0, p1);
      String nama = line.substring(p1 + 1, p2);
      String div = line.substring(p2 + 1);
      String uidURL = urlEncode(uid);

      html += "<tr><td>" + uid + "</td><td>" + nama + "</td><td>" + div + "</td><td>"
              "<a href='/edit?uid=" + uidURL + "'><button class='edit'>Edit</button></a>"
              "<a href='/hapus?uid=" + uidURL + "'><button class='hapus'>Hapus</button></a>"
              "</td></tr>";
    }
  }

  html += "</table></body></html>";

  file.close();
  server.send(200, "text/html", html);
}


void handleLogout() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");

  server.send(200, "text/html",
    "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'></head>"
    "<body style='font-family:Arial;text-align:center;margin-top:40px;'>"
    "<h2>Anda sudah logout</h2>"
    "<p>Klik tombol di bawah untuk login kembali</p>"
    "<button onclick=\"window.location.href='http://:logout@' + window.location.host + '/';\" "
    "style='padding:10px 20px;font-size:16px;'>Kembali ke Login</button>"
    "</body></html>");
}

void handleHapus() {
  if (!isAuthenticated()) return;
  if (!server.hasArg("uid")) {
    server.send(200, "text/plain", "UID tidak ditemukan");
    return;
  }

  String uidHapus = server.arg("uid");
  uidHapus.replace("%20", " ");  // decode spasi dari URL

  File lama = SD.open("/kartu.csv");
  File baru = SD.open("/temp.csv", FILE_WRITE);

  if (!lama || !baru) {
    server.send(500, "text/plain", "Gagal buka file");
    return;
  }

  baru.println("UID,Nama,Divisi");
  lama.readStringUntil('\n');  // skip header

  while (lama.available()) {
    String line = lama.readStringUntil('\n');
    line.trim();

// ===== Ambil Nama dari CSV =====
int koma1 = line.indexOf(',');
if (koma1 < 0) {
  baru.println(line);
  continue;
}

String namaCSV = line.substring(0, koma1);
String namaLower = namaCSV;
namaLower.toLowerCase();


    int koma = line.indexOf(',');
    if (koma > 0) {
      String uidFile = line.substring(0, koma);
      if (uidFile != uidHapus) {
        baru.println(line);
      }
    }
  }

  lama.close();
  baru.close();

  SD.remove("/kartu.csv");
  SD.rename("/temp.csv", "/kartu.csv");

  // 🔁 WAJIB: kirim browser balik ke dashboard
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleEditForm() {
  if (!isAuthenticated()) return;
  if (!server.hasArg("uid")) return;

  String uidEdit = server.arg("uid");
  String nama, divisi;

  if (!cariKartu(uidEdit, nama, divisi)) return;

  String daftarDivisi[] = {
    "IT","RESTO","HK","FB","HRD","ACCOUNTING",
    "FO","KITCHEN","ENG","PURCAS","GM","ASS GM","SALES"
  };
  int jumlahDivisi = sizeof(daftarDivisi) / sizeof(daftarDivisi[0]);

  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<style>"
                "body{font-family:Arial;text-align:center;background:#f2f2f2;font-size:16px;}"
                ".box{background:white;padding:20px;margin:20px auto;border-radius:8px;max-width:350px;}"
                "input,select{padding:8px;margin:6px;width:90%;font-size:1em;}"
                "button{padding:10px 16px;margin-top:10px;border:none;border-radius:6px;font-size:1em;background:#2196F3;color:white;}"
                "</style>"
                "</head><body>"

                "<div class='box'>"
                "<h3>Edit User</h3>"

                "<form action='/editsave' method='GET'>"
                "<input type='hidden' name='uid' value='" + uidEdit + "'>"

                "Nama:<br><input name='nama' value='" + nama + "'><br>"

                "Divisi:<br><select name='divisi'>";

  for (int i = 0; i < jumlahDivisi; i++) {
    if (daftarDivisi[i] == divisi)
      html += "<option selected value='" + daftarDivisi[i] + "'>" + daftarDivisi[i] + "</option>";
    else
      html += "<option value='" + daftarDivisi[i] + "'>" + daftarDivisi[i] + "</option>";
  }

  html += "</select><br>"

          "<button type='submit'>UPDATE DATA</button>"
          "</form>"

          "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleEditSave() {
  if (!isAuthenticated()) return;

  String uidEdit = server.arg("uid");
  String namaBaru = server.arg("nama");
  String divBaru = server.arg("divisi");

  File lama = SD.open("/kartu.csv");
  File baru = SD.open("/temp.csv", FILE_WRITE);

  baru.println("UID,Nama,Divisi");
  lama.readStringUntil('\n');

  while (lama.available()) {
    String line = lama.readStringUntil('\n');
    line.trim();
    int koma = line.indexOf(',');
    String uidFile = line.substring(0, koma);

    if (uidFile == uidEdit) {
      baru.println(uidEdit + "," + namaBaru + "," + divBaru);
    } else {
      baru.println(line);
    }
  }

  lama.close();
  baru.close();

  SD.remove("/kartu.csv");
  SD.rename("/temp.csv", "/kartu.csv");

  server.sendHeader("Location", "/");
  server.send(303);
}

// ================= TIME =================
void initTime() {
  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.google.com");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) delay(500);
}

void tampilLogoBoot() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  int16_t x1, y1;
  uint16_t w, h;

  // ===== Judul ABSENSI (besar) =====
  display.setTextSize(2);
  String title = "ABSENSI";
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  int xTitle = (128 - w) / 2;
  int yTitle = 18;  // agak ke atas biar seimbang
  display.setCursor(xTitle, yTitle);
  display.println(title);

  // ===== Sub teks Starting =====
  display.setTextSize(1);
  String sub = "Starting...";
  display.getTextBounds(sub, 0, 0, &x1, &y1, &w, &h);
  int xSub = (128 - w) / 2;
  int ySub = 45;
  display.setCursor(xSub, ySub);
  display.println(sub);

  display.display();
}

void tampilStatusAkhir() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  display.setTextSize(1);
  display.setCursor(15, 0);
  display.println("STATUS SISTEM");

  display.setCursor(0, 16);
  display.print("WIFI : ");
  display.println(wifiOK ? "OK" : "FAIL");

  display.setCursor(0, 28);
  display.print("SD   : ");
  display.println(sdOK ? "OK" : "FAIL");

  // ===== TAMPILKAN IP ADDRESS =====
  if (wifiOK) {
    display.setCursor(0, 40);
    display.print("IP: ");
    display.println(WiFi.localIP());
  }

  display.setCursor(10, 54);
  if (wifiOK && sdOK) {
    display.println("Memulai...");
  } else if (!wifiOK && sdOK) {
    display.println("Mode Offline");
  } else if (!sdOK) {
    display.println("SD Error!");
  }

  display.display();
}

void handleTambah() {
  if (!isAuthenticated()) return;

  if (server.hasArg("nama") && server.hasArg("divisi")) {

    File fuid = SD.open("/pending.txt");
    if (!fuid) {
      server.send(200, "text/plain", "Scan kartu dulu!");
      return;
    }

    String uid = fuid.readStringUntil('\n');
    uid.trim();
    fuid.close();
    SD.remove("/pending.txt");

    // 🔴 CEK DUPLIKAT UID
    if (uidSudahTerdaftar(uid)) {
      server.send(200, "text/plain", "Kartu sudah terdaftar!");
      return;
    }

    // ✅ SIMPAN USER BARU
    File file = SD.open("/kartu.csv", FILE_APPEND);
    if (!file) {
      server.send(500, "text/plain", "Gagal buka kartu.csv");
      return;
    }

    file.print(uid);
    file.print(",");
    file.print(server.arg("nama"));
    file.print(",");
    file.println(server.arg("divisi"));
    file.close();

    // 🔁 REDIRECT BALIK KE DASHBOARD
    server.sendHeader("Location", "/");
    server.send(303);
  } else {
    server.send(400, "text/plain", "Parameter kurang");
  }
}

void handleHapusAbsensi();

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_TOMBOL, INPUT_PULLUP);

  Wire.begin(21, 22);
  display.begin(0x3C, true);
  display.clearDisplay();
  display.display();
  delay(50);
  tampilLogoBoot();

  // ================= WIFI =================
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    retry++;
  }

  wifiOK = (WiFi.status() == WL_CONNECTED);
  if (wifiOK) initTime();

  // ================= SD CARD =================
  spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  sdOK = SD.begin(SD_CS, spiSD);

  if (sdOK) {
    if (!SD.exists("/kartu.csv")) {
      File f = SD.open("/kartu.csv", FILE_WRITE);
      f.println("UID,Nama,Divisi");
      f.close();
    }
    SD.remove("/pending.txt");
  }

  // ================= STATUS OLED =================
  while (!wifiOK || !sdOK) {
    tampilStatusSistem();
    delay(500);
  }
  tampilStatusAkhir();
  delay(2000);

  // ================= RFID =================
  SPI.begin(18, 19, 23, 5);
  mfrc522.PCD_Init();

  // ================= ROUTE WEB =================
  server.on("/", handleRoot);
  server.on("/tambah", handleTambah);
  server.on("/hapus", handleHapus);
  server.on("/edit", handleEditForm);
  server.on("/editsave", handleEditSave);
  server.on("/logout", handleLogout);
  server.on("/hapusabsen", handleHapusAbsensi);
  server.on("/laporan", handleLaporan);
  server.on("/downloadcsv", handleDownloadCSV);

  // 🔥 cegah browser minta favicon
  server.on("/favicon.ico", HTTP_GET, []() {
    server.send(204);
  });

  // ================= START SERVER =================
  server.begin();

  sistemSiap = true;
}

void handleHapusAbsensi() {
if (!isAuthenticated()) return;

String namaFilter = server.arg("nama");
namaFilter.toLowerCase();


  if (!SD.exists("/absensi.csv")) {
    server.send(200, "text/plain", "File absensi.csv tidak ditemukan");
    return;
  }

  String tgl = server.arg("tgl");
  String bln = server.arg("bln");
  String thn = server.arg("thn");
  String jamMulai   = server.arg("jam_mulai");   // format "HH:MM"
  String jamSelesai = server.arg("jam_selesai"); // format "HH:MM"


  // 🔒 Pengaman supaya tidak hapus semua data
  if (tgl=="" && bln=="" && thn=="" && jamMulai=="" && jamSelesai=="" && namaFilter=="") {
    server.send(200, "text/html",
      "<html><body style='font-family:Arial;text-align:center;'>"
      "<h2>Filter kosong! Tidak ada data dihapus.</h2>"
      "<a href='/'><button>Kembali</button></a>"
      "</body></html>");
    return;
  }

  File lama = SD.open("/absensi.csv");
  File baru = SD.open("/tempabsen.csv", FILE_WRITE);

  lama.readStringUntil('\n');   // LEWATI HEADER
baru.println("Nama,Divisi,Status,Waktu");

  if (!lama || !baru) {
    server.send(500, "text/plain", "Gagal membuka file");
    return;
  }

  int terhapus = 0;

  while (lama.available()) {
    String line = lama.readStringUntil('\n');
    line.trim();


    // ===== Ambil Nama dari CSV =====
int koma1 = line.indexOf(',');
if (koma1 < 0) {
  baru.println(line);
  continue;
}

String namaCSV = line.substring(0, koma1);
String namaLower = namaCSV;
namaLower.toLowerCase();


    int komaTerakhir = line.lastIndexOf(',');
    if (komaTerakhir < 0) {
      baru.println(line);
      continue;
    }

    String waktu = line.substring(komaTerakhir + 1);
    waktu.trim();

    int d  = waktu.substring(0, 2).toInt();
    int m  = waktu.substring(3, 5).toInt();
    int y  = waktu.substring(6, 10).toInt();
    int h  = waktu.substring(11, 13).toInt();
    int mn = waktu.substring(14, 16).toInt();
    int mulaiMenit = -1;
    int selesaiMenit = -1;

    if (jamMulai != "") {
      int jm = jamMulai.substring(0,2).toInt();
      int mm = jamMulai.substring(3,5).toInt();
      mulaiMenit = jm * 60 + mm;
    }

    if (jamSelesai != "") {
      int js = jamSelesai.substring(0,2).toInt();
      int ms = jamSelesai.substring(3,5).toInt();
      selesaiMenit = js * 60 + ms;
    }

    int totalMenit = h * 60 + mn;

    bool cocok = true;
    if (namaFilter != "" && namaLower.indexOf(namaFilter) == -1) cocok = false;

    if (tgl != "" && d != tgl.toInt()) cocok = false;
    if (bln != "" && m != bln.toInt()) cocok = false;
    if (thn != "" && y != thn.toInt()) cocok = false;
    if (mulaiMenit != -1 && totalMenit < mulaiMenit) cocok = false;
    if (selesaiMenit != -1 && totalMenit > selesaiMenit) cocok = false;


    if (cocok) {
      terhapus++;
    } else {
      baru.println(line);
    }
  }

  lama.close();
  baru.close();

  SD.remove("/absensi.csv");
  SD.rename("/tempabsen.csv", "/absensi.csv");

  server.send(200, "text/html",
    "<html><body style='font-family:Arial;text-align:center;'>"
    "<h2>" + String(terhapus) + " data absensi dihapus</h2>"
    "<a href='/'><button>Kembali</button></a>"
    "</body></html>");
}

void handleLaporan() {
  if (!isAuthenticated()) return;

  String namaFilter = server.arg("nama");
  namaFilter.toLowerCase();

  String tanggal = server.arg("tanggal");
  String jamMulai = server.arg("jam_mulai");
  String jamSelesai = server.arg("jam_selesai");

  File file = SD.open("/absensi.csv");
  if (!file) {
    server.send(200, "text/plain", "File absensi.csv tidak ditemukan");
    return;
  }

  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<style>"
                "body{font-family:Arial;background:#f2f2f2;text-align:center;}"
                "table{border-collapse:collapse;margin:auto;background:#fff;font-size:14px;}"
                "th,td{border:1px solid #ccc;padding:6px 10px;}"
                "th{background:#2196F3;color:white;}"
                "input,button{padding:6px;margin:4px;font-size:14px;}"
                ".box{background:#fff;padding:15px;margin:15px auto;max-width:420px;border-radius:8px;}"
                "</style></head><body>";

  // ================= FILTER =================
  html += "<div class='box'><h3>Filter Laporan</h3>";
  html += "<form method='GET' action='/laporan'>";

  html += "Nama:<br><input list='namaList' name='nama' value='" + server.arg("nama") + "'>";
  html += "<datalist id='namaList'>";

  File fileNama = SD.open("/absensi.csv");
  fileNama.readStringUntil('\n');
  while (fileNama.available()) {
    String line = fileNama.readStringUntil('\n');
    int p = line.indexOf(',');
    if (p > 0) {
      String nama = line.substring(0, p);
      html += "<option value='" + nama + "'>";
    }
  }
  fileNama.close();
  html += "</datalist><br>";

  html += "Tanggal:<br><input type='date' name='tanggal' value='" + tanggal + "'><br>";
  html += "Dari Jam:<br><input type='time' name='jam_mulai' value='" + jamMulai + "'><br>";
  html += "Sampai Jam:<br><input type='time' name='jam_selesai' value='" + jamSelesai + "'><br>";

  html += "<button type='submit'>Terapkan Filter</button>";
  html += "<button type='button' onclick='resetFilter()' style='background:#f44336;color:white;'>Reset</button>";
  html += "</form></div>";

  // ================= HAPUS DATA =================
  html += "<div class='box'>";
  html += "<h3>Hapus Data Absensi</h3>";
  html += "<form action='/hapusabsen' method='GET'>";

  html += "Nama (opsional):<br><input list='namaList' name='nama'><br>";
  html += "Tanggal:<br><input type='date' id='tanggalHapus'><br>";
  html += "Dari Jam:<br><input type='time' name='jam_mulai'><br>";
  html += "Sampai Jam:<br><input type='time' name='jam_selesai'><br>";

  html += "<input type='hidden' name='tgl'>";
  html += "<input type='hidden' name='bln'>";
  html += "<input type='hidden' name='thn'>";

  html += "<button style='background:#f44336;color:white;' type='submit' onclick='isiTanggalHapus()'>Hapus Data</button>";
  html += "</form></div>";

  // ================= TABEL =================
  html += "<h3>Hasil Laporan</h3>";
  html += "<a href='/downloadcsv'><button>Download Semua CSV</button></a><br><br>";
  html += "<table><tr><th>Nama</th><th>Divisi</th><th>Status</th><th>Waktu</th></tr>";

  file.readStringUntil('\n');

  int jamMulaiMenit = -1;
  int jamSelesaiMenit = -1;

  if (jamMulai != "") jamMulaiMenit = jamMulai.substring(0,2).toInt()*60 + jamMulai.substring(3,5).toInt();
  if (jamSelesai != "") jamSelesaiMenit = jamSelesai.substring(0,2).toInt()*60 + jamSelesai.substring(3,5).toInt();

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();

    int p1 = line.indexOf(',');
    int p2 = line.indexOf(',', p1 + 1);
    int p3 = line.indexOf(',', p2 + 1);

    String nama = line.substring(0, p1);
    String div = line.substring(p1 + 1, p2);
    String status = line.substring(p2 + 1, p3);
    String waktu = line.substring(p3 + 1);

    String namaLower = nama;
    namaLower.toLowerCase();

    bool cocok = true;

    if (namaFilter != "" && namaLower.indexOf(namaFilter) == -1) cocok = false;

    if (tanggal != "") {
      String tglCsv = waktu.substring(0,2) + "-" + waktu.substring(3,5) + "-" + waktu.substring(6,10);
      String tglFilter = tanggal.substring(8,10) + "-" + tanggal.substring(5,7) + "-" + tanggal.substring(0,4);
      if (tglCsv != tglFilter) cocok = false;
    }

    int totalMenit = waktu.substring(11,13).toInt()*60 + waktu.substring(14,16).toInt();
    if (jamMulaiMenit != -1 && totalMenit < jamMulaiMenit) cocok = false;
    if (jamSelesaiMenit != -1 && totalMenit > jamSelesaiMenit) cocok = false;

    if (cocok) html += "<tr><td>"+nama+"</td><td>"+div+"</td><td>"+status+"</td><td>"+waktu+"</td></tr>";
  }

  html += "</table><br><a href='/'><button>Kembali</button></a>";

  // ===== SCRIPT =====
  html += "<script>"
          "function resetFilter(){window.location.href='/laporan';}"
          "function isiTanggalHapus(){"
          "var t=document.getElementById('tanggalHapus').value;"
          "if(t!=''){var p=t.split('-');"
          "document.getElementsByName('thn')[0].value=p[0];"
          "document.getElementsByName('bln')[0].value=parseInt(p[1]);"
          "document.getElementsByName('tgl')[0].value=parseInt(p[2]);}}"
          "</script>";

  html += "</body></html>";

  file.close();
  server.send(200, "text/html", html);
}


void handleDownloadCSV() {
  if (!isAuthenticated()) return;

  File file = SD.open("/absensi.csv");
  if (!file) {
    server.send(200, "text/plain", "File tidak ditemukan");
    return;
  }

  server.sendHeader("Content-Type", "text/csv");
  server.sendHeader("Content-Disposition", "attachment; filename=laporan_absensi.csv");
  server.streamFile(file, "text/csv");
  file.close();
}


// ================= TAMPILAN ABSENSI =================
void tampilAbsensi(String status, String nama, String divisi, String waktu) {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  int16_t x1, y1;
  uint16_t w, h;

  // ===== STATUS (kecil, tengah atas) =====
  display.setTextSize(1);
  display.getTextBounds(status, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 0);
  display.println(status);

  // ===== NAMA (besar, tengah layar) =====
  display.setTextSize(2);
  display.getTextBounds(nama, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 15);
  display.println(nama);

  // ===== DIVISI =====
  display.setTextSize(1);
  String divText = "" + divisi;
  display.getTextBounds(divText, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 38);
  display.println(divText);

  // ===== WAKTU (kecil, bawah) =====
  display.setTextSize(1);
  display.getTextBounds(waktu, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 54);
  display.println(waktu);

  display.display();
}


// ================= LOOP =================
void loop() {
  if (!wifiOK || !sdOK) {
    tampilStatusSistem();
    return;
  }
  // ================= CEK WIFI BACKGROUND =================
  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 1000) {
    lastWifiCheck = millis();

    if (WiFi.status() == WL_CONNECTED && !wifiOK) {
      wifiOK = true;
      initTime();
    }
  }

  server.handleClient();

  if (!sistemSiap) return;

  bacaTombolMode();

  // ================= MODE DAFTAR KARTU =================
  static unsigned long lastDaftarScan = 0;

  if (modeDaftar) {

    if (millis() - lastDaftarScan < 3000) return;  // jeda anti dobel scan

    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {

      String uidBaru = uidToString(&mfrc522.uid);

      // 🔴 CEK APAKAH SUDAH TERDAFTAR
      if (uidSudahTerdaftar(uidBaru)) {
        tampilOLED("KARTU SUDAH", "TERDAFTAR", "");
        delay(2000);

        modeDaftar = false;  // keluar dari mode daftar
        tampilJamStandby();  // kembali ke layar absensi
        lastDaftarScan = millis();
        return;
      }

      // 🟢 KARTU BARU
      tampilOLED("KARTU BARU", uidBaru, "Buka HP utk daftar");

      File f = SD.open("/pending.txt", FILE_WRITE);
      if (f) {
        f.println(uidBaru);
        f.close();
      }

      lastDaftarScan = millis();
      delay(2000);
    }
    return;  // hentikan absensi saat mode daftar
  }

  static unsigned long lastDisplay = 0;
  if (millis() - lastDisplay > 1000) {
    tampilJamStandby();
    lastDisplay = millis();
  }

  // ================= RFID =================
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  String uid = uidToString(&mfrc522.uid);
  String waktu = getTimeString();


  String nama, divisi;
  bool dikenal = cariKartu(uid, nama, divisi);

  if (dikenal) {
    tampilAbsensi(
      modeMasuk ? "ABSEN MASUK" : "ABSEN PULANG",
      nama,
      divisi,
      waktu);

    simpanCSV(nama, divisi, waktu);

    unsigned long tampilMulai = millis();
    while (millis() - tampilMulai < 1500) {
      bacaTombolMode();
      server.handleClient();
    }
  } else {
    tampilOLED("KARTU TIDAK", "DIKENAL", waktu);
    delay(1500);
  }
}
