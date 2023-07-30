from PIL import Image
import os
import sys


def resize_image_128x(name: str):
    try:
        image = Image.open(name)
        print(image.size)
        x = image.resize((128, 128), Image.Resampling.BOX)
        x.save(name)
    except:
        print("can not process image {}".format(name))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Use scale_icon.py [folder]")
        exit()
    for f in os.listdir(sys.argv[1]):
        resize_image_128x(os.path.join(sys.argv[1], f))
