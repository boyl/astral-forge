"""读取 2.6.4 图片索引与几何，不提取或修改游戏图片。"""
import argparse,hashlib,json,struct
from pathlib import Path
PROFILE_SHA='ca376d2b741f65c75c416317517d24c316dd8a32a4eae6559030797b3b9aa88b'
# Exact-profile native bank initializer reads 0x8c5f0 bytes for image entries;
# each entry contains a uint32 file offset and uint32 stored byte length.
IMAGE_COUNT=0x8c5f0//8
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
    with (args.game_directory/'Astral Ascent.exe').open('rb') as stream:actual=hashlib.file_digest(stream,'sha256').hexdigest()
    if actual!=PROFILE_SHA:raise SystemExit('不支持此 EXE 指纹，未推测资源格式。')
    print(json.dumps({'profile':'2.6.4','imageCount':IMAGE_COUNT,'image':image_info(args.game_directory/'Assets.dat',args.index),'nativeOverrideAvailable':False},ensure_ascii=False,indent=2))
if __name__=='__main__':main()
