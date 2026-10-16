"""Makes the launcher's and the game window's icon (.ico and .png) from the project's emblem, the website's
icon (website/public/icon-512.png: a pale crescent moon and a drop of blood, our own drawing). Run it again
after the website's emblem changes. Needs Pillow.
usage: make_icon.py ../website/public/icon-512.png launcher/bloodborne.ico launcher/bloodborne.png"""
import sys
from PIL import Image

source = Image.open(sys.argv[1]).convert('RGBA')
source.resize((256, 256), Image.LANCZOS).save(
    sys.argv[2], sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
source.resize((128, 128), Image.LANCZOS).save(sys.argv[3])
