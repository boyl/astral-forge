"""按已验证记录格式读取图片索引与几何，不提取或修改游戏图片。"""
import argparse,hashlib,json,struct,re
from pathlib import Path
PROFILE_SHA='ca376d2b741f65c75c416317517d24c316dd8a32a4eae6559030797b3b9aa88b'
# Exact-profile native bank initializer reads 0x8c5f0 bytes for image entries;
# each entry contains a uint32 file offset and uint32 stored byte length.
IMAGE_COUNT=0x8c5f0//8

def validate_exe(path):
    """验证原生图片加载契约；版本号和文件哈希不参与能力白名单。"""
    data=path.read_bytes()
    if len(data)<64 or data[:2]!=b'MZ':raise ValueError('不是有效 PE')
    pe=struct.unpack_from('<I',data,60)[0]
    if pe+24>len(data) or data[pe:pe+4]!=b'PE\0\0':raise ValueError('PE 头无效')
    machine,count=struct.unpack_from('<HH',data,pe+4);size=struct.unpack_from('<H',data,pe+20)[0];optional=pe+24
    if machine!=0x8664 or not 1<=count<=96 or size<144 or optional+size+count*40>len(data) or struct.unpack_from('<H',data,optional)[0]!=0x20b:raise ValueError('需要完整 x64 PE')
    sections=[]
    for i in range(count):
        at=optional+size+i*40;name=data[at:at+8].rstrip(b'\0');virtual,rva,length,raw=struct.unpack_from('<IIII',data,at+8)
        if raw+length>len(data):raise ValueError('PE 节越界')
        sections.append((name,rva,raw,length))
    texts=[s for s in sections if s[0]==b'.text']
    if len(texts)!=1:raise ValueError('原生代码节不唯一')
    native=bytes.fromhex('40 56 48 83 ec 30 0f b7 41 04 48 8b f1 66 83 c8 01 83 79 28 00 66 89 41 04 0f 85 9b 01 00 00 48 83 79 30 00 0f 85 90 01 00 00 a8 02 74 0a 48 83 c4 30 5e e9 88 01 00 00 0f b6 05 39 d4 cf 09 f6 d0')
    mask='11111111111111111111111111100001111111000011101111110000111000011'
    pattern=b''.join(re.escape(bytes([value])) if mask[i]=='1' else b'.' for i,value in enumerate(native))
    _,text_rva,text_raw,text_length=texts[0]
    matches=list(re.finditer(pattern,data[text_raw:text_raw+text_length],re.DOTALL))
    if len(matches)!=1:raise ValueError('原生图片加载契约缺失或有歧义')
    entry=text_rva+matches[0].start();pdata_rva,pdata_size=struct.unpack_from('<II',data,optional+112+3*8)
    ranges=[s for s in sections if s[1]<=pdata_rva and pdata_rva+pdata_size<=s[1]+s[3]]
    if len(ranges)!=1 or not pdata_size or pdata_size%12:raise ValueError('原生函数边界表无效')
    _,base,raw,_=ranges[0];at=raw+pdata_rva-base
    boundaries=[struct.unpack_from('<III',data,i) for i in range(at,at+pdata_size,12)]
    if sum(start==entry and entry+len(native)<=end<=text_rva+text_length for start,end,_ in boundaries)!=1:raise ValueError('原生图片入口不在唯一函数边界')
    return hashlib.sha256(data).hexdigest()
def image_info(bank,index):
    if not 0<=index<IMAGE_COUNT:raise ValueError(f'图片索引必须在 0..{IMAGE_COUNT-1}')
    length=bank.stat().st_size
    with bank.open('rb') as stream:
        stream.seek(index*8);entry=stream.read(8)
        if len(entry)!=8:raise ValueError('图片索引表不完整')
        offset,stored=struct.unpack('<II',entry)
        if stored<8 or offset<0x8c5f0 or offset+stored>length:raise ValueError('图片记录范围无效')
        stream.seek(offset);geometry=stream.read(8)
    width,height,buffer_width,buffer_height=struct.unpack('<4H',geometry)
    if not width or not height or not buffer_width or not buffer_height:raise ValueError('图片几何无效')
    return {'index':index,'width':width,'height':height,'bufferWidth':buffer_width,'bufferHeight':buffer_height,'storedBytes':stored}
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game_directory',type=Path);parser.add_argument('index',type=int);args=parser.parse_args()
    try:actual=validate_exe(args.game_directory/'Astral Ascent.exe')
    except (ValueError,OSError,struct.error) as error:parser.exit(2,str(error)+'\n')
    print(json.dumps({'exeSha256':actual,'verifiedIndexCount':IMAGE_COUNT,'image':image_info(args.game_directory/'Assets.dat',args.index),'nativeOverrideAvailable':False},ensure_ascii=False,indent=2))
if __name__=='__main__':main()
