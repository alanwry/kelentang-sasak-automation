import os
import shutil
import subprocess
import requests
import argparse
import sys
import time

# FQBN sesuai spesifikasi Anda
DEFAULT_FQBN = "esp32:esp32:esp32s3:CDCOnBoot=cdc,PartitionScheme=min_spiffs"

def compile_firmware(project_dir, fqbn):
    print(f"[*] Compiling firmware (Incremental)...")
    main_ino_dir = os.path.join(project_dir, "main")
    build_dir = os.path.join(main_ino_dir, "build")
    
    # KITA TIDAK LAGI MENGHAPUS FOLDER BUILD DI SINI UNTUK MEMPERCEPAT PROSES
    
    # Menambahkan flag -j untuk kompilasi paralel (menggunakan semua core CPU)
    cmd = f'arduino-cli compile --fqbn {fqbn} "{main_ino_dir}" --build-path "{build_dir}" -j 0'
    # print(f"[debug] Executing: {cmd}")
    
    # Run arduino-cli compile
    result = subprocess.run(cmd, capture_output=True, text=True, shell=True)
    
    print(f"[debug] Compilation finished with return code: {result.returncode}")
    
    if result.returncode != 0:
        print("[!] Kompilasi gagal.")
        print("STDOUT:", result.stdout)
        print("STDERR:", result.stderr)
        return None, None
        
    bin_path = os.path.join(build_dir, "main.ino.bin")
    if not os.path.exists(bin_path):
        print(f"[!] Binary file not found at {bin_path}")
        return None, None
    
    return bin_path, build_dir

def upload_and_cleanup(bin_path, build_dir, device_ip):
    url = f"http://{device_ip}/update"
    
    try:
        file_size = os.path.getsize(bin_path)
        with open(bin_path, 'rb') as f:
            headers = {'Content-Type': 'application/octet-stream', 'Content-Length': str(file_size)}
            print(f"[*] Mengirim firmware ke {url}...")
            # Menunggu sebentar (timeout 5s) agar tidak terlalu lama memblokir jika ESP32 restart mendadak
            requests.post(url, data=f, headers=headers, timeout=5)
    except requests.exceptions.Timeout:
        print("[+] Upload dikirim (Timeout diharapkan karena ESP32 restart).")
    except Exception as e:
        print(f"[!] Info upload: {e}")
    
    print("[*] Menunggu 20 detik untuk proses flashing di ESP32...")
    time.sleep(20)
    
    # HAPUS PROSES PENGHAPUSAN FOLDER BUILD AGAR CACHE PERSISTEN
    print(f"[+] Folder build dipertahankan untuk kompilasi cepat selanjutnya.")
    
    print("[+] Program selesai.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("ip")
    args = parser.parse_args()
    
    bin_file, build_dir = compile_firmware(os.getcwd(), DEFAULT_FQBN)
    
    if bin_file:
        upload_and_cleanup(bin_file, build_dir, args.ip)
    else:
        sys.exit(1)
