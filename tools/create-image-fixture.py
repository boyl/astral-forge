"""生成自有 RGBA8 测试 PNG；四象限验证颜色、透明度和方向，无游戏素材。"""
import argparse,struct,zlib
from pathlib import Path
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('width',type=int);parser.add_argument('height',type=int);parser.add_argument('output',type=Path)
    args=parser.parse_args();width,height=args.width,args.height
    if not(0<width<=8192 and 0<height<=8192 and width*height*4<=64*1024*1024):parser.error('尺寸超出 RGBA8 资源限额')
    rows=bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            if y<height//2:pixel=(255,0,0,255) if x<width//2 else (0,255,0,255)
            else:pixel=(0,0,255,128) if x<width//2 else (255,255,0,128)
            if x%64<4 or y%64<4:pixel=(255,255,255,255)
            rows.extend(pixel)
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    image=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b'')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with args.output.open('xb') as stream:stream.write(image)
    print(f'已生成自有 RGBA8 测试图片：{args.output}')
if __name__=='__main__':main()
