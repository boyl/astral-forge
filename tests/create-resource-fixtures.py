"""生成测试自有像素，不读取或分发游戏图片。"""
from pathlib import Path
import sys,struct,zlib
root=Path(sys.argv[1]);assets=root/'插件/assets';outside=root/'outside';assets.mkdir(parents=True,exist_ok=True);outside.mkdir(exist_ok=True)
def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
def png(path,width,height,color,row,extras=b''):
    compressor=zlib.compressobj();parts=[]
    for _ in range(height):parts.append(compressor.compress(b'\0'+row))
    parts.append(compressor.flush())
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,color,0,0,0))+extras+chunk(b'IDAT',b''.join(parts))+chunk(b'IEND',b''))
row=bytes([0,180,255,128,255,120,0,255])
for path in [assets/'rgba.png',assets/'中文.png',outside/'outside.png']:png(path,2,1,6,row)
png(assets/'rgb.png',2,1,2,bytes([0,180,255,255,120,0]));png(assets/'gray.png',1,1,0,bytes([77]))
png(assets/'palette.png',1,1,3,b'\0',chunk(b'PLTE',bytes([5,15,25]))+chunk(b'tRNS',b'\0'))
# A valid tiny BMP tests container rejection independently of its extension.
pixel=bytes([255,180,0,0]);(assets/'not-png.bmp').write_bytes(b'BM'+struct.pack('<IHHI',58,0,0,54)+struct.pack('<IiiHHIIiiII',40,1,1,1,24,0,4,0,0,0,0)+pixel)
(assets/'corrupt.png').write_bytes(b'invalid PNG')
with (assets/'encoded-large.png').open('wb') as f:f.truncate(32*1024*1024+1)
png(assets/'axis-large.png',8193,1,2,bytes(8193*3));png(assets/'pixels-large.png',4097,4096,2,bytes(4097*3))
