# Kelentang Sasak Automation

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

Sistem otomasi berbasis **ESP32** untuk instrumen perkusi tradisional **Kelentang Sasak**. Proyek ini memungkinkan pemutaran file MIDI untuk mengendalikan 10 aktuator selenoid secara presisi, menggabungkan kearifan budaya dengan teknologi modern.

---

## 🛠️ Tentang Proyek
Proyek ini dikembangkan secara mandiri dari sisi perangkat keras (*hardware*) hingga perangkat lunak (*software*). Tujuannya adalah menciptakan alat yang mampu memutar musik tradisional secara otomatis, menjembatani teknologi *embedded* dengan seni musik tradisional Sasak.

## ✨ Fitur Utama
*   **Presisi MIDI:** Memutar file MIDI dari SD Card dengan sinkronisasi 10 aktuator selenoid.
*   **Multitasking Dual-Core:** Menggunakan FreeRTOS (ESP32) untuk memisahkan tugas pemrosesan MIDI yang kritis dan tugas sistem lainnya agar berjalan lancar tanpa *lag*.
*   **Web Dashboard:** Antarmuka berbasis web untuk manajemen file MIDI, konfigurasi waktu aktuator, pemetaan selenoid, dan pengaturan WiFi.
*   **Konektivitas Fleksibel:** Mendukung mode *Access Point* (AP) untuk konfigurasi mandiri dan *Station* (STA) untuk terhubung ke jaringan lokal.
*   **Sistem Notifikasi:** Audio feedback via *active buzzer* untuk status sistem (startup, input tombol, mode WiFi, dll).

## 🏗️ Arsitektur Sistem
*   **Core 1 (MIDI Task):** Pemrosesan MIDI *high-precision* untuk *timing* pemukulan instrumen yang tepat.
*   **Core 0 (System Task):** Menangani Web Server, manajemen WiFi, input tombol, dan notifikasi buzzer.

## 📂 Struktur Repositori
```text
/
├── main/              # Source code (Arduino/ESP32)
├── hardware/          # Skematik, Layout PCB, & Gerber files
├── docs/              # Dokumentasi fisik, foto perakitan, & wiring diagram
└── README.md
```

## 🔌 Hardware
Proyek ini dirancang menggunakan:
*   **Controller:** ESP32
*   **Aktuator:** 10 Selenoid (dengan driver MOSFET)
*   **Penyimpanan:** Modul SD Card
*   **Input/UI:** Tombol fisik & Display I2C

## 🚀 Cara Penggunaan
1.  **WiFi Mode:** Tahan tombol `MODE` selama 2 detik untuk masuk ke mode AP (IP: `192.168.4.1`), atau 5 detik untuk mode STA.
2.  **Web Dashboard:** Akses dasbor melalui peramban untuk unggah MIDI, konfigurasi aktuator, dan update *firmware*.
3.  **Buzzer Feedback:** Memudahkan pemantauan status sistem melalui pola bunyi yang berbeda (Startup, Button press, Mode changes, dll).

## 🤝 Kontribusi
Proyek ini bersifat *Open Source*. Saya sangat terbuka bagi siapa saja yang ingin mengembangkan sistem ini, baik dari sisi mekanik, elektronik, maupun optimasi kode.
*   Silakan buat *Pull Request* atau *Issue* jika Anda memiliki ide pengembangan.
*   Pastikan untuk memeriksa dokumentasi di folder `/hardware` dan `/docs` sebelum melakukan modifikasi pada desain.

## ⚖️ Lisensi
Proyek ini dilisensikan di bawah [MIT License](LICENSE).
