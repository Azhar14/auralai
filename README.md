# **💸 Proyek Deteksi Uang Rupiah dengan Luckfox Pico & DFPlayer**

Dokumentasi ini merangkum langkah-langkah, cara kerja, dan struktur dari sistem deteksi nominal uang Rupiah menggunakan AI (YOLOv5) yang di-deploy ke dalam *board* Luckfox Pico (RV1106) dan dihubungkan ke modul suara DFPlayer Mini.

## **1\. ⚙️ Cara Kerja Sistem (Architecture Flow)**

Sistem ini menggabungkan *Computer Vision* (AI) dengan sistem kendali *Embedded Linux* secara *real-time*. Berikut adalah alur kerjanya:

1. **Input Gambar (CSI Camera):** Program Linux mematikan proses bawaan (rkipc) agar kamera CSI bebas digunakan. Modul OpenCV Mobile menangkap *frame* gambar secara *real-time* lewat protokol V4L2.  
2. **Pemrosesan AI (NPU RV1106):** Gambar di-*resize* ke 640x640 piksel dan dikirim ke RKNPU (Neural Processing Unit). Model uang.rknn yang sudah dikuantisasi ke INT8 (agar kompatibel dan cepat di RV1106) memproses gambar untuk mencari fitur uang.  
3. **Post-Processing (CPU):** Hasil tebakan AI berupa vektor (tensor) diubah menjadi koordinat kotak (*bounding box*) dan nilai probabilitas (Conf). Kelas objek (ID 0 \- 6\) dipetakan ke dalam 7 nominal Rupiah.  
4. **Trigger Audio (UART & DFPlayer):** \- Jika probabilitas memenuhi batas (threshold), program C++ mengambil cls\_id (0 sampai 6).  
   * Program mengirimkan perintah Hexadecimal standar melalui Serial UART (/dev/ttyS3) ke DFPlayer Mini.  
   * **Cooldown System:** Terdapat jeda (delay) 3 detik menggunakan pustaka time.h agar suara DFPlayer tidak saling menimpa/spam saat uang ditahan di depan kamera.

## **2\. 📂 Struktur Direktori Proyek**

Proyek ini terbagi menjadi dua lingkungan kerja utama: **Ubuntu WSL** (untuk kompilasi) dan **Luckfox OS** (untuk eksekusi).

### **Lingkungan WSL (PC/Laptop)**

\~ (Home Directory)  
├── luckfox\_toolchain/                    \# Compiler khusus arsitektur ARM (RV1106)  
│   └── tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/...  
│  
└── luckfox\_pico\_rknn\_example/            \# Source Code Induk bawaan Rockchip/Luckfox  
    ├── build.sh                          \# Skrip untuk kompilasi (CMake)  
    ├── install/                          \# Folder output hasil kompilasi (Executable)  
    └── example/  
        └── luckfox\_pico\_yolov5/          \# Folder Proyek Utama Kita  
            ├── include/  
            │   └── postprocess.h         \# Header file (Setting kelas diubah ke 7\)  
            ├── model/  
            │   ├── uang.rknn             \# Model AI hasil konversi  
            │   └── uang\_labels.txt       \# Daftar 7 kelas uang Rupiah  
            └── src/  
                ├── main.cc               \# KODE UTAMA (Kamera \+ AI \+ Audio UART)  
                ├── postprocess.cc        \# Algoritma konversi tensor ke bounding box  
                └── yolov5.cc             \# Fungsi inisialisasi NPU

### **Lingkungan Luckfox Pico (Board)**

/root/luckfox\_pico\_yolov5\_demo/  
├── luckfox\_pico\_yolov5                   \# File Executable hasil kompilasi dari WSL  
└── model/  
    ├── uang.rknn                         \# File "Otak" AI (INT8)  
    └── uang\_labels.txt                   \# Label nama objek (disamarkan sbg coco\_80\_labels\_list.txt)

## **3\. 🛠️ Langkah-Langkah Pengerjaan (Step-by-Step)**

### **Tahap 1: Persiapan Model AI (Di PC/Windows)**

1. Labeling dan *training* dataset uang Rupiah menggunakan **Roboflow** (YOLOv5).  
2. Ekspor model ke format **ONNX**.  
3. Gunakan **RKNN-Toolkit2** di WSL/Linux untuk mengonversi ONNX menjadi RKNN.  
   * *Penting:* Harus dilakukan Kuantisasi (Quantization) tipe INT8 menggunakan gambar sampel (dataset.txt), karena chip RV1106 hanya menerima input/output INT8.  
   * Hasil akhirnya adalah file uang.rknn.

### **Tahap 2: Setup Toolchain & Source Code (Di Ubuntu WSL)**

1. Menarik (*clone*) Toolchain *Cross-Compiler* via git sparse-checkout dari repository GitHub resmi Luckfox.  
2. Menarik *Source Code* OpenCV dan RKNN C++ resmi dari repository Luckfox (luckfox\_pico\_rknn\_example).  
3. Mengatur variabel *environment* agar Linux tahu letak compiler-nya:  
   export LUCKFOX\_SDK\_PATH=\~/luckfox\_toolchain

### **Tahap 3: Modifikasi Bahasa C++**

1. **Mengubah Jumlah Kelas:** Mengubah \#define OBJ\_CLASS\_NUM 80 menjadi 7 di file include/postprocess.h.  
2. **Membuat Label Baru:** Membuat file uang\_labels.txt berisi 7 baris urutan nominal uang.  
3. **Suntik Kode UART (DFPlayer) di main.cc:**  
   * Menambahkan \#include \<termios.h\>.  
   * Menambahkan fungsi init\_uart() untuk membuka port /dev/ttyS3 dengan baudrate 9600\.  
   * Menambahkan fungsi play\_track(fd, track\_num) yang merakit *array hex* 10-byte spesifik untuk DFPlayer (mulai dengan 0x7E, diakhiri 0xEF).  
   * Menambahkan filter *delay* 3 detik menggunakan time(NULL).

### **Tahap 4: Kompilasi (Build)**

Mengubah kode sumber menjadi aplikasi yang bisa dijalankan di Luckfox:

cd \~/luckfox\_pico\_rknn\_example  
./build.sh TARGET\_SOC=rv1106

*(Pilih Opsi 1: uclibc, lalu Opsi 3: luckfox\_pico\_yolov5).*

### **Tahap 5: Pemindahan & Eksekusi (Deploy)**

1. Ekspor file *executable* dari WSL ke OS Windows menggunakan perintah cp /mnt/c/....  
2. Dorong file *executable* dan *model rknn* ke dalam Luckfox menggunakan Command Prompt Windows:  
   adb push luckfox\_pico\_yolov5 /root/luckfox\_pico\_yolov5\_demo/  
   adb push uang.rknn /root/luckfox\_pico\_yolov5\_demo/model/

3. Masuk ke *shell* Luckfox (adb shell), beri izin jalan (chmod \+x), dan eksekusi:  
   ./luckfox\_pico\_yolov5 model/uang.rknn

Sumber: 
[https://github.com/LuckfoxTECH/luckfox\_pico\_rknn\_example](https://github.com/LuckfoxTECH/luckfox_pico_rknn_example)

[https://universe.roboflow.com/project/indonesia-rupiah-detection/dataset/3](https://universe.roboflow.com/project/indonesia-rupiah-detection/dataset/3)

