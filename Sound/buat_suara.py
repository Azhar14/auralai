from gtts import gTTS
import os

# Urutan ini WAJIB sama persis dengan urutan 'names' di file data.yaml
nominal_uang = [
    "Seribu Rupiah",       # Class 0
    "Sepuluh ribu Rupiah", # Class 1
    "Seratus ribu Rupiah", # Class 2
    "Dua ribu Rupiah",     # Class 3
    "Dua puluh ribu Rupiah",# Class 4
    "Lima ribu Rupiah",    # Class 5
    "Lima puluh ribu Rupiah"# Class 6
]

# Buat folder 'mp3' jika belum ada
if not os.path.exists("mp3"):
    os.makedirs("mp3")

print("Mulai membuat file audio dengan Google TTS...")

for index, teks in enumerate(nominal_uang):
    # Logika Track_ID = Class_ID + 1 (DFPlayer mulai dari 1)
    track_id = index + 1 
    
    # Format nama file jadi 4 digit angka di depan (contoh: 0001_Seribu Rupiah.mp3)
    nama_file = f"mp3/{track_id:04d}_{teks}.mp3"
    
    # Generate suara bahasa Indonesia ('id')
    tts = gTTS(text=teks, lang='id', slow=False)
    tts.save(nama_file)
    print(f"Berhasil membuat: {nama_file}")

print("Selesai! Silakan copy folder 'mp3' ini ke dalam SD Card.")