"""合成 x64 PE：只包含原生图片入口和函数边界，用于边界失败测试。"""
import struct

def image_pe():
    data=bytearray(0x800);data[:2]=b'MZ';struct.pack_into('<I',data,60,0x80)
    data[0x80:0x84]=b'PE\0\0';struct.pack_into('<HH',data,0x84,0x8664,2);struct.pack_into('<H',data,0x94,240)
    optional=0x98;struct.pack_into('<H',data,optional,0x20b);struct.pack_into('<II',data,optional+136,0x2000,12)
    for at,name,rva,raw,length in [(optional+240,b'.text',0x1000,0x400,0x200),(optional+280,b'.pdata',0x2000,0x600,12)]:
        data[at:at+len(name)]=name;struct.pack_into('<IIII',data,at+8,length,rva,length,raw)
    native=bytes.fromhex('40 56 48 83 ec 30 0f b7 41 04 48 8b f1 66 83 c8 01 83 79 28 00 66 89 41 04 0f 85 9b 01 00 00 48 83 79 30 00 0f 85 90 01 00 00 a8 02 74 0a 48 83 c4 30 5e e9 88 01 00 00 0f b6 05 39 d4 cf 09 f6 d0')
    data[0x410:0x410+len(native)]=native;struct.pack_into('<III',data,0x600,0x1010,0x1070,0)
    return bytes(data)
