# 1. Bedah Arsitektur Hardware LG HT306 & Panduan Modifikasi Injeksi I2S

🌐 **Bahasa:** [English](./01_Hardware_Architecture_and_Mod_Guide.md) | **Bahasa Indonesia**

[⬅️ Kembali ke README Utama](../README.id.md)

Dokumen ini menjelaskan arsitektur internal mainboard **LG HT306 (`HT306SU` / `HT306SF` / `HT306PD`)**, anatomi 5 resistor seri SMD tempat penyuntikan sinyal I2S dari **`Waveshare RP2350-Zero`**, serta alasan teknis mengapa mode **`AUX`** dan **EQ Bawaan LG HT306 (`2 to 5.1 PLII`, `Bass Blast`, `Natural`)** tetap berfungsi normal tanpa mencampur/merusak suara 5.1 murni dari PC.

---

## 1.1. Rantai Chip Audio di Mainboard LG HT306

Berdasarkan *Service Manual* LG HT306, pemrosesan sinyal di dalam mainboard terbagi ke dalam 3 IC utama:

| Kode IC | Nama Komponen | Fungsi Asli di LG HT306 | Peran dalam Modifikasi Ini |
| :--- | :--- | :--- | :--- |
| **`IC501`** | **MediaTek `MT1389MH`** | SoC DVD Player, Dekoder MPEG/AC-3 internal, Upmixer *Dolby Pro Logic II*, & Mikrokontroler Sistem | Tetap bertugas menginisialisasi `IC602` saat menyala serta mengirim resep **EQ & Master Volume** melalui jalur **`I2C` (`SDA`/`SCL`)**. |
| **`IC602`** | **Pulsus `PS9850`** | 8-Channel Digital Audio PWM Processor (`192 kHz / 24-bit`, internal `ASRC 13–192 kHz`, **69-Band Biquad EQ**, Crystal `19,2 MHz`) | **Titik Injeksi Utama:** Menerima sinyal digital `BCK`, `LRCK`, `ADATA0`, `ADATA1`, `ADATA2` langsung dari `Waveshare RP2350-Zero`. |
| **`IC701` / `IC702`** | **TI `TAS5352`** | 6-Channel High-Power Class-D MOSFET Power Stage (`330W RMS Total`) | Menguatkan sinyal PWM dari `IC602` langsung ke 6 speaker fisik HT306 (`FL, FR, C, Sub, SL, SR`). |

---

## 1.2. Anatomi 5 Resistor Seri SMD Antara `MT1389MH` dan `Pulsus PS9850` (*No Trace Cutting*)

Pada mainboard LG HT306, jalur sinyal digital I2S antara kaki output **`IC501 (MediaTek MT1389MH)`** dan kaki input **`IC602 (Pulsus PS9850)`** hanya dibatasi oleh **1 buah resistor SMD seri (*damping resistor*, `33 Ω – 100 Ω`) pada masing-masing jalur**. Tidak ada komponen aktif atau *level shifter* di tengahnya karena kedua chip sama-sama bekerja pada tegangan **`3.3V CMOS`**.

```mermaid
flowchart LR
    subgraph MT["IC501: MediaTek MT1389MH"]
        O1["Pin ABCK"]
        O2["Pin ALRCK"]
        O3["Pin ADATA0 (FL/FR)"]
        O4["Pin ADATA1 (SL/SR)"]
        O5["Pin ADATA2 (C/Sub)"]
        I2C_M["Pin SDA & SCL (I2C)"]
    end

    subgraph RES["5x Resistor Seri SMD (Diangkat / Disisipi)"]
        R1["R_BCK (Pad A ──[33Ω]── Pad B)"]
        R2["R_LRCK (Pad A ──[33Ω]── Pad B)"]
        R3["R_DATA0 (Pad A ──[33Ω]── Pad B)"]
        R4["R_DATA1 (Pad A ──[33Ω]── Pad B)"]
        R5["R_DATA2 (Pad A ──[33Ω]── Pad B)"]
    end

    subgraph PS["IC602: Pulsus PS9850"]
        I1["Pin BCK"]
        I2["Pin LRCK"]
        I3["Pin SDATA0"]
        I4["Pin SDATA1"]
        I5["Pin SDATA2"]
        I2C_P["Pin SDA & SCL (EQ & Vol)"]
    end

    O1 --> R1 --> I1
    O2 --> R2 --> I2
    O3 --> R3 --> I3
    O4 --> R4 --> I4
    O5 --> R5 --> I5
    I2C_M ==>|"TETAP UTUH (Jangan Dilepas)"| I2C_P
```

### Prosedur Injeksi Tanpa Potong Jalur (*No Trace Cutting*)
1. **Setiap resistor SMD memiliki 2 titik timah (`Pad A` & `Pad B`):**
   * **`Pad A`:** Mengarah ke kaki output `MediaTek MT1389MH` (`IC501`).
   * **`Pad B`:** Mengarah langsung ke kaki input `Pulsus PS9850` (`IC602`).
2. **Langkah Penyambungan:**
   * Lepas (atau angkat satu sisi kaki) **5 resistor SMD** khusus jalur `BCK`, `LRCK`, `ADATA0`, `ADATA1`, dan `ADATA2`. Mengangkat resistor ini otomatis memutus sinyal I2S dari `MT1389MH` sehingga tidak beradu tegangan (*bus contention*) dengan `RP2350-Zero`.
   * Solder kabel dari `GP2`..`GP6` **`Waveshare RP2350-Zero`** (melalui resistor seri `33 Ω` pengaman) tepat ke **`Pad B`** (sisi yang mengarah ke `IC602`).
   * *(Opsional)* Jika ingin mempertahankan fungsi pemutar DVD asli sekaligus PC USB, hubungkan `Pad A` ke input `A` IC Multiplexer **`74LVC157`**, `RP2350-Zero` ke input `B`, output `Y` ke `Pad B`, dan pin `Select` ke **`GP8`**.

| Kelompok Jalur | Nama Sinyal | Tindakan pada Resistor SMD di Mainboard |
| :--- | :--- | :--- |
| **5 Jalur Audio I2S** | `BCK`, `LRCK`, `ADATA0`, `ADATA1`, `ADATA2` | ✅ **Angkat ke-5 resistornya**, lalu solder jalur dari `RP2350-Zero` ke **Pad B (arah `IC602`)**. |
| **Jalur Kontrol EQ & Volume** | `SDA`, `SCL` (**`I2C`**), `RESET`, dan `MCLK` (`19,2 MHz` Crystal) | ⛔ **JANGAN DILEPAS / BIARKAN UTUH** agar `MT1389MH` tetap menyalakan `IC602` dan seluruh tombol **EQ & Volume** di remote asli LG tetap berfungsi. |

---

## 1.3. Mengapa Mode `AUX` & Settingan *"2 to 5.1 (`PLII`)"* / EQ Bawaan HT306 Tidak Bentrok dengan Sinyal 5.1 PC?

Di dalam mainboard LG HT306, tugas pemrosesan suara dibagi ke dalam **2 chip yang terpisah**:

```mermaid
flowchart TD
    subgraph MT["1. IC501: MediaTek MT1389MH (Otak DVD & Upmixer)"]
        AUX["Input RCA AUX (Kosong)"] --> ADC["ADC Stereo"]
        ADC --> PL2["DSP Upmixer 2 to 5.1\n(Dolby Pro Logic II / Matrix)"]
        PL2 --> OUT_MT["Output I2S MT1389MH\n(ADATA0, ADATA1, ADATA2)"]
        EQ_CMD["Pengirim Resep EQ & Unmute 6-Ch\nvia Jalur I2C (SDA / SCL)"]
    end

    subgraph CUT["5 Resistor Seri SMD (SUDAH DIANGKAT)"]
        OUT_MT -.-x|"TERPUTUS DI SINI!\n(Suara Upmix Palsu Dibuang)"| X["❌"]
    end

    subgraph PICO["Waveshare RP2350-Zero (PC JRiver)"]
        USB_IN["5.1 Murni 96kHz/24-bit\ndari JRiver Media Center"]
    end

    subgraph PS["2. IC602: Pulsus PS9850 (Prosesor EQ & PWM 6-Ch)"]
        IN_PS["Input I2S (SDATA0, SDATA1, SDATA2)\nPemetaan 1-ke-1 Murni (6 Channel)"]
        BIQUAD["69-Band Hardware Biquad EQ\n(Bass Blast, Natural, Pop, Volume)"]
        PWM["6-Channel PWM Output ➔ Speaker 5.1"]
        IN_PS --> BIQUAD --> PWM
    end

    USB_IN ==>|"Masuk Murni 6-Ch"| IN_PS
    EQ_CMD ==>|"Hanya Mengatur Kurva EQ & Volume"| BIQUAD
```

1. **Proses *Upmix 2 to 5.1 (`Pro Logic II`)* Berjalan di Dalam `MT1389MH` (`IC501`), Bukan di `PS9850` (`IC602`):**
   * Saat Anda memilih mode `AUX` dan menyalakan settingan *2 to 5.1*, chip `MT1389MH` memang mencoba mengolah input analog RCA (yang kosong) menjadi 6 channel pada kaki output I2S-nya.
   * **Namun karena ke-5 resistor seri I2S dari `MT1389MH` sudah diangkat**, sinyal *upmix* dari `MT1389MH` tersebut **berhenti di kaki resistor yang terangkat dan terbuang sepenuhnya**.
2. **`Pulsus PS9850` (`IC602`) Bekerja Secara *1-to-1 Discrete 6-Channel*:**
   * Chip `Pulsus PS9850` **tidak memiliki pencampur Pro Logic II**. Kaki inputnya (`SDATA0`, `SDATA1`, `SDATA2`) selalu meneruskan 6 kanal diskrit dari `RP2350-Zero` secara murni (`FL/FR`, `SL/SR`, `C/Sub`) melewati **69-Band Hardware Biquad EQ** menuju penguat daya `TAS5352`.

### Efek Tiap Mode EQ / Sound Mode di Remote LG Saat Posisi `AUX`

| Settingan EQ / Mode di Remote LG (Saat `AUX`) | Perintah yang Dikirim `MT1389MH` ke `PS9850` via `I2C` | Hasil Akhir pada Suara 5.1 dari `RP2350-Zero` |
| :--- | :--- | :--- |
| **`BASS BLAST` / `NATURAL` / `POP` / `CLASSIC` / `PLII MOVIE` (Mode 5.1)** | Membuka (*unmute*) seluruh **6 channel amplifier** (`FL, FR, C, Sub, SL, SR`) di `PS9850` + memuat kurva **69-Band EQ** bawaan LG. | ✅ **TERBAIK & NORMAL 100%:** Suara tetap **murni diskrit 5.1 dari JRiver** (tidak tercampur sama sekali), ditambah karakter bass & warna suara khas LG HT306. |
| **`BYPASS` (Mode 2.0 Stereo Murni)** | Pada sebagian firmware LG, mode `BYPASS` di posisi `AUX` memerintahkan `PS9850` via `I2C` untuk me-*mute* speaker `Center` & `Rear` (karena mengira input `AUX` hanya stereo 2.0). | ⚠️ **Hindari memilih `BYPASS` jika speaker belakang ikut mati.** Gunakan `NATURAL` atau `PLII MOVIE` agar ke-6 kanal di `PS9850` selalu aktif penuh. |
| **`SPEAKER LEVEL` & `VOLUME` (`+`/`-`)** | Mengatur penguatan (*gain* dB) per kanal (`FL, FR, C, SW, SL, SR`) di dalam register `PS9850`. | ✅ **Tetap berfungsi 100%** (level Subwoofer/Center dapat diatur dari remote asli LG maupun dari JRiver). |

---

## 1.4. Prosedur Uji Coba Meja Kerja Sementara (Injeksi Langsung Tanpa Buffer `SN74LVC244A`)

> [!WARNING]
> Prosedur di bawah ini **HANYA** diperuntukkan bagi tahap pengujian awal di meja kerja (*bench test / smoke test*) guna memverifikasi bahwa keenam kanal audio 5.1 berbunyi normal sebelum merakit modul buffer. Metode ini **TIDAK DIREKOMENDASIKAN** untuk pemakaian harian permanen di dalam casing tertutup.

Metode injeksi langsung tanpa IC buffer `SN74LVC244A` dapat dilakukan secara aman **DENGAN SYARAT** urutan penyalaan/pemadaman (*power sequencing*) dipatuhi secara ketat dan nilai resistor redaman seri tetap dipasang.

### 1.4.1. Prasyarat Mutlak Sebelum Pengujian
1. **Resistor Seri Pengaman Wajib Terpasang:**
   - Jalur `GP2..GP6` **wajib** melalui resistor seri **`33 Ω – 100 Ω`** (jangan gunakan kabel jumper polos `0 Ω`). Nilai `47 Ω` (untuk clock) dan `100 Ω` (untuk data) identik dengan nilai asli bawaan LG dan berfungsi meredam *ringing* serta membatasi arus jika terjadi kesalahan urutan sesaat.
2. **Kabel Jumper Pendek:**
   - Gunakan kabel serabut tipis (AWG 28–30) dengan panjang maksimal **≤ 10 cm** dan panjang seragam antar-jalur.
3. **Verifikasi `Pad B` Tidak Hubung Singkat:**
   - Gunakan multimeter mode kontinuitas/resistansi: pastikan titik `Pad B` dari `R648`, `R647`, `R631`, `R632`, dan `R633` **tidak terhubung singkat** ke `Pad A` (`IC501`) maupun ke `DGND`.
4. **Penyolderan `GND` Lebih Dulu:**
   - Hubungkan `GND` `RP2350-Zero` ke titik `DGND` mainboard LG HT306 (dekat `IC602`) **sebelum** kabel sinyal `GP2..GP6` dihubungkan ke `Pad B`.
5. **Pin Yang Dibiarkan Terbuka (`NC`):**
   - `GP7` (`ADATA3`), `GP8` (`MUX`), dan `GP9` (`REMOCON`) **wajib dibiarkan terbuka (NC)** dan tidak boleh dihubungkan ke titik manapun di mainboard.

### 1.4.2. Mengapa Urutan Daya (*Power Sequencing*) Tidak Boleh Terbalik?
Pada arsitektur firmware RP2350 ([main.c](file:///e:/Project/HomeTheather/firmware/src/main.c#L154-L155) & [i2s_engine.c](file:///e:/Project/HomeTheather/firmware/src/i2s_engine.c#L257-L260)):
* Begitu kabel USB Type-C dicolokkan ke PC, mesin `PIO0_SM0` **langsung aktif bekerja**.
* Meskipun PC sedang hening / tidak memutar musik (mengirim `g_silence_block`), pin **`GP2` (`BCK`, 6,144 MHz)** dan **`GP3` (`LRCK`, 96 kHz)** tetap memancarkan pulsa gelombang kotak aktif bertegangan `0 V ↔ 3,3 V` secara terus-menerus.
* Apabila HT306 berada dalam status mati atau *Standby* (`VCC33 = 0 V`), pulsa tegangan `3,3 V` dari `GP2/GP3` akan mengalir melewati dioda proteksi ESD internal pada kaki input `IC602 (Pulsus PS9850)` menuju rel catu `VCC33` (*parasitic back-powering*). Hal ini dapat menyebabkan chip bekerja di luar batas spesifikasi (*partial power stress*).

### 1.4.3. Tabel Urutan Setup Uji Coba Manual yang Wajib Dipatuhi

| Tahapan Operasi | Urutan Langkah Manual (WAJIB DISIPLIN) | Status Sistem |
| :--- | :--- | :--- |
| **Kondisi Awal** | Kabel USB Type-C `RP2350-Zero` **TERCABUT** dari port USB PC. | RP2350 mati (`0 V`), HT306 mati/standby. |
| **Urutan MENYALAKAN** | **1.** Tancapkan steker listrik 220V AC HT306.<br>**2.** Tekan tombol **Power ON** pada HT306 dan pindahkan ke mode **`AUX`**.<br>**3.** Tunggu **2–3 detik** agar rel `VCC33` stabil di `3,28 V`, `IC101 MICOM` selesai mem-boot `IC602`, dan relay output bekerja.<br>**4.** Setel volume ke angka kecil (misal: `VOL 3–5`) dan pilih EQ `NATURAL` atau `PLII MOVIE`.<br>**5.** **BARU COLOKKAN KABEL USB `RP2350-Zero` KE PC**.<br>**6.** Buka aplikasi pemutar di PC (JRiver / test tone) dan jalankan tes kanal bertahap. | Rel `VCC33` sudah `3,28 V`, `IC602` siap menerima clock dan data tanpa risiko arus bocor. |
| **Urutan MEMATIKAN** | **1.** Hentikan playback audio di PC.<br>**2.** **CABUT KABEL USB TYPE-C `RP2350-Zero` DARI PC TERLEBIH DAHULU** (pastikan LED RP2350 padam total).<br>**3.** **BARU MATIKAN ATAU TEKAN STANDBY PADA HT306**.<br>**4.** Cabut steker listrik AC jika pengujian selesai. | Jalur I2S sudah kembali `0 V` sebelum rel `VCC33` HT306 turun ke `0 V`. |

### 1.4.4. Batasan & Alasan Mengapa Wajib Menambahkan Pengaman untuk Harian
Uji coba langsung ini **hanya solusi transisi**. Pengaman berbasis hardware (`SN74LVC244A`) atau berbasis sensor firmware (`GP10 VCC33 Sense`) tetap mutlak diperlukan sebelum alat digunakan harian karena:
1. **Pemadaman Listrik Tiba-Tiba (PLN Mati):** Jika listrik rumah padam mendadak sementara laptop/PC (menggunakan baterai atau UPS) masih menyala, rel `VCC33` HT306 langsung jatuh ke `0 V` sementara RP2350 masih terus menyemburkan sinyal clock ke `IC602`.
2. **PC Masuk Mode Sleep / Hibernate:** Jika pengguna mematikan HT306 lebih dulu karena terburu-buru, terjadi back-powering ke chip amplifier.
3. **Faktor Kelalaian Manusia (*Human Error*):** Sangat rentan lupa mencabut kabel USB sebelum mematikan HT306.

