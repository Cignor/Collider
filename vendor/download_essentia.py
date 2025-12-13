import urllib.request
import zipfile
import os
import shutil

url = "https://github.com/MTG/essentia/archive/refs/heads/master.zip"
zip_path = "essentia.zip"
extract_dir = "."
final_dir = "essentia"

print(f"Downloading {url}...")
try:
    urllib.request.urlretrieve(url, zip_path)
    print("Download complete.")

    print("Extracting...")
    with zipfile.ZipFile(zip_path, 'r') as zip_ref:
        zip_ref.extractall(extract_dir)
    
    # Rename essentia-master to essentia
    if os.path.exists(final_dir):
        shutil.rmtree(final_dir)
    
    os.rename("essentia-master", final_dir)
    print(f"Success! Essentia installed to {os.path.abspath(final_dir)}")

    # Cleanup
    os.remove(zip_path)

except Exception as e:
    print(f"Error: {e}")
